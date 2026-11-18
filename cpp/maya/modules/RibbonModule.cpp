#include "maya/modules/RibbonModule.h"
#include "core/Constants.h"
#include "core/ControlPlacement.h"
#include "core/JointMarkup.h"
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
MObject RibbonModuleNode::aKnotU;
MObject RibbonModuleNode::aBindU;
MObject RibbonModuleNode::aRestLength;
MObject RibbonModuleNode::aChainSign;
MObject RibbonModuleNode::aRestUp;
MObject RibbonModuleNode::aRestTangent;
MObject RibbonModuleNode::aIkRestOffset;
MObject RibbonModuleNode::aBindRestOffset;

namespace
{
	// A channel on the first control (D56 section 5): its name, rest (the default, D34), range, whether it is an angle and
	// whether a mirror negates it. The ranges keep the evaluation finite: a fraction of the rest length, a handle scale.
	struct ChannelSpec
	{
		const char* name;
		double defaultValue;
		double min;
		double max;
		bool isAngle;
		bool negate;
	};

	// In RibbonChannel order.
	const std::array<ChannelSpec, RibbonChannel::Count>& channelSpecs()
	{
		static const std::array<ChannelSpec, RibbonChannel::Count> specs =
		{
			ChannelSpec{ RigAttr::StartTwist, 0.0, 0.0, 0.0, true, true },
			ChannelSpec{ RigAttr::EndTwist, 0.0, 0.0, 0.0, true, true },
			ChannelSpec{ RigAttr::SineAmplitude, 0.0, -5.0, 5.0, false, false },
			ChannelSpec{ RigAttr::SineFrequency, 1.0, 0.0, 20.0, false, false },
			ChannelSpec{ RigAttr::SinePhase, 0.0, -20.0, 20.0, false, false },
			ChannelSpec{ RigAttr::SineOrient, 0.0, 0.0, 0.0, true, true },
			ChannelSpec{ RigAttr::SineStartFalloff, 0.2, 0.0, 1.0, false, false },
			ChannelSpec{ RigAttr::SineEndFalloff, 0.2, 0.0, 1.0, false, false },
			ChannelSpec{ RigAttr::StartAimBlend, 0.0, 0.0, 1.0, false, false },
			ChannelSpec{ RigAttr::EndAimBlend, 0.0, 0.0, 1.0, false, false },
			ChannelSpec{ RigAttr::BendRoundness, 1.0, 0.0, 2.0, false, false },
			ChannelSpec{ RigAttr::VolumePreserve, 0.0, 0.0, 1.0, false, false },
			ChannelSpec{ RigAttr::ScaleStart, 1.0, 0.0, 1.0, false, false },
			ChannelSpec{ RigAttr::ScaleMid, 1.0, 0.0, 1.0, false, false },
			ChannelSpec{ RigAttr::ScaleEnd, 1.0, 0.0, 1.0, false, false }
		};
		return specs;
	}

