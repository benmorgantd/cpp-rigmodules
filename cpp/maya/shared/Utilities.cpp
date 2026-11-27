#include "maya/shared/Utilities.h"
#include "core/Constants.h"
#include "core/JsonUtils.h"
#include "core/RigTemplateValidation.h"
#include "maya/shared/RigTraversal.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <maya/MArgList.h>
#include <maya/MDagModifier.h>
#include <maya/MDagPath.h>
#include <maya/MFnAttribute.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnMatrixData.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MFnTransform.h>
#include <maya/MGlobal.h>
#include <maya/MIntArray.h>
#include <maya/MItDependencyGraph.h>
#include <maya/MItMeshEdge.h>
#include <maya/MItMeshPolygon.h>
#include <maya/MMatrix.h>
#include <maya/MMessage.h>
#include <maya/MModelMessage.h>
#include <maya/MNamespace.h>
#include <maya/MObject.h>
#include <maya/MObjectHandle.h>
#include <maya/MPlug.h>
#include <maya/MProfiler.h>
#include <maya/MQuaternion.h>
#include <maya/MSelectionList.h>
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MStringArray.h>
#include <maya/MTime.h>
#include <maya/MVector.h>

// ---------------------------------
// Math Utilities
// ---------------------------------

MMatrix MathUtils::computeAimMatrix(
	const MPoint& pAim,
	const MPoint& pTarget,
	const MPoint& pUp,
	const MMatrix& fallbackMatrix,
	const float epsilon)
{
	// 1. Primary Aim Axis (+X)
	MVector vAim = pTarget - pAim;
	double aimLen = vAim.length();
	MVector hatX;  // the normalized matrix x axis.

	if (aimLen > epsilon)
	{
		hatX = vAim / aimLen;
	}
	else
	{
		// Fallback to primary X axis of fallback matrix if target overlaps aim origin
		hatX = MVector(fallbackMatrix[0][0], fallbackMatrix[0][1], fallbackMatrix[0][2]).normal();
	}

	// 2. Up Direction Vector (+Z raw direction)
	MVector vUpRaw = pUp - pAim;
	if (vUpRaw.length() < epsilon)
	{
		vUpRaw = MVector(fallbackMatrix[2][0], fallbackMatrix[2][1], fallbackMatrix[2][2]);
	}

	// 3. Side Axis (+Y) via Cross Product
	// Right-handed basis where +X is Aim and +Z is Up:
	// hatZ x hatX = hatY
	MVector hatY = vUpRaw ^ hatX;
	double yLen = hatY.length();

	if (yLen > epsilon)
	{
		hatY /= yLen;
	}
	else
	{
		// Singular/Collinear Fallback: choose orthogonal reference axis
		MVector fallbackUp(0.0, 1.0, 0.0);
		if (std::abs(hatX.y) > 0.9)
		{
			fallbackUp = MVector(0.0, 0.0, 1.0);
		}
		hatY = (fallbackUp ^ hatX).normal();
	}

	// 4. Orthonormalized Up Axis (+Z)
	// hatX x hatY = hatZ
	MVector hatZ = hatX ^ hatY;

	// 5. Construct Row-Major Maya Matrix
	double mData[4][4];

	// Row 0: +X Aim Axis
	mData[0][0] = hatX.x; mData[0][1] = hatX.y; mData[0][2] = hatX.z; mData[0][3] = 0.0;

	// Row 1: +Y Side Axis
	mData[1][0] = hatY.x; mData[1][1] = hatY.y; mData[1][2] = hatY.z; mData[1][3] = 0.0;

	// Row 2: +Z Up Axis
	mData[2][0] = hatZ.x; mData[2][1] = hatZ.y; mData[2][2] = hatZ.z; mData[2][3] = 0.0;

	// Row 3: Origin Position
	mData[3][0] = pAim.x; mData[3][1] = pAim.y; mData[3][2] = pAim.z; mData[3][3] = 1.0;

	return MMatrix(mData);
}

