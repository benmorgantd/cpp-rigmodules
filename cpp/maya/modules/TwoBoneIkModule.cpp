#include "maya/modules/TwoBoneIkModule.h"
#include "core/Constants.h"
#include "core/ControlPlacement.h"
#include "core/JointMarkup.h"
#include "core/JsonUtils.h"
#include "core/NamingConvention.h"
#include "core/RigJsonStructs.h"
#include "core/StringUtils.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/Side.h"
#include "maya/shared/TwoBoneIkPart.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <maya/MAngle.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnStringData.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MPlug.h>
#include <maya/MPoint.h>
#include <maya/MProfiler.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MTypeId.h>
#include <maya/MVector.h>

MTypeId TwoBoneIkNode::id(0x00218F);
MObject TwoBoneIkNode::aIkFkBlend;
MObject TwoBoneIkNode::aStretch;
MObject TwoBoneIkNode::aSoft;
MObject TwoBoneIkNode::aPin;
MObject TwoBoneIkNode::aRoll;
MObject TwoBoneIkNode::aPoleSpace;
MObject TwoBoneIkNode::aIkRestOffset;
MObject TwoBoneIkNode::aRestBendNormal;
MObject TwoBoneIkNode::aRestPlaneAngle;
MObject TwoBoneIkNode::aLimbLengths;
MObject TwoBoneIkNode::aFkVisible;
MObject TwoBoneIkNode::aIkVisible;
MObject TwoBoneIkNode::aFkMatchLocal;
MObject TwoBoneIkNode::aIkMatchLocal;
MObject TwoBoneIkNode::aMatchWarnings;

namespace
{
	// The IK solve of a restored pose may miss the joints by this fraction of the limb's reach before the restore warns.
	constexpr double kMaxIkMismatch{ 1.0e-3 };

	// Version 2 (D57): the IK handle, pole and settings controls come from the limb joints, so the three marker joints ("ik", "pole",
	// "settings") are gone. The step has no scene, so it cannot turn their positions into offsets: an old template builds with the
	// derived places. Anything else is left for the validation that follows.
	void dropMarkerJoints(nlohmann::json& args)
	{
		if (!args.is_object())
		{
			return;
		}
		for (const char* key : { "ik", "pole", "settings" })
		{
			args.erase(key);
		}
	}

	// The solve's helpers live in maya/shared/TwoBoneIkSolve (shared with Limb, D62).
	using TwoBoneIkSolve::computeLimbFrames;
	using TwoBoneIkSolve::computeRest;
	using TwoBoneIkSolve::makeFrame;
	using TwoBoneIkSolve::orthonormalFrame;
}

TwoBoneIkNode::TwoBoneIkNode()
{
}
TwoBoneIkNode::~TwoBoneIkNode()
{
}

const UpdateTable& TwoBoneIkNode::argUpdateTable()
{
	// Add a step here, and nowhere else, when TwoBoneIk's moduleArgs change shape or meaning. Step 0 takes version 1 to 2.
	static const UpdateTable table{ 1, { &dropMarkerJoints } };
	return table;
}

void* TwoBoneIkNode::creator()
{
	return new TwoBoneIkNode();
}

MStatus TwoBoneIkNode::initialize()
{
	MStatus status;

	status = initializeSharedAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	status = TwoBoneIkPart<TwoBoneIkNode>::createAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	status = TwoBoneIkPart<TwoBoneIkNode>::createMatchAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// AFFECTS RELATIONSHIPS
	declareStandardAffects();
	TwoBoneIkPart<TwoBoneIkNode>::declareAffects();
	TwoBoneIkPart<TwoBoneIkNode>::declareMatchAffects();

	return MStatus::kSuccess;
}