	RibbonMath::Params paramsFromChannels(const double* values)
	{
		RibbonMath::Params params;
		params.startTwist = values[RibbonChannel::StartTwist];
		params.endTwist = values[RibbonChannel::EndTwist];
		params.sineAmplitude = values[RibbonChannel::SineAmplitude];
		params.sineFrequency = values[RibbonChannel::SineFrequency];
		params.sinePhase = values[RibbonChannel::SinePhase];
		params.sineOrient = values[RibbonChannel::SineOrient];
		params.sineStartFalloff = values[RibbonChannel::SineStartFalloff];
		params.sineEndFalloff = values[RibbonChannel::SineEndFalloff];
		params.startAimBlend = values[RibbonChannel::StartAimBlend];
		params.endAimBlend = values[RibbonChannel::EndAimBlend];
		params.bendRoundness = values[RibbonChannel::BendRoundness];
		params.volumePreserve = values[RibbonChannel::VolumePreserve];
		params.scaleStart = values[RibbonChannel::ScaleStart];
		params.scaleMid = values[RibbonChannel::ScaleMid];
		params.scaleEnd = values[RibbonChannel::ScaleEnd];
		return params;
	}

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
	ModuleInterface makeRibbonInterface(unsigned int controlCount)
	{
		ModuleInterface moduleInterface;
		moduleInterface.childPlugs.push_back(ModulePlugInfo{ PlugAndSocketNames::Start, true });
		// The end follows the start's parent unless the template drives it from another socket (section 3.4).
		moduleInterface.childPlugs.push_back(ModulePlugInfo{ PlugAndSocketNames::End, true, true, "", PlugAndSocketNames::Start });
		for (unsigned int k = 0; k < controlCount; ++k)
		{
			const std::vector<std::string> names = FkChainModuleNode::socketNamesAt(k, controlCount);
			moduleInterface.sockets.insert(moduleInterface.sockets.end(), names.begin(), names.end());
		}
		return moduleInterface;
	}
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
	aIkControlCount = nAttr.create(RigAttr::IkControlCount, RigAttr::IkControlCount, MFnNumericData::kInt, 0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aIkControlCount));

	for (const auto& [pAttr, name] : { std::pair<MObject*, const char*>{ &aKnotU, RigAttr::KnotU }, { &aBindU, RigAttr::BindU } })
	{
		*pAttr = nAttr.create(name, name, MFnNumericData::kDouble, 0.0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setArray(true));
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(addAttribute(*pAttr));
	}

	aRestLength = nAttr.create(RigAttr::RestLength, RigAttr::RestLength, MFnNumericData::kDouble, 1.0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aRestLength));

	aChainSign = nAttr.create(RigAttr::ChainSign, RigAttr::ChainSign, MFnNumericData::kDouble, 1.0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aChainSign));

	for (const auto& [pAttr, name] : { std::pair<MObject*, const char*>{ &aRestUp, RigAttr::RestUp }, { &aRestTangent, RigAttr::RestTangent } })
	{
		*pAttr = nAttr.create(name, name, MFnNumericData::k3Double, 0.0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(addAttribute(*pAttr));
	}

	for (const auto& [pAttr, name] : { std::pair<MObject*, const char*>{ &aIkRestOffset, RigAttr::IkRestOffset }, { &aBindRestOffset, RigAttr::BindRestOffset } })
	{
		*pAttr = mAttr.create(name, name, MFnMatrixAttribute::kDouble, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(mAttr.setArray(true));
		CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(addAttribute(*pAttr));
	}

	// AFFECTS RELATIONSHIPS
	declareStandardAffects();
	std::vector<MObject> settings(aChannels.begin(), aChannels.end());
	settings.insert(settings.end(), { aIkControlCount, aKnotU, aBindU, aRestLength, aChainSign, aRestUp, aRestTangent, aIkRestOffset, aBindRestOffset });
	for (const MObject& oInput : settings)
	{
		attributeAffects(oInput, aControlOPM);
		attributeAffects(oInput, aJointOPM);
		attributeAffects(oInput, aOutputSocketMatrix);
	}

	return MStatus::kSuccess;
}

void RibbonModuleNode::evaluate(
	const RibbonMath::Setup& setup,
	const RibbonMath::Params& params,
	const MMatrix* mRest,
	const MMatrix* mLocal,
	const MMatrix& mStartDelta,
	const MMatrix& mEndDelta,
	RibbonSolve& outSolve)
{
	const int controlCount = setup.controlCount;

	// The curve is solved in the rest world, where the start and end sit at their rests, and the start's parent motion carries
	// the result. The end follows its own parent's motion, so a socket from another module can drive it (section 3.4).
	RibbonMath::Input input;
	for (int j = 0; j < controlCount; ++j)
	{
		input.local[j] = MathUtils::toMat4(mLocal[j]);
	}
	input.startOPM = MathUtils::toMat4(mRest[0]);
	// The plug deltas are motions in world axes (endOPM = endRest * delta), so the end's twist about its own X is read from the
	// motion in the end control's frame: rest rotation * delta * rest rotation^-1.
	const MMatrix mEndMotion = mEndDelta * mStartDelta.inverse();
	const MMatrix mEndRestRotation = MTransformationMatrix(mRest[controlCount - 1]).asRotateMatrix();
	input.endDelta = MathUtils::toMat4(mEndRestRotation * mEndMotion * mEndRestRotation.inverse());
	input.endOPM = MathUtils::toMat4(mRest[controlCount - 1] * mEndMotion);

	RibbonMath::Output output;
	RibbonMath::evaluate(setup, params, input, output);

	for (int j = 0; j < controlCount; ++j)
	{
		outSolve.mControlOPM[j] = MathUtils::toMMatrix(output.controlOPM[j]) * mStartDelta;
		outSolve.mWorld[j] = mLocal[j] * outSolve.mControlOPM[j];
	}
	for (int i = 0; i < setup.bindCount; ++i)
	{
		const unsigned int slot = static_cast<unsigned int>(controlCount + i);
		outSolve.mControlOPM[slot] = MathUtils::toMMatrix(output.bindOPM[i]) * mStartDelta;
		outSolve.mWorld[slot] = mLocal[slot] * outSolve.mControlOPM[slot];
	}
}

MStatus RibbonModuleNode::compute(const MPlug& plug, MDataBlock& data)
{
	const MObject oOutputAttr = plug.attribute();
	if (oOutputAttr != aControlOPM && oOutputAttr != aJointOPM && oOutputAttr != aOutputSocketMatrix)
	{
		return MStatus::kUnknownParameter;
	}

	MStatus status;
	MArrayDataHandle hControlInputData = data.inputArrayValue(aControlInputData, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hChildPlugs = data.inputArrayValue(aChildPlugs, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	const int controlCount = data.inputValue(aIkControlCount).asInt();
	const unsigned int slotCount = hControlInputData.elementCount();
	const int bindCount = static_cast<int>(slotCount) - controlCount;
	if (controlCount < 2 || bindCount < 1 || slotCount > MaxSlots)
	{
		// Not built yet: the build sets the count after it creates the controls.
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	// Everything the evaluation reads, as plain values: the stored rest, the channels, the controls' locals.
	RibbonMath::Setup setup;
	setup.controlCount = controlCount;
	setup.bindCount = bindCount;
	setup.restLength = data.inputValue(aRestLength).asDouble();
	setup.chainSign = data.inputValue(aChainSign).asDouble();
	{
		const MVector vUp = data.inputValue(aRestUp).asVector();
		const MVector vTangent = data.inputValue(aRestTangent).asVector();
		setup.restUp = { vUp.x, vUp.y, vUp.z };
		setup.restTangent = { vTangent.x, vTangent.y, vTangent.z };
	}
	// A saved scene keeps only the array elements that differ from their default (a knot at 0, an identity offset), and an
	// array with none has no handle, so a missing element is its default, which the Setup already holds.
	MStatus knotStatus;
	MStatus bindUStatus;
	MStatus ikOffsetStatus;
	MStatus bindOffsetStatus;
	MArrayDataHandle hKnotU = data.inputArrayValue(aKnotU, &knotStatus);
	MArrayDataHandle hBindU = data.inputArrayValue(aBindU, &bindUStatus);
	MArrayDataHandle hIkOffset = data.inputArrayValue(aIkRestOffset, &ikOffsetStatus);
	MArrayDataHandle hBindOffset = data.inputArrayValue(aBindRestOffset, &bindOffsetStatus);
	for (int j = 0; j < controlCount; ++j)
	{
		if (knotStatus && hKnotU.jumpToElement(j))
		{
			setup.knotU[j] = hKnotU.inputValue().asDouble();
		}
		if (ikOffsetStatus && hIkOffset.jumpToElement(j))
		{
			setup.ikOffset[j] = MathUtils::toMat4(hIkOffset.inputValue().asMatrix());
		}
	}
	for (int i = 0; i < bindCount; ++i)
	{
		if (bindUStatus && hBindU.jumpToElement(i))
		{
			setup.bindU[i] = hBindU.inputValue().asDouble();
		}
		if (bindOffsetStatus && hBindOffset.jumpToElement(i))
		{
			setup.bindOffset[i] = MathUtils::toMat4(hBindOffset.inputValue().asMatrix());
		}
	}

	double channelValues[RibbonChannel::Count];
	const std::array<ChannelSpec, RibbonChannel::Count>& specs = channelSpecs();
	for (unsigned int k = 0; k < RibbonChannel::Count; ++k)
	{
		MDataHandle hChannel = data.inputValue(aChannels[k]);
		channelValues[k] = specs[k].isAngle ? hChannel.asAngle().asRadians() : hChannel.asDouble();
	}

	MMatrix mLocal[MaxSlots];
	MMatrix mRest[MaxSlots];
	for (unsigned int slot = 0; slot < slotCount; ++slot)
	{
		status = readControlInput(hControlInputData, slot, mLocal[slot], mRest[slot]);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}

	RibbonSolve solve;
	evaluate(
		setup, paramsFromChannels(channelValues), mRest, mLocal,
		readChildPlugParentDelta(hChildPlugs, 0),
		readChildPlugParentDelta(hChildPlugs, controlCount - 1),
		solve);

	MArrayDataHandle hControlOPM = data.outputArrayValue(aControlOPM, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hJointOPM = data.outputArrayValue(aJointOPM, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hSocket = data.outputArrayValue(aOutputSocketMatrix, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// One solve writes all three outputs. Control OPM excludes the control's local matrix. Joint OPM and socket both want
	// the solved world matrix. Elements that were never created (the tweakers have no socket) are skipped.
	for (unsigned int slot = 0; slot < slotCount; ++slot)
	{
		writeOutputMatrix(hControlOPM, slot, solve.mControlOPM[slot]);
		writeOutputMatrix(hJointOPM, slot, solve.mWorld[slot]);
		writeOutputMatrix(hSocket, slot, solve.mWorld[slot]);
	}

	const MObject oNode = plug.node();
	commitOutput(data, oNode, aControlOPM, hControlOPM);
	commitOutput(data, oNode, aJointOPM, hJointOPM);
	commitOutput(data, oNode, aOutputSocketMatrix, hSocket);
	data.setClean(plug);
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
	ModuleInverseResult result;
	const unsigned int slotCount = static_cast<unsigned int>(context.controlRestWorld.size());
	const int controlCount = static_cast<int>(context.ikControlCount);
	result.controlLocal.resize(slotCount);
	result.socketWorld.assign(slotCount, MMatrix::identity);

	if (controlCount < 2 || slotCount <= static_cast<unsigned int>(controlCount) || slotCount > MaxSlots)
	{
		result.reason = "the ribbon's slots are not what its build made, so its controls come from the sidecar or rest";
		return result;
	}
	const int bindCount = static_cast<int>(slotCount) - controlCount;

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
	RibbonMath::Setup setup;
	const char* pReason = "";
	if (!RibbonMath::buildSetup(controlRest, controlCount, bindRest, bindCount, setup, pReason))
	{
		result.reason = pReason;
		return result;
	}

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
	const unsigned int lastBind = slotCount - 1;
	for (const int j : { 0, controlCount - 1 })
	{
		const unsigned int bindSlot = j == 0 ? firstBind : lastBind;
		if (hasJoint(bindSlot))
		{
			mLocal[j] = ModuleInverse::localFromWorld(*input.jointWorld[bindSlot], mRest[j], j == 0 ? mStartDelta : mEndDelta);
			solved[j] = true;
		}
	}
	RibbonSolve solve;
	constexpr int MaxEndRefinePasses = 24;
	constexpr double EndRefineTolerance = 1e-9;
	for (int pass = 0; pass < MaxEndRefinePasses && (solved[0] || solved[controlCount - 1]); ++pass)
	{
		const MMatrix mKeepFirst = mLocal[firstBind];
		const MMatrix mKeepLast = mLocal[lastBind];
		mLocal[firstBind] = MMatrix::identity;
		mLocal[lastBind] = MMatrix::identity;
		evaluate(setup, params, mRest, mLocal, mStartDelta, mEndDelta, solve);
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
			mLocal[j] = ModuleInverse::localFromWorld(mGap * mEndWorld, mRest[j], j == 0 ? mStartDelta : mEndDelta);
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
	evaluate(setup, params, mRest, mLocal, mStartDelta, mEndDelta, solve);
	bool hasEveryBindJoint = true;
	for (int slot = controlCount; slot < static_cast<int>(slotCount); ++slot)
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
	evaluate(setup, params, mRest, mLocal, mStartDelta, mEndDelta, solve);
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

RigModuleBuildResult RibbonModuleNode::createModule(
	const MString& moduleName,
	const Side& side,
	const ShapeType& shapeType,
	const std::vector<RibbonSlotInput>& controls,
	const std::vector<RibbonSlotInput>& joints,
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
	std::vector<RibbonSlotInput> slots = controls;
	slots.insert(slots.end(), joints.begin(), joints.end());
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
		// follow the start, and nothing transforms by it (inheritsTransform is off).
		if (slot != 0 && slot != controlCount - 1)
		{
			dagMod.reparentNode(oControls[slot], oControls[0]);
		}
		status = wireControl(oModule, oControls[slot], slot, input.mRestWorld, dagMod);
		if (!status)
		{
			result.status = status;
			return result;
		}

		if (slot >= controlCount)
		{
			// A tweaker is the residual layer: hidden, still keyable (section 3.2).
			MPlug pVisibility = MFnDependencyNode(oControls[slot]).findPlug("visibility", false, &status);
			CHECK_MSTATUS_AND_RETURN(status, result);
			dagMod.newPlugValueBool(pVisibility, false);
			continue;
		}

		RigModuleSocketInfo socketInfo;
		socketInfo.oTransform = oControls[slot];
		socketInfo.mRestWorld = input.mRestWorld;
		socketInfo.outputIndex = slot;
		const std::vector<std::string> socketNames = FkChainModuleNode::socketNamesAt(slot, controlCount);
		for (const std::string& socketName : socketNames)
		{
			result.parentSockets[socketName] = socketInfo;
		}
		DagPlugAndSocketUtils::setParentSocketNames(oModule, slot, socketNames, dagMod);

		if (slot == 0 || slot == controlCount - 1)
		{
			const char* plugName = slot == 0 ? PlugAndSocketNames::Start : PlugAndSocketNames::End;
			result.childPlugs[plugName] = RigModuleChildPlugInfo{ oControls[slot], slot, "", slot == 0 ? "" : PlugAndSocketNames::Start };
			DagPlugAndSocketUtils::setChildPlugName(oModule, slot, plugName, dagMod);
		}
	}

	// 3. The rest data the evaluation reads.
	dagMod.newPlugValueInt(MPlug(oModule, RibbonModuleNode::aIkControlCount), static_cast<int>(controlCount));
	for (unsigned int j = 0; j < controlCount; ++j)
	{
		dagMod.newPlugValueDouble(MPlug(oModule, RibbonModuleNode::aKnotU).elementByLogicalIndex(j), setup.knotU[j]);
		MPlug pOffset = MPlug(oModule, RibbonModuleNode::aIkRestOffset).elementByLogicalIndex(j);
		DagUtils::setMatrixPlug(pOffset, MathUtils::toMMatrix(setup.ikOffset[j]), dagMod);
	}
	for (int i = 0; i < setup.bindCount; ++i)
	{
		dagMod.newPlugValueDouble(MPlug(oModule, RibbonModuleNode::aBindU).elementByLogicalIndex(i), setup.bindU[i]);
		MPlug pOffset = MPlug(oModule, RibbonModuleNode::aBindRestOffset).elementByLogicalIndex(i);
		DagUtils::setMatrixPlug(pOffset, MathUtils::toMMatrix(setup.bindOffset[i]), dagMod);
	}
	dagMod.newPlugValueDouble(MPlug(oModule, RibbonModuleNode::aRestLength), setup.restLength);
	dagMod.newPlugValueDouble(MPlug(oModule, RibbonModuleNode::aChainSign), setup.chainSign);
	for (const auto& [oAttr, vValue] : { std::pair<const MObject&, RibbonMath::Vec3>{ RibbonModuleNode::aRestUp, setup.restUp }, { RibbonModuleNode::aRestTangent, setup.restTangent } })
	{
		MPlug pVector(oModule, oAttr);
		dagMod.newPlugValueDouble(pVector.child(0), vValue.x);
		dagMod.newPlugValueDouble(pVector.child(1), vValue.y);
		dagMod.newPlugValueDouble(pVector.child(2), vValue.z);
	}

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
	if (!readModuleArgs(args, { "joints" }, ctx, ribbonArgs, { "controlCount", "controlName", RigAttr::StartAimBlend, RigAttr::EndAimBlend }))
	{
		return false;
	}

	const std::string argsPath = ctx.where + "." + TemplateKey::ModuleArgs;
	bool valid = true;
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

	RibbonMath::Mat4 controlRest[RibbonMath::MaxControls];
	RibbonMath::Mat4 bindRestArray[RibbonMath::MaxBindJoints];
	std::copy(controlFrames.begin(), controlFrames.end(), controlRest);
	std::copy(bindRest.begin(), bindRest.end(), bindRestArray);
	RibbonMath::Setup setup;
	const char* pReason = "";
	if (!RibbonMath::buildSetup(controlRest, ribbonArgs.controlCount, bindRestArray, static_cast<int>(joints.size()), setup, pReason))
	{
		ctx.report.addError(argsPath + ".joints", pReason);
		return false;
	}

	// Names (D57): <base>IK_<nn> for the ik controls, <base>Tweak_<nn> for the tweakers (nn is the bind joint's index). The base
	// is controlName, else the first bind joint's usage without its trailing _NN. A usage another module already uses is
	// reported by the template's one-usage-per-rig check (D25).
	const std::string base = ribbonArgs.controlName.empty() ? StringUtils::stripNumberSuffix(ribbonArgs.joints.front().usage) : ribbonArgs.controlName;
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

	outPlan.moduleInterface = makeRibbonInterface(static_cast<unsigned int>(controls.size()));
	for (const RibbonSlotInput& slot : controls)
	{
		outPlan.moduleInterface.controls.push_back(slot.controlMarkup);
	}
	for (const RibbonSlotInput& slot : joints)
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
	outPlan.build = [moduleName, side, shapeType, controls, joints, setup, channelDefaults](MObject& oRigRoot, MDagModifier& dagMod)
	{
		return RibbonModuleNode::createModule(moduleName, side, shapeType, controls, joints, setup, channelDefaults, oRigRoot, dagMod);
	};
	return true;
}
