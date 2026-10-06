#include "AimModule.h"
#include "RigControlNode.h"
#include "RigJsonStructs.h"

#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnTransform.h>
#include <maya/MFnMatrixData.h>
#include <maya/MMatrix.h>
#include <maya/MDataHandle.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MPlug.h>
#include <maya/MSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MGlobal.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>
#include <maya/MArrayDataBuilder.h>

// ------------------------------------------------------------------
// AimModuleNode Implementation
// ------------------------------------------------------------------

MTypeId AimModuleNode::id(0x00218D);

MObject AimModuleNode::aInputRestMatrix;
MObject AimModuleNode::aControlMatrix;
MObject AimModuleNode::aOutputControlOPM;
MObject AimModuleNode::aOutputJointOPM;

AimModuleNode::AimModuleNode() {}
AimModuleNode::~AimModuleNode() {}

const MString AimModuleNode::aCommandString = "aimModule";

void* AimModuleNode::creator()
{
	return new AimModuleNode();
}

MStatus AimModuleNode::initialize()
{
	MStatus status;

	status = RigModuleNodeBase::initializeBaseAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MFnMatrixAttribute mAttr;

	// 1. INPUT ARRAYS
	aInputRestMatrix = mAttr.create("inputRestMatrix", "irm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setStorable(true);
	mAttr.setKeyable(true);
	addAttribute(aInputRestMatrix);

	aControlMatrix = mAttr.create("controlMatrix", "cm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setStorable(true);
	mAttr.setKeyable(true);
	addAttribute(aControlMatrix);

	// 2. OUTPUT ARRAYS
	aOutputControlOPM = mAttr.create("outputControlOPM", "copm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setUsesArrayDataBuilder(true);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	addAttribute(aOutputControlOPM);

	aOutputJointOPM = mAttr.create("outputJointOPM", "jopm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setUsesArrayDataBuilder(true);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	addAttribute(aOutputJointOPM);

	// 3. AFFECTS RELATIONSHIPS
	attributeAffects(aParentWorldMatrix, aOutputControlOPM);
	attributeAffects(aInputRestMatrix, aOutputControlOPM);

	attributeAffects(aControlMatrix, aOutputJointOPM);
	attributeAffects(aInputRestMatrix, aOutputJointOPM);
	attributeAffects(aParentWorldMatrix, aOutputJointOPM);

	attributeAffects(aControlMatrix, aOutputSocketMatrix);
	attributeAffects(aParentWorldMatrix, aOutputSocketMatrix);
	attributeAffects(aInputRestMatrix, aOutputSocketMatrix);

	return MStatus::kSuccess;
}

MStatus AimModuleNode::evaluateModuleSolver(const MPlug& plug, MDataBlock& data)
{
	unsigned int index = plug.isElement() ? plug.logicalIndex() : 0;

	MMatrix mParentWorld = getInputMatrix(data, aParentWorldMatrix);
	MMatrix mParentOffset = getInputMatrix(data, aParentModuleOffset);

	// Module base world matrix (Row-Major: Local Step * Parent World)
	MMatrix mModuleBaseWorld = mParentOffset * mParentWorld;

	// -------------------------------------------------------------------
	// 1. Output Control Offset Parent Matrix Array
	// -------------------------------------------------------------------
	if (plug == aOutputControlOPM || plug.array() == aOutputControlOPM)
	{
		// Place controls in world space using their rest offsets relative to module base
		for (unsigned int i = 0; i < 3; ++i)
		{
			MMatrix mRestLocal = getInputMatrix(data, aInputRestMatrix, i);
			MMatrix mControlOPM = mRestLocal * mModuleBaseWorld;
			setOutputMatrix(data, aOutputControlOPM, i, mControlOPM);
		}

		MArrayDataHandle hOutControlArray = data.outputArrayValue(aOutputControlOPM);
		hOutControlArray.setAllClean();
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	// -------------------------------------------------------------------
	// 2. Output Joint Offset Parent Matrix & Socket Matrix Arrays
	// Index 0: Aim Joint, Index 1: Target Joint, Index 2: Up Vector Joint
	// -------------------------------------------------------------------
	if (plug == aOutputJointOPM || plug.array() == aOutputJointOPM ||
		plug == aOutputSocketMatrix || plug.array() == aOutputSocketMatrix)
	{
		// Step A: Calculate Unconstrained Base World Transforms for Inputs
		MMatrix mRest0 = getInputMatrix(data, aInputRestMatrix, 0);
		MMatrix mControl0 = getInputMatrix(data, aControlMatrix, 0);
		MMatrix mAimBaseWorld = mControl0 * mRest0 * mModuleBaseWorld;

		MMatrix mRest1 = getInputMatrix(data, aInputRestMatrix, 1);
		MMatrix mControl1 = getInputMatrix(data, aControlMatrix, 1);
		MMatrix mTargetWorld = mControl1 * mRest1 * mModuleBaseWorld;

		MMatrix mRest2 = getInputMatrix(data, aInputRestMatrix, 2);
		MMatrix mControl2 = getInputMatrix(data, aControlMatrix, 2);
		MMatrix mUpWorld = mControl2 * mRest2 * mModuleBaseWorld;

		// Step B: Extract World Space Positions (Row 3 in Maya MMatrix)
		MVector pAim(mAimBaseWorld[3][0], mAimBaseWorld[3][1], mAimBaseWorld[3][2]);
		MVector pTarget(mTargetWorld[3][0], mTargetWorld[3][1], mTargetWorld[3][2]);
		MVector pUp(mUpWorld[3][0], mUpWorld[3][1], mUpWorld[3][2]);

		// Step C: Compute Orthonormal Aim Basis (Aim = +X, Up = +Z)
		// 1. Primary Aim Axis (+X)
		MVector vAim = pTarget - pAim;
		double aimLen = vAim.length();
		MVector hatX;

		if (aimLen > 1e-6)
		{
			hatX = vAim / aimLen;
		}
		else
		{
			// Fallback to base orientation X axis if target overlaps origin
			hatX = MVector(mAimBaseWorld[0][0], mAimBaseWorld[0][1], mAimBaseWorld[0][2]).normal();
		}

		// 2. Up Direction Vector (+Z raw direction)
		MVector vUpRaw = pUp - pAim;
		if (vUpRaw.length() < 1e-6)
		{
			vUpRaw = MVector(mAimBaseWorld[2][0], mAimBaseWorld[2][1], mAimBaseWorld[2][2]);
		}

		// 3. Side Axis (+Y) via Cross Product
		// In a right-handed system with +X Aim and +Z Up:
		// hatZ x hatX = hatY
		MVector hatY = vUpRaw ^ hatX; // ^ operator is cross product in Maya API
		double yLen = hatY.length();

		if (yLen > 1e-6)
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

		// Step D: Build Solved Aim World Matrix (Row-Major)
		double mData[4][4];

		// Row 0: +X Aim Axis
		mData[0][0] = hatX.x; mData[0][1] = hatX.y; mData[0][2] = hatX.z; mData[0][3] = 0.0;

		// Row 1: +Y Side Axis
		mData[1][0] = hatY.x; mData[1][1] = hatY.y; mData[1][2] = hatY.z; mData[1][3] = 0.0;

		// Row 2: +Z Up Axis
		mData[2][0] = hatZ.x; mData[2][1] = hatZ.y; mData[2][2] = hatZ.z; mData[2][3] = 0.0;

		// Row 3: Aim Joint Position
		mData[3][0] = pAim.x; mData[3][1] = pAim.y; mData[3][2] = pAim.z; mData[3][3] = 1.0;

		MMatrix mAimSolvedWorld(mData);

		// Step E: Write Solved Outputs
		setOutputMatrix(data, aOutputJointOPM, 0, mAimSolvedWorld);
		setOutputMatrix(data, aOutputJointOPM, 1, mTargetWorld);
		setOutputMatrix(data, aOutputJointOPM, 2, mUpWorld);

		setOutputMatrix(data, aOutputSocketMatrix, 0, mAimSolvedWorld);
		setOutputMatrix(data, aOutputSocketMatrix, 1, mTargetWorld);
		setOutputMatrix(data, aOutputSocketMatrix, 2, mUpWorld);

		// Clean array handles in-place
		MArrayDataHandle hOutJointArray = data.outputArrayValue(aOutputJointOPM);
		hOutJointArray.setAllClean();

		MArrayDataHandle hOutSocketArray = data.outputArrayValue(aOutputSocketMatrix);
		hOutSocketArray.setAllClean();

		data.setClean(plug);
		return MStatus::kSuccess;
	}

	return MStatus::kUnknownParameter;
}

MObject AimModuleNode::createModule(
	const MString& moduleName,
	const MDagPath& aimJoint,
	const MDagPath& targetJoint,
	const MDagPath& upJoint,
	MObject oParentModule,
	unsigned int parentModuleSocketIndex,
	MObject oRigRoot,
	MDGModifier& dgMod,
	MDagModifier& dagMod,
	MStatus* status)
{
	MStatus localStat;

	// 1. Create the AimModule node
	MObject oModule = dgMod.createNode("aimModule");
	dgMod.renameNode(oModule, moduleName);

	// Retrieve module plugs
	MFnDependencyNode moduleFn(oModule);
	MPlug inputRestMatrix = moduleFn.findPlug("inputRestMatrix", false);
	MPlug controlMatrix = moduleFn.findPlug("controlMatrix", false);
	MPlug outputControlOPM = moduleFn.findPlug("outputControlOPM", false);
	MPlug outputJointOPM = moduleFn.findPlug("outputJointOPM", false);

	const MDagPath jointDags[3] = { aimJoint, targetJoint, upJoint };
	MMatrix mFirstControlWorld = MMatrix::identity;
	const unsigned int numJoints = 3;

	// Set the numSockets attribute for downstream children modules
	MPlug pNumSockets = moduleFn.findPlug("numSockets", false);
	if (!pNumSockets.isNull())
	{
		dgMod.newPlugValueShort(pNumSockets, static_cast<short>(numJoints));
	}

	for (unsigned int i = 0; i < numJoints; ++i)
	{
		MDagPath jointDag = jointDags[i];

		MFnDagNode jointFnDag(jointDag);
		MPlug jointOpmPlug = jointFnDag.findPlug("offsetParentMatrix", false);
		MPlug jointParentInvMatPlug = jointFnDag.findPlug("parentInverseMatrix", false);
		MMatrix mJointWorld = jointDag.inclusiveMatrix();

		// Fetch parent inverse matrix value
		MObject parentInvObj;
		jointParentInvMatPlug.elementByLogicalIndex(0).getValue(parentInvObj);
		MFnMatrixData parentInvData(parentInvObj);
		MMatrix mJointParentInv = parentInvData.matrix();

		// Bake joint world transform relative to parent into offsetParentMatrix
		MMatrix jointOffsetParentMatrix = mJointWorld * mJointParentInv;
		MFnMatrixData matrixData;
		MObject opmMatrixDataObject = matrixData.create(jointOffsetParentMatrix);
		jointOpmPlug.setValue(opmMatrixDataObject);

		// Zero out the joint's local transform channels
		MFnTransform jointFnTrans(jointDag);
		jointFnTrans.set(MTransformationMatrix::identity);

		// Store rest matrix on module input plug
		MPlug inputRestPlug = inputRestMatrix.elementByLogicalIndex(i);
		inputRestPlug.setValue(opmMatrixDataObject);

		// Create control transform for this element
		MObject controlTransform = RigControlNode::createRigControl(oModule, dagMod, jointDag.partialPathName());

		// Wire control and joint matrix plugs to module
		MFnDependencyNode controlMFn(controlTransform);
		MPlug controlMatrixPlug = controlMFn.findPlug("matrix", false);
		MPlug controlOpmPlug = controlMFn.findPlug("offsetParentMatrix", false);

		dgMod.connect(controlMatrixPlug, controlMatrix.elementByLogicalIndex(i));
		dgMod.connect(outputControlOPM.elementByLogicalIndex(i), controlOpmPlug);
		dgMod.connect(outputJointOPM.elementByLogicalIndex(i), jointOpmPlug);

		if (i == 0)
		{
			dgMod.doIt();
			MSelectionList sel;
			sel.add(controlTransform);
			MDagPath firstControlDag;
			sel.getDagPath(0, firstControlDag);
			mFirstControlWorld = firstControlDag.inclusiveMatrix();
		}
	}

	// 2. Wire parent module socket connection
	if (!oParentModule.isNull())
	{
		localStat = RigModuleCommandHelpers::connectToParentModule(
			oParentModule,
			oModule,
			parentModuleSocketIndex,
			dgMod,
			mFirstControlWorld
		);

		if (!localStat)
		{
			MGlobal::displayError("Failed to connect Aim module to parent.");
			if (status)
			{
				*status = localStat;
			}
			return MObject::kNullObj;
		}
	}

	// 3. Wire module to rig root
	if (!oRigRoot.isNull())
	{
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dgMod, oModule);
	}

	dgMod.doIt();
	dagMod.doIt();

	if (status)
	{
		*status = MStatus::kSuccess;
	}
	return oModule;
}


MObject AimModuleNode::createModule(
	const RigModuleData& moduleData,
	MObject rigRoot,
	MObject parentModule,
	MDGModifier& dgMod,
	MDagModifier& dagMod)
{
	MStatus status;

	// 1. Extract AimModuleArgs from the moduleArgs variant
	const auto* aimArgs = std::get_if<AimModuleArgs>(&moduleData.moduleArgs);
	if (!aimArgs)
	{
		MGlobal::displayError(MString("Invalid moduleArgs variant type for AimModule: ") + moduleData.name.c_str());
		return MObject::kNullObj;
	}

	// 2. Resolve string joint names into MDagPath instances
	const std::string jointNames[3] = { aimArgs->aimJoint, aimArgs->targetJoint, aimArgs->upJoint };
	MDagPath jointDags[3];

	for (unsigned int i = 0; i < 3; ++i)
	{
		MSelectionList selList;
		status = selList.add(jointNames[i].c_str());
		if (status && selList.length() > 0)
		{
			status = selList.getDagPath(0, jointDags[i]);
			if (!status)
			{
				MGlobal::displayError(MString("Failed to retrieve MDagPath for joint: ") + jointNames[i].c_str());
				return MObject::kNullObj;
			}
		}
		else
		{
			MGlobal::displayError(MString("Could not find joint in scene: ") + jointNames[i].c_str());
			return MObject::kNullObj;
		}
	}

	// 3. Resolve parent socket index safely
	unsigned int socketIndex = 0;
	if (moduleData.parentSocketIndex >= 0)
	{
		socketIndex = static_cast<unsigned int>(moduleData.parentSocketIndex);
	}
	else
	{
		short numSockets = 0;
		if (!parentModule.isNull())
		{
			MFnDependencyNode fnParent(parentModule);
			MPlug pNumSockets = fnParent.findPlug("numSockets", false);
			if (!pNumSockets.isNull())
			{
				numSockets = pNumSockets.asShort();
			}

			if (numSockets > 0)
			{
				socketIndex = static_cast<unsigned int>(numSockets - 1);
			}
			else
			{
				MGlobal::displayError(MString("Parent module did not define numSockets: ") + MString(fnParent.name()));
				socketIndex = 0;
			}
		}
	}

	// 4. Delegate to the explicit creation method
	MStatus executionStatus;
	MObject oModule = AimModuleNode::createModule(
		moduleData.name.c_str(),
		jointDags[0],
		jointDags[1],
		jointDags[2],
		parentModule,
		socketIndex,
		rigRoot,
		dgMod,
		dagMod,
		&executionStatus
	);

	if (!executionStatus)
	{
		MGlobal::displayError(MString("Failed to build Aim module: ") + moduleData.name.c_str());
		return MObject::kNullObj;
	}

	return oModule;
}