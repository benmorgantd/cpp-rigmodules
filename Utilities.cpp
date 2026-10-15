#include "Utilities.h"
#include "RigModule.h"

#include <maya/MMatrix.h>
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
#include <maya/MDGModifier.h>
#include <maya/MFnDependencyNode.h>

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
MMatrix DagUtils::bakeTransformsToOPM(const MDagPath& jointDag)
{
	MFnDagNode jointFnDag(jointDag);
	MPlug pJointParentInvMat = jointFnDag.findPlug("parentInverseMatrix", false);

	MMatrix mJointWorld = jointDag.inclusiveMatrix();

	MObject parentInvObj;
	pJointParentInvMat.elementByLogicalIndex(0).getValue(parentInvObj);
	MFnMatrixData parentInvData(parentInvObj);
	MMatrix mJointParentInv = parentInvData.matrix();

	// Calculate offsetParentMatrix relative to parent world space
	// TODO: do we need to include orient?
	MMatrix mJointOffsetParent = mJointWorld * mJointParentInv;
	MFnMatrixData matrixData;
	MObject oMatrixData = matrixData.create(mJointOffsetParent);

	MPlug pOffsetParentMatrix = jointFnDag.findPlug("offsetParentMatrix", false);
	pOffsetParentMatrix.setValue(oMatrixData);

	// Zero out local transform channels
	MFnTransform jointFnTrans(jointDag);
	jointFnTrans.set(MTransformationMatrix::identity);

	return mJointOffsetParent;
}


void DagUtils::setMatrixPlug(MPlug& matrixPlug, MMatrix matrix)
{
	if (!matrixPlug.isNull() && !matrixPlug.isArray())
	{
		MFnMatrixData fnMatrixData;
		MObject oMatrixData = fnMatrixData.create(matrix);
		matrixPlug.setValue(oMatrixData);
	}
	else
	{
		MGlobal::displayError("The given matrix plug was null or an array.");
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