MStatus TwoBoneIkNode::compute(const MPlug& plug, MDataBlock& data)
{
	const MObject oOutputAttr = plug.attribute();

	if (oOutputAttr == aFkVisible || oOutputAttr == aIkVisible)
	{
		const double blend = data.inputValue(aIkFkBlend).asDouble();
		const bool isVisible = TwoBoneIkSolve::isBlendControlVisible(oOutputAttr == aFkVisible, blend);
		data.outputValue(oOutputAttr).setDouble(isVisible ? 1.0 : 0.0);
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	if (oOutputAttr == aFkMatchLocal || oOutputAttr == aIkMatchLocal || oOutputAttr == aMatchWarnings)
	{
		return TwoBoneIkPart<TwoBoneIkNode>::computeMatch(plug, data, &TwoBoneIkNode::inverse);
	}

	if (oOutputAttr != aControlOPM && oOutputAttr != aJointOPM && oOutputAttr != aOutputSocketMatrix)
	{
		return MStatus::kUnknownParameter;
	}

	MStatus status;
	TwoBoneIkEval eval;
	status = TwoBoneIkPart<TwoBoneIkNode>::readEval(data, eval);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	TwoBoneIkSolveResult result;
	TwoBoneIkSolve::evaluate(eval, result);

	SolvedOutputs outputs = openSolvedOutputs(data, aJointOPM, status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
	{
		writeSolvedSlot(outputs, slot, result.mControlOPM[slot], result.mJointWorld[slot]);
	}
	commitSolvedOutputs(data, plug, aJointOPM, outputs);
	return MStatus::kSuccess;
}

std::vector<ModuleInverseChannel> TwoBoneIkNode::inverseChannels()
{
	// In TwoBoneIkInverseChannel order. The defaults are the channels' rests.
	return
	{
		{ RigAttr::Soft, TwoBoneIkSlot::Settings, 0.0 },
		{ RigAttr::Stretch, TwoBoneIkSlot::Settings, 0.0 },
		{ RigAttr::Pin, TwoBoneIkSlot::Pole, 0.0 },
		{ RigAttr::Roll, TwoBoneIkSlot::Ik, 0.0 },
		{ RigAttr::ControlSpace, TwoBoneIkSlot::Pole, 0.0 },
		{ RigAttr::IkFkBlend, TwoBoneIkSlot::Settings, 1.0 }
	};
}

// Reads the same two deltas TwoBoneIkSolve::evaluate() hands the pole (section 4.4), from the plugs: the child plug's parent motion for
// Parent, and the IK handle's world socket against its rest for Ik. The handle doesn't depend on the pole, so pulling its
// socket is no cycle.
MStatus TwoBoneIkNode::spaceDelta(const MObject& oModule, unsigned int slot, unsigned int label, MMatrix& outDelta)
{
	constexpr unsigned int kParentSpace{ 0 };
	constexpr unsigned int kIkSpace{ 1 };

	if (slot != TwoBoneIkSlot::Pole || label > kIkSpace)
	{
		MGlobal::displayError(MString("TwoBoneIk has no space ") + label + " on control slot " + slot + ".");
		return MStatus::kInvalidParameter;
	}

	MStatus status;
	if (label == kParentSpace)
	{
		MPlug pParentMatrix;
		MPlug pParentRest;
		status = getChildPlugMatrixPlugs(oModule, TwoBoneIkSlot::Pole, pParentMatrix, pParentRest);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		outDelta = MathUtils::computeDeltaFromRest(DagUtils::getMatrixFromPlug(pParentRest), DagUtils::getMatrixFromPlug(pParentMatrix));
		return MStatus::kSuccess;
	}

	const MPlug pIkWorld = getSocketMatrixPlug(oModule, TwoBoneIkSlot::Ik, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	outDelta = MathUtils::computeDeltaFromRest(getControlRestWorld(oModule, TwoBoneIkSlot::Ik), DagUtils::getMatrixFromPlug(pIkWorld));
	return MStatus::kSuccess;
}

// Allocates (the contract's vectors, the warnings), which is fine for a restore. compute() never calls it (D52 section 6).
ModuleInverseResult TwoBoneIkNode::inverse(const ModuleInverseContext& context, const ModuleInverseInput& input)
{
	ModuleInverseResult result;
	result.controlLocal.resize(TwoBoneIkSlot::Count);
	result.socketWorld.resize(TwoBoneIkSlot::Count);

	// The forward evaluation's inputs: the controls at their fallback values to start with, and the settings the forward
	// solve will read.
	const std::vector<ModuleInverseChannel> wanted = inverseChannels();
	const auto channel = [&input, &wanted](unsigned int which)
	{
		return which < input.channels.size() ? input.channels[which] : wanted[which].defaultValue;
	};
	TwoBoneIkEval eval;
	std::array<MMatrix, TwoBoneIkSlot::Count> restWorlds;
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
	{
		restWorlds[slot] = context.controlRestWorld[slot];
		eval.mRest[slot] = context.controlRestWorld[slot];
		eval.mLocal[slot] = input.fallbackLocal[slot];
	}
	eval.mUpperDelta = input.parentDelta[TwoBoneIkSlot::Upper];
	eval.mIkDelta = input.parentDelta[TwoBoneIkSlot::Ik];
	eval.mPoleParentDelta = input.parentDelta[TwoBoneIkSlot::Pole];
	eval.mSettingsDelta = input.parentDelta[TwoBoneIkSlot::Settings];
	eval.poleFollowsIk = std::lround(channel(TwoBoneIkInverseChannel::Space)) == 1;
	eval.blend = channel(TwoBoneIkInverseChannel::IkFkBlend);
	eval.stretch = channel(TwoBoneIkInverseChannel::Stretch);
	eval.soft = channel(TwoBoneIkInverseChannel::Soft);
	eval.pin = channel(TwoBoneIkInverseChannel::Pin);
	eval.rollRadians = channel(TwoBoneIkInverseChannel::Roll);
	eval.rest = computeRest(restWorlds);

	const auto fillSockets = [&result, &eval]()
	{
		TwoBoneIkSolveResult forward;
		TwoBoneIkSolve::evaluate(eval, forward);
		for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
		{
			result.socketWorld[slot] = forward.mJointWorld[slot];
		}
	};

	// Slots 3 to 5 have no joints (D57): the handle and pole come from the limb's three, which are read here.
	bool hasLimb = input.jointWorld.size() >= TwoBoneIkSlot::LimbCount;
	for (unsigned int slot = 0; hasLimb && slot < TwoBoneIkSlot::LimbCount; ++slot)
	{
		hasLimb = input.jointWorld[slot].has_value();
	}
	if (!hasLimb)
	{
		result.reason = "a limb joint is missing from the source, so the limb's controls come from the sidecar or rest";
		fillSockets();
		return result;
	}
	result.solved = true;

	// 1. FK: the chain walk, so a stretched or sheared pose is reproduced in the FK controls at any blend.
	ModuleInverseContext fkContext;
	ModuleInverseInput fkInput;
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::LimbCount; ++slot)
	{
		fkContext.controlRestWorld.push_back(context.controlRestWorld[slot]);
		fkInput.jointWorld.push_back(input.jointWorld[slot]);
		fkInput.fallbackLocal.push_back(input.fallbackLocal[slot]);
	}
	fkInput.parentDelta.push_back(input.parentDelta[TwoBoneIkSlot::Upper]);
	const ModuleInverseResult fk = ModuleInverse::solveChain(fkContext, fkInput, true);
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::LimbCount; ++slot)
	{
		result.controlLocal[slot] = fk.controlLocal[slot];
		eval.mLocal[slot] = *fk.controlLocal[slot];
	}

	// 2. The IK handle and pole from the joints' positions.
	const MVector vRoot = MathUtils::translationOf(*input.jointWorld[TwoBoneIkSlot::Upper]);
	const MVector vMid = MathUtils::translationOf(*input.jointWorld[TwoBoneIkSlot::Lower]);
	const MVector vEnd = MathUtils::translationOf(*input.jointWorld[TwoBoneIkSlot::End]);
	TwoBoneIkInverseInput ikInput;
	ikInput.root = vRoot;
	ikInput.mid = vMid;
	ikInput.end = vEnd;
	ikInput.length1 = eval.rest.length1;
	ikInput.length2 = eval.rest.length2;
	ikInput.stretch = eval.stretch;
	ikInput.soft = eval.soft;
	ikInput.pin = eval.pin;
	ikInput.rollRadians = eval.rollRadians;
	ikInput.restPlaneAngle = eval.rest.restPlaneAngle;
	ikInput.restAxis = MathUtils::translationOf(restWorlds[TwoBoneIkSlot::Ik]) - MathUtils::translationOf(restWorlds[TwoBoneIkSlot::Upper]);
	ikInput.restPoleOffset = MathUtils::translationOf(restWorlds[TwoBoneIkSlot::Pole]) - MathUtils::translationOf(restWorlds[TwoBoneIkSlot::Upper]);
	const TwoBoneIkInverseResult ik = TwoBoneIkSolve::computeTwoBoneIkInverse(ikInput);

	// The handle: at the solved position, turned the way the hand joint is (the frame the forward solve gives the end).
	const MMatrix mEndFrame = eval.rest.ikRestOffset[TwoBoneIkSlot::End].inverse() * (*input.jointWorld[TwoBoneIkSlot::End]);
	// With a foot on the ikTarget (D74), the end reaches rest * F * the handle's motion, so the handle is placed to make F land there.
	// The handle has no scale channel, so in the scene it carries its space's scale (a Layout's): the reach is given that scale, so the
	// handle's local comes out rigid and the scene's handle is the one placed here. Only the foot's case needs it: F turns about pivots
	// away from the handle, so a scaled handle would move the reach, where without a foot only the reach's position is read.
	const double scale = MVector(eval.mIkDelta[0][0], eval.mIkDelta[0][1], eval.mIkDelta[0][2]).length();
	MMatrix mScaledReach = orthonormalFrame(mEndFrame, ik.handle);
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 3; ++c)
		{
			mScaledReach[r][c] *= scale;
		}
	}
	const MMatrix& mIkRest = restWorlds[TwoBoneIkSlot::Ik];
	const MMatrix mHandleWorld = input.ikTargetMotion.has_value() ? mIkRest * input.ikTargetMotion->inverse() * mIkRest.inverse() * mScaledReach : mScaledReach;
	if (input.ikTargetMotion.has_value() && eval.blend > 0.0)
	{
		// The forward evaluation behind the sockets (and a Limb's ribbon) reaches the target, as LimbNode::compute does.
		eval.hasTarget = true;
		eval.mTargetDelta = mIkRest.inverse() * mScaledReach;
	}
	eval.mLocal[TwoBoneIkSlot::Ik] = ModuleInverse::localFromWorld(mHandleWorld, restWorlds[TwoBoneIkSlot::Ik], eval.mIkDelta);
	result.controlLocal[TwoBoneIkSlot::Ik] = eval.mLocal[TwoBoneIkSlot::Ik];

	// The pole: in Ik space it moves with the matched handle (the one being written), not the handle's current world.
	// Its rotation channels are locked, so only the local translation that puts it at the solved position is set.
	const MMatrix mPoleDelta = eval.poleFollowsIk ? MathUtils::computeDeltaFromRest(restWorlds[TwoBoneIkSlot::Ik], mHandleWorld) : eval.mPoleParentDelta;
	const MPoint pPoleLocal = MPoint(ik.pole) * (restWorlds[TwoBoneIkSlot::Pole] * mPoleDelta).inverse();
	eval.mLocal[TwoBoneIkSlot::Pole] = makeFrame(MVector(1.0, 0.0, 0.0), MVector(0.0, 1.0, 0.0), MVector(0.0, 0.0, 1.0), MVector(pPoleLocal.x, pPoleLocal.y, pPoleLocal.z));
	result.controlLocal[TwoBoneIkSlot::Pole] = eval.mLocal[TwoBoneIkSlot::Pole];

	// The settings control has no joint, so it keeps its fallback. The sockets are what the forward solve gives these controls.
	fillSockets();

	// 3. A pose IK can't make. FK always matches, so only the IK side warns.
	if (eval.blend > 0.0 && eval.blend < 1.0)
	{
		result.warnings.push_back("ikFkBlend is between 0 and 1, so the IK handle and pole match the joints only approximately. FK matches exactly. Match at blend 0 or 1.");
		return result;
	}
	TwoBoneIkInput check;
	check.root = vRoot;
	check.handle = ik.handle;
	check.pole = ik.pole;
	check.length1 = eval.rest.length1;
	check.length2 = eval.rest.length2;
	check.stretch = eval.stretch;
	check.soft = eval.soft;
	check.pin = eval.pin;
	check.rollRadians = eval.rollRadians;
	check.restPlaneAngle = eval.rest.restPlaneAngle;
	check.restBendNormal = eval.rest.restBendNormal;
	check.restAxis = ikInput.restAxis;
	const TwoBoneIkResult forward = TwoBoneIkSolve::computeTwoBoneIk(check);
	const double tolerance = kMaxIkMismatch * (eval.rest.length1 + eval.rest.length2);
	if ((forward.mid - vMid).length() > tolerance || (forward.end - vEnd).length() > tolerance)
	{
		result.warnings.push_back("IK can't make the joints' pose: their bone lengths differ from the rig's with stretch and pin off, or the limb is past its reach. "
			"The IK handle and pole are as near as the limb gets. FK matches exactly.");
	}
	return result;
}

