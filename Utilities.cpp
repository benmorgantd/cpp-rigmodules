#include "Utilities.h"
#include "RigModule.h"
#include "Constants.h"

#include <maya/MMatrix.h>
#include <maya/MString.h>
#include <maya/MVector.h>
#include <maya/MObject.h>
#include <maya/MFnTransform.h>
#include <maya/MPlug.h>
#include <maya/MFnMatrixData.h>
#include <maya/MDagPath.h>
#include <maya/MGlobal.h>
#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MDagPathArray.h>
#include <maya/MObjectArray.h>
#include <maya/MSelectionList.h>
#include <maya/MSyntax.h>
#include <maya/MDagModifier.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MStatus.h>
#include <maya/MStringArray.h>

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

// ---------------------------------
// DAG Utilities
// ---------------------------------
// Locks and hides rotation and scale channels on a control object
void DagUtils::lockAndHideTranslate(MObject oControl)
{
	MFnTransform fnControl(oControl);
	MPlug pTranslate = fnControl.findPlug("translate", false);
	pTranslate.setLocked(true);
	pTranslate.setKeyable(false);
	pTranslate.setChannelBox(false);
}

void DagUtils::lockAndHideRotate(MObject oControl)
{
	MFnTransform fnControl(oControl);
	MPlug pRotation = fnControl.findPlug("rotate", false);
	pRotation.setLocked(true);
	pRotation.setKeyable(false);
	pRotation.setChannelBox(false);
}

void DagUtils::lockAndHideScale(MObject oControl)
{
	MFnTransform fnControl(oControl);
	MPlug pScale = fnControl.findPlug("scale", false);
	pScale.setLocked(true);
	pScale.setKeyable(false);
	pScale.setChannelBox(false);
}

