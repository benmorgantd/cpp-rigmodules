#include "maya/modules/AimModule.h"
#include "core/RigJsonStructs.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/Side.h"
#include "maya/shared/Utilities.h"

#include <nlohmann/json.hpp>
#include <string>

#include <maya/MArrayDataHandle.h>
#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MPlug.h>
#include <maya/MPoint.h>
#include <maya/MTypeId.h>
#include <maya/MVector.h>

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

	MPoint position(const MMatrix& mWorld)
	{
		return MPoint(mWorld[3][0], mWorld[3][1], mWorld[3][2]);
	}

	// The aim joint's world from the three part worlds (the aim part's own, as its control step left it): +X toward the
	// target, +Z toward up, then the rest offset so it equals the joint's rest matrix at rest. compute() and the inverse
	// both build the aim joint here, so the two can't drift (D41).
	MMatrix computeAimJointWorld(const MMatrix& mAimWorld, const MMatrix& mTargetWorld, const MMatrix& mUpWorld, const MMatrix& mAimRestOffset)
	{
		return mAimRestOffset * MathUtils::computeAimMatrix(position(mAimWorld), position(mTargetWorld), position(mUpWorld), mAimWorld);
	}

	// Offset so the aim joint keeps its original rest orientation (bind pose safe): jointRest * aimSolvedRest^-1.
	MMatrix computeAimRestOffset(const MMatrix& mAimRest, const MMatrix& mTargetRest, const MMatrix& mUpRest)
	{
		const MMatrix mAimSolvedRest = MathUtils::computeAimMatrix(position(mAimRest), position(mTargetRest), position(mUpRest), mAimRest);
		return mAimRest * mAimSolvedRest.inverse();
	}

	// Two aim joints this far apart in any matrix element disagree. Matrices hold centimeters and unit axes.
	constexpr double kAimJointTolerance{ 1e-3 };
}

AimModuleNode::AimModuleNode()
{
}
AimModuleNode::~AimModuleNode()
{
}

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

// Solves all three parts once. compute() writes every output array from this one solve.
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
	// 3. Row-major: offset first, then the live solve. At rest this equals the joint's original rest matrix.
	const MMatrix mAimRestOffset = data.inputValue(aAimRestOffset).asMatrix();
	outResult.mPartWorld[AimPart::Aim] = computeAimJointWorld(
		outResult.mPartWorld[AimPart::Aim], outResult.mPartWorld[AimPart::Target], outResult.mPartWorld[AimPart::Up], mAimRestOffset);

	return MStatus::kSuccess;
}

