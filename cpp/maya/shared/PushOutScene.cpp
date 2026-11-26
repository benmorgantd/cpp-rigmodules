#include "maya/shared/PushOutScene.h"
#include "core/Constants.h"
#include "core/JointMarkup.h"
#include "core/PushOutSolve.h"
#include "maya/nodes/PushOut.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/JointMarkupScene.h"
#include "maya/shared/Utilities.h"

#include <cmath>
#include <vector>

#include <nlohmann/json.hpp>

#include <maya/MDagPath.h>
#include <maya/MEulerRotation.h>
#include <maya/MFnAttribute.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MNamespace.h>
#include <maya/MPlug.h>
#include <maya/MPxTransform.h>
#include <maya/MSelectionList.h>
#include <maya/MTransformationMatrix.h>

namespace
{
	// The display is drawn in this blue, apart from the rig's controls and the markers.
	constexpr float DisplayWireColor[3]{ 0.2f, 0.8f, 1.0f };

	// How far a display's offset from its joint may be from a unit scale before it is refused.
	constexpr double ScaleTolerance{ 1.0e-4 };

	constexpr const char* MultMatrixType{ "multMatrix" };
	constexpr const char* ComposeMatrixType{ "composeMatrix" };

	bool isNodeOfType(const MObject& oNode, const char* typeName)
	{
		return !oNode.isNull() && MFnDependencyNode(oNode).typeName() == typeName;
	}

	// Three decimals would lose a joint-sized offset, ten make the text long: six is below any rig's tolerance.
	double rounded(double value)
	{
		return std::round(value * 1.0e6) / 1.0e6;
	}

	// The node on the other end of a matrix plug's connection, or a null object.
	MObject sourceNode(const MObject& oNode, const MObject& oAttr)
	{
		const MPlug pSource = MPlug(oNode, oAttr).source();
		return pSource.isNull() ? MObject::kNullObj : pSource.node();
	}

	struct DisplayParts
	{
		MObject oPushOut;
		MObject oDisplay;
		MObject oMult;
		MObject oCompose;
		MObject oJoint;
		MObject oRadius;
		MObject oHalfLength;
		MObject oMarker;
	};
}

void PushOutScene::findPushOuts(const MString& absNamespace, MObjectArray& oOutPushOuts)
{
	oOutPushOuts.clear();
	for (MItDependencyNodes it; !it.isDone(); it.next())
	{
		const MObject oNode = it.thisNode();
		if (MFnDependencyNode(oNode).typeId() == RigPushOutNode::id && DagUtils::getAbsoluteNamespace(oNode) == absNamespace)
		{
			oOutPushOuts.append(oNode);
		}
	}
}

void PushOutScene::findDisplays(const MString& absNamespace, MObjectArray& oOutDisplays)
{
	oOutDisplays.clear();
	for (MItDependencyNodes it; !it.isDone(); it.next())
	{
		const MObject oNode = it.thisNode();
		const MFnDependencyNode fnNode(oNode);
		if (fnNode.typeId() == RigControlNode::id && fnNode.hasAttribute(RigAttr::PushOutDisplay) && DagUtils::getAbsoluteNamespace(oNode) == absNamespace)
		{
			oOutDisplays.append(oNode);
		}
	}
}

MObject PushOutScene::followedJoint(const MObject& oDisplay)
{
	const MObject oMult = sourceNode(oDisplay, MPxTransform::offsetParentMatrix);
	if (!isNodeOfType(oMult, MultMatrixType))
	{
		return MObject::kNullObj;
	}
	MStatus status;
	const MPlug pMatrixIn = MFnDependencyNode(oMult).findPlug("matrixIn", false, &status);
	if (!status)
	{
		return MObject::kNullObj;
	}
	const MPlug pJointWorld = pMatrixIn.elementByLogicalIndex(1).source();
	return pJointWorld.isNull() ? MObject::kNullObj : pJointWorld.node();
}

