#include "maya/modules/RibbonModule.h"
#include "core/Constants.h"
#include "core/ControlPlacement.h"
#include "core/JointMarkup.h"
#include "core/NamingConvention.h"
#include "core/RibbonMath.h"
#include "core/RigJsonStructs.h"
#include "core/StringUtils.h"
#include "maya/modules/FkChainModule.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/Side.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include <maya/MAngle.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MPlug.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MTypeId.h>
#include <maya/MVector.h>

MTypeId RibbonModuleNode::id(0x002191);
std::array<MObject, RibbonChannel::Count> RibbonModuleNode::aChannels;
MObject RibbonModuleNode::aIkControlCount;
MObject RibbonModuleNode::aFkControlCount;
MObject RibbonModuleNode::aKnotU;
MObject RibbonModuleNode::aBindU;
MObject RibbonModuleNode::aRestLength;
MObject RibbonModuleNode::aChainSign;
MObject RibbonModuleNode::aRestUp;
MObject RibbonModuleNode::aRestTangent;
MObject RibbonModuleNode::aIkRestOffset;
MObject RibbonModuleNode::aBindRestOffset;
MObject RibbonModuleNode::aFkRollRest;
MObject RibbonModuleNode::aFkRollRestIn;
MObject RibbonModuleNode::aFkRollShare;
MObject RibbonModuleNode::aAnchorRest;
MObject RibbonModuleNode::aStartAnchorRotation;
MObject RibbonModuleNode::aEndAnchorRotation;

namespace
{
	using RibbonSolve::ChannelSpec;
	using RibbonSolve::channelSpecs;
	using RibbonSolve::paramsFromChannels;

	// An anchored ribbon's bind joints must be within this share of the anchors' length of the anchors they sit at (Limb's rule), and
	// each anchor plug's socket within it of its anchor joint at rest.
	constexpr double kBindTolerance{ 1.0e-3 };
	// How far an anchor plug's socket's rest axes may be from its anchor joint's, per component of the unit axes.
	constexpr double kAxisTolerance{ 1.0e-3 };

	// Version 2 (D57): the ik controls come from the bind joints, so "controls" (their joints) gives way to "controlCount". A
	// template that already has controlCount, or whose controls isn't a list, is left for the validation that follows.
	void replaceControlsWithCount(nlohmann::json& args)
	{
		if (!args.is_object() || !args.contains("controls") || args.contains("controlCount") || !args["controls"].is_array())
		{
			return;
		}
		args["controlCount"] = args["controls"].size();
		args.erase("controls");
	}

	// Socket names of ik control k, the way FkChain names its own: index_k, plus start and end on the ends.
	ModuleInterface makeRibbonInterface(unsigned int controlCount, unsigned int bindCount, unsigned int fkCount)
	{
		const bool hasFkChain = fkCount > 0;
		ModuleInterface moduleInterface;
		moduleInterface.childPlugs.push_back(ModulePlugInfo{ PlugAndSocketNames::Start, true });
		// The end follows the start's parent unless the template drives it from another socket (section 3.4). With an fk chain the end
		// is on the chain, so it has no plug of its own (D59).
		if (!hasFkChain)
		{
			moduleInterface.childPlugs.push_back(ModulePlugInfo{ PlugAndSocketNames::End, true, true, "", PlugAndSocketNames::Start });
		}
		for (unsigned int k = 0; k < controlCount; ++k)
		{
			const std::vector<std::string> names = FkChainModuleNode::socketNamesAt(k, controlCount);
			moduleInterface.sockets.insert(moduleInterface.sockets.end(), names.begin(), names.end());
		}
		// Every bind joint and every fk control is a socket too (D78), so each is a constraint driver and a child plug's parent.
		for (unsigned int i = 0; i < bindCount; ++i)
		{
			moduleInterface.sockets.push_back(std::string(PlugAndSocketNames::Bind) + std::to_string(i));
		}
		for (unsigned int k = 0; k < fkCount; ++k)
		{
			moduleInterface.sockets.push_back(std::string(PlugAndSocketNames::Fk) + std::to_string(k));
		}
		return moduleInterface;
	}
}

RibbonSolve::RestAttrs RibbonModuleNode::restAttrs()
{
	return { aIkControlCount, aKnotU, aBindU, aRestLength, aChainSign, aRestUp, aRestTangent, aIkRestOffset, aBindRestOffset, aFkRollRest, aFkRollRestIn, aFkRollShare };
}

RibbonModuleNode::RibbonModuleNode()
{
}
RibbonModuleNode::~RibbonModuleNode()
{
}

const UpdateTable& RibbonModuleNode::argUpdateTable()
{
	// Add a step here, and nowhere else, when the Ribbon's moduleArgs change shape or meaning. Step 0 takes version 1 to 2.
	static const UpdateTable table{ 1, { &replaceControlsWithCount } };
	return table;
}

void* RibbonModuleNode::creator()
{
	return new RibbonModuleNode();
}

MStatus RibbonModuleNode::initialize()
{
	MStatus status;
	MFnNumericAttribute nAttr;
	MFnMatrixAttribute mAttr;
	MFnUnitAttribute uAttr;

	status = initializeSharedAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// Channel inputs, connected from the first control at build.
	const std::array<ChannelSpec, RibbonChannel::Count>& specs = channelSpecs();
	for (unsigned int k = 0; k < RibbonChannel::Count; ++k)
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

	// Rest data, stored at build like Aim's aimRestOffset.
	status = RibbonSolve::createRestAttributes(restAttrs(), RigAttr::IkRestOffset);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	aFkControlCount = nAttr.create(RigAttr::FkControlCount, RigAttr::FkControlCount, MFnNumericData::kInt, 0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aFkControlCount));

	// The anchored form (D80), stored at build.
	aAnchorRest = mAttr.create(RigAttr::AnchorRest, RigAttr::AnchorRest, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setArray(true));
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aAnchorRest));
	for (const auto& [pAttr, name] : { std::pair<MObject*, const char*>{ &aStartAnchorRotation, RigAttr::StartAnchorRotation }, { &aEndAnchorRotation, RigAttr::EndAnchorRotation } })
	{
		*pAttr = nAttr.create(name, name, MFnNumericData::kShort, AnchorRotation::Full, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(addAttribute(*pAttr));
	}

	// AFFECTS RELATIONSHIPS
	declareStandardAffects();
	std::vector<MObject> settings(aChannels.begin(), aChannels.end());
	settings.insert(settings.end(), { aIkControlCount, aFkControlCount, aKnotU, aBindU, aRestLength, aChainSign, aRestUp, aRestTangent, aIkRestOffset, aBindRestOffset, aFkRollRest, aFkRollRestIn, aFkRollShare,
		aAnchorRest, aStartAnchorRotation, aEndAnchorRotation });
	for (const MObject& oInput : settings)
	{
		attributeAffects(oInput, aControlOPM);
		attributeAffects(oInput, aJointOPM);
		attributeAffects(oInput, aOutputSocketMatrix);
	}

	return MStatus::kSuccess;
}