MStatus AimModuleNode::compute(const MPlug& plug, MDataBlock& data)
{
	const MObject oOutputAttr = plug.attribute();
	if (oOutputAttr != aControlOPM && oOutputAttr != aJointOPM && oOutputAttr != aOutputSocketMatrix)
	{
		return MStatus::kUnknownParameter;
	}

	MStatus status;
	AimSolveResult solve;
	status = solveAim(data, solve);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MArrayDataHandle hControlOPM = data.outputArrayValue(aControlOPM, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hJointOPM = data.outputArrayValue(aJointOPM, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hSocket = data.outputArrayValue(aOutputSocketMatrix, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// One solve writes all three outputs. Control OPM excludes the control's local matrix. Joint OPM and socket
	// both want the solved world matrix. Elements that were never created (e.g. controlOPM[0], there is no aim
	// control) are skipped.
	for (unsigned int i = 0; i < AimPart::Count; ++i)
	{
		writeOutputMatrix(hControlOPM, i, solve.mControlOPM[i]);
		writeOutputMatrix(hJointOPM, i, solve.mPartWorld[i]);
		writeOutputMatrix(hSocket, i, solve.mPartWorld[i]);
	}

	const MObject oNode = plug.node();
	commitOutput(data, oNode, aControlOPM, hControlOPM);
	commitOutput(data, oNode, aJointOPM, hJointOPM);
	commitOutput(data, oNode, aOutputSocketMatrix, hSocket);
	data.setClean(plug);
	return MStatus::kSuccess;
}

ModuleInverseResult AimModuleNode::inverse(const ModuleInverseContext& context, const ModuleInverseInput& input)
{
	ModuleInverseResult result;
	result.controlLocal.resize(AimPart::Count);
	result.socketWorld.resize(AimPart::Count);
	result.solved = true;

	// Target and up are control steps on their own joints, each carried by its own parent socket.
	MMatrix mPartWorld[AimPart::Count];
	for (const unsigned int part : { AimPart::Target, AimPart::Up })
	{
		const MMatrix& mRestWorld = context.controlRestWorld[part];
		MMatrix mLocal = input.fallbackLocal[part];
		if (input.jointWorld[part].has_value())
		{
			mLocal = ModuleInverse::localFromWorld(*input.jointWorld[part], mRestWorld, input.parentDelta[part]);
			result.controlLocal[part] = mLocal;
		}
		else
		{
			result.solved = false;
			result.reason = std::string("the ") + kAimPartNames[part] + " joint is missing from the source";
		}
		mPartWorld[part] = RigModuleNodeBase::solveControlStep(mLocal, mRestWorld, input.parentDelta[part]).mWorld;
	}

	// The aim joint isn't inverted. It follows from the other two, like compute() does it.
	const MMatrix& mAimRest = context.controlRestWorld[AimPart::Aim];
	const MMatrix mAimWorld = RigModuleNodeBase::solveControlStep(MMatrix::identity, mAimRest, input.parentDelta[AimPart::Aim]).mWorld;
	const MMatrix mAimRestOffset = computeAimRestOffset(mAimRest, context.controlRestWorld[AimPart::Target], context.controlRestWorld[AimPart::Up]);
	mPartWorld[AimPart::Aim] = computeAimJointWorld(mAimWorld, mPartWorld[AimPart::Target], mPartWorld[AimPart::Up], mAimRestOffset);

	if (input.jointWorld[AimPart::Aim].has_value() && !input.jointWorld[AimPart::Aim]->isEquivalent(mPartWorld[AimPart::Aim], kAimJointTolerance))
	{
		result.warnings.push_back("the source's aim joint is not where the target and up joints aim it, so it wasn't driven by this aim");
	}

	for (unsigned int part = 0; part < AimPart::Count; ++part)
	{
		result.socketWorld[part] = mPartWorld[part];
	}
	return result;
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

	// 2. Per-part: bake joint, store rest, wire joint OPM, and create a control for target/up.
	for (unsigned int i = 0; i < AimPart::Count; ++i)
	{
		MObject oJoint = parts[i].oJoint;
		MMatrix mRestWorld = parts[i].mRestWorld;
		MFnDependencyNode fnJoint(oJoint);
		const char* partName = kAimPartNames[i];

		status = AimModuleNode::wireJoint(oAimModule, oJoint, i, mRestWorld, dagMod);
		if (!status)
		{
			result.status = status;
			return result;
		}

		// Only target and up have controls. The aim joint is purely solved.
		MObject oControl = MObject::kNullObj;
		if (i == AimPart::Aim)
		{
			setControlRestMatrix(oAimModule, mRestWorld, i, dagMod);   // rest only, nothing is wired to the local matrix
		}
		else
		{
			oControl = RigControlNode::createRigControl(dagMod, fnJoint.name(), shapeType, side, parts[i].controlMarkup, MEulerRotation::kXYZ);
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

	const MMatrix mAimRestOffset = computeAimRestOffset(mAimRest, mTargetRest, mUpRest);
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
		parts[i].controlMarkup = makeControlMarkup(selectors[i]->side, selectors[i]->usage);
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
	outPlan.moduleInterface.controls = { parts[AimPart::Target].controlMarkup, parts[AimPart::Up].controlMarkup };

	const MString moduleName = ctx.moduleData.name.c_str();
	const Side side = getSideFromString(ctx.moduleData.side);
	const ShapeType shapeType = getShapeTypeFromString(ctx.moduleData.shapeType);
	outPlan.build = [moduleName, side, shapeType, parts](MObject& oRigRoot, MDagModifier& dagMod)
	{
		return AimModuleNode::createModule(moduleName, side, shapeType, parts, oRigRoot, dagMod);
	};
	return true;
}