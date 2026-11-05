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
#include <maya/MVector.h>
#include <maya/MPlug.h>
#include <maya/MGlobal.h>
#include <maya/MDagModifier.h>
#include <maya/MObject.h>
#include <maya/MTypeId.h>

MTypeId AimModuleNode::id(0x00218D);
MObject AimModuleNode::aAimRestOffset;

namespace
{
	// Used for child plug names, parent socket names and the moduleArgs keys (they must match AimModuleArgs's fields).
	// Index matches AimPart.
	const char* const kAimPartNames[AimPart::Count] = { "aim", "target", "up" };

	// Below these the aim solve has no defined direction.
	constexpr double kMinAimDistance = 1.0e-4;
	constexpr double kMinAimUpSine = 1.0e-4;

	// Mirrors createModule: every part is a child plug and a socket, and only the aim part has no control.
	ModuleInterface makeAimInterface()
	{
		ModuleInterface moduleInterface;
		for (unsigned int i = 0; i < AimPart::Count; ++i)
		{
			moduleInterface.childPlugs.push_back(ModulePlugInfo{ kAimPartNames[i], i != AimPart::Aim });
			moduleInterface.sockets.push_back(kAimPartNames[i]);
		}
		return moduleInterface;
	}

	MVector restPosition(const MMatrix& mRestWorld)
	{
		return MVector(mRestWorld[3][0], mRestWorld[3][1], mRestWorld[3][2]);
	}
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

		DagPlugAndSocketUtils::setParentSocketNames(oAimModule, i, { partName }, dagMod);
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

bool AimModuleNode::planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan)
{
	AimModuleArgs aimArgs;
	if (!readModuleArgs(args, { kAimPartNames[AimPart::Aim], kAimPartNames[AimPart::Target], kAimPartNames[AimPart::Up] }, ctx, aimArgs))
	{
		return false;
	}

	// claimJoint rejects a joint picked twice, which also covers the "three distinct joints" requirement.
	// Claim all three even after a failure, so the report lists every bad selector at once.
	const JointSelector* selectors[AimPart::Count] = { &aimArgs.aim, &aimArgs.target, &aimArgs.up };
	std::array<AimPartInput, AimPart::Count> parts;
	bool allClaimed = true;
	for (unsigned int i = 0; i < AimPart::Count; ++i)
	{
		allClaimed = claimJoint(ctx, *selectors[i], parts[i].oJoint, parts[i].mRestWorld) && allClaimed;
	}
	if (!allClaimed)
	{
		return false;
	}

	// A degenerate rest pose gives the aim solve no direction, and the rest offset would be garbage.
	const MVector aimToTarget = restPosition(parts[AimPart::Target].mRestWorld) - restPosition(parts[AimPart::Aim].mRestWorld);
	const MVector aimToUp = restPosition(parts[AimPart::Up].mRestWorld) - restPosition(parts[AimPart::Aim].mRestWorld);
	const std::string argsPath = ctx.where + "." + TemplateKey::ModuleArgs;
	if (aimToTarget.length() < kMinAimDistance || aimToUp.length() < kMinAimDistance)
	{
		ctx.report.addError(argsPath, "the target and up joints must not sit on the aim joint");
		return false;
	}
	if ((aimToTarget.normal() ^ aimToUp.normal()).length() < kMinAimUpSine)
	{
		ctx.report.addError(argsPath, "the up joint lies on the aim line, so the up direction is undefined");
		return false;
	}

	outPlan.moduleInterface = makeAimInterface();

	const MString moduleName = ctx.moduleData.name.c_str();
	const Side side = getSideFromString(ctx.moduleData.side);
	const ShapeType shapeType = getShapeTypeFromString(ctx.moduleData.shapeType);
	outPlan.build = [moduleName, side, shapeType, parts](MObject& oRigRoot, MDagModifier& dagMod)
	{
		return AimModuleNode::createModule(moduleName, side, shapeType, parts, oRigRoot, dagMod);
	};
	return true;
}