#include "maya/modules/LimbModule.h"
#include "core/Constants.h"
#include "core/ControlPlacement.h"
#include "core/JointMarkup.h"
#include "core/NamingConvention.h"
#include "core/RibbonMath.h"
#include "core/RigJsonStructs.h"
#include "core/StringUtils.h"
#include "maya/nodes/Constraints.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RibbonSolve.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/Side.h"
#include "maya/shared/TwoBoneIkPart.h"
#include "maya/shared/TwoBoneIkSolve.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include <maya/MAngle.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MPlug.h>
#include <maya/MTypeId.h>

MTypeId LimbNode::id(0x002192);
MObject LimbNode::aIkFkBlend;
MObject LimbNode::aStretch;
MObject LimbNode::aSoft;
MObject LimbNode::aPin;
MObject LimbNode::aRoll;
MObject LimbNode::aPoleSpace;
MObject LimbNode::aIkRestOffset;
MObject LimbNode::aRestBendNormal;
MObject LimbNode::aRestPlaneAngle;
MObject LimbNode::aLimbLengths;
MObject LimbNode::aFkVisible;
MObject LimbNode::aIkVisible;
std::array<MObject, LimbChannel::Count> LimbNode::aChannels;
MObject LimbNode::aIkControlCount;
MObject LimbNode::aKnotU;
MObject LimbNode::aBindU;
MObject LimbNode::aRestLength;
MObject LimbNode::aChainSign;
MObject LimbNode::aRestUp;
MObject LimbNode::aRestTangent;
MObject LimbNode::aRibbonIkRestOffset;
MObject LimbNode::aBindRestOffset;
MObject LimbNode::aFkRollRest;
MObject LimbNode::aFkRollRestIn;

namespace
{
	using RibbonSolve::ChannelSpec;

	// The bind joints must be within this share of the limb's length of the joints they sit at.
	constexpr double kBindTolerance{ 1.0e-3 };

	constexpr int kSwingType{ ConstraintTypes::indexOf(ConstraintTypes::Swing) };
	constexpr int kTwistType{ ConstraintTypes::indexOf(ConstraintTypes::Twist) };

	// In LimbChannel order: the Ribbon's, less the aim blends and the end handle scale, then the elbow's softness (D62).
	const std::array<ChannelSpec, LimbChannel::Count>& limbChannelSpecs()
	{
		static const std::array<ChannelSpec, LimbChannel::Count> specs = []
		{
			const std::array<ChannelSpec, RibbonChannel::Count>& r = RibbonSolve::channelSpecs();
			return std::array<ChannelSpec, LimbChannel::Count>
			{
				r[RibbonChannel::StartTwist], r[RibbonChannel::EndTwist], r[RibbonChannel::SineAmplitude], r[RibbonChannel::SineFrequency],
				r[RibbonChannel::SinePhase], r[RibbonChannel::SineOrient], r[RibbonChannel::SineStartFalloff], r[RibbonChannel::SineEndFalloff],
				r[RibbonChannel::VolumePreserve], r[RibbonChannel::ScaleStart], r[RibbonChannel::ScaleMid], r[RibbonChannel::ScaleEnd],
				ChannelSpec{ RigAttr::BendSoftness, 0.0, 0.0, 1.0, false, false }
			};
		}();
		return specs;
	}

	// The evaluation's parameters from the limb's channels: the ribbon's own for those it shares, and the ribbon's aim blends and end
	// handle scale at their rests (a limb's ends come from its joints).
	RibbonMath::Params paramsFromLimbChannels(const double* values)
	{
		double ribbonValues[RibbonChannel::Count];
		const std::array<ChannelSpec, RibbonChannel::Count>& r = RibbonSolve::channelSpecs();
		for (unsigned int k = 0; k < RibbonChannel::Count; ++k)
		{
			ribbonValues[k] = r[k].defaultValue;
		}
		ribbonValues[RibbonChannel::StartTwist] = values[LimbChannel::StartTwist];
		ribbonValues[RibbonChannel::EndTwist] = values[LimbChannel::EndTwist];
		ribbonValues[RibbonChannel::SineAmplitude] = values[LimbChannel::SineAmplitude];
		ribbonValues[RibbonChannel::SineFrequency] = values[LimbChannel::SineFrequency];
		ribbonValues[RibbonChannel::SinePhase] = values[LimbChannel::SinePhase];
		ribbonValues[RibbonChannel::SineOrient] = values[LimbChannel::SineOrient];
		ribbonValues[RibbonChannel::SineStartFalloff] = values[LimbChannel::SineStartFalloff];
		ribbonValues[RibbonChannel::SineEndFalloff] = values[LimbChannel::SineEndFalloff];
		ribbonValues[RibbonChannel::VolumePreserve] = values[LimbChannel::VolumePreserve];
		ribbonValues[RibbonChannel::ScaleStart] = values[LimbChannel::ScaleStart];
		ribbonValues[RibbonChannel::ScaleMid] = values[LimbChannel::ScaleMid];
		ribbonValues[RibbonChannel::ScaleEnd] = values[LimbChannel::ScaleEnd];
		RibbonMath::Params params = RibbonSolve::paramsFromChannels(ribbonValues);
		params.cornerSoftness = values[LimbChannel::BendSoftness];
		return params;
	}

