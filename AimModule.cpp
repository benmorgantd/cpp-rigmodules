#include "AimModule.h"
#include "RigControlNode.h"
#include "RigJsonStructs.h"
#include "Utilities.h"
#include "RigModule.h"
#include "Side.h"

#include <string>
#include <nlohmann/json.hpp>

#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MDataBlock.h>
#include <maya/MMatrix.h>
#include <maya/MPoint.h>
#include <maya/MPlug.h>
#include <maya/MGlobal.h>
#include <maya/MDagModifier.h>

MTypeId AimModuleNode::id(0x00218D);
MObject AimModuleNode::aAimRestOffset;

namespace
{
	// Used for both child plug names and parent socket names. Index matches AimPart.
	const char* const kAimPartNames[AimPart::Count] = { "aim", "target", "up" };
}

AimModuleNode::AimModuleNode() {}
AimModuleNode::~AimModuleNode() {}

void* AimModuleNode::creator()
{
	return new AimModuleNode();
}

MStatus AimModuleNode::initialize()
{
	MStatus status;
	MFnMatrixAttribute mAttr;

	status = initializeSharedAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	aAimRestOffset = mAttr.create("aimRestOffset", "aimRestOffset", MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	mAttr.setStorable(true);
	status = addAttribute(aAimRestOffset);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// AFFECTS RELATIONSHIPS
	declareStandardAffects();
	attributeAffects(aAimRestOffset, aJointOPM);
	attributeAffects(aAimRestOffset, aOutputSocketMatrix);

	return MStatus::kSuccess;
}

// Solves all three parts once. Cheap enough that every output array re-solves rather than caching between computes.
MStatus AimModuleNode::solveAim(MDataBlock& data, AimSolveResult& outResult)
{
	MStatus status;

	MArrayDataHandle hControlInputData = data.inputArrayValue(aControlInputData, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hChildPlugs = data.inputArrayValue(aChildPlugs, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// 1. Each part is an independent control step carried by its own parent socket.
	for (unsigned int i = 0; i < AimPart::Count; ++i)
	{
		MMatrix mControlLocal = MMatrix::identity;     // stays identity for the aim part, nothing is wired to it
		MMatrix mControlRestWorld = MMatrix::identity;
		readControlInput(hControlInputData, i, mControlLocal, mControlRestWorld);

		const MMatrix mParentDelta = readChildPlugParentDelta(hChildPlugs, i);
		const ControlSolveStep step = solveControlStep(mControlLocal, mControlRestWorld, mParentDelta);

		outResult.mControlOPM[i] = step.mOPM;
		outResult.mPartWorld[i] = step.mWorld;
	}

	// 2. Aim solve (+X toward target, +Z toward up) from the three world positions.
	const MMatrix& mAimWorld = outResult.mPartWorld[AimPart::Aim];
	const MMatrix& mTargetWorld = outResult.mPartWorld[AimPart::Target];
	const MMatrix& mUpWorld = outResult.mPartWorld[AimPart::Up];

	const MPoint pAim(mAimWorld[3][0], mAimWorld[3][1], mAimWorld[3][2]);
	const MPoint pTarget(mTargetWorld[3][0], mTargetWorld[3][1], mTargetWorld[3][2]);
	const MPoint pUp(mUpWorld[3][0], mUpWorld[3][1], mUpWorld[3][2]);

	const MMatrix mAimSolved = MathUtils::computeAimMatrix(pAim, pTarget, pUp, mAimWorld);

	// 3. Row-major: offset first, then the live solve. At rest this equals the joint's original rest matrix.
	const MMatrix mAimRestOffset = data.inputValue(aAimRestOffset).asMatrix();
	outResult.mPartWorld[AimPart::Aim] = mAimRestOffset * mAimSolved;

	return MStatus::kSuccess;
}

MStatus AimModuleNode::compute(const MPlug& plug, MDataBlock& data)
{
	const MObject oOutputAttr = plug.attribute();
	const bool plugIsControlOPM = oOutputAttr == aControlOPM;
	const bool plugIsJointOPM = oOutputAttr == aJointOPM;
	const bool plugIsOutputSocketMatrix = oOutputAttr == aOutputSocketMatrix;

	if (!plugIsControlOPM && !plugIsJointOPM && !plugIsOutputSocketMatrix)
	{
		return MStatus::kUnknownParameter;
	}

	MStatus status;
	AimSolveResult solve;
	status = solveAim(data, solve);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MArrayDataHandle hOutputData = data.outputArrayValue(oOutputAttr, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// Control OPM excludes the control's local matrix. Joint OPM and socket both want the solved world matrix.
	// Elements that were never created (e.g. controlOPM[0], there is no aim control) are skipped.
	for (unsigned int i = 0; i < AimPart::Count; ++i)
	{
		writeOutputMatrix(hOutputData, i, plugIsControlOPM ? solve.mControlOPM[i] : solve.mPartWorld[i]);
	}

	hOutputData.setAllClean();
	data.setClean(plug);
	return MStatus::kSuccess;
}

RigModuleBuildResult AimModuleNode::createModule(
	const MString& moduleName,
	const Side& side,
	const ShapeType& shapeType,
	const std::array<AimPartInput, AimPart::Count>& parts,
	MObject& oRigRoot,
	MDagModifier& dagMod)
{
	RigModuleBuildResult result;
	MStatus status;

	// 1. Module node
	MObject oAimModule = dagMod.MDGModifier::createNode(AimModuleNode::id);
	dagMod.renameNode(oAimModule, moduleName);

	MPlug pSide(oAimModule, AimModuleNode::aSide);
	dagMod.newPlugValueShort(pSide, getShortFromSide(side));
	result.moduleNode = oAimModule;

	MPlug pOutputJointOPM(oAimModule, AimModuleNode::aJointOPM);

	// 2. Per-part: bake joint, store rest, wire joint OPM, and create a control for target/up.
	for (unsigned int i = 0; i < AimPart::Count; ++i)
	{
		MObject oJoint = parts[i].oJoint;
		MMatrix mRestWorld = parts[i].mRestWorld;
		MFnDependencyNode fnJoint(oJoint);
		const char* partName = kAimPartNames[i];

		// The module drives the joint's world transform through its OPM, so nothing may be inherited.
		DagUtils::setInheritsTransforms(oJoint, false, dagMod);
		DagUtils::setSegmentScaleCompensate(oJoint, false, dagMod);
		status = DagUtils::bakeJointTransformsToOPM(oJoint, mRestWorld, dagMod);
		if (!status)
		{
			MGlobal::displayError(MString("Failed to bake aim module joint: ") + fnJoint.name());
			result.status = status;
			return result;
		}

		MPlug pJointOpm = fnJoint.findPlug("offsetParentMatrix", false, &status);
		if (!status)
		{
			result.status = status;
			return result;
		}
		dagMod.connect(pOutputJointOPM.elementByLogicalIndex(i), pJointOpm);

		// Only target and up have controls. The aim joint is purely solved.
		MObject oControl = MObject::kNullObj;
		if (i == AimPart::Aim)
		{
			setControlRestMatrix(oAimModule, mRestWorld, i, dagMod);   // rest only, nothing is wired to the local matrix
		}
		else
		{
			oControl = RigControlNode::createRigControl(oAimModule, dagMod, fnJoint.name(), shapeType, side);
			status = wireControl(oAimModule, oControl, i, mRestWorld, dagMod);
			if (!status)
			{
				result.status = status;
				return result;
			}
		}

		// Socket and child plug share a name. They live in separate maps/namespaces ("layout.end" vs. "target").
		RigModuleSocketInfo socketInfo;
		socketInfo.oTransform = oControl;      // null for aim, children get the matrix but no DAG parent
		socketInfo.mRestWorld = mRestWorld;
		socketInfo.outputIndex = i;
		result.parentSockets[partName] = socketInfo;
		result.childPlugs[partName] = RigModuleChildPlugInfo{ oControl, i };

		DagPlugAndSocketUtils::setParentSocketName(oAimModule, i, partName, dagMod);
		DagPlugAndSocketUtils::setChildPlugName(oAimModule, i, partName, dagMod);
	}

	// 3. Orientation offset so the aim joint keeps its original rest orientation (bind pose safe).
	const MMatrix& mAimRest = parts[AimPart::Aim].mRestWorld;
	const MMatrix& mTargetRest = parts[AimPart::Target].mRestWorld;
	const MMatrix& mUpRest = parts[AimPart::Up].mRestWorld;

	const MMatrix mAimSolvedRest = MathUtils::computeAimMatrix(
		MPoint(mAimRest[3][0], mAimRest[3][1], mAimRest[3][2]),
		MPoint(mTargetRest[3][0], mTargetRest[3][1], mTargetRest[3][2]),
		MPoint(mUpRest[3][0], mUpRest[3][1], mUpRest[3][2]),
		mAimRest);

	MMatrix mAimRestOffset = mAimRest * mAimSolvedRest.inverse();
	MPlug pAimRestOffset(oAimModule, AimModuleNode::aAimRestOffset);
	DagUtils::setMatrixPlug(pAimRestOffset, mAimRestOffset, dagMod);

	// 4. Network
	if (!oRigRoot.isNull())
	{
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dagMod, oAimModule, aRigRoot);
	}

	// Shared callback: skips the DAG reparent for control-less plugs/sockets (aim), always connects matrices.
	result.dagParentCallback = AimModuleNode::reparentChildPlugToSocket;
	result.status = MStatus::kSuccess;
	return result;
}

RigModuleBuildResult AimModuleNode::buildFromJson(const nlohmann::json& args, BuildContext& ctx)
{
	RigModuleBuildResult result;
	AimModuleArgs aimArgs;

	try
	{
		// Version migration goes here: branch on ctx.moduleData.moduleVersion.
		aimArgs = args.get<AimModuleArgs>();
	}
	catch (const nlohmann::json::exception& e)
	{
		MGlobal::displayError(MString("Invalid Aim args for ") + ctx.moduleData.name.c_str() + ": " + e.what());
		return result;
	}

	if (aimArgs.aimJoint == aimArgs.targetJoint || aimArgs.aimJoint == aimArgs.upJoint || aimArgs.targetJoint == aimArgs.upJoint)
	{
		MGlobal::displayError(MString("Aim module needs three distinct joints: ") + ctx.moduleData.name.c_str());
		return result;
	}

	const std::string jointNames[AimPart::Count] = { aimArgs.aimJoint, aimArgs.targetJoint, aimArgs.upJoint };
	std::array<AimPartInput, AimPart::Count> parts;

	for (unsigned int i = 0; i < AimPart::Count; ++i)
	{
		MStatus status = DagUtils::getJointAndWorldMatrix(jointNames[i].c_str(), parts[i].oJoint, parts[i].mRestWorld);
		if (!status)
		{
			return result;   // helper already reported which joint failed
		}
	}

	return AimModuleNode::createModule(
		ctx.moduleData.name.c_str(),
		getSideFromString(ctx.moduleData.side),
		getShapeTypeFromString(ctx.moduleData.shapeType),
		parts,
		ctx.oRigRoot,
		ctx.dagMod);
}