MStatus PushOutScene::queueCreateDisplays(const MObjectArray& oPushOuts, MDagModifier& dagMod, MObjectArray& oOutDisplays, std::string& outError)
{
	MStatus status;
	outError.clear();
	oOutDisplays.clear();

	// The two utility nodes of a display come from matrixNodes. It is loaded by default, and loading is a no-op when it is.
	MGlobal::executeCommand("loadPlugin -quiet \"matrixNodes\"", false, false);

	std::vector<DisplayParts> parts(oPushOuts.length());
	for (unsigned int i = 0; i < oPushOuts.length(); ++i)
	{
		DisplayParts& part = parts[i];
		part.oPushOut = oPushOuts[i];
		const MString pushOutName = MNamespace::stripNamespaceFromName(MFnDependencyNode(part.oPushOut).name());

		const MPlug pJointWorld = MPlug(part.oPushOut, RigPushOutNode::aCapsuleMatrix).source();
		if (pJointWorld.isNull())
		{
			outError = std::string(pushOutName.asChar()) + " has no joint connected to follow";
			return MStatus::kFailure;
		}
		part.oJoint = pJointWorld.node();

		const MString displayName = pushOutName + "_display";
		// By absolute name: a bare name in a selection list is not looked up in the current namespace.
		const MString absNamespace = DagUtils::getAbsoluteNamespace(part.oPushOut);
		MSelectionList existing;
		if (existing.add((absNamespace == ":" ? MString(":") : absNamespace + ":") + displayName) == MStatus::kSuccess)
		{
			outError = std::string("a node named '") + displayName.asChar() + "' already exists: hide the displays first";
			return MStatus::kFailure;
		}

		part.oDisplay = dagMod.createNode(RigControlNode::id, MObject::kNullObj, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(dagMod.renameNode(part.oDisplay, displayName));
		part.oMult = dagMod.MDGModifier::createNode(MString(MultMatrixType), &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(dagMod.renameNode(part.oMult, displayName + "Offset"));
		part.oCompose = dagMod.MDGModifier::createNode(MString(ComposeMatrixType), &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(dagMod.renameNode(part.oCompose, displayName + "Size"));
	}
	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// Attributes before their connections: the size the user edits and the marker that says this node is a display.
	MFnNumericAttribute nAttr;
	MFnTypedAttribute tAttr;
	for (DisplayParts& part : parts)
	{
		part.oRadius = nAttr.create(RigAttr::DisplayRadius, RigAttr::DisplayRadius, MFnNumericData::kDouble, 1.0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		nAttr.setMin(1.0e-6);
		nAttr.setStorable(true);
		nAttr.setChannelBox(true);
		CHECK_MSTATUS_AND_RETURN_IT(dagMod.addAttribute(part.oDisplay, part.oRadius));

		part.oHalfLength = nAttr.create(RigAttr::DisplayHalfLength, RigAttr::DisplayHalfLength, MFnNumericData::kDouble, 0.0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		nAttr.setMin(0.0);
		nAttr.setStorable(true);
		nAttr.setChannelBox(true);
		CHECK_MSTATUS_AND_RETURN_IT(dagMod.addAttribute(part.oDisplay, part.oHalfLength));

		part.oMarker = tAttr.create(RigAttr::PushOutDisplay, RigAttr::PushOutDisplay, MFnData::kString, MObject::kNullObj, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		tAttr.setHidden(true);
		CHECK_MSTATUS_AND_RETURN_IT(dagMod.addAttribute(part.oDisplay, part.oMarker));
	}
	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	for (const DisplayParts& part : parts)
	{
		const MFnDependencyNode fnPushOut(part.oPushOut);
		dagMod.newPlugValueDouble(MPlug(part.oDisplay, part.oRadius), MPlug(part.oPushOut, RigPushOutNode::aRadius).asDouble());
		dagMod.newPlugValueDouble(MPlug(part.oDisplay, part.oHalfLength), MPlug(part.oPushOut, RigPushOutNode::aHalfLength).asDouble());
		dagMod.newPlugValueString(MPlug(part.oDisplay, part.oMarker), fnPushOut.name());

		dagMod.newPlugValueShort(MPlug(part.oDisplay, RigControlNode::aShapeType), static_cast<short>(ShapeType::Capsule));
		dagMod.newPlugValueBool(MPlug(part.oDisplay, RigControlNode::aFilled), true);
		dagMod.newPlugValueBool(MPlug(part.oDisplay, RigControlNode::aXray), true);
		MPlug pColor(part.oDisplay, RigControlNode::aWireColor);
		for (unsigned int channel = 0; channel < 3; ++channel)
		{
			dagMod.newPlugValueFloat(pColor.child(channel), DisplayWireColor[channel]);
		}

		// The size drives the shape: the diagonal is (halfLength, radius, radius), which RigControlNode reads as the capsule.
		const MFnDependencyNode fnCompose(part.oCompose);
		const MPlug pRadius(part.oDisplay, part.oRadius);
		const MPlug pHalfLength(part.oDisplay, part.oHalfLength);
		CHECK_MSTATUS_AND_RETURN_IT(dagMod.connect(pHalfLength, fnCompose.findPlug("inputScaleX", false, &status)));
		CHECK_MSTATUS_AND_RETURN_IT(dagMod.connect(pRadius, fnCompose.findPlug("inputScaleY", false, &status)));
		CHECK_MSTATUS_AND_RETURN_IT(dagMod.connect(pRadius, fnCompose.findPlug("inputScaleZ", false, &status)));
		CHECK_MSTATUS_AND_RETURN_IT(dagMod.connect(fnCompose.findPlug("outputMatrix", false, &status), MPlug(part.oDisplay, RigControlNode::aShapeTransform)));

		// The display's place is the stored offset times the joint's live world.
		const MFnDependencyNode fnMult(part.oMult);
		MPlug pMatrixIn = fnMult.findPlug("matrixIn", false, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		MPlug pOffsetIn = pMatrixIn.elementByLogicalIndex(0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(DagUtils::setMatrixPlug(pOffsetIn, DagUtils::getMatrixFromPlug(MPlug(part.oPushOut, RigPushOutNode::aCapsuleOffset)), dagMod));
		MPlug pJointIn = pMatrixIn.elementByLogicalIndex(1, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		MPlug pJointWorld = MPlug(part.oJoint, MPxTransform::worldMatrix).elementByLogicalIndex(0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(dagMod.connect(pJointWorld, pJointIn));
		CHECK_MSTATUS_AND_RETURN_IT(dagMod.connect(fnMult.findPlug("matrixSum", false, &status), MPlug(part.oDisplay, MPxTransform::offsetParentMatrix)));
	}
	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	for (const DisplayParts& part : parts)
	{
		oOutDisplays.append(part.oDisplay);
	}
	return MStatus::kSuccess;
}

MStatus PushOutScene::queueDeleteDisplays(const MObjectArray& oDisplays, MDagModifier& dagMod)
{
	for (unsigned int i = 0; i < oDisplays.length(); ++i)
	{
		const MObject oMult = sourceNode(oDisplays[i], MPxTransform::offsetParentMatrix);
		const MObject oCompose = sourceNode(oDisplays[i], RigControlNode::aShapeTransform);
		// The utility nodes first, so the display is the last thing deleted with nothing left feeding it.
		if (isNodeOfType(oMult, MultMatrixType))
		{
			CHECK_MSTATUS_AND_RETURN_IT(dagMod.deleteNode(oMult));
		}
		if (isNodeOfType(oCompose, ComposeMatrixType))
		{
			CHECK_MSTATUS_AND_RETURN_IT(dagMod.deleteNode(oCompose));
		}
		CHECK_MSTATUS_AND_RETURN_IT(dagMod.deleteNode(oDisplays[i]));
	}
	return MStatus::kSuccess;
}

bool PushOutScene::readCapsule(const MObject& oDisplay, std::string& outCapsuleJson, std::string& outError)
{
	const MObject oJoint = followedJoint(oDisplay);
	if (oJoint.isNull())
	{
		outError = "the display follows no joint, so it is not one made by showRigPushOuts";
		return false;
	}
	bool hasMarkup = false;
	JointMarkup markup;
	if (!JointMarkupScene::readMarkup(oJoint, hasMarkup, markup) || !hasMarkup)
	{
		outError = std::string(MFnDependencyNode(oJoint).name().asChar()) + " has no joint markup, so it cannot be named in the template";
		return false;
	}

	MDagPath displayPath;
	MDagPath jointPath;
	if (!MDagPath::getAPathTo(oDisplay, displayPath) || !MDagPath::getAPathTo(oJoint, jointPath))
	{
		outError = "the display or its joint is not in the DAG";
		return false;
	}
	const MMatrix mOffset = displayPath.inclusiveMatrix() * jointPath.inclusiveMatrixInverse();

	// A capsule has no scale: its size is radius and halfLength. A scaled display would be saved as something else.
	for (int axis = 0; axis < 3; ++axis)
	{
		const double length = std::sqrt(mOffset[axis][0] * mOffset[axis][0] + mOffset[axis][1] * mOffset[axis][1] + mOffset[axis][2] * mOffset[axis][2]);
		if (std::fabs(length - 1.0) > ScaleTolerance)
		{
			outError = "the display is scaled: set its radius and halfLength instead, a capsule has no scale";
			return false;
		}
	}

	const MFnDependencyNode fnDisplay(oDisplay);
	const double radius = fnDisplay.findPlug(RigAttr::DisplayRadius, false).asDouble();
	const double halfLength = fnDisplay.findPlug(RigAttr::DisplayHalfLength, false).asDouble();
	const MEulerRotation euler = MTransformationMatrix(mOffset).eulerRotation();

	nlohmann::ordered_json capsule = nlohmann::ordered_json::object();
	capsule[TemplateKey::CapsuleFollow] = { markup.side, markup.usage };
	capsule[TemplateKey::CapsuleRadius] = rounded(radius);
	capsule[TemplateKey::CapsuleHalfLength] = rounded(halfLength);
	capsule[TemplateKey::CapsuleOffset][TemplateKey::OffsetTranslate] = { rounded(mOffset[3][0]), rounded(mOffset[3][1]), rounded(mOffset[3][2]) };
	capsule[TemplateKey::CapsuleOffset][TemplateKey::OffsetRotate] = { rounded(VectorMath::toDegrees(euler.x)), rounded(VectorMath::toDegrees(euler.y)),
		rounded(VectorMath::toDegrees(euler.z)) };
	outCapsuleJson = capsule.dump();
	return true;
}