	// The ribbon's start frame (D62): the start joint's translation and swing, its twist about its rest X taken out. This is the Swing
	// constraint (D31) with the joint as both driver and parent, as the hand-built arm hung its shoulder ribbon.
	MMatrix startAnchor(const MMatrix& mRest, const MMatrix& mWorld)
	{
		const MMatrix mDelta = MathUtils::computeDeltaFromRest(mRest, mWorld);
		return mRest * RigConstraintNode::computeDelta(kSwingType, mRest, mDelta, mDelta);
	}

	// The ribbon's end frame: the end joint's translation and its twist on the swing of the mid joint, so the hand's swing never reaches
	// the forearm. This is the Twist constraint (D31) with the end joint as driver and the mid joint as parent.
	MMatrix endAnchor(const MMatrix& mEndRest, const MMatrix& mEndWorld, const MMatrix& mMidRest, const MMatrix& mMidWorld)
	{
		return mEndRest * RigConstraintNode::computeDelta(
			kTwistType, mEndRest, MathUtils::computeDeltaFromRest(mMidRest, mMidWorld), MathUtils::computeDeltaFromRest(mEndRest, mEndWorld));
	}
}

RibbonSolve::RestAttrs LimbNode::restAttrs()
{
	return { aIkControlCount, aKnotU, aBindU, aRestLength, aChainSign, aRestUp, aRestTangent, aRibbonIkRestOffset, aBindRestOffset, aFkRollRest, aFkRollRestIn };
}

LimbNode::LimbNode()
{
}
LimbNode::~LimbNode()
{
}

void* LimbNode::creator()
{
	return new LimbNode();
}

MStatus LimbNode::initialize()
{
	MStatus status;
	MFnNumericAttribute nAttr;
	MFnUnitAttribute uAttr;

	status = initializeSharedAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);
	status = TwoBoneIkPart<LimbNode>::createAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// The ribbon's channel inputs, connected from the settings control at build.
	const std::array<ChannelSpec, LimbChannel::Count>& specs = limbChannelSpecs();
	for (unsigned int k = 0; k < LimbChannel::Count; ++k)
	{
		const ChannelSpec& spec = specs[k];
		if (spec.isAngle)
		{
			aChannels[k] = uAttr.create(spec.name, spec.name, MFnUnitAttribute::kAngle, 0.0, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			CHECK_MSTATUS_AND_RETURN_IT(uAttr.setStorable(true));
		}
		else
		{
			aChannels[k] = nAttr.create(spec.name, spec.name, MFnNumericData::kDouble, spec.defaultValue, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			CHECK_MSTATUS_AND_RETURN_IT(nAttr.setMin(spec.min));
			CHECK_MSTATUS_AND_RETURN_IT(nAttr.setMax(spec.max));
			CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
		}
		CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aChannels[k]));
	}

	status = RibbonSolve::createRestAttributes(restAttrs(), RigAttr::RibbonIkRestOffset);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// AFFECTS RELATIONSHIPS
	declareStandardAffects();
	TwoBoneIkPart<LimbNode>::declareAffects();
	// The ribbon rides the blended joints, so every two-bone setting also moves the ribbon's controls.
	for (const MObject& oInput : { aIkFkBlend, aStretch, aSoft, aPin, aRoll, aIkRestOffset, aRestBendNormal, aRestPlaneAngle, aLimbLengths })
	{
		attributeAffects(oInput, aControlOPM);
	}
	std::vector<MObject> ribbonInputs(aChannels.begin(), aChannels.end());
	ribbonInputs.insert(ribbonInputs.end(), { aIkControlCount, aKnotU, aBindU, aRestLength, aChainSign, aRestUp, aRestTangent, aRibbonIkRestOffset, aBindRestOffset, aFkRollRest, aFkRollRestIn });
	for (const MObject& oInput : ribbonInputs)
	{
		attributeAffects(oInput, aControlOPM);
		attributeAffects(oInput, aJointOPM);
		attributeAffects(oInput, aOutputSocketMatrix);
	}
	return MStatus::kSuccess;
}

