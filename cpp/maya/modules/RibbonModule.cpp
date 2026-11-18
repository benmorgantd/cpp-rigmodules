#include "maya/modules/RibbonModule.h"
#include "core/Constants.h"
#include "core/RibbonMath.h"
#include "core/RigJsonStructs.h"
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

	RibbonMath::Mat4 toMat4(const MMatrix& mMatrix)
	{
		RibbonMath::Mat4 result;
		for (int r = 0; r < 4; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				result.m[r][c] = mMatrix[r][c];
			}
		}
		return result;
	}

	MMatrix toMMatrix(const RibbonMath::Mat4& mat)
	{
		return MMatrix(mat.m);
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
		input.local[j] = toMat4(mLocal[j]);
	}
	input.startOPM = toMat4(mRest[0]);
	// The plug deltas are motions in world axes (endOPM = endRest * delta), so the end's twist about its own X is read from the
	// motion in the end control's frame: rest rotation * delta * rest rotation^-1.
	const MMatrix mEndMotion = mEndDelta * mStartDelta.inverse();
	const MMatrix mEndRestRotation = MTransformationMatrix(mRest[controlCount - 1]).asRotateMatrix();
	input.endDelta = toMat4(mEndRestRotation * mEndMotion * mEndRestRotation.inverse());
	input.endOPM = toMat4(mRest[controlCount - 1] * mEndMotion);

	RibbonMath::Output output;
	RibbonMath::evaluate(setup, params, input, output);

	for (int j = 0; j < controlCount; ++j)
	{
		outSolve.mControlOPM[j] = toMMatrix(output.controlOPM[j]) * mStartDelta;
		outSolve.mWorld[j] = mLocal[j] * outSolve.mControlOPM[j];
	}
	for (int i = 0; i < setup.bindCount; ++i)
	{
		const unsigned int slot = static_cast<unsigned int>(controlCount + i);
		outSolve.mControlOPM[slot] = toMMatrix(output.bindOPM[i]) * mStartDelta;
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
			setup.ikOffset[j] = toMat4(hIkOffset.inputValue().asMatrix());
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
			setup.bindOffset[i] = toMat4(hBindOffset.inputValue().asMatrix());
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
		controlRest[j] = toMat4(context.controlRestWorld[j]);
	}
	for (int i = 0; i < bindCount; ++i)
	{
		bindRest[i] = toMat4(context.controlRestWorld[controlCount + i]);
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

	// 1. The ends from their joints: the ordinary local from world.
	for (const int j : { 0, controlCount - 1 })
	{
		if (hasJoint(j))
		{
			mLocal[j] = ModuleInverse::localFromWorld(*input.jointWorld[j], mRest[j], j == 0 ? mStartDelta : mEndDelta);
		}
	}

	// 2. The interior controls. Their OPMs ride the base curve, which only the ends set, so the ends fixed above give them.
	RibbonSolve solve;
	evaluate(setup, params, mRest, mLocal, mStartDelta, mEndDelta, solve);
	for (int j = 1; j < controlCount - 1; ++j)
	{
		if (hasJoint(j))
		{
			mLocal[j] = *input.jointWorld[j] * solve.mControlOPM[j].inverse();
		}
	}

	// 3. The tweakers are the residual: each bind joint against the frame the detail curve gives it, which the controls
	// above fixed. A bind joint that is present comes back exactly whatever a missing control left.
	evaluate(setup, params, mRest, mLocal, mStartDelta, mEndDelta, solve);
	bool hasEveryBindJoint = true;
	for (int slot = controlCount; slot < static_cast<int>(slotCount); ++slot)
	{
		if (hasJoint(slot))
		{
			mLocal[slot] = *input.jointWorld[slot] * solve.mControlOPM[slot].inverse();
		}
		else
		{
			hasEveryBindJoint = false;
		}
	}

	for (unsigned int slot = 0; slot < slotCount; ++slot)
	{
		if (hasJoint(slot))
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

	// 2. Per slot: wire the joint, create and wire the control. Every slot's joint follows its control's world, the tweakers
	// drive the bind joints and the ik controls their marker joints.
	for (unsigned int slot = 0; slot < slots.size(); ++slot)
	{
		const RibbonSlotInput& input = slots[slot];
		MFnDependencyNode fnJoint(input.oJoint);

		status = RibbonModuleNode::wireJoint(oModule, input.oJoint, slot, input.mRestWorld, dagMod);
		if (!status)
		{
			result.status = status;
			return result;
		}

		oControls[slot] = RigControlNode::createRigControl(dagMod, fnJoint.name(), shapeType, side, input.controlMarkup, MEulerRotation::kXYZ);
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
		DagUtils::setMatrixPlug(pOffset, toMMatrix(setup.ikOffset[j]), dagMod);
	}
	for (int i = 0; i < setup.bindCount; ++i)
	{
		dagMod.newPlugValueDouble(MPlug(oModule, RibbonModuleNode::aBindU).elementByLogicalIndex(i), setup.bindU[i]);
		MPlug pOffset = MPlug(oModule, RibbonModuleNode::aBindRestOffset).elementByLogicalIndex(i);
		DagUtils::setMatrixPlug(pOffset, toMMatrix(setup.bindOffset[i]), dagMod);
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
	if (!readModuleArgs(args, { "controls", "joints" }, ctx, ribbonArgs, { RigAttr::StartAimBlend, RigAttr::EndAimBlend }))
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
	if (ribbonArgs.controls.size() < 2 || ribbonArgs.controls.size() > static_cast<std::size_t>(RibbonMath::MaxControls))
	{
		ctx.report.addError(argsPath + ".controls", "a ribbon has between 2 and " + std::to_string(RibbonMath::MaxControls)
			+ " control joints (a start, an end and the interior ones), got " + std::to_string(ribbonArgs.controls.size()));
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
	// used twice, which also keeps a control joint from being a bind joint.
	const auto claimAll = [&ctx](const std::vector<JointSelector>& selectors, std::vector<RibbonSlotInput>& outSlots)
	{
		bool allClaimed = true;
		for (const JointSelector& selector : selectors)
		{
			RibbonSlotInput slot;
			if (!RigModuleNodeBase::claimJoint(ctx, selector, slot.oJoint, slot.mRestWorld))
			{
				allClaimed = false;
				continue;
			}
			slot.controlMarkup = makeControlMarkup(selector.side, selector.usage);
			outSlots.push_back(slot);
		}
		return allClaimed;
	};
	std::vector<RibbonSlotInput> controls;
	std::vector<RibbonSlotInput> joints;
	valid = claimAll(ribbonArgs.controls, controls);
	valid = claimAll(ribbonArgs.joints, joints) && valid;
	if (!valid)
	{
		return false;
	}

	RibbonMath::Mat4 controlRest[RibbonMath::MaxControls];
	RibbonMath::Mat4 bindRest[RibbonMath::MaxBindJoints];
	for (std::size_t j = 0; j < controls.size(); ++j)
	{
		controlRest[j] = toMat4(controls[j].mRestWorld);
	}
	for (std::size_t i = 0; i < joints.size(); ++i)
	{
		bindRest[i] = toMat4(joints[i].mRestWorld);
	}
	RibbonMath::Setup setup;
	const char* pReason = "";
	if (!RibbonMath::buildSetup(controlRest, static_cast<int>(controls.size()), bindRest, static_cast<int>(joints.size()), setup, pReason))
	{
		ctx.report.addError(argsPath + ".controls", pReason);
		return false;
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
