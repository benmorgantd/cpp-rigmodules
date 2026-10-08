#include "Utilities.h"
#include <maya/MMatrix.h>
#include <maya/MVector.h>
#include <maya/MObject.h>
#include <maya/MFnTransform.h>
#include <maya/MPlug.h>
#include <maya/MMatrix.h>
#include <maya/MFnMatrixData.h>
#include <maya/MDagPath.h>


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

// Bakes joint world transform relative to its parent into its offsetParentMatrix, 
// zeroes out local channels, and sets the result on the provided input rest matrix plug
MMatrix DagUtils::bakeJointOpmAndRestMatrix(const MDagPath& jointDag, MPlug& pRestMatrixPlug)
{
	MFnDagNode jointFnDag(jointDag);
	MPlug pJointOpm = jointFnDag.findPlug("offsetParentMatrix", false);
	MPlug pJointParentInvMat = jointFnDag.findPlug("parentInverseMatrix", false);

	MMatrix mJointWorld = jointDag.inclusiveMatrix();

	MObject parentInvObj;
	pJointParentInvMat.elementByLogicalIndex(0).getValue(parentInvObj);
	MFnMatrixData parentInvData(parentInvObj);
	MMatrix mJointParentInv = parentInvData.matrix();

	// Calculate offsetParentMatrix relative to parent world space
	MMatrix mJointOffsetParent = mJointWorld * mJointParentInv;
	MFnMatrixData matrixData;
	MObject oMatrixData = matrixData.create(mJointOffsetParent);

	pJointOpm.setValue(oMatrixData);

	// Zero out local transform channels
	MFnTransform jointFnTrans(jointDag);
	jointFnTrans.set(MTransformationMatrix::identity);

	// Store value in the rest matrix plug
	pRestMatrixPlug.setValue(oMatrixData);

	return mJointOffsetParent;
}