namespace
{
	// A matrix as scale (row lengths) and rotation (unit rows, no shear). Returns the rotation as a quaternion.
	MQuaternion decomposeScaleRotation(const MMatrix& mMatrix, double outScale[3])
	{
		double mRotation[4][4] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 1 } };
		for (int row = 0; row < 3; ++row)
		{
			const MVector vRow(mMatrix[row][0], mMatrix[row][1], mMatrix[row][2]);
			outScale[row] = vRow.length();
			const double inverseLength = outScale[row] > 1.0e-12 ? 1.0 / outScale[row] : 1.0;
			for (int column = 0; column < 3; ++column)
			{
				mRotation[row][column] = mMatrix[row][column] * inverseLength;
			}
		}
		MQuaternion qRotation;
		qRotation = MMatrix(mRotation);
		return qRotation;
	}

	// The short way round: a quaternion and its negative are the same rotation, so the one nearer a is taken.
	MQuaternion slerpShortArc(const MQuaternion& qA, const MQuaternion& qB, double t)
	{
		double dot = qA.x * qB.x + qA.y * qB.y + qA.z * qB.z + qA.w * qB.w;
		const double sign = dot < 0.0 ? -1.0 : 1.0;
		dot *= sign;

		double weightA = 1.0 - t;
		double weightB = t;
		if (dot < 0.9995)
		{
			const double theta = std::acos(dot);
			const double inverseSine = 1.0 / std::sin(theta);
			weightA = std::sin((1.0 - t) * theta) * inverseSine;
			weightB = std::sin(t * theta) * inverseSine;
		}
		MQuaternion qResult(
			qA.x * weightA + qB.x * sign * weightB,
			qA.y * weightA + qB.y * sign * weightB,
			qA.z * weightA + qB.z * sign * weightB,
			qA.w * weightA + qB.w * sign * weightB);
		qResult.normalizeIt();
		return qResult;
	}
}

MMatrix MathUtils::blendMatrices(const MMatrix& a, const MMatrix& b, double t)
{
	if (t <= 0.0)
	{
		return a;
	}
	if (t >= 1.0)
	{
		return b;
	}

	double scaleA[3];
	double scaleB[3];
	const MQuaternion qRotation = slerpShortArc(decomposeScaleRotation(a, scaleA), decomposeScaleRotation(b, scaleB), t);
	const MMatrix mRotation = qRotation.asMatrix();

	double mData[4][4];
	for (int row = 0; row < 3; ++row)
	{
		const double scale = scaleA[row] + (scaleB[row] - scaleA[row]) * t;
		for (int column = 0; column < 3; ++column)
		{
			mData[row][column] = mRotation[row][column] * scale;
		}
		mData[row][3] = 0.0;
	}
	for (int column = 0; column < 3; ++column)
	{
		mData[3][column] = a[3][column] + (b[3][column] - a[3][column]) * t;
	}
	mData[3][3] = 1.0;
	return MMatrix(mData);
}

// ---------------------------------
// DAG Utilities
// ---------------------------------
// Locks and hides rotation and scale channels on a control object
double TimeUtils::getSceneFrameRate()
{
	return 1.0 / MTime(1.0, MTime::uiUnit()).as(MTime::kSeconds);
}

bool TimeUtils::isSameFrameRate(double a, double b)
{
	constexpr double kFrameRateTolerance{ 1e-3 };
	return std::abs(a - b) <= kFrameRateTolerance;
}

void DagUtils::lockAndHideTranslate(MObject oControl)
{
	MPlug pTranslate(oControl, MPxTransform::translate);
	pTranslate.setLocked(true);
	pTranslate.setKeyable(false);
	pTranslate.setChannelBox(false);
}

void DagUtils::lockAndHideRotate(MObject oControl)
{
	MPlug pRotation(oControl, MPxTransform::rotate);
	pRotation.setLocked(true);
	pRotation.setKeyable(false);
	pRotation.setChannelBox(false);
}

void DagUtils::lockAndHideScale(MObject oControl)
{
	MPlug pScale(oControl, MPxTransform::scale);
	pScale.setLocked(true);
	pScale.setKeyable(false);
	pScale.setChannelBox(false);
}

// Zeroes a joint's local channels and jointOrient and writes its rest world matrix to its OPM. The caller turns
// inheritsTransform off in the same modifier, so the OPM alone places the joint in world space. mTransformWorld is the
// rest from the read phase (every caller passes the plan's rest), never read back from the scene.
MStatus DagUtils::bakeJointTransformsToOPM(const MObject& oJoint, const MMatrix& mTransformWorld, MDagModifier& dagMod)
{
	MStatus status;
	if (!oJoint.hasFn(MFn::kJoint))
	{
		MGlobal::displayError("Given object must be a joint");
		return MStatus::kFailure;
	}

	status = zeroJointOrient(oJoint, dagMod);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	status = zeroLocalTransforms(oJoint, dagMod);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MPlug pOffsetParentMatrix(oJoint, MPxTransform::offsetParentMatrix);
	status = DagUtils::setMatrixPlug(pOffsetParentMatrix, mTransformWorld, dagMod);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	return status;
}