MStatus LimbNode::compute(const MPlug& plug, MDataBlock& data)
{
	using Part = TwoBoneIkPart<LimbNode>;
	const MObject oOutputAttr = plug.attribute();

	if (oOutputAttr == aFkVisible || oOutputAttr == aIkVisible)
	{
		const double blend = data.inputValue(aIkFkBlend).asDouble();
		data.outputValue(oOutputAttr).setDouble(Part::isVisible(oOutputAttr == aFkVisible, blend) ? 1.0 : 0.0);
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	if (oOutputAttr != aControlOPM && oOutputAttr != aJointOPM && oOutputAttr != aOutputSocketMatrix)
	{
		return MStatus::kUnknownParameter;
	}

	// 1. The two-bone solve: the blended start, mid and end joints, and the six controls' offsets.
	MStatus status;
	TwoBoneIkEval eval;
	status = Part::readEval(data, eval);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	TwoBoneIkSolveResult ik;
	TwoBoneIkSolve::evaluate(eval, ik);

	MArrayDataHandle hControlInputData = data.inputArrayValue(aControlInputData, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	const int controlCount = data.inputValue(aIkControlCount).asInt();
	const int bindCount = static_cast<int>(hControlInputData.elementCount()) - static_cast<int>(LimbSlot::FirstRibbon) - controlCount;
	if (controlCount < 3 || controlCount > RibbonMath::MaxControls || bindCount < 1 || bindCount > RibbonMath::MaxBindJoints)
	{
		// Not built yet: the build sets the count after it creates the controls.
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	// 2. The ribbon's stored rest, with the limb's three joint rests as the anchors the curve passes through.
	RibbonMath::Setup setup;
	RibbonSolve::readSetup(data, restAttrs(), controlCount, bindCount, TwoBoneIkSlot::LimbCount, setup);
	setup.fkAnchors = true;
	for (unsigned int k = 0; k < TwoBoneIkSlot::LimbCount; ++k)
	{
		setup.fkRest[k] = MathUtils::toMat4(eval.mRest[k]);
	}
	if (!RibbonMath::fkKnots(setup.fkRest, TwoBoneIkSlot::LimbCount, setup.fkKnotU))
	{
		data.setClean(plug);
		return MStatus::kSuccess;
	}
	RibbonMath::markAnchorCorners(setup);

	double channelValues[LimbChannel::Count];
	RibbonSolve::readChannels(data, aChannels.data(), limbChannelSpecs().data(), LimbChannel::Count, channelValues);

	MMatrix mRibbonLocal[RibbonSolve::MaxSlots];
	MMatrix mRibbonRest[RibbonSolve::MaxSlots];
	status = readControlInputs(hControlInputData, LimbSlot::FirstRibbon, static_cast<unsigned int>(controlCount + bindCount), mRibbonLocal, mRibbonRest);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// 3. The curve through the blended joints (D62): the start's swing, the mid joint, the end's twist on the mid's swing.
	const MMatrix anchors[TwoBoneIkSlot::LimbCount] =
	{
		startAnchor(eval.mRest[TwoBoneIkSlot::Upper], ik.mJointWorld[TwoBoneIkSlot::Upper]),
		ik.mJointWorld[TwoBoneIkSlot::Lower],
		endAnchor(eval.mRest[TwoBoneIkSlot::End], ik.mJointWorld[TwoBoneIkSlot::End], eval.mRest[TwoBoneIkSlot::Lower], ik.mJointWorld[TwoBoneIkSlot::Lower])
	};
	RibbonSolve::Result solve;
	RibbonSolve::evaluate(setup, paramsFromLimbChannels(channelValues), mRibbonRest, mRibbonLocal, MMatrix::identity, MMatrix::identity, solve, anchors);

	SolvedOutputs outputs = openSolvedOutputs(data, aJointOPM, status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
	{
		writeSolvedSlot(outputs, slot, ik.mControlOPM[slot], ik.mJointWorld[slot]);
	}
	// The ribbon's controls and bind joints have no socket.
	for (int i = 0; i < controlCount + bindCount; ++i)
	{
		writeSolvedSlot(outputs, LimbSlot::FirstRibbon + static_cast<unsigned int>(i), solve.mControlOPM[i], solve.mWorld[i], false);
	}
	commitSolvedOutputs(data, plug, aJointOPM, outputs);
	return MStatus::kSuccess;
}

ModuleInverseResult LimbNode::inverse(const ModuleInverseContext& context, const ModuleInverseInput&)
{
	ModuleInverseResult result;
	result.controlLocal.resize(context.controlRestWorld.size());
	result.socketWorld.assign(context.controlRestWorld.size(), MMatrix::identity);
	result.reason = "the Limb's inverse is not built yet (D62 phase 3), so its controls come from the sidecar or rest";
	return result;
}

RigModuleBuildResult LimbNode::createModule(
	const MString& moduleName,
	const Side& side,
	const ShapeType& shapeType,
	const std::array<TwoBoneIkSlotInput, TwoBoneIkSlot::Count>& slots,
	const TwoBoneIkRest& rest,
	const std::vector<RibbonSlotInput>& ribbonControls,
	const std::vector<RibbonSlotInput>& bindJoints,
	const RibbonMath::Setup& setup,
	double ikFkBlend,
	MObject& oRigRoot,
	MDagModifier& dagMod)
{
	using Part = TwoBoneIkPart<LimbNode>;
	RigModuleBuildResult result;
	MStatus status;

	// 1. Module node
	MObject oModule = dagMod.MDGModifier::createNode(LimbNode::id);
	dagMod.renameNode(oModule, moduleName);
	dagMod.newPlugValueShort(MPlug(oModule, LimbNode::aSide), getShortFromSide(side));
	result.moduleNode = oModule;

	// 2. The two-bone limb's six slots.
	MObject oControls[TwoBoneIkSlot::Count];
	status = Part::createSlots(oModule, side, shapeType, slots, rest, result, oControls, dagMod);
	if (!status)
	{
		result.status = status;
		return result;
	}

	// 3. The ribbon's ik controls, which have no joint (D57): created loose, and put under the start plug's DAG parent in pass 2 (the
	// callback below). Then the tweakers, one per bind joint, hidden and under the first ik control. Neither has a socket or a plug.
	const unsigned int controlCount = static_cast<unsigned int>(ribbonControls.size());
	std::vector<MObject> oRibbonControls;
	for (unsigned int j = 0; j < controlCount; ++j)
	{
		const RibbonSlotInput& input = ribbonControls[j];
		const MObject oControl = RigControlNode::createRigControl(dagMod, MString(input.baseName.c_str()), shapeType, side, input.controlMarkup, MEulerRotation::kXYZ);
		status = wireControl(oModule, oControl, LimbSlot::FirstRibbon + j, input.mRestWorld, dagMod);
		if (!status)
		{
			result.status = status;
			return result;
		}
		oRibbonControls.push_back(oControl);
	}
	for (unsigned int i = 0; i < bindJoints.size(); ++i)
	{
		const RibbonSlotInput& input = bindJoints[i];
		const unsigned int slot = LimbSlot::FirstRibbon + controlCount + i;
		status = LimbNode::wireJoint(oModule, input.oJoint, slot, input.mRestWorld, dagMod);
		if (!status)
		{
			result.status = status;
			return result;
		}
		const MObject oControl = RigControlNode::createRigControl(
			dagMod, MFnDependencyNode(input.oJoint).name(), shapeType, side, input.controlMarkup, MEulerRotation::kXYZ);
		dagMod.reparentNode(oControl, oRibbonControls[0]);
		status = wireControl(oModule, oControl, slot, input.mRestWorld, dagMod);
		if (!status)
		{
			result.status = status;
			return result;
		}
		// A tweaker is the residual layer: hidden, still keyable (design/ribbon.md section 3.2).
		dagMod.newPlugValueBool(MPlug(oControl, MPxTransform::visibility), false);
	}

	// 4. The ribbon's rest data.
	RibbonSolve::writeRestData(oModule, restAttrs(), setup, dagMod);

	// 5. The channels, all on the settings control. The controls must exist before an attribute can be added to them, and the
	// attributes before they can be wired, so the build runs the modifier here, as TwoBoneIk does.
	status = dagMod.doIt();
	if (!status)
	{
		result.status = status;
		return result;
	}
	status = Part::queueChannels(oControls, ikFkBlend, dagMod);
	CHECK_MSTATUS_AND_RETURN(status, result);
	const std::array<ChannelSpec, LimbChannel::Count>& specs = limbChannelSpecs();
	for (const ChannelSpec& spec : specs)
	{
		status = spec.isAngle
			? RigPoseScene::addAngleChannel(oControls[TwoBoneIkSlot::Settings], spec.name, spec.negate, dagMod)
			: RigPoseScene::addDoubleChannel(oControls[TwoBoneIkSlot::Settings], spec.name, spec.defaultValue, spec.min, spec.max, spec.negate, dagMod);
		CHECK_MSTATUS_AND_RETURN(status, result);
	}
	status = dagMod.doIt();
	if (!status)
	{
		result.status = status;
		return result;
	}
	status = Part::wireChannels(oModule, oControls, dagMod);
	CHECK_MSTATUS_AND_RETURN(status, result);
	for (unsigned int k = 0; k < LimbChannel::Count; ++k)
	{
		MPlug pControlAttr = MFnDependencyNode(oControls[TwoBoneIkSlot::Settings]).findPlug(specs[k].name, false, &status);
		CHECK_MSTATUS_AND_RETURN(status, result);
		status = dagMod.connect(pControlAttr, MPlug(oModule, LimbNode::aChannels[k]));
		CHECK_MSTATUS_AND_RETURN(status, result);
	}

	// 6. Network
	if (!oRigRoot.isNull())
	{
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dagMod, oModule, aRigRoot);
	}

	// The shared callback does the plugs. The ribbon's ik controls are leaf children of the start plug's DAG parent (author, 2026-11-19),
	// the same control the start control goes under, and a socket with no control (another limb's joint) leaves them where they are.
	result.dagParentCallback = [oRibbonControls](const RigModuleDagReparentArgs& args)
	{
		MStatus callbackStatus = LimbNode::reparentChildPlugToSocket(args);
		CHECK_MSTATUS_AND_RETURN_IT(callbackStatus);
		if (args.childPlug.plugIndex == TwoBoneIkSlot::Upper && args.reparentDag && !args.parentSocket.oTransform.isNull())
		{
			for (const MObject& oControl : oRibbonControls)
			{
				callbackStatus = args.dagMod.reparentNode(oControl, args.parentSocket.oTransform);
				CHECK_MSTATUS_AND_RETURN_IT(callbackStatus);
			}
		}
		return callbackStatus;
	};
	result.status = MStatus::kSuccess;
	return result;
}

bool LimbNode::planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan)
{
	LimbArgs limbArgs;
	if (!readModuleArgs(args, { "joints", "bindJoints" }, ctx, limbArgs,
		{ "controlCount", "controlName", "poleOffset", "settingsOffset", RigAttr::IkFkBlend, "ikOrientToWorld", "ikAlignToPole" }))
	{
		return false;
	}

	const std::string argsPath = ctx.where + "." + TemplateKey::ModuleArgs;
	bool valid = true;
	if (limbArgs.controlCount < 3 || limbArgs.controlCount > RibbonMath::MaxControls)
	{
		ctx.report.addError(argsPath + ".controlCount", "a limb's ribbon has between 3 and " + std::to_string(RibbonMath::MaxControls)
			+ " ik controls (the start, the mid joint, the end and the ones between), got " + std::to_string(limbArgs.controlCount));
		valid = false;
	}
	if (limbArgs.bindJoints.size() < 5 || limbArgs.bindJoints.size() > static_cast<std::size_t>(RibbonMath::MaxBindJoints))
	{
		ctx.report.addError(argsPath + ".bindJoints", "a limb has between 5 and " + std::to_string(RibbonMath::MaxBindJoints)
			+ " bind joints (the start, the mid joint, the end and the ones between), got " + std::to_string(limbArgs.bindJoints.size()));
		valid = false;
	}
	if (!valid)
	{
		return false;
	}

	// Claim every joint even after a failure, so the report lists every bad selector at once. A bind joint is a leaf: the tweaker drives
	// it by its offset parent matrix, and a child would be dragged along by a joint that is not one of the limb's.
	std::vector<RibbonSlotInput> bindJoints;
	for (const JointSelector& selector : limbArgs.bindJoints)
	{
		RibbonSlotInput slot;
		if (!RigModuleNodeBase::claimJoint(ctx, selector, slot.oJoint, slot.mRestWorld))
		{
			valid = false;
			continue;
		}
		if (MFnDagNode(slot.oJoint).childCount() != 0)
		{
			ctx.report.addError(argsPath + ".bindJoints", "'" + formatJointSelector(selector) + "' has child joints, and a bind joint must be a leaf");
			valid = false;
			continue;
		}
		slot.controlMarkup = makeControlMarkup(selector.side, selector.usage);
		bindJoints.push_back(slot);
	}

	const TwoBoneIkArgs ikArgs{ limbArgs.joints, limbArgs.controlName, limbArgs.poleOffset, limbArgs.settingsOffset, limbArgs.ikFkBlend,
		limbArgs.ikOrientToWorld, limbArgs.ikAlignToPole };
	std::array<TwoBoneIkSlotInput, TwoBoneIkSlot::Count> slots;
	TwoBoneIkRest rest;
	valid = TwoBoneIkSolve::planSlots(ikArgs, ctx, slots, rest) && valid;
	if (!valid)
	{
		return false;
	}

	// The curve follows the joints' X axes, so they must run down both chains, one way.
	std::vector<RibbonMath::Mat4> limbRest;
	std::vector<std::string> limbNames;
	for (unsigned int i = 0; i < TwoBoneIkSlot::LimbCount; ++i)
	{
		limbRest.push_back(MathUtils::toMat4(slots[i].mRestWorld));
		limbNames.push_back(formatJointSelector(limbArgs.joints[i]));
	}
	// The end joint may point past the limb (an ankle at the ball of the foot), so it gets the wider limit.
	ControlPlacement::ChainCheck chainCheck = ControlPlacement::checkChain(limbRest, ControlPlacement::MaxLimbEndAxisAngleDegrees);
	if (chainCheck.problem != ControlPlacement::ChainProblem::None)
	{
		ctx.report.addError(argsPath + ".joints", ControlPlacement::describeChainProblem(chainCheck, limbNames));
		return false;
	}
	std::vector<RibbonMath::Mat4> bindRest;
	std::vector<std::string> bindNames;
	for (std::size_t i = 0; i < bindJoints.size(); ++i)
	{
		bindRest.push_back(MathUtils::toMat4(bindJoints[i].mRestWorld));
		bindNames.push_back(formatJointSelector(limbArgs.bindJoints[i]));
	}
	chainCheck = ControlPlacement::checkChain(bindRest);
	if (chainCheck.problem != ControlPlacement::ChainProblem::None)
	{
		ctx.report.addError(argsPath + ".bindJoints", ControlPlacement::describeChainProblem(chainCheck, bindNames));
		return false;
	}

	// Where the bind joints sit: one at the root, one at the end and exactly one at the mid joint. Stacked neighbours were rejected above.
	const auto distanceBetween = [](const MMatrix& a, const MMatrix& b)
	{
		return (MathUtils::translationOf(a) - MathUtils::translationOf(b)).length();
	};
	const double upperLength = distanceBetween(slots[TwoBoneIkSlot::Upper].mRestWorld, slots[TwoBoneIkSlot::Lower].mRestWorld);
	const double lowerLength = distanceBetween(slots[TwoBoneIkSlot::Lower].mRestWorld, slots[TwoBoneIkSlot::End].mRestWorld);
	const double tolerance = kBindTolerance * (upperLength + lowerLength);
	if (distanceBetween(bindJoints.front().mRestWorld, slots[TwoBoneIkSlot::Upper].mRestWorld) > tolerance)
	{
		ctx.report.addError(argsPath + ".bindJoints", "the first bind joint '" + bindNames.front() + "' must sit at the root joint '" + limbNames.front() + "'");
		valid = false;
	}
	if (distanceBetween(bindJoints.back().mRestWorld, slots[TwoBoneIkSlot::End].mRestWorld) > tolerance)
	{
		ctx.report.addError(argsPath + ".bindJoints", "the last bind joint '" + bindNames.back() + "' must sit at the end joint '" + limbNames.back() + "'");
		valid = false;
	}
	int atMid = 0;
	for (const RibbonSlotInput& bind : bindJoints)
	{
		atMid += distanceBetween(bind.mRestWorld, slots[TwoBoneIkSlot::Lower].mRestWorld) <= tolerance ? 1 : 0;
	}
	if (atMid != 1)
	{
		ctx.report.addError(argsPath + ".bindJoints", (atMid == 0 ? std::string("no bind joint sits") : std::to_string(atMid) + " bind joints sit") + " at the mid joint '" + limbNames[1]
			+ "', and the elbow or knee needs exactly one");
		valid = false;
	}
	if (!valid)
	{
		return false;
	}

	// The ribbon's ik controls sit on the curve through the three joints (D62, design/limb.md section 5.1), at parameters from the bone
	// lengths, and the setup is built from them.
	const std::vector<double> controlU = ControlPlacement::limbControlParameters(upperLength, lowerLength, limbArgs.controlCount);
	RibbonMath::Mat4 anchorRest[TwoBoneIkSlot::LimbCount];
	for (unsigned int i = 0; i < TwoBoneIkSlot::LimbCount; ++i)
	{
		anchorRest[i] = limbRest[i];
	}
	RibbonMath::Mat4 controlRest[RibbonMath::MaxControls];
	RibbonMath::Mat4 bindRestArray[RibbonMath::MaxBindJoints];
	std::copy(bindRest.begin(), bindRest.end(), bindRestArray);
	RibbonMath::Setup setup;
	const char* pReason = "";
	if (!RibbonMath::buildAnchoredSetup(anchorRest, TwoBoneIkSlot::LimbCount, controlU.data(), limbArgs.controlCount, bindRestArray,
		static_cast<int>(bindJoints.size()), setup, controlRest, pReason))
	{
		ctx.report.addError(argsPath + ".bindJoints", pReason);
		return false;
	}

	// Names (D57): <base>RibbonIK_<nn> for the ik controls and <base>Tweak_<nn> for the tweakers (nn is the bind joint's index), the base
	// being the one the two-bone limb's derived controls use.
	const std::string base = limbArgs.controlName.empty() ? StringUtils::stripNumberSuffix(limbArgs.joints.front().usage) : limbArgs.controlName;
	const std::string& firstSide = limbArgs.joints.front().side;
	std::vector<RibbonSlotInput> ribbonControls;
	for (int j = 0; j < limbArgs.controlCount; ++j)
	{
		RibbonSlotInput slot;
		slot.mRestWorld = MathUtils::toMMatrix(controlRest[j]);
		slot.baseName = StringUtils::numberedName(base + "RibbonIK", j);
		slot.controlMarkup = makeControlMarkup(firstSide, slot.baseName);
		ribbonControls.push_back(slot);
	}
	for (std::size_t i = 0; i < bindJoints.size(); ++i)
	{
		bindJoints[i].controlMarkup = makeControlMarkup(limbArgs.bindJoints[i].side, StringUtils::numberedName(base + "Tweak", static_cast<int>(i)));
	}

	outPlan.moduleInterface = TwoBoneIkSolve::makeInterface();
	for (const TwoBoneIkSlotInput& slot : slots)
	{
		outPlan.moduleInterface.controls.push_back(slot.controlMarkup);
	}
	for (const RibbonSlotInput& slot : ribbonControls)
	{
		outPlan.moduleInterface.controls.push_back(slot.controlMarkup);
	}
	for (const RibbonSlotInput& slot : bindJoints)
	{
		outPlan.moduleInterface.controls.push_back(slot.controlMarkup);
	}

	const MString moduleName = ctx.moduleData.name.c_str();
	const Side side = getSideFromString(ctx.moduleData.side);
	const ShapeType shapeType = getShapeTypeFromString(ctx.moduleData.shapeType);
	const double ikFkBlend = limbArgs.ikFkBlend;
	outPlan.build = [moduleName, side, shapeType, slots, rest, ribbonControls, bindJoints, setup, ikFkBlend](MObject& oRigRoot, MDagModifier& dagMod)
	{
		return LimbNode::createModule(moduleName, side, shapeType, slots, rest, ribbonControls, bindJoints, setup, ikFkBlend, oRigRoot, dagMod);
	};
	return true;
}