MStatus RibbonModuleNode::compute(const MPlug& plug, MDataBlock& data)
{
	const MObject oOutputAttr = plug.attribute();
	if (oOutputAttr != aControlOPM && oOutputAttr != aJointOPM && oOutputAttr != aOutputSocketMatrix)
	{
		return MStatus::kUnknownParameter;
	}

	MStatus status;
	MArrayDataHandle hAnchorRest = data.inputArrayValue(aAnchorRest, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	if (hAnchorRest.elementCount() == RibbonSolve::AnchorCount)
	{
		return computeAnchored(plug, data, hAnchorRest);
	}
	MArrayDataHandle hControlInputData = data.inputArrayValue(aControlInputData, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hChildPlugs = data.inputArrayValue(aChildPlugs, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	const int controlCount = data.inputValue(aIkControlCount).asInt();
	const int fkCount = data.inputValue(aFkControlCount).asInt();
	const unsigned int slotCount = hControlInputData.elementCount();
	const int bindCount = static_cast<int>(slotCount) - controlCount - fkCount;
	if (controlCount < 2 || bindCount < 1 || fkCount < 0 || fkCount == 1 || fkCount > RibbonMath::MaxControls || slotCount > MaxSlots)
	{
		// Not built yet: the build sets the count after it creates the controls.
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	// Everything the evaluation reads, as plain values: the stored rest, the channels, the controls' locals.
	RibbonMath::Setup setup;
	RibbonSolve::readSetup(data, restAttrs(), controlCount, bindCount, fkCount, setup);

	double channelValues[RibbonChannel::Count];
	RibbonSolve::readChannels(data, aChannels.data(), channelSpecs().data(), RibbonChannel::Count, channelValues);

	MMatrix mLocal[MaxSlots];
	MMatrix mRest[MaxSlots];
	status = readControlInputs(hControlInputData, 0, slotCount, mLocal, mRest);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	if (fkCount > 0)
	{
		// The fk chain's rests are the controls' own, and its parameters their chord shares, so neither is stored. Its rest roll needs
		// the rest curve built, so that is stored (fkRollRest).
		for (int k = 0; k < fkCount; ++k)
		{
			setup.fkRest[k] = MathUtils::toMat4(mRest[controlCount + bindCount + k]);
		}
		if (!RibbonMath::fkKnots(setup.fkRest, fkCount, setup.fkKnotU))
		{
			data.setClean(plug);
			return MStatus::kSuccess;
		}
	}

	RibbonSolve::Result solve;
	RibbonSolve::evaluate(
		setup, paramsFromChannels(channelValues), mRest, mLocal,
		readChildPlugParentDelta(hChildPlugs, 0),
		fkCount > 0 ? MMatrix::identity : readChildPlugParentDelta(hChildPlugs, controlCount - 1),
		solve);

	SolvedOutputs outputs = openSolvedOutputs(data, aJointOPM, status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	for (unsigned int slot = 0; slot < slotCount; ++slot)
	{
		writeSolvedSlot(outputs, slot, solve.mControlOPM[slot], solve.mWorld[slot]);   // every slot has a socket element (D78)
	}
	commitSolvedOutputs(data, plug, aJointOPM, outputs);
	return MStatus::kSuccess;
}

MStatus RibbonModuleNode::computeAnchored(const MPlug& plug, MDataBlock& data, MArrayDataHandle& hAnchorRest)
{
	MStatus status;
	MArrayDataHandle hControlInputData = data.inputArrayValue(aControlInputData, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hChildPlugs = data.inputArrayValue(aChildPlugs, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// The curve's controls are its two ends, which have no slot, and the interior ones, which are the leading slots.
	const int curveCount = data.inputValue(aIkControlCount).asInt();
	const int interiorCount = curveCount - 2;
	const int bindCount = static_cast<int>(hControlInputData.elementCount()) - interiorCount;
	if (curveCount < 2 || curveCount > RibbonMath::MaxControls || bindCount < 1 || bindCount > RibbonMath::MaxBindJoints)
	{
		// Not built yet: the build sets the count after it creates the controls.
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	MMatrix mAnchorRest[RibbonSolve::AnchorCount];
	for (unsigned int k = 0; k < RibbonSolve::AnchorCount; ++k)
	{
		if (hAnchorRest.jumpToElement(k))
		{
			mAnchorRest[k] = hAnchorRest.inputValue().asMatrix();
		}
	}
	RibbonMath::Setup setup;
	RibbonSolve::readSetup(data, restAttrs(), curveCount, bindCount, static_cast<int>(RibbonSolve::AnchorCount), setup);
	if (!RibbonSolve::setAnchors(setup, mAnchorRest))
	{
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	double channelValues[RibbonChannel::Count];
	RibbonSolve::readChannels(data, aChannels.data(), channelSpecs().data(), RibbonChannel::Count, channelValues);

	// By the curve's controls (the ends at the anchors with no local, the interior from their slots), then the bind joints.
	MMatrix mLocal[MaxSlots];
	MMatrix mRest[MaxSlots];
	mRest[0] = mAnchorRest[0];
	mRest[curveCount - 1] = mAnchorRest[RibbonSolve::AnchorCount - 1];
	status = readControlInputs(hControlInputData, 0, static_cast<unsigned int>(interiorCount), mLocal + 1, mRest + 1);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	status = readControlInputs(hControlInputData, static_cast<unsigned int>(interiorCount), static_cast<unsigned int>(bindCount), mLocal + curveCount, mRest + curveCount);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// The anchors as the plugs carry them, each its rest carried by its plug's motion, so an unconnected plug leaves it at rest. The build
	// checked that each plug's socket rests at its anchor joint.
	const unsigned int firstPlug = static_cast<unsigned int>(interiorCount + bindCount);
	MMatrix mAnchorWorld[RibbonSolve::AnchorCount];
	for (unsigned int k = 0; k < RibbonSolve::AnchorCount; ++k)
	{
		mAnchorWorld[k] = mAnchorRest[k] * readChildPlugParentDelta(hChildPlugs, firstPlug + k);
	}
	MMatrix mFrames[RibbonSolve::AnchorCount];
	RibbonSolve::anchorFrames(mAnchorRest, mAnchorWorld, data.inputValue(aStartAnchorRotation).asShort(), data.inputValue(aEndAnchorRotation).asShort(), mFrames);

	RibbonSolve::Result solve;
	RibbonSolve::evaluate(setup, paramsFromChannels(channelValues), mRest, mLocal, MMatrix::identity, MMatrix::identity, solve, mFrames);

	SolvedOutputs outputs = openSolvedOutputs(data, aJointOPM, status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	for (int j = 0; j < interiorCount; ++j)
	{
		writeSolvedSlot(outputs, static_cast<unsigned int>(j), solve.mControlOPM[j + 1], solve.mWorld[j + 1]);
	}
	for (int i = 0; i < bindCount; ++i)
	{
		writeSolvedSlot(outputs, static_cast<unsigned int>(interiorCount + i), solve.mControlOPM[curveCount + i], solve.mWorld[curveCount + i]);
	}
	commitSolvedOutputs(data, plug, aJointOPM, outputs);
	return MStatus::kSuccess;
}

std::vector<ModuleInverseChannel> RibbonModuleNode::inverseChannels()
{
	std::vector<ModuleInverseChannel> channels;
	for (const ChannelSpec& spec : channelSpecs())
	{
		channels.push_back(ModuleInverseChannel{ spec.name, 0, spec.defaultValue });
	}
	return channels;
}

// Allocates (the contract's vectors), which is fine for a restore. compute() never calls it.
ModuleInverseResult RibbonModuleNode::inverse(const ModuleInverseContext& context, const ModuleInverseInput& input)
{
	if (context.anchorRest.size() == RibbonSolve::AnchorCount)
	{
		return anchoredInverse(context, input);
	}
	ModuleInverseResult result;
	const unsigned int slotCount = static_cast<unsigned int>(context.controlRestWorld.size());
	const int controlCount = static_cast<int>(context.ikControlCount);
	const int fkCount = static_cast<int>(context.fkControlCount);
	result.controlLocal.resize(slotCount);
	result.socketWorld.assign(slotCount, MMatrix::identity);

	if (controlCount < 2 || fkCount == 1 || fkCount > RibbonMath::MaxControls || slotCount <= static_cast<unsigned int>(controlCount + fkCount) || slotCount > MaxSlots)
	{
		result.reason = "the ribbon's slots are not what its build made, so its controls come from the sidecar or rest";
		return result;
	}
	const int bindCount = static_cast<int>(slotCount) - controlCount - fkCount;

	// The setup comes from the stored rests, the way the plan step made it.
	RibbonMath::Mat4 controlRest[RibbonMath::MaxControls];
	RibbonMath::Mat4 bindRest[RibbonMath::MaxBindJoints];
	for (int j = 0; j < controlCount; ++j)
	{
		controlRest[j] = MathUtils::toMat4(context.controlRestWorld[j]);
	}
	for (int i = 0; i < bindCount; ++i)
	{
		bindRest[i] = MathUtils::toMat4(context.controlRestWorld[controlCount + i]);
	}
	RibbonMath::Mat4 fkRest[RibbonMath::MaxControls];
	for (int k = 0; k < fkCount; ++k)
	{
		fkRest[k] = MathUtils::toMat4(context.controlRestWorld[controlCount + bindCount + k]);
	}
	RibbonMath::Setup setup;
	const char* pReason = "";
	if (!RibbonMath::buildSetup(controlRest, controlCount, bindRest, bindCount, fkRest, fkCount, setup, pReason))
	{
		result.reason = pReason;
		return result;
	}
	RibbonSolve::applyStoredShares(context.fkRollShare, setup);

	const std::vector<ModuleInverseChannel> wanted = inverseChannels();
	double channelValues[RibbonChannel::Count];
	for (unsigned int k = 0; k < RibbonChannel::Count; ++k)
	{
		channelValues[k] = k < input.channels.size() ? input.channels[k] : wanted[k].defaultValue;
	}
	const RibbonMath::Params params = paramsFromChannels(channelValues);

	MMatrix mRest[MaxSlots];
	MMatrix mLocal[MaxSlots];
	for (unsigned int slot = 0; slot < slotCount; ++slot)
	{
		mRest[slot] = context.controlRestWorld[slot];
		mLocal[slot] = input.fallbackLocal[slot];
	}
	const MMatrix& mStartDelta = input.parentDelta[0];
	const MMatrix& mEndDelta = input.parentDelta[controlCount - 1];

	const auto hasJoint = [&input](unsigned int slot)
	{
		return slot < input.jointWorld.size() && input.jointWorld[slot].has_value();
	};

	// 1. The ends from the first and last bind joints, whose frames they share (D57). A joint's frame is not quite its end
	// control's (the sine layer and a stretch move it), so the control is refined: with the tweaker at rest, each pass moves the
	// control by the gap its joint is still away from where the solve puts it, until the gap is gone. Each pass costs one
	// evaluation, and a restore is not playback. The two ends move each other's tangents, so a pass settles about a quarter of
	// the gap, and the cap leaves it below 1e-7.
	bool solved[MaxSlots] = {};
	const unsigned int firstBind = static_cast<unsigned int>(controlCount);
	const unsigned int lastBind = static_cast<unsigned int>(controlCount + bindCount - 1);
	for (const int j : { 0, controlCount - 1 })
	{
		const unsigned int bindSlot = j == 0 ? firstBind : lastBind;
		if (hasJoint(bindSlot))
		{
			mLocal[j] = ModuleInverse::localFromWorld(*input.jointWorld[bindSlot], mRest[j], j == 0 ? mStartDelta : mEndDelta);
			solved[j] = true;
		}
	}
	RibbonSolve::Result solve;
	constexpr int MaxEndRefinePasses = 24;
	constexpr double EndRefineTolerance = 1e-9;
	for (int pass = 0; pass < MaxEndRefinePasses && (solved[0] || solved[controlCount - 1]); ++pass)
	{
		const MMatrix mKeepFirst = mLocal[firstBind];
		const MMatrix mKeepLast = mLocal[lastBind];
		mLocal[firstBind] = MMatrix::identity;
		mLocal[lastBind] = MMatrix::identity;
		RibbonSolve::evaluate(setup, params, mRest, mLocal, mStartDelta, mEndDelta, solve);
		mLocal[firstBind] = mKeepFirst;
		mLocal[lastBind] = mKeepLast;
		bool settled = true;
		for (const int j : { 0, controlCount - 1 })
		{
			const unsigned int bindSlot = j == 0 ? firstBind : lastBind;
			if (!solved[j])
			{
				continue;
			}
			const MMatrix mEndWorld = mLocal[j] * solve.mControlOPM[j];
			const MMatrix mGap = *input.jointWorld[bindSlot] * solve.mWorld[bindSlot].inverse();
			settled = settled && mGap.isEquivalent(MMatrix::identity, EndRefineTolerance);
			// An end rides its parent's motion, or with an fk chain the curve through the chain, whose offset parent matrix the
			// solve just gave.
			mLocal[j] = setup.fkCount > 0
				? ModuleInverse::localFromWorld(mGap * mEndWorld, solve.mControlOPM[j], MMatrix::identity)
				: ModuleInverse::localFromWorld(mGap * mEndWorld, mRest[j], j == 0 ? mStartDelta : mEndDelta);
		}
		if (settled)
		{
			break;
		}
	}

	// 2. The interior controls have no joint, so they keep the fallback (the sidecar's value, else the rest) and the tweakers
	// below carry what is left.
	// 3. The tweakers are the residual: each bind joint against the frame the detail curve gives it, which the controls
	// above fixed. A bind joint that is present comes back exactly whatever the interior controls hold.
	RibbonSolve::evaluate(setup, params, mRest, mLocal, mStartDelta, mEndDelta, solve);
	bool hasEveryBindJoint = true;
	for (int slot = controlCount; slot < controlCount + bindCount; ++slot)
	{
		if (hasJoint(slot))
		{
			mLocal[slot] = *input.jointWorld[slot] * solve.mControlOPM[slot].inverse();
			solved[slot] = true;
		}
		else
		{
			hasEveryBindJoint = false;
		}
	}

	for (unsigned int slot = 0; slot < slotCount; ++slot)
	{
		if (solved[slot])
		{
			result.controlLocal[slot] = mLocal[slot];
		}
	}
	RibbonSolve::evaluate(setup, params, mRest, mLocal, mStartDelta, mEndDelta, solve);
	for (unsigned int slot = 0; slot < slotCount; ++slot)
	{
		result.socketWorld[slot] = solve.mWorld[slot];
	}

	result.solved = hasEveryBindJoint;
	if (!result.solved)
	{
		result.reason = "a bind joint is missing from the source, so its tweaker comes from the sidecar or rest";
	}
	return result;
}

// Allocates (the contract's vectors), which is fine for a restore. compute() never calls it.
ModuleInverseResult RibbonModuleNode::anchoredInverse(const ModuleInverseContext& context, const ModuleInverseInput& input)
{
	ModuleInverseResult result;
	const unsigned int slotCount = static_cast<unsigned int>(context.controlRestWorld.size());
	const int curveCount = static_cast<int>(context.ikControlCount);
	const int interiorCount = curveCount - 2;
	// The slots are the interior controls, the tweakers, then the four plugs (start, mid, end, settings).
	const int bindCount = static_cast<int>(slotCount) - interiorCount - static_cast<int>(RibbonSolve::AnchorCount) - 1;
	result.controlLocal.resize(slotCount);
	result.socketWorld.assign(slotCount, MMatrix::identity);
	if (curveCount < 2 || curveCount > RibbonMath::MaxControls || bindCount < 1 || bindCount > RibbonMath::MaxBindJoints)
	{
		result.reason = "the ribbon's slots are not what its build made, so its controls come from the sidecar or rest";
		return result;
	}

	// 1. The anchors: each plug's parent, which the sweep solved first (it is this module's parent), carrying its anchor's rest.
	const unsigned int firstPlug = static_cast<unsigned int>(interiorCount + bindCount);
	const MMatrix* mAnchorRest = context.anchorRest.data();
	MMatrix mAnchorWorld[RibbonSolve::AnchorCount];
	for (unsigned int k = 0; k < RibbonSolve::AnchorCount; ++k)
	{
		mAnchorWorld[k] = mAnchorRest[k] * input.parentDelta[firstPlug + k];
	}
	MMatrix mFrames[RibbonSolve::AnchorCount];
	RibbonSolve::anchorFrames(mAnchorRest, mAnchorWorld, context.startAnchorRotation, context.endAnchorRotation, mFrames);

	// 2. The setup, rebuilt from the stored rests the way the plan step made it.
	RibbonMath::Mat4 anchorRest[RibbonSolve::AnchorCount];
	for (unsigned int k = 0; k < RibbonSolve::AnchorCount; ++k)
	{
		anchorRest[k] = MathUtils::toMat4(mAnchorRest[k]);
	}
	RibbonMath::Mat4 bindRest[RibbonMath::MaxBindJoints];
	for (int i = 0; i < bindCount; ++i)
	{
		bindRest[i] = MathUtils::toMat4(context.controlRestWorld[static_cast<std::size_t>(interiorCount + i)]);
	}
	const double upperLength = (MathUtils::translationOf(mAnchorRest[1]) - MathUtils::translationOf(mAnchorRest[0])).length();
	const double lowerLength = (MathUtils::translationOf(mAnchorRest[2]) - MathUtils::translationOf(mAnchorRest[1])).length();
	const std::vector<double> controlU = ControlPlacement::limbControlParameters(upperLength, lowerLength, curveCount);
	RibbonMath::Mat4 controlRest[RibbonMath::MaxControls];
	RibbonMath::Setup setup;
	const char* pReason = "";
	if (!RibbonMath::buildAnchoredSetup(anchorRest, static_cast<int>(RibbonSolve::AnchorCount), controlU.data(), curveCount, bindRest, bindCount, setup, controlRest, pReason)
		|| !RibbonSolve::setAnchors(setup, mAnchorRest))
	{
		result.reason = pReason[0] != '\0' ? pReason : "the ribbon's anchors sit on each other, so it has no curve";
		return result;
	}
	RibbonSolve::applyStoredShares(context.fkRollShare, setup);

	const std::vector<ModuleInverseChannel> wanted = inverseChannels();
	double channelValues[RibbonChannel::Count];
	for (unsigned int k = 0; k < RibbonChannel::Count; ++k)
	{
		channelValues[k] = k < input.channels.size() ? input.channels[k] : wanted[k].defaultValue;
	}
	const RibbonMath::Params params = paramsFromChannels(channelValues);

	// By the curve's controls (the ends with no local), then the bind joints, as compute() lays them out.
	MMatrix mRest[MaxSlots];
	MMatrix mLocal[MaxSlots];
	mRest[0] = mAnchorRest[0];
	mRest[curveCount - 1] = mAnchorRest[RibbonSolve::AnchorCount - 1];
	for (int j = 0; j < interiorCount; ++j)
	{
		mRest[j + 1] = context.controlRestWorld[static_cast<std::size_t>(j)];
		mLocal[j + 1] = input.fallbackLocal[static_cast<std::size_t>(j)];
	}
	for (int i = 0; i < bindCount; ++i)
	{
		mRest[curveCount + i] = context.controlRestWorld[static_cast<std::size_t>(interiorCount + i)];
		mLocal[curveCount + i] = input.fallbackLocal[static_cast<std::size_t>(interiorCount + i)];
	}

	// 3. The interior ik controls have no joint, so they keep the fallback (the sidecar's value, else the rest). The tweakers carry what is
	// left, so a bind joint that is present comes back exactly whatever the ik controls hold.
	RibbonSolve::Result solve;
	RibbonSolve::evaluate(setup, params, mRest, mLocal, MMatrix::identity, MMatrix::identity, solve, mFrames);
	bool hasEveryBindJoint = true;
	for (int i = 0; i < bindCount; ++i)
	{
		const std::size_t slot = static_cast<std::size_t>(interiorCount + i);
		if (slot < input.jointWorld.size() && input.jointWorld[slot].has_value())
		{
			result.controlLocal[slot] = *input.jointWorld[slot] * solve.mControlOPM[curveCount + i].inverse();
			mLocal[curveCount + i] = *result.controlLocal[slot];
		}
		else
		{
			hasEveryBindJoint = false;
		}
	}

	// 4. The sockets, with the tweakers as they now are.
	RibbonSolve::evaluate(setup, params, mRest, mLocal, MMatrix::identity, MMatrix::identity, solve, mFrames);
	for (int j = 0; j < interiorCount; ++j)
	{
		result.socketWorld[static_cast<std::size_t>(j)] = solve.mWorld[j + 1];
	}
	for (int i = 0; i < bindCount; ++i)
	{
		result.socketWorld[static_cast<std::size_t>(interiorCount + i)] = solve.mWorld[curveCount + i];
	}

	result.solved = hasEveryBindJoint;
	if (!result.solved)
	{
		result.reason = "a bind joint is missing from the source, so its tweaker comes from the sidecar or rest";
	}
	return result;
}

RigModuleBuildResult RibbonModuleNode::createModule(
	const MString& moduleName,
	const Side& side,
	const ShapeType& shapeType,
	const std::vector<RibbonSlotInput>& controls,
	const std::vector<RibbonSlotInput>& joints,
	const std::vector<RibbonSlotInput>& fkControls,
	const RibbonMath::Setup& setup,
	const std::array<double, RibbonChannel::Count>& channelDefaults,
	MObject& oRigRoot,
	MDagModifier& dagMod)
{
	RigModuleBuildResult result;
	MStatus status;

	// 1. Module node
	MObject oModule = dagMod.MDGModifier::createNode(RibbonModuleNode::id);
	dagMod.renameNode(oModule, moduleName);
	dagMod.newPlugValueShort(MPlug(oModule, RibbonModuleNode::aSide), getShortFromSide(side));
	result.moduleNode = oModule;

	const unsigned int controlCount = static_cast<unsigned int>(controls.size());
	const unsigned int firstFk = controlCount + static_cast<unsigned int>(joints.size());
	const bool hasFkChain = !fkControls.empty();
	std::vector<RibbonSlotInput> slots = controls;
	slots.insert(slots.end(), joints.begin(), joints.end());
	slots.insert(slots.end(), fkControls.begin(), fkControls.end());
	std::vector<MObject> oControls(slots.size());

	// 2. Per slot: wire the joint, create and wire the control. The tweakers drive the bind joints.
	for (unsigned int slot = 0; slot < slots.size(); ++slot)
	{
		const RibbonSlotInput& input = slots[slot];

		// An ik control has no joint (D57): its jointOPM and jointRestWorld elements are never made, which the module's arrays
		// and the inverse allow for.
		MString controlBaseName(input.baseName.c_str());
		if (!input.oJoint.isNull())
		{
			status = RibbonModuleNode::wireJoint(oModule, input.oJoint, slot, input.mRestWorld, dagMod);
			if (!status)
			{
				result.status = status;
				return result;
			}
			controlBaseName = MFnDependencyNode(input.oJoint).name();
		}

		oControls[slot] = RigControlNode::createRigControl(dagMod, controlBaseName, shapeType, side, input.controlMarkup, MEulerRotation::kXYZ);
		// All controls hang from the start control, except the end, which hangs from its own parent. Visibility and the outliner
		// follow the start, and nothing transforms by it (inheritsTransform is off). An fk chain's end is on the chain, so it hangs
		// from the start like the rest, and each fk control hangs from the one before, as FkChain's do.
		if (slot > firstFk)
		{
			dagMod.reparentNode(oControls[slot], oControls[slot - 1]);
		}
		else if (slot != 0 && (slot != controlCount - 1 || hasFkChain))
		{
			dagMod.reparentNode(oControls[slot], oControls[0]);
		}
		status = wireControl(oModule, oControls[slot], slot, input.mRestWorld, dagMod);
		if (!status)
		{
			result.status = status;
			return result;
		}

		RigModuleSocketInfo socketInfo;
		socketInfo.oTransform = oControls[slot];
		socketInfo.mRestWorld = input.mRestWorld;
		socketInfo.outputIndex = slot;
		std::vector<std::string> socketNames;
		if (slot >= firstFk)
		{
			// An fk control has no joint: it shapes the curve the ik controls float on. Its socket (fk_<n>, D78) carries its own
			// transform, so a child plug on it parents under the visible control like an FkChain's.
			socketNames.push_back(std::string(PlugAndSocketNames::Fk) + std::to_string(slot - firstFk));
		}
		else if (slot >= controlCount)
		{
			// A tweaker is the residual layer: not drawn, still keyable (section 3.2). Its socket (bind_<n>, D78) is its world, which is
			// the bind joint's, and carries its transform: it is hidden through `shown`, not visibility, so a child plug's control can be
			// parented under it without being hidden too.
			dagMod.newPlugValueBool(MPlug(oControls[slot], RigControlNode::aShown), false);
			socketNames.push_back(std::string(PlugAndSocketNames::Bind) + std::to_string(slot - controlCount));
		}
		else
		{
			socketNames = FkChainModuleNode::socketNamesAt(slot, controlCount);
		}
		for (const std::string& socketName : socketNames)
		{
			result.parentSockets[socketName] = socketInfo;
		}
		DagPlugAndSocketUtils::setParentSocketNames(oModule, slot, socketNames, dagMod);

		if (slot == 0 || (slot == controlCount - 1 && !hasFkChain))
		{
			const char* plugName = slot == 0 ? PlugAndSocketNames::Start : PlugAndSocketNames::End;
			result.childPlugs[plugName] = RigModuleChildPlugInfo{ oControls[slot], slot, "", slot == 0 ? "" : PlugAndSocketNames::Start };
			DagPlugAndSocketUtils::setChildPlugName(oModule, slot, plugName, dagMod);
		}
	}

	// 3. The rest data the evaluation reads.
	RibbonSolve::writeRestData(oModule, restAttrs(), setup, dagMod);
	dagMod.newPlugValueInt(MPlug(oModule, RibbonModuleNode::aFkControlCount), static_cast<int>(fkControls.size()));

	// 4. The channels, on the first control. The controls must exist before an attribute can be added to them, and the
	// attributes before they can be wired, so the build runs the modifier here, as TwoBoneIk does.
	status = dagMod.doIt();
	if (!status)
	{
		result.status = status;
		return result;
	}
	const std::array<ChannelSpec, RibbonChannel::Count>& specs = channelSpecs();
	for (unsigned int k = 0; k < RibbonChannel::Count; ++k)
	{
		if (!RibbonSolve::hasChannel(k, false))
		{
			continue;
		}
		const ChannelSpec& spec = specs[k];
		status = spec.isAngle
			? RigPoseScene::addAngleChannel(oControls[0], spec.name, spec.negate, dagMod)
			: RigPoseScene::addDoubleChannel(oControls[0], spec.name, channelDefaults[k], spec.min, spec.max, spec.negate, dagMod);
		CHECK_MSTATUS_AND_RETURN(status, result);
	}
	status = dagMod.doIt();
	if (!status)
	{
		result.status = status;
		return result;
	}
	for (unsigned int k = 0; k < RibbonChannel::Count; ++k)
	{
		if (!RibbonSolve::hasChannel(k, false))
		{
			continue;
		}
		MPlug pControlAttr = MFnDependencyNode(oControls[0]).findPlug(specs[k].name, false, &status);
		CHECK_MSTATUS_AND_RETURN(status, result);
		status = dagMod.connect(pControlAttr, MPlug(oModule, RibbonModuleNode::aChannels[k]));
		CHECK_MSTATUS_AND_RETURN(status, result);
	}

	// 5. Network
	if (!oRigRoot.isNull())
	{
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dagMod, oModule, aRigRoot);
	}

	result.dagParentCallback = RibbonModuleNode::reparentChildPlugToSocket;
	result.status = MS::kSuccess;
	return result;
}

bool RibbonModuleNode::planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan)
{
	RibbonArgs ribbonArgs;
	if (!readModuleArgs(args, { "joints" }, ctx, ribbonArgs, { "controlCount", "controlName", RigAttr::StartAimBlend, RigAttr::EndAimBlend, "fkStyle", "fkControlCount", "twistRamp",
		"anchorJoints", RigAttr::StartAnchorRotation, RigAttr::EndAnchorRotation, RigAttr::BendSoftness }))
	{
		return false;
	}
	if (args.contains("anchorJoints"))
	{
		return planAnchored(ribbonArgs, args, ctx, outPlan);
	}

	const std::string argsPath = ctx.where + "." + TemplateKey::ModuleArgs;
	bool valid = true;
	for (const char* key : { RigAttr::StartAnchorRotation, RigAttr::EndAnchorRotation, RigAttr::BendSoftness })
	{
		if (args.contains(key))
		{
			ctx.report.addError(argsPath + "." + key, "is for an anchored ribbon, so it needs anchorJoints as well");
			valid = false;
		}
	}
	// The channels clamp to their range, so a value outside it would build a rest the channel can't hold.
	for (const auto& [key, value] : { std::pair<const char*, double>{ RigAttr::StartAimBlend, ribbonArgs.startAimBlend },
		{ RigAttr::EndAimBlend, ribbonArgs.endAimBlend } })
	{
		if (value < 0.0 || value > 1.0)
		{
			ctx.report.addError(argsPath + "." + key, "must be between 0 and 1, got " + std::to_string(value));
			valid = false;
		}
	}
	if (ribbonArgs.controlCount < 2 || ribbonArgs.controlCount > RibbonMath::MaxControls)
	{
		ctx.report.addError(argsPath + ".controlCount", "a ribbon has between 2 and " + std::to_string(RibbonMath::MaxControls)
			+ " ik controls (a start, an end and the interior ones), got " + std::to_string(ribbonArgs.controlCount));
		valid = false;
	}
	// fkControlCount is the fk chain's own size, so it means nothing without the chain. Unset (-1) it follows controlCount.
	if (ribbonArgs.fkControlCount != -1 && !ribbonArgs.fkStyle)
	{
		ctx.report.addError(argsPath + ".fkControlCount", "sizes the fk chain, so it needs fkStyle set to true as well");
		valid = false;
	}
	if (ribbonArgs.fkStyle && ribbonArgs.fkControlCount != -1 && (ribbonArgs.fkControlCount < 2 || ribbonArgs.fkControlCount > RibbonMath::MaxControls))
	{
		ctx.report.addError(argsPath + ".fkControlCount", "an fk chain has between 2 and " + std::to_string(RibbonMath::MaxControls)
			+ " controls, got " + std::to_string(ribbonArgs.fkControlCount));
		valid = false;
	}
	// twistRamp shares out the fk chain's roll, so it means nothing without the chain.
	if (!ribbonArgs.twistRamp.empty())
	{
		std::string rampReason;
		if (!ribbonArgs.fkStyle)
		{
			ctx.report.addError(argsPath + ".twistRamp", "shares out the fk chain's roll, so it needs fkStyle set to true, or anchorJoints, as well");
			valid = false;
		}
		else if (!RibbonMath::validateTwistRamp(ribbonArgs.twistRamp, rampReason))
		{
			ctx.report.addError(argsPath + ".twistRamp", rampReason);
			valid = false;
		}
	}
	if (ribbonArgs.joints.size() < 2 || ribbonArgs.joints.size() > static_cast<std::size_t>(RibbonMath::MaxBindJoints))
	{
		ctx.report.addError(argsPath + ".joints", "a ribbon has between 2 and " + std::to_string(RibbonMath::MaxBindJoints)
			+ " bind joints, got " + std::to_string(ribbonArgs.joints.size()));
		valid = false;
	}
	if (!valid)
	{
		return false;
	}

	// Claim every joint even after a failure, so the report lists every bad selector at once. claimJoint rejects a joint
	// used twice.
	std::vector<RibbonSlotInput> joints;
	for (const JointSelector& selector : ribbonArgs.joints)
	{
		RibbonSlotInput slot;
		if (!RigModuleNodeBase::claimJoint(ctx, selector, slot.oJoint, slot.mRestWorld))
		{
			valid = false;
			continue;
		}
		slot.controlMarkup = makeControlMarkup(selector.side, selector.usage);
		joints.push_back(slot);
	}
	if (!valid)
	{
		return false;
	}

	// The ik controls come from the bind joints (D57): the axes must run down the chain, in one direction.
	std::vector<RibbonMath::Mat4> bindRest;
	std::vector<std::string> jointNames;
	for (std::size_t i = 0; i < joints.size(); ++i)
	{
		bindRest.push_back(MathUtils::toMat4(joints[i].mRestWorld));
		jointNames.push_back(formatJointSelector(ribbonArgs.joints[i]));
	}
	const ControlPlacement::ChainCheck chainCheck = ControlPlacement::checkChain(bindRest);
	if (chainCheck.problem != ControlPlacement::ChainProblem::None)
	{
		ctx.report.addError(argsPath + ".joints", ControlPlacement::describeChainProblem(chainCheck, jointNames));
		return false;
	}
	const std::vector<RibbonMath::Mat4> controlFrames = ControlPlacement::placeAlongChain(bindRest, ribbonArgs.controlCount);

	// The fk chain (D59) is placed the same way, with its own count.
	const int fkCount = ribbonArgs.fkStyle ? (ribbonArgs.fkControlCount == -1 ? ribbonArgs.controlCount : ribbonArgs.fkControlCount) : 0;
	std::vector<RibbonMath::Mat4> fkFrames;
	if (fkCount > 0)
	{
		fkFrames = ControlPlacement::placeAlongChain(bindRest, fkCount);
	}

	RibbonMath::Mat4 controlRest[RibbonMath::MaxControls];
	RibbonMath::Mat4 bindRestArray[RibbonMath::MaxBindJoints];
	RibbonMath::Mat4 fkRest[RibbonMath::MaxControls];
	std::copy(controlFrames.begin(), controlFrames.end(), controlRest);
	std::copy(bindRest.begin(), bindRest.end(), bindRestArray);
	std::copy(fkFrames.begin(), fkFrames.end(), fkRest);
	RibbonMath::Setup setup;
	const char* pReason = "";
	if (!RibbonMath::buildSetup(controlRest, ribbonArgs.controlCount, bindRestArray, static_cast<int>(joints.size()), fkRest, fkCount, setup, pReason))
	{
		ctx.report.addError(argsPath + ".joints", pReason);
		return false;
	}
	RibbonMath::applyTwistRamp(setup, ribbonArgs.twistRamp);

	// Names (D57): <base>IK_<nn> for the ik controls, <base>Tweak_<nn> for the tweakers (nn is the bind joint's index). The base
	// is controlName, else the first bind joint's usage without its trailing _NN. A usage another module already uses is
	// reported by the template's one-usage-per-rig check (D25).
	const std::string base = ribbonArgs.controlName.empty() ? StringUtils::stripNumberSuffix(ribbonArgs.joints.front().usage) : ribbonArgs.controlName;
	if (!NamingConvention::isValidBase(base))
	{
		ctx.report.addError(argsPath + ".controlName", "the ik and tweak controls are named from '" + base + "', which is not camelCase (a lowercase "
			"letter, then letters and digits). Set controlName to a camelCase name");
		return false;
	}
	const std::string& firstSide = ribbonArgs.joints.front().side;
	std::vector<RibbonSlotInput> controls;
	for (int j = 0; j < ribbonArgs.controlCount; ++j)
	{
		RibbonSlotInput slot;
		slot.mRestWorld = MathUtils::toMMatrix(controlFrames[static_cast<std::size_t>(j)]);
		slot.baseName = StringUtils::numberedName(base + "IK", j);
		slot.controlMarkup = makeControlMarkup(firstSide, slot.baseName);
		controls.push_back(slot);
	}
	for (std::size_t i = 0; i < joints.size(); ++i)
	{
		joints[i].controlMarkup = makeControlMarkup(ribbonArgs.joints[i].side, StringUtils::numberedName(base + "Tweak", static_cast<int>(i)));
	}
	std::vector<RibbonSlotInput> fkControls;
	for (int k = 0; k < fkCount; ++k)
	{
		RibbonSlotInput slot;
		slot.mRestWorld = MathUtils::toMMatrix(fkFrames[static_cast<std::size_t>(k)]);
		slot.baseName = StringUtils::numberedName(base + "FK", k);
		slot.controlMarkup = makeControlMarkup(firstSide, slot.baseName);
		fkControls.push_back(slot);
	}

	outPlan.moduleInterface = makeRibbonInterface(static_cast<unsigned int>(controls.size()), static_cast<unsigned int>(joints.size()), static_cast<unsigned int>(fkCount));
	for (const RibbonSlotInput& slot : controls)
	{
		outPlan.moduleInterface.controls.push_back(slot.controlMarkup);
	}
	for (const RibbonSlotInput& slot : joints)
	{
		outPlan.moduleInterface.controls.push_back(slot.controlMarkup);
	}
	for (const RibbonSlotInput& slot : fkControls)
	{
		outPlan.moduleInterface.controls.push_back(slot.controlMarkup);
	}

	const MString moduleName = ctx.moduleData.name.c_str();
	const Side side = getSideFromString(ctx.moduleData.side);
	const ShapeType shapeType = getShapeTypeFromString(ctx.moduleData.shapeType);
	std::array<double, RibbonChannel::Count> channelDefaults;
	for (unsigned int k = 0; k < RibbonChannel::Count; ++k)
	{
		channelDefaults[k] = channelSpecs()[k].defaultValue;
	}
	channelDefaults[RibbonChannel::StartAimBlend] = ribbonArgs.startAimBlend;
	channelDefaults[RibbonChannel::EndAimBlend] = ribbonArgs.endAimBlend;
	outPlan.build = [moduleName, side, shapeType, controls, joints, fkControls, setup, channelDefaults](MObject& oRigRoot, MDagModifier& dagMod)
	{
		return RibbonModuleNode::createModule(moduleName, side, shapeType, controls, joints, fkControls, setup, channelDefaults, oRigRoot, dagMod);
	};
	return true;
}

// The anchored form (D80, design/limb-split.md section 3.2): Limb's plan step for its ribbon, moved, with the three anchors read from
// anchorJoints instead of a two-bone limb's joints.
bool RibbonModuleNode::planAnchored(const RibbonArgs& ribbonArgs, const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan)
{
	const std::string argsPath = ctx.where + "." + TemplateKey::ModuleArgs;
	bool valid = true;
	for (const char* key : { "fkStyle", "fkControlCount", RigAttr::StartAimBlend, RigAttr::EndAimBlend })
	{
		if (args.contains(key))
		{
			ctx.report.addError(argsPath + "." + key, "is not used by an anchored ribbon: its anchors give the curve and its ends' frames");
			valid = false;
		}
	}
	if (ribbonArgs.anchorJoints.size() != RibbonSolve::AnchorCount)
	{
		ctx.report.addError(argsPath + ".anchorJoints", "an anchored ribbon has 3 anchor joints (start, mid and end), got " + std::to_string(ribbonArgs.anchorJoints.size()));
		valid = false;
	}
	// The detail curve the bind joints ride is drawn through the ik controls, so with no control at the mid anchor it would cut the corner:
	// the first interior control is the mid's, and there is always one.
	const int maxInterior = RibbonMath::MaxControls - 2;
	if (ribbonArgs.controlCount < 1 || ribbonArgs.controlCount > maxInterior)
	{
		ctx.report.addError(argsPath + ".controlCount", "an anchored ribbon has between 1 and " + std::to_string(maxInterior)
			+ " interior ik controls (the first at the mid anchor; its ends are the anchors), got " + std::to_string(ribbonArgs.controlCount));
		valid = false;
	}
	RibbonAnchors anchors;
	if (ribbonArgs.startAnchorRotation == "swing")
	{
		anchors.startRotation = AnchorRotation::Swing;
	}
	else if (ribbonArgs.startAnchorRotation != "full")
	{
		ctx.report.addError(argsPath + "." + RigAttr::StartAnchorRotation, "must be \"full\" or \"swing\", got \"" + ribbonArgs.startAnchorRotation + "\"");
		valid = false;
	}
	if (ribbonArgs.endAnchorRotation == "twist")
	{
		anchors.endRotation = AnchorRotation::Twist;
	}
	else if (ribbonArgs.endAnchorRotation != "full")
	{
		ctx.report.addError(argsPath + "." + RigAttr::EndAnchorRotation, "must be \"full\" or \"twist\", got \"" + ribbonArgs.endAnchorRotation + "\"");
		valid = false;
	}
	if (ribbonArgs.bendSoftness < 0.0 || ribbonArgs.bendSoftness > 1.0)
	{
		ctx.report.addError(argsPath + "." + RigAttr::BendSoftness, "must be between 0 and 1, got " + std::to_string(ribbonArgs.bendSoftness));
		valid = false;
	}
	std::string rampReason;
	if (!ribbonArgs.twistRamp.empty() && !RibbonMath::validateTwistRamp(ribbonArgs.twistRamp, rampReason))
	{
		ctx.report.addError(argsPath + ".twistRamp", rampReason);
		valid = false;
	}
	if (ribbonArgs.joints.size() < 5 || ribbonArgs.joints.size() > static_cast<std::size_t>(RibbonMath::MaxBindJoints))
	{
		ctx.report.addError(argsPath + ".joints", "an anchored ribbon has between 5 and " + std::to_string(RibbonMath::MaxBindJoints)
			+ " bind joints (one at each anchor and the ones between), got " + std::to_string(ribbonArgs.joints.size()));
		valid = false;
	}
	if (!valid)
	{
		return false;
	}

	// The anchors are read, not claimed: the module that owns their sockets drives them (a TwoBoneIk's joints).
	std::vector<RibbonMath::Mat4> anchorRest;
	std::vector<std::string> anchorNames;
	for (unsigned int k = 0; k < RibbonSolve::AnchorCount; ++k)
	{
		const JointSelector& selector = ribbonArgs.anchorJoints[k];
		anchors.jointNames[k] = formatJointSelector(selector);
		if (!RigModuleNodeBase::findJointRest(ctx, selector, anchors.mRest[k]))
		{
			valid = false;
			continue;
		}
		anchorRest.push_back(MathUtils::toMat4(anchors.mRest[k]));
		anchorNames.push_back(anchors.jointNames[k]);
	}
	// Claim every bind joint even after a failure, so the report lists every bad selector at once. A bind joint may have child joints: the
	// tweaker drives it by its offset parent matrix and inheritsTransform is off, so a child is not dragged along.
	std::vector<RibbonSlotInput> joints;
	for (const JointSelector& selector : ribbonArgs.joints)
	{
		RibbonSlotInput slot;
		if (!RigModuleNodeBase::claimJoint(ctx, selector, slot.oJoint, slot.mRestWorld))
		{
			valid = false;
			continue;
		}
		joints.push_back(slot);
	}
	if (!valid)
	{
		return false;
	}

	// The curve follows the anchors' X axes, so they must run down the chain, one way. The end anchor may point past it (an ankle at the
	// ball of the foot), so it gets the wider limit.
	ControlPlacement::ChainCheck chainCheck = ControlPlacement::checkChain(anchorRest, ControlPlacement::MaxLimbEndAxisAngleDegrees);
	if (chainCheck.problem != ControlPlacement::ChainProblem::None)
	{
		ctx.report.addError(argsPath + ".anchorJoints", ControlPlacement::describeChainProblem(chainCheck, anchorNames));
		return false;
	}
	std::vector<RibbonMath::Mat4> bindRest;
	std::vector<std::string> bindNames;
	for (std::size_t i = 0; i < joints.size(); ++i)
	{
		bindRest.push_back(MathUtils::toMat4(joints[i].mRestWorld));
		bindNames.push_back(formatJointSelector(ribbonArgs.joints[i]));
	}
	chainCheck = ControlPlacement::checkChain(bindRest);
	if (chainCheck.problem != ControlPlacement::ChainProblem::None)
	{
		ctx.report.addError(argsPath + ".joints", ControlPlacement::describeChainProblem(chainCheck, bindNames));
		return false;
	}

	// Where the bind joints sit: one at the start anchor, one at the end and exactly one at the mid. Stacked neighbours were rejected above.
	const auto distanceBetween = [](const MMatrix& a, const MMatrix& b)
	{
		return (MathUtils::translationOf(a) - MathUtils::translationOf(b)).length();
	};
	const double upperLength = distanceBetween(anchors.mRest[0], anchors.mRest[1]);
	const double lowerLength = distanceBetween(anchors.mRest[1], anchors.mRest[2]);
	anchors.tolerance = kBindTolerance * (upperLength + lowerLength);
	if (distanceBetween(joints.front().mRestWorld, anchors.mRest[0]) > anchors.tolerance)
	{
		ctx.report.addError(argsPath + ".joints", "the first bind joint '" + bindNames.front() + "' must sit at the start anchor '" + anchors.jointNames[0] + "'");
		valid = false;
	}
	if (distanceBetween(joints.back().mRestWorld, anchors.mRest[2]) > anchors.tolerance)
	{
		ctx.report.addError(argsPath + ".joints", "the last bind joint '" + bindNames.back() + "' must sit at the end anchor '" + anchors.jointNames[2] + "'");
		valid = false;
	}
	int atMid = 0;
	for (const RibbonSlotInput& bind : joints)
	{
		atMid += distanceBetween(bind.mRestWorld, anchors.mRest[1]) <= anchors.tolerance ? 1 : 0;
	}
	if (atMid != 1)
	{
		ctx.report.addError(argsPath + ".joints", (atMid == 0 ? std::string("no bind joint sits") : std::to_string(atMid) + " bind joints sit")
			+ " at the mid anchor '" + anchors.jointNames[1] + "', and the corner needs exactly one to carry its bend");
		valid = false;
	}
	if (!valid)
	{
		return false;
	}

	// The curve's controls sit on the curve through the anchors at parameters from the segment lengths: its two ends, which have no
	// control, and the interior ones, the first at the mid anchor.
	const int curveCount = ribbonArgs.controlCount + 2;
	const std::vector<double> controlU = ControlPlacement::limbControlParameters(upperLength, lowerLength, curveCount);
	RibbonMath::Mat4 anchorRestArray[RibbonSolve::AnchorCount];
	std::copy(anchorRest.begin(), anchorRest.end(), anchorRestArray);
	RibbonMath::Mat4 controlRest[RibbonMath::MaxControls];
	RibbonMath::Mat4 bindRestArray[RibbonMath::MaxBindJoints];
	std::copy(bindRest.begin(), bindRest.end(), bindRestArray);
	RibbonMath::Setup setup;
	const char* pReason = "";
	if (!RibbonMath::buildAnchoredSetup(anchorRestArray, static_cast<int>(RibbonSolve::AnchorCount), controlU.data(), curveCount, bindRestArray,
		static_cast<int>(joints.size()), setup, controlRest, pReason))
	{
		ctx.report.addError(argsPath + ".joints", pReason);
		return false;
	}
	RibbonMath::applyTwistRamp(setup, ribbonArgs.twistRamp);

	// Names (D57): <base>IK_<nn> for the interior ik controls, numbered by their place on the curve from _01 (the start anchor is _00), and
	// <base>Tweak_<nn> for the tweakers (nn is the bind joint's index).
	const std::string base = ribbonArgs.controlName.empty() ? StringUtils::stripNumberSuffix(ribbonArgs.joints.front().usage) : ribbonArgs.controlName;
	if (!NamingConvention::isValidBase(base))
	{
		ctx.report.addError(argsPath + ".controlName", "the ik and tweak controls are named from '" + base + "', which is not camelCase (a lowercase "
			"letter, then letters and digits). Set controlName to a camelCase name");
		return false;
	}
	const std::string& firstSide = ribbonArgs.joints.front().side;
	std::vector<RibbonSlotInput> controls;
	for (int j = 0; j < ribbonArgs.controlCount; ++j)
	{
		RibbonSlotInput slot;
		slot.mRestWorld = MathUtils::toMMatrix(controlRest[j + 1]);
		slot.baseName = StringUtils::numberedName(base + "IK", j + 1);
		slot.controlMarkup = makeControlMarkup(firstSide, slot.baseName);
		controls.push_back(slot);
	}
	for (std::size_t i = 0; i < joints.size(); ++i)
	{
		joints[i].controlMarkup = makeControlMarkup(ribbonArgs.joints[i].side, StringUtils::numberedName(base + "Tweak", static_cast<int>(i)));
	}

	// The anchors and settings have no control and no default parent. A constraint would hand the anchor a motion from another rest, so
	// none is allowed on them.
	ModuleInterface& moduleInterface = outPlan.moduleInterface;
	for (const char* plugName : { PlugAndSocketNames::Start, PlugAndSocketNames::Mid, PlugAndSocketNames::End })
	{
		moduleInterface.childPlugs.push_back(ModulePlugInfo{ plugName, false, false, "", "", false, {}, true, false });
	}
	moduleInterface.childPlugs.push_back(ModulePlugInfo{ PlugAndSocketNames::Settings, false, false, "", "", false, {}, true, true });
	for (std::size_t j = 0; j < controls.size(); ++j)
	{
		moduleInterface.sockets.push_back(std::string(PlugAndSocketNames::Index) + std::to_string(j));
	}
	for (std::size_t i = 0; i < joints.size(); ++i)
	{
		moduleInterface.sockets.push_back(std::string(PlugAndSocketNames::Bind) + std::to_string(i));
	}
	for (const RibbonSlotInput& slot : controls)
	{
		moduleInterface.controls.push_back(slot.controlMarkup);
	}
	for (const RibbonSlotInput& slot : joints)
	{
		moduleInterface.controls.push_back(slot.controlMarkup);
	}

	const MString moduleName = ctx.moduleData.name.c_str();
	const Side side = getSideFromString(ctx.moduleData.side);
	const ShapeType shapeType = getShapeTypeFromString(ctx.moduleData.shapeType);
	std::array<double, RibbonChannel::Count> channelDefaults;
	for (unsigned int k = 0; k < RibbonChannel::Count; ++k)
	{
		channelDefaults[k] = channelSpecs()[k].defaultValue;
	}
	channelDefaults[RibbonChannel::BendSoftness] = ribbonArgs.bendSoftness;
	outPlan.build = [moduleName, side, shapeType, controls, joints, setup, anchors, channelDefaults](MObject& oRigRoot, MDagModifier& dagMod)
	{
		return RibbonModuleNode::createAnchoredModule(moduleName, side, shapeType, controls, joints, setup, anchors, channelDefaults, oRigRoot, dagMod);
	};
	return true;
}

RigModuleBuildResult RibbonModuleNode::createAnchoredModule(
	const MString& moduleName,
	const Side& side,
	const ShapeType& shapeType,
	const std::vector<RibbonSlotInput>& controls,
	const std::vector<RibbonSlotInput>& joints,
	const RibbonMath::Setup& setup,
	const RibbonAnchors& anchors,
	const std::array<double, RibbonChannel::Count>& channelDefaults,
	MObject& oRigRoot,
	MDagModifier& dagMod)
{
	RigModuleBuildResult result;
	MStatus status;

	// 1. Module node
	MObject oModule = dagMod.MDGModifier::createNode(RibbonModuleNode::id);
	dagMod.renameNode(oModule, moduleName);
	dagMod.newPlugValueShort(MPlug(oModule, RibbonModuleNode::aSide), getShortFromSide(side));
	result.moduleNode = oModule;

	// 2. The interior ik controls, which have no joint (D57): created loose, and put under the start plug's DAG parent in pass 2 (the
	// callback below). Each has a socket, index_<j>, which carries its transform.
	const unsigned int controlCount = static_cast<unsigned int>(controls.size());
	std::vector<MObject> oControls;
	for (unsigned int j = 0; j < controlCount; ++j)
	{
		const RibbonSlotInput& input = controls[j];
		const MObject oControl = RigControlNode::createRigControl(dagMod, MString(input.baseName.c_str()), shapeType, side, input.controlMarkup, MEulerRotation::kXYZ);
		status = wireControl(oModule, oControl, j, input.mRestWorld, dagMod);
		if (!status)
		{
			result.status = status;
			return result;
		}
		oControls.push_back(oControl);
		RigModuleSocketInfo socket;
		socket.oTransform = oControl;
		socket.mRestWorld = input.mRestWorld;
		socket.outputIndex = j;
		const std::string socketName = std::string(PlugAndSocketNames::Index) + std::to_string(j);
		result.parentSockets[socketName] = socket;
		DagPlugAndSocketUtils::setParentSocketNames(oModule, j, { socketName }, dagMod);
	}

	// 3. The tweakers, one per bind joint, hidden, leaf children of the first ik control (the plan step makes at least one). Each has a
	// socket, bind_<i>, which carries its transform (D78).
	for (unsigned int i = 0; i < joints.size(); ++i)
	{
		const RibbonSlotInput& input = joints[i];
		const unsigned int slot = controlCount + i;
		status = RibbonModuleNode::wireJoint(oModule, input.oJoint, slot, input.mRestWorld, dagMod);
		if (!status)
		{
			result.status = status;
			return result;
		}
		const MObject oControl = RigControlNode::createRigControl(dagMod, MFnDependencyNode(input.oJoint).name(), shapeType, side, input.controlMarkup, MEulerRotation::kXYZ);
		dagMod.reparentNode(oControl, oControls[0]);
		status = wireControl(oModule, oControl, slot, input.mRestWorld, dagMod);
		if (!status)
		{
			result.status = status;
			return result;
		}
		dagMod.newPlugValueBool(MPlug(oControl, RigControlNode::aShown), false);
		RigModuleSocketInfo socket;
		socket.oTransform = oControl;
		socket.mRestWorld = input.mRestWorld;
		socket.outputIndex = slot;
		const std::string socketName = std::string(PlugAndSocketNames::Bind) + std::to_string(i);
		result.parentSockets[socketName] = socket;
		DagPlugAndSocketUtils::setParentSocketNames(oModule, slot, { socketName }, dagMod);
	}

	// 4. The plugs, after the slots, with no control: the three anchors, then the settings.
	const unsigned int firstPlug = controlCount + static_cast<unsigned int>(joints.size());
	const char* plugNames[] = { PlugAndSocketNames::Start, PlugAndSocketNames::Mid, PlugAndSocketNames::End, PlugAndSocketNames::Settings };
	for (unsigned int p = 0; p < 4; ++p)
	{
		RigModuleChildPlugInfo plugInfo;
		plugInfo.plugIndex = firstPlug + p;
		result.childPlugs[plugNames[p]] = plugInfo;
		DagPlugAndSocketUtils::setChildPlugName(oModule, firstPlug + p, plugNames[p], dagMod);
	}

	// 5. The rest data the evaluation reads, and the anchors'.
	RibbonSolve::writeRestData(oModule, restAttrs(), setup, dagMod);
	for (unsigned int k = 0; k < RibbonSolve::AnchorCount; ++k)
	{
		MPlug pAnchorRest = MPlug(oModule, RibbonModuleNode::aAnchorRest).elementByLogicalIndex(k);
		DagUtils::setMatrixPlug(pAnchorRest, anchors.mRest[k], dagMod);
	}
	dagMod.newPlugValueShort(MPlug(oModule, RibbonModuleNode::aStartAnchorRotation), anchors.startRotation);
	dagMod.newPlugValueShort(MPlug(oModule, RibbonModuleNode::aEndAnchorRotation), anchors.endRotation);

	// 6. Network
	if (!oRigRoot.isNull())
	{
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dagMod, oModule, aRigRoot);
	}

	// Pass 2. The settings plug names the control the channels go on. Each anchor plug's socket must rest at its anchor joint, or the curve
	// the plan built is not the one the plug hands it (design/limb-split.md section 2.2), and the start plug's DAG parent takes the ik controls.
	result.dagParentCallback = [oControls, anchors, firstPlug, channelDefaults](const RigModuleDagReparentArgs& args)
	{
		const std::string moduleName = MFnDependencyNode(args.oChildModuleNode).name().asChar();
		const unsigned int plug = args.childPlug.plugIndex - firstPlug;
		if (plug == RibbonSolve::AnchorCount)
		{
			if (!args.reparentDag || args.dagSocket.oTransform.isNull())
			{
				MGlobal::displayError(MString("The settings plug of '") + moduleName.c_str() + "' needs a socket of this rig with a control, to hold its channels.");
				return MStatus(MStatus::kFailure);
			}
			return RibbonModuleNode::hostChannels(args.oChildModuleNode, args.dagSocket.oTransform, channelDefaults, args.dagMod);
		}

		const MMatrix& mSocketRest = args.parentSocket.mRestWorld;
		const MMatrix& mAnchorRest = anchors.mRest[plug];
		bool atAnchor = (MathUtils::translationOf(mSocketRest) - MathUtils::translationOf(mAnchorRest)).length() <= anchors.tolerance;
		for (unsigned int r = 0; r < 3 && atAnchor; ++r)
		{
			for (unsigned int c = 0; c < 3; ++c)
			{
				atAnchor = atAnchor && std::fabs(mSocketRest[r][c] - mAnchorRest[r][c]) <= kAxisTolerance;
			}
		}
		if (!atAnchor)
		{
			const char* anchorPlugs[] = { PlugAndSocketNames::Start, PlugAndSocketNames::Mid, PlugAndSocketNames::End };
			MGlobal::displayError(MString("The ") + anchorPlugs[plug] + " plug of '" + moduleName.c_str() + "'"
				+ " takes a socket that does not rest at its anchor joint " + anchors.jointNames[plug].c_str() + ". Its anchorJoints and its parentStructure must name the same joints.");
			return MStatus(MStatus::kFailure);
		}
		MStatus callbackStatus = RibbonModuleNode::reparentChildPlugToSocket(args);
		CHECK_MSTATUS_AND_RETURN_IT(callbackStatus);
		if (plug == 0 && args.reparentDag && !args.dagSocket.oTransform.isNull())
		{
			for (const MObject& oControl : oControls)
			{
				callbackStatus = args.dagMod.reparentNode(oControl, args.dagSocket.oTransform);
				CHECK_MSTATUS_AND_RETURN_IT(callbackStatus);
			}
		}
		return callbackStatus;
	};
	result.status = MS::kSuccess;
	return result;
}

MStatus RibbonModuleNode::hostChannels(const MObject& oModule, const MObject& oHost, const std::array<double, RibbonChannel::Count>& channelDefaults, MDagModifier& dagMod)
{
	MStatus status;
	const MFnDependencyNode fnHost(oHost);
	const std::array<ChannelSpec, RibbonChannel::Count>& specs = channelSpecs();
	for (unsigned int k = 0; k < RibbonChannel::Count; ++k)
	{
		if (!RibbonSolve::hasChannel(k, true))
		{
			continue;
		}
		const ChannelSpec& spec = specs[k];
		if (fnHost.hasAttribute(spec.name))
		{
			MGlobal::displayError(MString("'") + MFnDependencyNode(oModule).name() + "' cannot put its channel '" + spec.name + "' on '" + fnHost.name()
				+ "', which already has one of that name: two modules cannot add the same channel to one control.");
			return MStatus::kFailure;
		}
		status = spec.isAngle
			? RigPoseScene::addAngleChannel(oHost, spec.name, spec.negate, dagMod)
			: RigPoseScene::addDoubleChannel(oHost, spec.name, channelDefaults[k], spec.min, spec.max, spec.negate, dagMod);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	// The attributes must exist before they can be wired, so the build runs the modifier here, as createModule does.
	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);
	for (unsigned int k = 0; k < RibbonChannel::Count; ++k)
	{
		if (!RibbonSolve::hasChannel(k, true))
		{
			continue;
		}
		MPlug pHostAttr = fnHost.findPlug(specs[k].name, false, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = dagMod.connect(pHostAttr, MPlug(oModule, RibbonModuleNode::aChannels[k]));
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	return MStatus::kSuccess;
}