RigModuleBuildResult TwoBoneIkNode::createModule(
	const MString& moduleName,
	const Side& side,
	const ShapeType& shapeType,
	const std::array<TwoBoneIkSlotInput, TwoBoneIkSlot::Count>& slots,
	const TwoBoneIkRest& rest,
	double ikFkBlend,
	MObject& oRigRoot,
	MDagModifier& dagMod)
{
	using Part = TwoBoneIkPart<TwoBoneIkNode>;
	RigModuleBuildResult result;
	MStatus status;

	// 1. Module node
	MObject oModule = dagMod.MDGModifier::createNode(TwoBoneIkNode::id);
	dagMod.renameNode(oModule, moduleName);
	dagMod.newPlugValueShort(MPlug(oModule, TwoBoneIkNode::aSide), getShortFromSide(side));
	result.moduleNode = oModule;

	// 2. Per slot: the joint, the control, the sockets and the child plug, and the rest data the solve reads.
	MObject oControls[TwoBoneIkSlot::Count];
	status = Part::createSlots(oModule, side, shapeType, slots, rest, result, oControls, dagMod);
	if (!status)
	{
		result.status = status;
		return result;
	}

	// 3. The channels. The controls must exist before an attribute can be added to them, and the attributes before they can be wired,
	// as in RigConstraintNode::createConstraint, so the build runs the modifier here.
	status = dagMod.doIt();
	if (!status)
	{
		result.status = status;
		return result;
	}
	status = Part::queueChannels(oControls, ikFkBlend, dagMod);
	CHECK_MSTATUS_AND_RETURN(status, result);
	status = dagMod.doIt();
	if (!status)
	{
		result.status = status;
		return result;
	}
	status = Part::wireChannels(oModule, oControls, dagMod);
	CHECK_MSTATUS_AND_RETURN(status, result);

	// 4. Network
	if (!oRigRoot.isNull())
	{
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dagMod, oModule, aRigRoot);
	}

	// Shared callback: skips the DAG reparent for the sockets with no control, always connects matrices.
	result.dagParentCallback = TwoBoneIkNode::reparentChildPlugToSocket;
	result.status = MStatus::kSuccess;
	return result;
}