// Bakes joint world transform relative to its parent into its opm and zeroes its transforms.
// TODO: pass in transform matrix gathered from read phase.
// NOTE: this is already happening when inheritsTransforms has been turned off, so we just need to write the world matrix to OPM
MStatus DagUtils::bakeJointTransformsToOPM(const MObject& oJoint, const MMatrix& mTransformWorld, MDagModifier& dagMod)
{
	MStatus status;
	MFnTransform fnTransform(oJoint);
	if (!fnTransform.object().hasFn(MFn::kJoint))
	{
		MGlobal::displayError("Given object must be a joint");
		return MStatus::kFailure;
	}
	
	MPlug pInheritsTransforms = fnTransform.findPlug("inheritsTransforms", false);
	if (pInheritsTransforms.asBool() == true)
	{
		MGlobal::displayError("Given joint must have inheritsTransforms off before baking to OPM");
		return MStatus::kFailure;
	}

	// First zero out the current local matrix joint orient so that we can position the joint in world space locally using OPM
	// TODO: separate this into a fn
	for (const char* axis : { "X", "Y", "Z" })
	{
		MPlug pJointOrient = fnTransform.findPlug(MString("jointOrient") + axis, false, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = dagMod.newPlugValueDouble(pJointOrient, 0.0);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}

	// Zero out local matrix
	status = zeroLocalTransforms(oJoint, dagMod);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// Now that we've zeroed out all transforms, we can place the joint in world space using its OPM (because inherits transforms is off)
	MPlug pOffsetParentMatrix = fnTransform.findPlug("offsetParentMatrix", false, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	status = DagUtils::setMatrixPlug(pOffsetParentMatrix, mTransformWorld, dagMod);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	return status;
}

MStatus DagUtils::zeroLocalTransforms(const MObject& oTransform, MDagModifier& dagMod)
{
	if (oTransform.isNull() || !oTransform.hasFn(MFn::kTransform))
	{
		MGlobal::displayError("Given object is null or is not a transform");
		return MStatus::kFailure;
	}

	MFnTransform fnTransform(oTransform);
	for (const char* xform : { "translate", "rotate", "scale" })
	{
		for (const char* axis : { "X", "Y", "Z" })
		{
			MPlug pTransform = fnTransform.findPlug(MString(xform) + axis, false);

			if (!pTransform.isNull() && !pTransform.isLocked())
			{
				dagMod.newPlugValueDouble(pTransform, (xform == "scale") ? 1.0 : 0.0);
			}
			else
			{
				MGlobal::displayError(MString("Plug was locked and could not be zeroed: ") + xform + axis);
			}
		}
	}
	return MStatus::kSuccess;
}

MStatus DagUtils::setInheritsTransforms(const MObject& transform, bool value, MDagModifier& dagMod)
{
	if (!transform.hasFn(MFn::kTransform))
	{
		MGlobal::displayError("Given object is not a DAG transform, cannot set inheritsTransforms.");
		return MStatus::kFailure;
	}

	MFnDependencyNode fnTransform(transform);
	MPlug pInheritsTransform = fnTransform.findPlug("inheritsTransform", false);

	if (pInheritsTransform.isNull())
	{
		return MStatus::kFailure;
	}

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
	MPlug pSegmentScaleCompensate = fnTransform.findPlug("segmentScaleCompensate", false);

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

MDagPath DagUtils::getDagPathFromName(MString& name)
{
	MDagPath jointDag;
	MStatus status;
	MSelectionList selList;
	status = selList.add(name);

	if (status && selList.length() > 0)
	{
		status = selList.getDagPath(0, jointDag);
	}
	else
	{
		MGlobal::displayError(MString("Failed to find a dag path from given string ") + name);
	}

	return jointDag;
}

// DAG plug and socket utils -----------------------------------------------
MStatus DagPlugAndSocketUtils::setParentSocketName(const MObject& oModule, const unsigned int index, const char* name, MDagModifier& dagMod)
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

	return dagMod.newPlugValueString(pSocketNamesElement, MString(name));
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

// String Utils -----------------------------------------------------------
// Separate a string by semicolons and see if the given token is in it 
bool StringUtils::nameListContains(const MString& nameList, const MString& token)
{
	MStringArray splitString;
	nameList.split(';', splitString);

	for (unsigned int i = 0; i < splitString.length(); ++i)
	{
		if (splitString[i] == token)
		{
			return true;
		}
	}
	return false;
}


// RigModuleCommandHelpers ------------------------------------------------
const char* RigModuleCommandUtils::kNameFlagShort = "-n";
const char* RigModuleCommandUtils::kNameFlagLong = "-name";
const char* RigModuleCommandUtils::kJointsFlagShort = "-j";
const char* RigModuleCommandUtils::kJointsFlagLong = "-joints";
const char* RigModuleCommandUtils::kParentModuleShort = "-pm";
const char* RigModuleCommandUtils::kParentModuleLong = "-parentModule";
const char* RigModuleCommandUtils::kParentSocketIndexShort = "-psi";
const char* RigModuleCommandUtils::kParentSocketIndexLong = "-parentSocketIndex";
const char* RigModuleCommandUtils::kRigRootLong = "-rigRoot";
const char* RigModuleCommandUtils::kRigRootShort = "-rr";


MString RigModuleCommandUtils::getModuleNameFromArgs(const MArgDatabase& argData)
{
	// Gather the passed in module name
	MString moduleName = "fkChainModule_01";
	if (argData.isFlagSet(kNameFlagShort))
	{
		argData.getFlagArgument(kNameFlagShort, 0, moduleName);
	}

	return moduleName;
}

MObject RigModuleCommandUtils::getRigRootFromArgs(const MArgDatabase& argData)
{
	MString rigRootName;
	MObject oRigRoot;
	MStatus status;

	if (argData.isFlagSet(kRigRootShort))
	{
		status = argData.getFlagArgument(kRigRootShort, 0, rigRootName);

		if (status == MStatus::kSuccess && !rigRootName.isEmpty())
		{
			MSelectionList sel;
			status = sel.add(rigRootName);

			if (status == MStatus::kSuccess)
			{
				status = sel.getDependNode(0, oRigRoot);
			}
		}
	}
	return oRigRoot;
}

MDagPathArray RigModuleCommandUtils::getJointsFromArgs(const MArgDatabase& argData, MStatus& status)
{
	MDagPathArray jointDags;
	MObjectArray jointObjs;
	unsigned int numJoints = 0;

	if (argData.isFlagSet(kJointsFlagShort))
	{
		numJoints = argData.numberOfFlagUses(kJointsFlagShort);

		for (unsigned int i = 0; i < numJoints; ++i)
		{
			MArgList flagArgs;
			// Fetch the argument list for the i-th occurance of the joints flag
			status = argData.getFlagArgumentList(kJointsFlagShort, i, flagArgs);
			if (status == MStatus::kSuccess)
			{
				for (unsigned int j = 0; j < flagArgs.length(); ++j)
				{
					MString jointName = flagArgs.asString(j, &status);
					MDagPath jointDag;
					MSelectionList jointSel;
					jointSel.add(jointName);
					status = jointSel.getDagPath(0, jointDag);

					if (status == MStatus::kSuccess && !jointDag.node().isNull())
					{
						jointDags.append(jointDag);
					}
					else
					{
						MGlobal::displayError("Failure getting joint depend node: " + jointName);
						status = MStatus::kFailure;
					}

				}
			}
		}
	}

	if (jointDags.length() == 0 || numJoints == 0)
	{
		MGlobal::displayError("At least one joint must be given with the -j flag.");
		return MDagPathArray();
	}
	return jointDags;
}

MSyntax RigModuleCommandUtils::createBaseModuleSyntax(MStatus& status)
{
	MSyntax syntax;
	status = syntax.addFlag(kNameFlagShort, kNameFlagLong, MSyntax::kString);
	status = syntax.addFlag(kJointsFlagShort, kJointsFlagLong, MSyntax::kString);
	status = syntax.makeFlagMultiUse(kJointsFlagShort);
	status = syntax.addFlag(kParentModuleShort, kParentModuleLong, MSyntax::kString);
	status = syntax.addFlag(kParentSocketIndexShort, kParentSocketIndexLong, MSyntax::kLong);
	status = syntax.addFlag(kRigRootShort, kRigRootLong, MSyntax::kString);
	return syntax;
}

