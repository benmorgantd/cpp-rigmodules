#include "maya/modules/LimbModule.h"
#include "core/Constants.h"
#include "core/ControlPlacement.h"
#include "core/JointMarkup.h"
#include "core/NamingConvention.h"
#include "core/RibbonMath.h"
#include "core/RigJsonStructs.h"
#include "core/StringUtils.h"
#include "maya/modules/TwoBoneIkModule.h"
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
#include <optional>
#include <string>
#include <vector>

#include <maya/MAngle.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnMatrixAttribute.h>
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
MObject LimbNode::aIkTargetMatrix;
MObject LimbNode::aIkTargetRestMatrix;
MObject LimbNode::aIkTargetConnected;
MObject LimbNode::aHandleSocketMatrix;
MObject LimbNode::aFkMatchLocal;
MObject LimbNode::aIkMatchLocal;
MObject LimbNode::aMatchWarnings;
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

	// The anchors' part of the ribbon's setup (D62): the limb joints' rests (the first three of mRests) are the anchors the curve passes
	// through, with a corner at the mid joint. The stored setup is read back with this in compute(), and the inverse rebuilds it, so
	// both run the one rule. False when two joints sit on each other.
	bool setAnchors(RibbonMath::Setup& setup, const MMatrix* mRests)
	{
		setup.fkAnchors = true;
		for (unsigned int k = 0; k < TwoBoneIkSlot::LimbCount; ++k)
		{
			setup.fkRest[k] = MathUtils::toMat4(mRests[k]);
		}
		if (!RibbonMath::fkKnots(setup.fkRest, TwoBoneIkSlot::LimbCount, setup.fkKnotU))
		{
			return false;
		}
		RibbonMath::markAnchorCorners(setup);
		return true;
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
	status = TwoBoneIkPart<LimbNode>::createMatchAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	aIkTargetConnected = nAttr.create(RigAttr::IkTargetConnected, RigAttr::IkTargetConnected, MFnNumericData::kBoolean, false, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aIkTargetConnected));

	MFnMatrixAttribute mAttr;
	aIkTargetMatrix = mAttr.create(RigAttr::IkTargetMatrix, RigAttr::IkTargetMatrix, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aIkTargetMatrix));
	aIkTargetRestMatrix = mAttr.create(RigAttr::IkTargetRestMatrix, RigAttr::IkTargetRestMatrix, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aIkTargetRestMatrix));

	aHandleSocketMatrix = mAttr.create(RigAttr::HandleSocketMatrix, RigAttr::HandleSocketMatrix, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setArray(true));
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setUsesArrayDataBuilder(true));
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setWritable(false));
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(false));
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setWorldSpace(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aHandleSocketMatrix));

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
	TwoBoneIkPart<LimbNode>::declareMatchAffects();
	// The ribbon rides the blended joints, so every two-bone setting also moves the ribbon's controls.
	for (const MObject& oInput : { aIkFkBlend, aStretch, aSoft, aPin, aRoll, aIkRestOffset, aRestBendNormal, aRestPlaneAngle, aLimbLengths, aIkTargetConnected, aIkTargetMatrix, aIkTargetRestMatrix })
	{
		attributeAffects(oInput, aControlOPM);
	}
	for (const MObject& oInput : { aIkTargetConnected, aIkTargetMatrix, aIkTargetRestMatrix })
	{
		attributeAffects(oInput, aJointOPM);
		attributeAffects(oInput, aOutputSocketMatrix);
	}
	// The handle's sockets follow the controls and the plugs they hang from, and nothing the target moves.
	for (const MObject& oInput : { aControlMatrix, aControlRestWorld, aChildPlugParentMatrix, aChildPlugParentRestMatrix, aPoleSpace })
	{
		attributeAffects(oInput, aHandleSocketMatrix);
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

	// The matched IK/FK switch (D62 phase 3) is the two-bone limb's: the ribbon rides the joints and has nothing to match.
	if (oOutputAttr == aFkMatchLocal || oOutputAttr == aIkMatchLocal || oOutputAttr == aMatchWarnings)
	{
		return Part::computeMatch(plug, data, &TwoBoneIkNode::inverse);
	}

	if (oOutputAttr == aHandleSocketMatrix)
	{
		return computeHandleSockets(plug, data);
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
	if (eval.blend > 0.0 && data.inputValue(aIkTargetConnected).asBool())
	{
		eval.hasTarget = true;
		eval.mTargetDelta = MathUtils::computeDeltaFromRest(data.inputValue(aIkTargetRestMatrix).asMatrix(), data.inputValue(aIkTargetMatrix).asMatrix());
	}
	TwoBoneIkSolveResult ik;
	TwoBoneIkSolve::evaluate(eval, ik);

	MArrayDataHandle hControlInputData = data.inputArrayValue(aControlInputData, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	const int controlCount = data.inputValue(aIkControlCount).asInt();
	const int bindCount = static_cast<int>(hControlInputData.elementCount()) - static_cast<int>(TwoBoneIkSlot::Count) - controlCount;
	if (controlCount < 3 || controlCount > RibbonMath::MaxControls || bindCount < 1 || bindCount > RibbonMath::MaxBindJoints)
	{
		// Not built yet: the build sets the count after it creates the controls.
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	// 2. The ribbon's stored rest, with the limb's three joint rests as the anchors the curve passes through.
	RibbonMath::Setup setup;
	RibbonSolve::readSetup(data, restAttrs(), controlCount, bindCount, TwoBoneIkSlot::LimbCount, setup);
	if (!setAnchors(setup, eval.mRest))
	{
		data.setClean(plug);
		return MStatus::kSuccess;
	}

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
	// The ribbon's ik controls have no socket, and each bind joint has one (bind_<n>): the tweaker's world, which is the joint's.
	for (int i = 0; i < controlCount + bindCount; ++i)
	{
		writeSolvedSlot(outputs, LimbSlot::FirstRibbon + static_cast<unsigned int>(i), solve.mControlOPM[i], solve.mWorld[i], i >= controlCount);
	}
	commitSolvedOutputs(data, plug, aJointOPM, outputs);
	return MStatus::kSuccess;
}

MStatus LimbNode::computeHandleSockets(const MPlug& plug, MDataBlock& data)
{
	MStatus status;
	TwoBoneIkEval eval;
	status = TwoBoneIkPart<LimbNode>::readEval(data, eval);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// The handle, then the pole in the space it is in, as TwoBoneIkSolve::evaluate does (slot order matters, D52 section 4.4).
	const ControlSolveStep ikStep = solveControlStep(eval.mLocal[TwoBoneIkSlot::Ik], eval.mRest[TwoBoneIkSlot::Ik], eval.mIkDelta);
	const MMatrix mPoleDelta = eval.poleFollowsIk ? MathUtils::computeDeltaFromRest(eval.mRest[TwoBoneIkSlot::Ik], ikStep.mWorld) : eval.mPoleParentDelta;
	const ControlSolveStep poleStep = solveControlStep(eval.mLocal[TwoBoneIkSlot::Pole], eval.mRest[TwoBoneIkSlot::Pole], mPoleDelta);
	const ControlSolveStep settingsStep = solveControlStep(eval.mLocal[TwoBoneIkSlot::Settings], eval.mRest[TwoBoneIkSlot::Settings], eval.mSettingsDelta);

	MArrayDataHandle hSocket = data.outputArrayValue(aHandleSocketMatrix, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	writeOutputMatrix(hSocket, TwoBoneIkSlot::Ik, ikStep.mWorld);
	writeOutputMatrix(hSocket, TwoBoneIkSlot::Pole, poleStep.mWorld);
	writeOutputMatrix(hSocket, TwoBoneIkSlot::Settings, settingsStep.mWorld);
	commitOutput(data, plug.node(), aHandleSocketMatrix, hSocket);
	data.setClean(plug);
	return MStatus::kSuccess;
}

std::vector<ModuleInverseChannel> LimbNode::inverseChannels()
{
	// The two-bone limb's, then the ribbon's in LimbChannel order, all on the settings control. The defaults are the channels' rests.
	std::vector<ModuleInverseChannel> channels = TwoBoneIkNode::inverseChannels();
	for (const ChannelSpec& spec : limbChannelSpecs())
	{
		channels.push_back(ModuleInverseChannel{ spec.name, TwoBoneIkSlot::Settings, spec.defaultValue });
	}
	return channels;
}

// Allocates (the contract's vectors, the warnings), which is fine for a restore. compute() never calls it. The forward evaluation is the
// source of truth (design/limb.md section 7), so the three steps are the forward ones, run backwards where a joint gives the answer.
ModuleInverseResult LimbNode::inverse(const ModuleInverseContext& context, const ModuleInverseInput& input)
{
	ModuleInverseResult result;
	const unsigned int slotCount = static_cast<unsigned int>(context.controlRestWorld.size());
	const int controlCount = static_cast<int>(context.ikControlCount);
	const int bindCount = static_cast<int>(slotCount) - static_cast<int>(LimbSlot::FirstRibbon) - controlCount;
	result.controlLocal.resize(slotCount);
	result.socketWorld.assign(slotCount, MMatrix::identity);
	if (controlCount < 3 || controlCount > RibbonMath::MaxControls || bindCount < 1 || bindCount > RibbonMath::MaxBindJoints)
	{
		result.reason = "the Limb's slots are not what its build made, so its controls come from the sidecar or rest";
		return result;
	}

	// 1. The two-bone inverse on the first six slots: the FK, IK handle and pole controls from the three limb joints, and the forward
	// evaluation of them, which is the blended joint worlds the ribbon rides.
	ModuleInverseContext ikContext;
	ModuleInverseInput ikInput;
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
	{
		ikContext.controlRestWorld.push_back(context.controlRestWorld[slot]);
		ikInput.jointWorld.push_back(slot < input.jointWorld.size() ? input.jointWorld[slot] : std::nullopt);
		ikInput.parentDelta.push_back(input.parentDelta[slot]);
		ikInput.fallbackLocal.push_back(input.fallbackLocal[slot]);
	}
	ikInput.channels.assign(input.channels.begin(), input.channels.begin() + std::min<std::size_t>(input.channels.size(), TwoBoneIkInverseChannel::Count));
	const ModuleInverseResult ik = TwoBoneIkNode::inverse(ikContext, ikInput);
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
	{
		result.controlLocal[slot] = ik.controlLocal[slot];
		result.socketWorld[slot] = ik.socketWorld[slot];
	}
	result.warnings = ik.warnings;

	// 2. The ribbon's setup, rebuilt from the stored rests the way the plan step made it, and the anchors from the forward joints.
	const std::vector<ModuleInverseChannel> wanted = inverseChannels();
	double channelValues[LimbChannel::Count];
	for (unsigned int k = 0; k < LimbChannel::Count; ++k)
	{
		const std::size_t at = TwoBoneIkInverseChannel::Count + k;
		channelValues[k] = at < input.channels.size() ? input.channels[at] : wanted[at].defaultValue;
	}
	const unsigned int ribbonSlots = static_cast<unsigned int>(controlCount + bindCount);
	RibbonMath::Mat4 anchorRest[TwoBoneIkSlot::LimbCount];
	for (unsigned int i = 0; i < TwoBoneIkSlot::LimbCount; ++i)
	{
		anchorRest[i] = MathUtils::toMat4(context.controlRestWorld[i]);
	}
	RibbonMath::Mat4 bindRest[RibbonMath::MaxBindJoints];
	for (int i = 0; i < bindCount; ++i)
	{
		bindRest[i] = MathUtils::toMat4(context.controlRestWorld[LimbSlot::FirstRibbon + static_cast<unsigned int>(controlCount + i)]);
	}
	const double upperLength = (MathUtils::translationOf(context.controlRestWorld[TwoBoneIkSlot::Lower]) - MathUtils::translationOf(context.controlRestWorld[TwoBoneIkSlot::Upper])).length();
	const double lowerLength = (MathUtils::translationOf(context.controlRestWorld[TwoBoneIkSlot::End]) - MathUtils::translationOf(context.controlRestWorld[TwoBoneIkSlot::Lower])).length();
	const std::vector<double> controlU = ControlPlacement::limbControlParameters(upperLength, lowerLength, controlCount);
	RibbonMath::Mat4 controlRest[RibbonMath::MaxControls];
	RibbonMath::Setup setup;
	const char* pReason = "";
	if (!RibbonMath::buildAnchoredSetup(anchorRest, TwoBoneIkSlot::LimbCount, controlU.data(), controlCount, bindRest, bindCount, setup, controlRest, pReason)
		|| !setAnchors(setup, &context.controlRestWorld[0]))
	{
		result.reason = pReason[0] != '\0' ? pReason : "the Limb's joints sit on each other, so its ribbon has no curve";
		return result;
	}

	MMatrix mRest[RibbonSolve::MaxSlots];
	MMatrix mLocal[RibbonSolve::MaxSlots];
	for (unsigned int j = 0; j < ribbonSlots; ++j)
	{
		mRest[j] = context.controlRestWorld[LimbSlot::FirstRibbon + j];
		mLocal[j] = input.fallbackLocal[LimbSlot::FirstRibbon + j];
	}
	const MMatrix* pJoint = result.socketWorld.data();   // the blended start, mid and end
	const MMatrix anchors[TwoBoneIkSlot::LimbCount] =
	{
		startAnchor(context.controlRestWorld[TwoBoneIkSlot::Upper], pJoint[TwoBoneIkSlot::Upper]),
		pJoint[TwoBoneIkSlot::Lower],
		endAnchor(context.controlRestWorld[TwoBoneIkSlot::End], pJoint[TwoBoneIkSlot::End], context.controlRestWorld[TwoBoneIkSlot::Lower], pJoint[TwoBoneIkSlot::Lower])
	};

	// 3. The ribbon's ik controls have no joint, so they keep the fallback (the sidecar's value, else the rest): the ends are the anchors,
	// which the two-bone half already matched. The tweakers carry what is left, so a bind joint that is present comes back exactly
	// whatever the ik controls hold.
	RibbonSolve::Result solve;
	RibbonSolve::evaluate(setup, paramsFromLimbChannels(channelValues), mRest, mLocal, MMatrix::identity, MMatrix::identity, solve, anchors);
	bool hasEveryBindJoint = true;
	for (int i = 0; i < bindCount; ++i)
	{
		const unsigned int slot = LimbSlot::FirstRibbon + static_cast<unsigned int>(controlCount + i);
		if (slot < input.jointWorld.size() && input.jointWorld[slot].has_value())
		{
			result.controlLocal[slot] = *input.jointWorld[slot] * solve.mControlOPM[controlCount + i].inverse();
			mLocal[controlCount + i] = *result.controlLocal[slot];
		}
		else
		{
			hasEveryBindJoint = false;
		}
	}

	// The bind joints' sockets are what a child of one of them follows: the ribbon with the tweakers as they now are.
	RibbonSolve::evaluate(setup, paramsFromLimbChannels(channelValues), mRest, mLocal, MMatrix::identity, MMatrix::identity, solve, anchors);
	for (int i = 0; i < bindCount; ++i)
	{
		result.socketWorld[LimbSlot::FirstRibbon + static_cast<unsigned int>(controlCount + i)] = solve.mWorld[controlCount + i];
	}

	result.solved = ik.solved && hasEveryBindJoint;
	if (!ik.solved)
	{
		result.reason = ik.reason;
	}
	else if (!hasEveryBindJoint)
	{
		result.reason = "a bind joint is missing from the source, so its tweaker comes from the sidecar or rest";
	}
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

	// The ikTarget child plug (D62 section 8): no control, joint or socket, and optional, so a template that leaves it out keeps the end
	// joint on the IK handle.
	{
		RigModuleChildPlugInfo ikTargetPlug;
		ikTargetPlug.plugIndex = LimbSlot::IkTargetPlug;
		ikTargetPlug.optional = true;
		result.childPlugs[PlugAndSocketNames::IkTarget] = ikTargetPlug;
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
		// The bind joint's socket (bind_<n>). It has no DAG transform: the tweaker is hidden, and a child's controls would inherit that.
		RigModuleSocketInfo bindSocket;
		bindSocket.mRestWorld = input.mRestWorld;
		bindSocket.outputIndex = slot;
		const std::string bindSocketName = std::string(PlugAndSocketNames::Bind) + std::to_string(i);
		result.parentSockets[bindSocketName] = bindSocket;
		DagPlugAndSocketUtils::setParentSocketNames(oModule, slot, { bindSocketName }, dagMod);
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
		// The target is a matrix connection only: it has no control to reparent, and the solve is told it has a parent.
		if (args.childPlug.plugIndex == LimbSlot::IkTargetPlug)
		{
			MStatus targetStatus;
			const MPlug pSocket = LimbNode::getSocketMatrixPlug(args.oParentTargetModuleNode, args.parentSocket.outputIndex, &targetStatus);
			CHECK_MSTATUS_AND_RETURN_IT(targetStatus);
			MPlug pTargetRest(args.oChildModuleNode, LimbNode::aIkTargetRestMatrix);
			DagUtils::setMatrixPlug(pTargetRest, args.parentSocket.mRestWorld, args.dagMod);
			targetStatus = args.dagMod.connect(pSocket, MPlug(args.oChildModuleNode, LimbNode::aIkTargetMatrix));
			CHECK_MSTATUS_AND_RETURN_IT(targetStatus);
			return args.dagMod.newPlugValueBool(MPlug(args.oChildModuleNode, LimbNode::aIkTargetConnected), true);
		}
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
	// The target moves the end joint and what follows it, and none of the handle, the pole and the settings control (blindSockets): that is
	// what lets a foot hang from the handle and feed the target. compute() keeps to it through computeHandleSockets.
	outPlan.moduleInterface.childPlugs.push_back(ModulePlugInfo{ PlugAndSocketNames::IkTarget, false, false, "", "", true,
		{ PlugAndSocketNames::Ik, PlugAndSocketNames::Pole, PlugAndSocketNames::Settings } });
	for (const TwoBoneIkSlotInput& slot : slots)
	{
		outPlan.moduleInterface.controls.push_back(slot.controlMarkup);
	}
	for (std::size_t i = 0; i < bindJoints.size(); ++i)
	{
		outPlan.moduleInterface.sockets.push_back(std::string(PlugAndSocketNames::Bind) + std::to_string(i));
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