MStatus DagUtils::zeroJointOrient(const MObject& oJoint, MDagModifier& dagMod)
{
	MStatus status;
	MFnDependencyNode fnJoint(oJoint, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	for (const char* name : { MayaAttr::JointOrientX, MayaAttr::JointOrientY, MayaAttr::JointOrientZ })
	{
		MPlug pJointOrient = fnJoint.findPlug(name, false, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = dagMod.newPlugValueDouble(pJointOrient, 0.0);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	return MStatus::kSuccess;
}

MStatus DagUtils::zeroLocalTransforms(const MObject& oTransform, MDagModifier& dagMod)
{
	if (oTransform.isNull() || !oTransform.hasFn(MFn::kTransform))
	{
		MGlobal::displayError("Given object is null or is not a transform");
		return MStatus::kFailure;
	}

	MFnTransform fnTransform(oTransform);
	for (const auto& transform : kDefaultTransformValues)
	{
		MPlug pTransform(oTransform, transform.attr);

		if (!pTransform.isNull() && !pTransform.isLocked())
		{
			dagMod.newPlugValueDouble(pTransform, transform.value);
		}
		else
		{
			MFnAttribute fnAttr(transform.attr);
			MGlobal::displayError(MString("Plug was locked and could not be zeroed: " + fnAttr.name()));
		}
	}
	return MStatus::kSuccess;
}

MStatus DagUtils::setInheritsTransform(const MObject& transform, bool value, MDagModifier& dagMod)
{
	if (!transform.hasFn(MFn::kTransform))
	{
		MGlobal::displayError("Given object is not a DAG transform, cannot set inheritsTransform.");
		return MStatus::kFailure;
	}

	MPlug pInheritsTransform(transform, MPxTransform::inheritsTransform);
	return dagMod.newPlugValueBool(pInheritsTransform, value);
}

MStatus DagUtils::setSegmentScaleCompensate(MObject& oTransform, bool value, MDagModifier& dagMod)
{
	if (!oTransform.hasFn(MFn::kJoint))
	{
		MGlobal::displayError("Given object is not a DAG joint, cannot set segment scale compensate.");
		return MStatus::kFailure;
	}

	MFnDependencyNode fnTransform(oTransform);
	MPlug pSegmentScaleCompensate = fnTransform.findPlug(MayaAttr::SegmentScaleCompensate, false);

	if (pSegmentScaleCompensate.isNull())
	{
		return MStatus::kFailure;
	}

	return dagMod.newPlugValueBool(pSegmentScaleCompensate, value);
}

MMatrix DagUtils::getMatrixFromPlug(const MPlug& pMatrixPlug)
{
	MMatrix matrixValue;

	MObject dataObj;
	MStatus status = pMatrixPlug.getValue(dataObj);

	if (status && !dataObj.isNull() && dataObj.hasFn(MFn::kMatrixData))
	{
		MFnMatrixData fnMatrixData(dataObj);
		matrixValue = fnMatrixData.matrix();
	}

	return matrixValue;
}


MStatus DagUtils::setMatrixPlug(MPlug& matrixPlug, MMatrix matrix, MDagModifier& dagMod)
{
	if (!matrixPlug.isNull() && !matrixPlug.isArray())
	{
		MFnMatrixData fnMatrixData;
		MObject oMatrixData = fnMatrixData.create(matrix);
		return dagMod.newPlugValue(matrixPlug, oMatrixData);
	}
	else
	{
		MGlobal::displayError("The given matrix plug was null or an array.");
		return MStatus::kFailure;
	}
}

MStatus DagUtils::getDagPathFromName(const MString& name, MDagPath& outPath, NodeKind kind)
{
	MSelectionList sel;
	MDagPath path;
	if (!sel.add(name) || !sel.getDagPath(0, path))
	{
		MGlobal::displayError(MString("No DAG node named ") + name);
		return MStatus::kFailure;
	}

	if (kind == NodeKind::kJoint && !path.hasFn(MFn::kJoint))
	{
		MGlobal::displayError(name + " is not a joint.");
		return MStatus::kFailure;
	}
	if (kind == NodeKind::kMesh)
	{
		if (!MeshUtils::findMeshTransform(path, outPath))
		{
			MGlobal::displayError(name + " is not a mesh.");
			return MStatus::kFailure;
		}
		return MStatus::kSuccess;
	}

	outPath = path;
	return MStatus::kSuccess;
}

bool DagUtils::isChannelWritable(const MPlug& pChannel)
{
	if (pChannel.isLocked())
	{
		return false;
	}
	const MPlug pSource = pChannel.source();
	return pSource.isNull() || pSource.node().hasFn(MFn::kAnimCurve);
}

MStatus DagUtils::getMObjectFromName(const MString& name, MObject& oNode)
{
	MSelectionList sel;
	MStatus status = sel.add(name);
	if (!status)
	{
		return status;
	}
	return sel.getDependNode(0, oNode);
}

MString DagUtils::getAbsoluteNamespace(const MObject& oNode)
{
	const MString nodeNamespace = MNamespace::getNamespaceFromName(MFnDependencyNode(oNode).absoluteName());
	if (nodeNamespace.length() == 0 || nodeNamespace == ":")
	{
		return ":";
	}
	return nodeNamespace.substring(0, 0) == ":" ? nodeNamespace : MString(":") + nodeNamespace;
}

MString DagUtils::getAbsoluteName(const MString& namespaceName, const MString& shortName)
{
	if (namespaceName.length() == 0)
	{
		return MString(":") + shortName;
	}
	return MString(":") + namespaceName + ":" + shortName;
}

MString DagUtils::getPartialPathName(const MObject& oDagNode)
{
	MDagPath dagPath;
	MFnDagNode(oDagNode).getPath(dagPath);
	return dagPath.partialPathName();
}

MObject DagUtils::findUpstreamNode(const MObject& oNode, MFn::Type filter)
{
	MStatus status;
	MObject oRoot = oNode;
	MItDependencyGraph itGraph(oRoot, filter, MItDependencyGraph::kUpstream, MItDependencyGraph::kBreadthFirst,
		MItDependencyGraph::kNodeLevel, &status);
	if (!status || itGraph.isDone())
	{
		return MObject::kNullObj;
	}
	return itGraph.currentItem();
}

MObjectArray DagUtils::getTopmostNodes(const MObjectArray& oNodes)
{
	std::vector<MObjectHandle> handles;
	for (unsigned int i = 0; i < oNodes.length(); ++i)
	{
		handles.emplace_back(oNodes[i]);
	}

	MObjectArray oTopmost;
	for (unsigned int i = 0; i < oNodes.length(); ++i)
	{
		MStatus status;
		const MFnDagNode fnNode(oNodes[i], &status);
		const bool hasParentInSet = status && fnNode.parentCount() > 0
			&& std::find(handles.begin(), handles.end(), MObjectHandle(fnNode.parent(0))) != handles.end();
		if (!hasParentInSet)
		{
			oTopmost.append(oNodes[i]);
		}
	}
	return oTopmost;
}

MStatus DagUtils::queueLockAndHideTransform(const MObject& oTransform, MDagModifier& dagMod, const std::vector<std::string>& keepChannels)
{
	MStatus status;
	const MFnDagNode fnTransform(oTransform, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// The children, not the compound plugs: setAttr -lock on a compound leaves the children keyable in the channel box.
	for (const char* channel : { "tx", "ty", "tz", "rx", "ry", "rz", "sx", "sy", "sz" })
	{
		if (std::find(keepChannels.begin(), keepChannels.end(), channel) != keepChannels.end())
		{
			continue;
		}
		status = dagMod.commandToExecute(MString("setAttr -lock true -keyable false -channelBox false ")
			+ MelUtils::quoted(fnTransform.fullPathName() + "." + channel));
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	return MStatus::kSuccess;
}

MStatus DagUtils::queueHideAndLockVisibility(const MObject& oTransform, MDagModifier& dagMod)
{
	MStatus status;
	const MFnDagNode fnTransform(oTransform, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	const MString plug = MelUtils::quoted(fnTransform.fullPathName() + ".visibility");
	status = dagMod.commandToExecute(MString("setAttr ") + plug + " 0");
	CHECK_MSTATUS_AND_RETURN_IT(status);
	return dagMod.commandToExecute(MString("setAttr -lock true -keyable false -channelBox false ") + plug);
}

MStatus DagUtils::queueLockAndHideVisibility(const MObject& oTransform, MDagModifier& dagMod)
{
	MStatus status;
	const MFnDagNode fnTransform(oTransform, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	return dagMod.commandToExecute(MString("setAttr -lock true -keyable false -channelBox false ")
		+ MelUtils::quoted(fnTransform.fullPathName() + ".visibility"));
}

// ---------------------------------
// Mesh, MEL and command utilities
// ---------------------------------
bool MeshUtils::findMeshTransform(const MDagPath& path, MDagPath& outTransform)
{
	MDagPath shape = path;
	if (!shape.extendToShape() || !shape.hasFn(MFn::kMesh))
	{
		return false;
	}
	outTransform = shape;
	outTransform.pop();
	return true;
}

MStatus MeshUtils::getVertexTargets(const MSelectionList& selection, std::vector<MeshVertexTarget>& outTargets)
{
	for (unsigned int i = 0; i < selection.length(); ++i)
	{
		MDagPath path;
		MObject oComponent;
		MDagPath mesh;
		if (!selection.getDagPath(i, path, oComponent) || !findMeshTransform(path, mesh))
		{
			continue;
		}

		bool allVertices = oComponent.isNull();
		std::vector<unsigned int> vertices;
		if (!allVertices)
		{
			MStatus status;
			if (oComponent.hasFn(MFn::kMeshVertComponent))
			{
				MFnSingleIndexedComponent fnComponent(oComponent, &status);
				CHECK_MSTATUS_AND_RETURN_IT(status);
				allVertices = fnComponent.isComplete();
				MIntArray elements;
				fnComponent.getElements(elements);
				vertices.assign(elements.length(), 0);
				for (unsigned int e = 0; e < elements.length(); ++e)
				{
					vertices[e] = static_cast<unsigned int>(elements[e]);
				}
			}
			else if (oComponent.hasFn(MFn::kMeshEdgeComponent))
			{
				for (MItMeshEdge itEdge(path, oComponent); !itEdge.isDone(); itEdge.next())
				{
					vertices.push_back(static_cast<unsigned int>(itEdge.index(0)));
					vertices.push_back(static_cast<unsigned int>(itEdge.index(1)));
				}
			}
			else if (oComponent.hasFn(MFn::kMeshPolygonComponent))
			{
				for (MItMeshPolygon itFace(path, oComponent); !itFace.isDone(); itFace.next())
				{
					MIntArray faceVertices;
					itFace.getVertices(faceVertices);
					for (unsigned int v = 0; v < faceVertices.length(); ++v)
					{
						vertices.push_back(static_cast<unsigned int>(faceVertices[v]));
					}
				}
			}
			else
			{
				continue;
			}
		}

		MeshVertexTarget* existing = nullptr;
		for (MeshVertexTarget& target : outTargets)
		{
			if (target.mesh == mesh)
			{
				existing = &target;
			}
		}
		if (!existing)
		{
			outTargets.emplace_back();
			existing = &outTargets.back();
			existing->mesh = mesh;
			existing->allVertices = false;
		}
		existing->allVertices = existing->allVertices || allVertices;
		existing->vertices.insert(existing->vertices.end(), vertices.begin(), vertices.end());
	}
	for (MeshVertexTarget& target : outTargets)
	{
		std::sort(target.vertices.begin(), target.vertices.end());
		target.vertices.erase(std::unique(target.vertices.begin(), target.vertices.end()), target.vertices.end());
		if (target.allVertices)
		{
			target.vertices.clear();
		}
	}
	return MStatus::kSuccess;
}

MStatus MeshUtils::getMeshShape(const MDagPath& mesh, MDagPath& outShape)
{
	outShape = mesh;
	MStatus status = outShape.extendToShape();
	if (!status || !outShape.hasFn(MFn::kMesh))
	{
		MGlobal::displayError(mesh.partialPathName() + " has no mesh shape.");
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MObject MeshUtils::createAllVerticesComponent(unsigned int vertexCount)
{
	MFnSingleIndexedComponent fnComponent;
	MObject oComponent = fnComponent.create(MFn::kMeshVertComponent);
	fnComponent.setCompleteData(static_cast<int>(vertexCount));
	return oComponent;
}

MString MelUtils::quoted(const MString& text)
{
	return MString("\"") + text + "\"";
}

MStatus CommandUtils::readRigRoot(const MArgDatabase& argData, MStatus parseStatus, const char* commandName, MObject& oOutRigRoot)
{
	if (!parseStatus)
	{
		MGlobal::displayError(MString("Error parsing ") + commandName + " arguments.");
		return parseStatus;
	}

	MString rigRootName;
	MStatus status = readSingleObject(argData, commandName, "rig root", rigRootName);
	if (!status)
	{
		return status;
	}
	return RigTraversal::getRigRoot(rigRootName, oOutRigRoot);
}

MStatus CommandUtils::readControlScope(const MArgDatabase& argData, MStatus parseStatus, const char* commandName, RigControlScope& outScope)
{
	if (!parseStatus)
	{
		MGlobal::displayError(MString("Error parsing ") + commandName + " arguments.");
		return parseStatus;
	}

	MStringArray names;
	MStatus status = argData.getObjects(names);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	return RigTraversal::getControlScope(names, outScope);
}

MSyntax CommandUtils::controlScopeSyntax()
{
	MSyntax syntax;
	syntax.setObjectType(MSyntax::kStringObjects, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus CommandUtils::readSingleObject(const MArgDatabase& argData, const char* commandName, const char* what, MString& outName)
{
	MStringArray objects;
	MStatus status = argData.getObjects(objects);
	if (!status || objects.length() != 1)
	{
		MGlobal::displayError(MString(commandName) + " takes exactly one " + what + ".");
		return MStatus::kFailure;
	}
	outName = objects[0];
	return MStatus::kSuccess;
}

MStatus CommandUtils::readStringList(const MArgDatabase& argData, const char* flag, std::vector<std::string>& outValues)
{
	const unsigned int uses = argData.numberOfFlagUses(flag);
	for (unsigned int i = 0; i < uses; ++i)
	{
		MArgList flagArgs;
		MStatus status = argData.getFlagArgumentList(flag, i, flagArgs);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		outValues.push_back(flagArgs.asString(0).asChar());
	}
	return MStatus::kSuccess;
}

MStatus CommandUtils::readFilePath(const MArgDatabase& argData, const char* flag, const char* commandName, MString& outPath)
{
	if (!argData.isFlagSet(flag) || argData.getFlagArgument(flag, 0, outPath) != MStatus::kSuccess || outPath.length() == 0)
	{
		MGlobal::displayError(MString(commandName) + " needs " + flag + ".");
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus CommandUtils::readJoints(const MArgDatabase& argData, const char* commandName, MDagPathArray& outJoints)
{
	MStringArray jointNames;
	MStatus status = argData.getObjects(jointNames);
	if (!status || jointNames.length() == 0)
	{
		MGlobal::displayError(MString(commandName) + " needs at least one joint name.");
		return MStatus::kFailure;
	}
	for (unsigned int i = 0; i < jointNames.length(); ++i)
	{
		MDagPath jointPath;
		status = DagUtils::getDagPathFromName(jointNames[i], jointPath, DagUtils::NodeKind::kJoint);
		if (!status)
		{
			return status;   // the helper reported which name failed
		}
		outJoints.append(jointPath);
	}
	return MStatus::kSuccess;
}

MStatus CommandUtils::readTransforms(const MArgDatabase& argData, const char* commandName, MDagPathArray& outNodes)
{
	MStringArray names;
	MStatus status = argData.getObjects(names);
	if (!status || names.length() == 0)
	{
		MGlobal::displayError(MString(commandName) + " needs at least one node name.");
		return MStatus::kFailure;
	}
	for (unsigned int i = 0; i < names.length(); ++i)
	{
		MDagPath path;
		status = DagUtils::getDagPathFromName(names[i], path);
		if (!status)
		{
			return status;   // the helper reported which name failed
		}
		if (!path.hasFn(MFn::kTransform))
		{
			MGlobal::displayError(MString(commandName) + ": " + names[i] + " is not a transform.");
			return MStatus::kFailure;
		}
		outNodes.append(path);
	}
	return MStatus::kSuccess;
}

MStatus CommandUtils::readMeshesOrSelection(const MArgDatabase& argData, const char* commandName, MDagPathArray& outMeshes)
{
	MStringArray objects;
	argData.getObjects(objects);
	for (unsigned int i = 0; i < objects.length(); ++i)
	{
		MDagPath mesh;
		MStatus status = DagUtils::getDagPathFromName(objects[i], mesh, DagUtils::NodeKind::kMesh);
		if (!status)
		{
			return status;
		}
		outMeshes.append(mesh);
	}
	if (objects.length() > 0)
	{
		return MStatus::kSuccess;
	}

	MSelectionList selection;
	MGlobal::getActiveSelectionList(selection);
	for (unsigned int i = 0; i < selection.length(); ++i)
	{
		MDagPath path;
		MDagPath mesh;
		if (selection.getDagPath(i, path) && MeshUtils::findMeshTransform(path, mesh))
		{
			outMeshes.append(mesh);
		}
	}
	if (outMeshes.length() == 0)
	{
		MGlobal::displayError(MString(commandName) + " needs meshes: name them or select them.");
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus CommandUtils::readTextFile(const MString& path, const char* kind, std::string& outText)
{
	std::ifstream file(path.asChar(), std::ios::in | std::ios::binary);
	if (!file)
	{
		MGlobal::displayError(MString("Could not read the ") + kind + " file: " + path);
		return MStatus::kFailure;
	}
	std::stringstream text;
	text << file.rdbuf();
	outText = text.str();
	return MStatus::kSuccess;
}

MStatus CommandUtils::writeTextFile(const MString& path, const char* kind, const std::string& text)
{
	std::ofstream file(path.asChar(), std::ios::out | std::ios::trunc);
	file << text;
	file.close();
	if (!file)
	{
		MGlobal::displayError(MString("Could not write the ") + kind + " file: " + path);
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

void ReportUtils::displayReport(const ValidationReport& report)
{
	for (const std::string& warning : report.warnings)
	{
		MGlobal::displayWarning(warning.c_str());
	}
	for (const std::string& error : report.errors)
	{
		MGlobal::displayError(error.c_str());
	}
}

// Profiler utils -----------------------------------------------------------
namespace
{
	int gProfilerCategory{ -1 };
}

void ProfilerUtils::addCategory()
{
	gProfilerCategory = MProfiler::addCategory(PluginInfo::ProfilerCategory, "RigModules plugin events");
}

void ProfilerUtils::removeCategory()
{
	MProfiler::removeCategory(PluginInfo::ProfilerCategory);
	gProfilerCategory = -1;
}

int ProfilerUtils::category()
{
	return gProfilerCategory;
}

// Active selection ---------------------------------------------------------
namespace
{
	struct ObjectHandleHash
	{
		std::size_t operator()(const MObjectHandle& handle) const
		{
			return handle.hashCode();
		}
	};

	std::unordered_set<MObjectHandle, ObjectHandleHash> gSelectedNodes;
	unsigned int gSelectionLength{ 0 };
	MCallbackId gSelectionCallback{ 0 };
	bool gSelectionCallbackActive{ false };

	void readActiveSelection(void*)
	{
		MSelectionList selection;
		MGlobal::getActiveSelectionList(selection);
		gSelectionLength = selection.length();
		gSelectedNodes.clear();
		for (unsigned int i = 0; i < gSelectionLength; ++i)
		{
			MObject oNode;
			if (selection.getDependNode(i, oNode) == MStatus::kSuccess && !oNode.isNull())
			{
				gSelectedNodes.insert(MObjectHandle(oNode));
			}
		}
	}
}

MStatus ActiveSelection::start()
{
	MStatus status;
	gSelectionCallback = MModelMessage::addCallback(MModelMessage::kActiveListModified, readActiveSelection, nullptr, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	gSelectionCallbackActive = true;
	readActiveSelection(nullptr);
	return MStatus::kSuccess;
}

void ActiveSelection::stop()
{
	if (gSelectionCallbackActive)
	{
		MMessage::removeCallback(gSelectionCallback);
		gSelectionCallbackActive = false;
	}
	gSelectedNodes.clear();
	gSelectionLength = 0;
}

bool ActiveSelection::contains(const MObject& oNode)
{
	// A deleted node's memory can be reused by a new node, so a stale entry must not match it.
	const auto it = gSelectedNodes.find(MObjectHandle(oNode));
	return it != gSelectedNodes.end() && it->isValid();
}

unsigned int ActiveSelection::length()
{
	return gSelectionLength;
}

// DAG plug and socket utils -----------------------------------------------
MStatus DagPlugAndSocketUtils::setParentSocketNames(const MObject& oModule, const unsigned int index, const std::vector<std::string>& names, MDagModifier& dagMod)
{
	if (oModule.isNull())
	{
		return MStatus::kFailure;
	}

	MFnDependencyNode fnModule(oModule);

	MPlug pSocketNames = fnModule.findPlug(RigAttr::SocketNames, false);
	if (pSocketNames.isNull())
	{
		return MStatus::kFailure;
	}

	MPlug pSocketNamesElement = pSocketNames.elementByLogicalIndex(index);
	if (pSocketNamesElement.isNull())
	{
		return MStatus::kFailure;
	}

	return dagMod.newPlugValueString(pSocketNamesElement, MString(JsonUtils::writeJsonString(names).c_str()));
}


MStatus DagPlugAndSocketUtils::setChildPlugName(const MObject& oModule, const unsigned int index, const char* name, MDagModifier& dagMod)
{
	if (oModule.isNull())
	{
		return MStatus::kFailure;
	}

	MFnDependencyNode fnModule(oModule);

	MPlug pChildPlugs = fnModule.findPlug(RigAttr::ChildPlugs, false);
	if (pChildPlugs.isNull())
	{
		return MStatus::kFailure;
	}

	MPlug pChildPlugsElement = pChildPlugs.elementByLogicalIndex(index);
	if (pChildPlugsElement.isNull())
	{
		return MStatus::kFailure;
	}

	MObject oChildPlugName = fnModule.attribute(RigAttr::ChildPlugName);
	MPlug pChildPlugsElementName = pChildPlugsElement.child(oChildPlugName);

	return dagMod.newPlugValueString(pChildPlugsElementName, MString(name));
}

// ---------------------------------
// Scoped rig namespace
// ---------------------------------

ScopedRigNamespace::ScopedRigNamespace(const MString& rigNamespace)
	: fOriginalNamespace(MNamespace::currentNamespace())
{
	const MString namespacePath = MString(":") + rigNamespace;
	if (!MNamespace::namespaceExists(namespacePath))
	{
		fStatus = MNamespace::addNamespace(namespacePath);
		if (!fStatus)
		{
			MGlobal::displayError("Could not create the rig namespace '" + namespacePath + "': " + fStatus.errorString());
			return;
		}
	}
	fStatus = MNamespace::setCurrentNamespace(namespacePath);
}

ScopedRigNamespace::~ScopedRigNamespace()
{
	MNamespace::setCurrentNamespace(fOriginalNamespace);
}

void CreatedNamespaces::captureBefore()
{
	fBefore = MNamespace::getNamespaces(":", true);
	fCreated.clear();
}

void CreatedNamespaces::recordCreated()
{
	const MStringArray after = MNamespace::getNamespaces(":", true);
	std::vector<MString> created;
	for (unsigned int i = 0; i < after.length(); ++i)
	{
		if (fBefore.indexOf(after[i]) < 0)
		{
			created.push_back(after[i]);
		}
	}
	// Children before parents, so a parent is empty when its turn comes.
	auto depth = [](const MString& name)
	{
		return std::count(name.asChar(), name.asChar() + name.length(), ':');
	};
	std::sort(created.begin(), created.end(), [&](const MString& a, const MString& b)
	{
		return depth(a) > depth(b);
	});
	fCreated.clear();
	for (const MString& name : created)
	{
		fCreated.append(name);
	}
}

void CreatedNamespaces::removeCreated() const
{
	const MString current = MNamespace::currentNamespace();
	for (unsigned int i = 0; i < fCreated.length(); ++i)
	{
		const MString absolute = fCreated[i].asChar()[0] == ':' ? fCreated[i] : MString(":") + fCreated[i];
		if (!MNamespace::namespaceExists(absolute) || absolute == current)
		{
			continue;
		}
		// Nodes (or a child namespace) the user added since the build keep it.
		if (MNamespace::getNamespaceObjects(absolute, false).length() > 0 || MNamespace::getNamespaces(absolute, false).length() > 0)
		{
			continue;
		}
		const MStatus status = MNamespace::removeNamespace(absolute, false);
		if (!status)
		{
			MGlobal::displayWarning("Could not remove the namespace '" + absolute + "' after undo: " + status.errorString());
		}
	}
}