bool TwoBoneIkNode::planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan)
{
	TwoBoneIkArgs ikArgs;
	if (!readModuleArgs(args, { "joints" }, ctx, ikArgs, { "controlName", "poleOffset", "settingsOffset", RigAttr::IkFkBlend, "ikOrientToWorld", "ikAlignToPole" }))
	{
		return false;
	}

	std::array<TwoBoneIkSlotInput, TwoBoneIkSlot::Count> slots;
	TwoBoneIkRest rest;
	if (!TwoBoneIkSolve::planSlots(ikArgs, ctx, slots, rest))
	{
		return false;
	}

	outPlan.moduleInterface = TwoBoneIkSolve::makeInterface();
	for (const TwoBoneIkSlotInput& slot : slots)
	{
		outPlan.moduleInterface.controls.push_back(slot.controlMarkup);
	}

	const MString moduleName = ctx.moduleData.name.c_str();
	const Side side = getSideFromString(ctx.moduleData.side);
	const ShapeType shapeType = getShapeTypeFromString(ctx.moduleData.shapeType);
	const double ikFkBlend = ikArgs.ikFkBlend;
	outPlan.build = [moduleName, side, shapeType, slots, rest, ikFkBlend](MObject& oRigRoot, MDagModifier& dagMod)
	{
		return TwoBoneIkNode::createModule(moduleName, side, shapeType, slots, rest, ikFkBlend, oRigRoot, dagMod);
	};
	return true;
}
