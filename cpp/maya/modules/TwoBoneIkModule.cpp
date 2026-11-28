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
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <array>
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
MObject TwoBoneIkNode::aIkTargetMatrix;
MObject TwoBoneIkNode::aIkTargetRestMatrix;
MObject TwoBoneIkNode::aIkTargetConnected;
MObject TwoBoneIkNode::aHandleSocketMatrix;

namespace
{
	// The IK solve of a restored pose may miss the joints by this fraction of the limb's reach before the restore warns.
	constexpr double kMaxIkMismatch{ 1.0e-3 };

	// Version 2: the IK handle, pole and settings controls come from the limb joints, so the three marker joints ("ik", "pole",
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

	// The solve's helpers live in maya/shared/TwoBoneIkSolve.
	using TwoBoneIkSolve::computeLimbFrames;
	using TwoBoneIkSolve::computeRest;
	using TwoBoneIkSolve::makeFrame;
	using TwoBoneIkSolve::orthonormalFrame;
}

// The two-bone half of the node: its attributes and affects, its plug reads, the ikTarget, its six slots' controls, joints, sockets and
// child plugs, its channels and the match outputs. It was a template shared with the Limb (D62) until the Limb was removed (D80 phase
// 3); these are its functions, unchanged, file-local to the one node that uses them.
//
// The ikTarget child plug (D62 section 8) is its own pair of inputs, not an element of childPlugs, and the ik, pole and settings sockets
// are their own output array, not elements of outputSocketMatrix (getSocketMatrixPlug finds them there). Maya orders the graph by
// attribute, so with the target and the handle's sockets on shared attributes a foot that hangs from the handle and feeds the target
// would be a cycle. aIkTargetConnected is set at build when the template wires the plug: unset, the end joint reaches the IK handle.
namespace Part
{
	// Defined below, in the order the template had them.
	MStatus readEval(MDataBlock& data, TwoBoneIkEval& eval);
	std::optional<MMatrix> readTargetDelta(MDataBlock& data);
	void applyTarget(const std::optional<MMatrix>& mTargetDelta, TwoBoneIkEval& eval);

	// Creates and adds the half's attributes, after TwoBoneIkNode::initializeSharedAttributes().
	MStatus createAttributes()
	{
		MStatus status;
		MFnNumericAttribute nAttr;
		MFnMatrixAttribute mAttr;
		MFnUnitAttribute uAttr;

		// Channel inputs, connected from the owning controls at build. Blend, stretch, soft and pin are clamped to 0..1.
		struct ClampedInput
		{
			MObject& attr;
			const char* name;
			double defaultValue;
		};
		const ClampedInput clampedInputs[] =
		{
			{ TwoBoneIkNode::aIkFkBlend, RigAttr::IkFkBlend, 1.0 },
			{ TwoBoneIkNode::aStretch, RigAttr::Stretch, 0.0 },
			{ TwoBoneIkNode::aSoft, RigAttr::Soft, 0.0 },
			{ TwoBoneIkNode::aPin, RigAttr::Pin, 0.0 }
		};
		for (const ClampedInput& input : clampedInputs)
		{
			input.attr = nAttr.create(input.name, input.name, MFnNumericData::kDouble, input.defaultValue, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			CHECK_MSTATUS_AND_RETURN_IT(nAttr.setMin(0.0));
			CHECK_MSTATUS_AND_RETURN_IT(nAttr.setMax(1.0));
			CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
			CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(input.attr));
		}

		TwoBoneIkNode::aRoll = uAttr.create(RigAttr::Roll, RigAttr::Roll, MFnUnitAttribute::kAngle, 0.0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(uAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(TwoBoneIkNode::aRoll));

		TwoBoneIkNode::aPoleSpace = nAttr.create(RigAttr::PoleSpace, RigAttr::PoleSpace, MFnNumericData::kShort, PoleSpaceLabels::ParentIndex, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setMin(0));
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setMax(1));
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(TwoBoneIkNode::aPoleSpace));

		// Stored at build, like Aim's aimRestOffset.
		TwoBoneIkNode::aIkRestOffset = mAttr.create(RigAttr::IkRestOffset, RigAttr::IkRestOffset, MFnMatrixAttribute::kDouble, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(mAttr.setArray(true));
		CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(TwoBoneIkNode::aIkRestOffset));

		TwoBoneIkNode::aRestBendNormal = nAttr.create(RigAttr::RestBendNormal, RigAttr::RestBendNormal, MFnNumericData::k3Double, 0.0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(TwoBoneIkNode::aRestBendNormal));

		TwoBoneIkNode::aRestPlaneAngle = nAttr.create(RigAttr::RestPlaneAngle, RigAttr::RestPlaneAngle, MFnNumericData::kDouble, 0.0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(TwoBoneIkNode::aRestPlaneAngle));

		TwoBoneIkNode::aLimbLengths = nAttr.create(RigAttr::LimbLengths, RigAttr::LimbLengths, MFnNumericData::k2Double, 0.0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(TwoBoneIkNode::aLimbLengths));

		// Outputs that drive the controls' visibility, a function of the blend alone.
		for (MObject* pAttr : { &TwoBoneIkNode::aFkVisible, &TwoBoneIkNode::aIkVisible })
		{
			const char* name = pAttr == &TwoBoneIkNode::aFkVisible ? RigAttr::FkVisible : RigAttr::IkVisible;
			*pAttr = nAttr.create(name, name, MFnNumericData::kDouble, 1.0, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			CHECK_MSTATUS_AND_RETURN_IT(nAttr.setWritable(false));
			CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(false));
			CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(*pAttr));
		}

		// The ikTarget: whether it is wired, the target's world and its rest. Then the handle's own socket array.
		TwoBoneIkNode::aIkTargetConnected = nAttr.create(RigAttr::IkTargetConnected, RigAttr::IkTargetConnected, MFnNumericData::kBoolean, false, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(TwoBoneIkNode::aIkTargetConnected));
		TwoBoneIkNode::aIkTargetMatrix = mAttr.create(RigAttr::IkTargetMatrix, RigAttr::IkTargetMatrix, MFnMatrixAttribute::kDouble, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(TwoBoneIkNode::aIkTargetMatrix));
		TwoBoneIkNode::aIkTargetRestMatrix = mAttr.create(RigAttr::IkTargetRestMatrix, RigAttr::IkTargetRestMatrix, MFnMatrixAttribute::kDouble, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(TwoBoneIkNode::aIkTargetRestMatrix));

		TwoBoneIkNode::aHandleSocketMatrix = mAttr.create(RigAttr::HandleSocketMatrix, RigAttr::HandleSocketMatrix, MFnMatrixAttribute::kDouble, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(mAttr.setArray(true));
		CHECK_MSTATUS_AND_RETURN_IT(mAttr.setUsesArrayDataBuilder(true));
		CHECK_MSTATUS_AND_RETURN_IT(mAttr.setWritable(false));
		CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(false));
		CHECK_MSTATUS_AND_RETURN_IT(mAttr.setWorldSpace(true));
		return MPxNode::addAttribute(TwoBoneIkNode::aHandleSocketMatrix);
	}

	// The half's attributeAffects, after TwoBoneIkNode::declareStandardAffects().
	void declareAffects()
	{
		// Every setting changes the solved joints and the sockets that carry them. The pole's space also moves the pole control.
		for (const MObject& oInput : { TwoBoneIkNode::aIkFkBlend, TwoBoneIkNode::aStretch, TwoBoneIkNode::aSoft, TwoBoneIkNode::aPin, TwoBoneIkNode::aRoll, TwoBoneIkNode::aPoleSpace, TwoBoneIkNode::aIkRestOffset,
			TwoBoneIkNode::aRestBendNormal, TwoBoneIkNode::aRestPlaneAngle, TwoBoneIkNode::aLimbLengths, TwoBoneIkNode::aIkTargetConnected, TwoBoneIkNode::aIkTargetMatrix, TwoBoneIkNode::aIkTargetRestMatrix })
		{
			MPxNode::attributeAffects(oInput, TwoBoneIkNode::aJointOPM);
			MPxNode::attributeAffects(oInput, TwoBoneIkNode::aOutputSocketMatrix);
		}
		// The handle's sockets follow the controls and the plugs they hang from, and nothing the target moves.
		for (const MObject& oInput : { TwoBoneIkNode::aControlMatrix, TwoBoneIkNode::aControlRestWorld, TwoBoneIkNode::aChildPlugParentMatrix, TwoBoneIkNode::aChildPlugParentRestMatrix, TwoBoneIkNode::aPoleSpace })
		{
			MPxNode::attributeAffects(oInput, TwoBoneIkNode::aHandleSocketMatrix);
		}
		MPxNode::attributeAffects(TwoBoneIkNode::aPoleSpace, TwoBoneIkNode::aControlOPM);
		MPxNode::attributeAffects(TwoBoneIkNode::aIkFkBlend, TwoBoneIkNode::aFkVisible);
		MPxNode::attributeAffects(TwoBoneIkNode::aIkFkBlend, TwoBoneIkNode::aIkVisible);
	}

	// The match outputs (D52 section 4.1), pulled only by setRigIkFkBlend and never in playback: the control locals that reproduce the current
	// pose on each side, by slot (aFkMatchLocal 0 to 2, aIkMatchLocal 3 and 4), and what the match can't do, a JSON list of messages. The node
	// declares aFkMatchLocal and aIkMatchLocal (matrix arrays) and aMatchWarnings (a string). Call after createAttributes().
	MStatus createMatchAttributes()
	{
		MStatus status;
		MFnMatrixAttribute mAttr;
		MFnTypedAttribute tAttr;
		// Maya computes an output only when something pulls it, and nothing is connected to these.
		for (MObject* pAttr : { &TwoBoneIkNode::aFkMatchLocal, &TwoBoneIkNode::aIkMatchLocal })
		{
			const char* name = pAttr == &TwoBoneIkNode::aFkMatchLocal ? RigAttr::FkMatchLocal : RigAttr::IkMatchLocal;
			*pAttr = mAttr.create(name, name, MFnMatrixAttribute::kDouble, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			CHECK_MSTATUS_AND_RETURN_IT(mAttr.setArray(true));
			CHECK_MSTATUS_AND_RETURN_IT(mAttr.setWritable(false));
			CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(false));
			CHECK_MSTATUS_AND_RETURN_IT(mAttr.setUsesArrayDataBuilder(true));
			CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(*pAttr));
		}
		MFnStringData fnEmptyList;
		const MObject oEmptyList = fnEmptyList.create("[]");
		TwoBoneIkNode::aMatchWarnings = tAttr.create(RigAttr::MatchWarnings, RigAttr::MatchWarnings, MFnData::kString, oEmptyList, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(tAttr.setWritable(false));
		CHECK_MSTATUS_AND_RETURN_IT(tAttr.setStorable(false));
		return MPxNode::addAttribute(TwoBoneIkNode::aMatchWarnings);
	}

	// The match outputs read everything the pose does: the controls, the child plugs' parents, every two-bone setting and the target. A match
	// places the handle so a raised foot lands on the joints (D74). The match outputs feed nothing, so the target makes no loop.
	void declareMatchAffects()
	{
		for (const MObject& oInput : { TwoBoneIkNode::aControlMatrix, TwoBoneIkNode::aControlRestWorld, TwoBoneIkNode::aChildPlugParentMatrix, TwoBoneIkNode::aChildPlugParentRestMatrix,
			TwoBoneIkNode::aIkFkBlend, TwoBoneIkNode::aStretch, TwoBoneIkNode::aSoft, TwoBoneIkNode::aPin, TwoBoneIkNode::aRoll, TwoBoneIkNode::aPoleSpace, TwoBoneIkNode::aIkRestOffset, TwoBoneIkNode::aRestBendNormal,
			TwoBoneIkNode::aRestPlaneAngle, TwoBoneIkNode::aLimbLengths, TwoBoneIkNode::aIkTargetConnected, TwoBoneIkNode::aIkTargetMatrix, TwoBoneIkNode::aIkTargetRestMatrix })
		{
			MPxNode::attributeAffects(oInput, TwoBoneIkNode::aFkMatchLocal);
			MPxNode::attributeAffects(oInput, TwoBoneIkNode::aIkMatchLocal);
			MPxNode::attributeAffects(oInput, TwoBoneIkNode::aMatchWarnings);
		}
	}

	// The one branch of compute() that allocates (the inverse's vectors and warnings), against D36's rule. It runs only when a tool pulls a
	// match output: playback never does, and bench (D52 phase 8) checks that. The inverse is the restore's, run on the joints this evaluation
	// solved, so what a match command writes is what a restore of the same pose would. `inverse` is the two-bone inverse (TwoBoneIkNode::inverse):
	// only the six slots are matched.
	// With the target connected (D74) the joints reach it, as compute() does, and the inverse places the handle so the foot's motion F (the
	// target's motion over the handle's) lands on them, so a match with a raised foot puts no pop in the ankle.
	MStatus computeMatch(const MPlug& plug, MDataBlock& data, ModuleInverseFn inverse)
	{
		MProfilingScope profile(ProfilerUtils::category(), MProfiler::kColorC_L2, "twoBoneIk computeMatch");   // no such event in a playback profile shows the branch never ran
		MStatus status;
		TwoBoneIkEval eval;
		status = readEval(data, eval);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		const std::optional<MMatrix> mTargetDelta = readTargetDelta(data);
		applyTarget(mTargetDelta, eval);
		TwoBoneIkSolveResult pose;
		TwoBoneIkSolve::evaluate(eval, pose);

		ModuleInverseContext context;
		ModuleInverseInput input;
		for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
		{
			context.controlRestWorld.push_back(eval.mRest[slot]);
			input.fallbackLocal.push_back(eval.mLocal[slot]);
			if (slot < TwoBoneIkSlot::LimbCount)
			{
				input.jointWorld.push_back(pose.mJointWorld[slot]);
			}
			else
			{
				input.jointWorld.emplace_back();
			}
		}
		// The inverse reads the start, IK handle, pole (in Parent space) and settings deltas, and no other.
		input.parentDelta.assign(TwoBoneIkSlot::Count, MMatrix::identity);
		input.parentDelta[TwoBoneIkSlot::Upper] = eval.mUpperDelta;
		input.parentDelta[TwoBoneIkSlot::Ik] = eval.mIkDelta;
		input.parentDelta[TwoBoneIkSlot::Pole] = eval.mPoleParentDelta;
		input.parentDelta[TwoBoneIkSlot::Settings] = eval.mSettingsDelta;
		input.channels =
		{
			eval.soft, eval.stretch, eval.pin, eval.rollRadians,
			eval.poleFollowsIk ? 1.0 : 0.0,
			eval.blend
		};
		if (mTargetDelta.has_value())
		{
			// The target's motion is F times the handle's, whatever the blend: F is the foot's channels alone.
			const MMatrix mHandleWorld = eval.mLocal[TwoBoneIkSlot::Ik] * eval.mRest[TwoBoneIkSlot::Ik] * eval.mIkDelta;
			input.ikTargetMotion = *mTargetDelta * MathUtils::computeDeltaFromRest(eval.mRest[TwoBoneIkSlot::Ik], mHandleWorld).inverse();
		}
		const ModuleInverseResult result = inverse(context, input);

		MArrayDataHandle hFkMatch = data.outputArrayValue(TwoBoneIkNode::aFkMatchLocal, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		MArrayDataHandle hIkMatch = data.outputArrayValue(TwoBoneIkNode::aIkMatchLocal, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		for (unsigned int slot = 0; slot < TwoBoneIkSlot::Settings && slot < result.controlLocal.size(); ++slot)
		{
			if (!result.controlLocal[slot].has_value())
			{
				continue;
			}
			TwoBoneIkNode::writeOutputMatrix(slot < TwoBoneIkSlot::LimbCount ? hFkMatch : hIkMatch, slot, *result.controlLocal[slot]);
		}

		data.outputValue(TwoBoneIkNode::aMatchWarnings).setString(JsonUtils::writeJsonString(result.warnings).c_str());

		const MObject oNode = plug.node();
		TwoBoneIkNode::commitOutput(data, oNode, TwoBoneIkNode::aFkMatchLocal, hFkMatch);
		TwoBoneIkNode::commitOutput(data, oNode, TwoBoneIkNode::aIkMatchLocal, hIkMatch);
		data.setClean(MPlug(oNode, TwoBoneIkNode::aMatchWarnings));
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	// Reads the limb's plugs. compute() writes every output array from one evaluation of this.
	MStatus readEval(MDataBlock& data, TwoBoneIkEval& eval)
	{
		MStatus status;

		MArrayDataHandle hControlInputData = data.inputArrayValue(TwoBoneIkNode::aControlInputData, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		MArrayDataHandle hChildPlugs = data.inputArrayValue(TwoBoneIkNode::aChildPlugs, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
		{
			status = TwoBoneIkNode::readControlInput(hControlInputData, slot, eval.mLocal[slot], eval.mRest[slot]);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		eval.mUpperDelta = TwoBoneIkNode::readChildPlugParentDelta(hChildPlugs, TwoBoneIkSlot::Upper);
		eval.mIkDelta = TwoBoneIkNode::readChildPlugParentDelta(hChildPlugs, TwoBoneIkSlot::Ik);
		eval.mSettingsDelta = TwoBoneIkNode::readChildPlugParentDelta(hChildPlugs, TwoBoneIkSlot::Settings);
		eval.poleFollowsIk = data.inputValue(TwoBoneIkNode::aPoleSpace).asShort() == 1;
		if (!eval.poleFollowsIk)
		{
			eval.mPoleParentDelta = TwoBoneIkNode::readChildPlugParentDelta(hChildPlugs, TwoBoneIkSlot::Pole);
		}

		// The settings are read at every blend: the match outputs run the inverse at blend 0 too, with these values.
		eval.blend = data.inputValue(TwoBoneIkNode::aIkFkBlend).asDouble();
		eval.stretch = data.inputValue(TwoBoneIkNode::aStretch).asDouble();
		eval.soft = data.inputValue(TwoBoneIkNode::aSoft).asDouble();
		eval.pin = data.inputValue(TwoBoneIkNode::aPin).asDouble();
		eval.rollRadians = data.inputValue(TwoBoneIkNode::aRoll).asAngle().asRadians();
		if (eval.blend > 0.0)
		{
			const double2& lengths = data.inputValue(TwoBoneIkNode::aLimbLengths).asDouble2();
			eval.rest.length1 = lengths[0];
			eval.rest.length2 = lengths[1];
			eval.rest.restPlaneAngle = data.inputValue(TwoBoneIkNode::aRestPlaneAngle).asDouble();
			eval.rest.restBendNormal = data.inputValue(TwoBoneIkNode::aRestBendNormal).asVector();

			MArrayDataHandle hRestOffset = data.inputArrayValue(TwoBoneIkNode::aIkRestOffset, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			for (unsigned int slot = 0; slot < TwoBoneIkSlot::LimbCount; ++slot)
			{
				status = hRestOffset.jumpToElement(slot);
				CHECK_MSTATUS_AND_RETURN_IT(status);
				eval.rest.ikRestOffset[slot] = hRestOffset.inputValue().asMatrix();
			}
		}

		return MStatus::kSuccess;
	}

	// The target's motion since rest, or none when the template leaves ikTarget unwired. Never called for the handle's sockets: they must
	// not pull the target (a foot that hangs from the handle feeds it).
	std::optional<MMatrix> readTargetDelta(MDataBlock& data)
	{
		if (!data.inputValue(TwoBoneIkNode::aIkTargetConnected).asBool())
		{
			return std::nullopt;
		}
		return MathUtils::computeDeltaFromRest(data.inputValue(TwoBoneIkNode::aIkTargetRestMatrix).asMatrix(), data.inputValue(TwoBoneIkNode::aIkTargetMatrix).asMatrix());
	}

	// The joints reach the target above blend 0. At blend 0 they are FK and the target is ignored.
	void applyTarget(const std::optional<MMatrix>& mTargetDelta, TwoBoneIkEval& eval)
	{
		if (mTargetDelta.has_value() && eval.blend > 0.0)
		{
			eval.hasTarget = true;
			eval.mTargetDelta = *mTargetDelta;
		}
	}

	// readEval, then the target for the joints' solve. At blend 0 the target is not read, so FK pulls nothing through it.
	MStatus readEvalWithTarget(MDataBlock& data, TwoBoneIkEval& eval)
	{
		const MStatus status = readEval(data, eval);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		if (eval.blend > 0.0)
		{
			applyTarget(readTargetDelta(data), eval);
		}
		return MStatus::kSuccess;
	}

	// The ik, pole and settings sockets (ModulePlugInfo::blindSockets of ikTarget): they follow their controls and never read the target.
	MStatus computeHandleSockets(const MPlug& plug, MDataBlock& data)
	{
		MStatus status;
		TwoBoneIkEval eval;
		status = readEval(data, eval);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		// The handle, then the pole in the space it is in, as TwoBoneIkSolve::evaluate does (slot order matters, D52 section 4.4).
		const ControlSolveStep ikStep = RigModuleNodeBase::solveControlStep(eval.mLocal[TwoBoneIkSlot::Ik], eval.mRest[TwoBoneIkSlot::Ik], eval.mIkDelta);
		const MMatrix mPoleDelta = eval.poleFollowsIk ? MathUtils::computeDeltaFromRest(eval.mRest[TwoBoneIkSlot::Ik], ikStep.mWorld) : eval.mPoleParentDelta;
		const ControlSolveStep poleStep = RigModuleNodeBase::solveControlStep(eval.mLocal[TwoBoneIkSlot::Pole], eval.mRest[TwoBoneIkSlot::Pole], mPoleDelta);
		const ControlSolveStep settingsStep = RigModuleNodeBase::solveControlStep(eval.mLocal[TwoBoneIkSlot::Settings], eval.mRest[TwoBoneIkSlot::Settings], eval.mSettingsDelta);

		MArrayDataHandle hSocket = data.outputArrayValue(TwoBoneIkNode::aHandleSocketMatrix, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		TwoBoneIkNode::writeOutputMatrix(hSocket, TwoBoneIkSlot::Ik, ikStep.mWorld);
		TwoBoneIkNode::writeOutputMatrix(hSocket, TwoBoneIkSlot::Pole, poleStep.mWorld);
		TwoBoneIkNode::writeOutputMatrix(hSocket, TwoBoneIkSlot::Settings, settingsStep.mWorld);
		TwoBoneIkNode::commitOutput(data, plug.node(), TwoBoneIkNode::aHandleSocketMatrix, hSocket);
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	// The pass 2 callback's half for the ikTarget plug (childPlug.plugIndex is TwoBoneIkSlot::IkTargetPlug). The target is a matrix
	// connection only: it has no control to reparent, and the solve is told it has a parent.
	MStatus connectIkTarget(const RigModuleDagReparentArgs& args)
	{
		MStatus status;
		const MPlug pSocket = TwoBoneIkNode::getSocketMatrixPlug(args.oParentTargetModuleNode, args.parentSocket.outputIndex, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		MPlug pTargetRest(args.oChildModuleNode, TwoBoneIkNode::aIkTargetRestMatrix);
		DagUtils::setMatrixPlug(pTargetRest, args.parentSocket.mRestWorld, args.dagMod);
		status = args.dagMod.connect(pSocket, MPlug(args.oChildModuleNode, TwoBoneIkNode::aIkTargetMatrix));
		CHECK_MSTATUS_AND_RETURN_IT(status);
		return args.dagMod.newPlugValueBool(MPlug(args.oChildModuleNode, TwoBoneIkNode::aIkTargetConnected), true);
	}

	// Per slot: bake and wire the joint, create and wire the control, name the sockets and child plug. Then the rest data the solve
	// reads. oControls gets the six controls, which the caller needs for the channels.
	MStatus createSlots(
		const MObject& oModule,
		const Side& side,
		const ShapeType& shapeType,
		const std::array<TwoBoneIkSlotInput, TwoBoneIkSlot::Count>& slots,
		const TwoBoneIkRest& rest,
		RigModuleBuildResult& result,
		MObject (&oControls)[TwoBoneIkSlot::Count],
		MDagModifier& dagMod)
	{
		MStatus status;
		for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
		{
			const TwoBoneIkSlotInput& input = slots[slot];

			// The IK, pole and settings controls have no joint (D57): their jointOPM and jointRestWorld elements are never made, which
			// compute(), the inverse and the match outputs allow for. Their nodes are named from their usage.
			MString controlBaseName(input.baseName.c_str());
			if (!input.oJoint.isNull())
			{
				status = TwoBoneIkNode::wireJoint(oModule, input.oJoint, slot, input.mRestWorld, dagMod);
				CHECK_MSTATUS_AND_RETURN_IT(status);
				controlBaseName = MFnDependencyNode(input.oJoint).name();
			}

			oControls[slot] = RigControlNode::createRigControl(dagMod, controlBaseName, shapeType, side, input.controlMarkup, MEulerRotation::kXYZ);
			// FK lower under FK upper and FK end under FK lower. The IK, pole and settings controls stay out of the FK
			// hierarchy: visibility inherits through the DAG, and the FK controls hide in IK (section 3.2).
			if (slot == TwoBoneIkSlot::Lower || slot == TwoBoneIkSlot::End)
			{
				dagMod.reparentNode(oControls[slot], oControls[slot - 1]);
			}
			status = TwoBoneIkNode::wireControl(oModule, oControls[slot], slot, input.mRestWorld, dagMod);
			CHECK_MSTATUS_AND_RETURN_IT(status);

			// index_0 to index_2 (and start and end) carry the blended joint's world, and the FK control's transform as the DAG parent (D78).
			// The FK controls hide in IK through `shown`, not visibility, so a child's control under one is not hidden with it.
			RigModuleSocketInfo socketInfo;
			socketInfo.oTransform = oControls[slot];
			socketInfo.mRestWorld = input.mRestWorld;
			socketInfo.outputIndex = slot;
			const std::vector<std::string> socketNames = TwoBoneIkSolve::socketNamesAt(slot);
			for (const std::string& socketName : socketNames)
			{
				result.parentSockets[socketName] = socketInfo;
			}
			DagPlugAndSocketUtils::setParentSocketNames(oModule, slot, socketNames, dagMod);

			if (const char* plugName = TwoBoneIkSolve::childPlugNameAt(slot))
			{
				RigModuleChildPlugInfo plugInfo{ oControls[slot], slot };
				if (slot == TwoBoneIkSlot::Settings)
				{
					plugInfo.followsPlug = PlugAndSocketNames::Start;
				}
				result.childPlugs[plugName] = plugInfo;
				DagPlugAndSocketUtils::setChildPlugName(oModule, slot, plugName, dagMod);
			}
		}

		// The ikTarget child plug (D62 section 8): no control, joint or socket, and optional, so a template that leaves it out keeps the end
		// joint on the IK handle. Its callback is connectIkTarget.
		RigModuleChildPlugInfo ikTargetPlug;
		ikTargetPlug.plugIndex = TwoBoneIkSlot::IkTargetPlug;
		ikTargetPlug.optional = true;
		result.childPlugs[PlugAndSocketNames::IkTarget] = ikTargetPlug;

		// The rest data the solve reads.
		for (unsigned int i = 0; i < TwoBoneIkSlot::LimbCount; ++i)
		{
			MPlug pOffset = MPlug(oModule, TwoBoneIkNode::aIkRestOffset).elementByLogicalIndex(i);
			DagUtils::setMatrixPlug(pOffset, rest.ikRestOffset[i], dagMod);
		}
		MPlug pBendNormal(oModule, TwoBoneIkNode::aRestBendNormal);
		dagMod.newPlugValueDouble(pBendNormal.child(0), rest.restBendNormal.x);
		dagMod.newPlugValueDouble(pBendNormal.child(1), rest.restBendNormal.y);
		dagMod.newPlugValueDouble(pBendNormal.child(2), rest.restBendNormal.z);
		dagMod.newPlugValueDouble(MPlug(oModule, TwoBoneIkNode::aRestPlaneAngle), rest.restPlaneAngle);
		MPlug pLengths(oModule, TwoBoneIkNode::aLimbLengths);
		dagMod.newPlugValueDouble(pLengths.child(0), rest.length1);
		dagMod.newPlugValueDouble(pLengths.child(1), rest.length2);
		return MStatus::kSuccess;
	}

	// Queues the half's channels on its controls. The controls must exist before an attribute can be added to them, and the attributes
	// before they can be wired, as in RigConstraintTwoBoneIkNode::createConstraint, so the build runs the modifier between this and wireChannels.
	// The pole's space is built at rest.poleSpace, the template's poleParentDefault. The channel's default is that value, so zeroing
	// the controls returns the pole to the space it was built in.
	MStatus queueChannels(const MObject (&oControls)[TwoBoneIkSlot::Count], double ikFkBlend, const TwoBoneIkRest& rest, MDagModifier& dagMod)
	{
		MStatus status = RigPoseScene::addDoubleChannel(oControls[TwoBoneIkSlot::Settings], RigAttr::IkFkBlend, ikFkBlend, 0.0, 1.0, false, dagMod);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = RigPoseScene::addDoubleChannel(oControls[TwoBoneIkSlot::Settings], RigAttr::Stretch, 0.0, 0.0, 1.0, false, dagMod);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = RigPoseScene::addDoubleChannel(oControls[TwoBoneIkSlot::Settings], RigAttr::Soft, 0.0, 0.0, 1.0, false, dagMod);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = RigPoseScene::addAngleChannel(oControls[TwoBoneIkSlot::Ik], RigAttr::Roll, true, dagMod);   // a reflection reverses a roll's sense
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = RigPoseScene::addDoubleChannel(oControls[TwoBoneIkSlot::Pole], RigAttr::Pin, 0.0, 0.0, 1.0, false, dagMod);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		// The pole's module-owned space (D52 section 4.4): the module reads it, so it is wired here and not through a rigConstraint.
		return RigPoseScene::addEnumChannel(oControls[TwoBoneIkSlot::Pole], RigAttr::ControlSpace, { PoleSpaceLabels::Parent, PoleSpaceLabels::Ik }, rest.poleSpace, false, dagMod);
	}

	// Wires the channels queueChannels added to the module, drives the controls' visibility, and locks and hides the transform
	// channels the animator doesn't use.
	MStatus wireChannels(const MObject& oModule, const MObject (&oControls)[TwoBoneIkSlot::Count], MDagModifier& dagMod)
	{
		MStatus status;
		struct ChannelWire
		{
			unsigned int slot;
			const char* controlAttr;
			const MObject& moduleAttr;
		};
		const ChannelWire wires[] =
		{
			{ TwoBoneIkSlot::Settings, RigAttr::IkFkBlend, TwoBoneIkNode::aIkFkBlend },
			{ TwoBoneIkSlot::Settings, RigAttr::Stretch, TwoBoneIkNode::aStretch },
			{ TwoBoneIkSlot::Settings, RigAttr::Soft, TwoBoneIkNode::aSoft },
			{ TwoBoneIkSlot::Ik, RigAttr::Roll, TwoBoneIkNode::aRoll },
			{ TwoBoneIkSlot::Pole, RigAttr::Pin, TwoBoneIkNode::aPin },
			{ TwoBoneIkSlot::Pole, RigAttr::ControlSpace, TwoBoneIkNode::aPoleSpace }
		};
		for (const ChannelWire& wire : wires)
		{
			MPlug pControlAttr = MFnDependencyNode(oControls[wire.slot]).findPlug(wire.controlAttr, false, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			status = dagMod.connect(pControlAttr, MPlug(oModule, wire.moduleAttr));
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}

		// FK lower and end are visible below blend 1, the IK handle and pole above blend 0. Start and settings are always visible.
		const std::pair<unsigned int, const MObject&> visibility[] =
		{
			{ TwoBoneIkSlot::Lower, TwoBoneIkNode::aFkVisible }, { TwoBoneIkSlot::End, TwoBoneIkNode::aFkVisible },
			{ TwoBoneIkSlot::Ik, TwoBoneIkNode::aIkVisible }, { TwoBoneIkSlot::Pole, TwoBoneIkNode::aIkVisible }
		};
		for (const auto& [slot, oVisibleAttr] : visibility)
		{
			status = dagMod.connect(MPlug(oModule, oVisibleAttr), MPlug(oControls[slot], RigControlNode::aShown));
			CHECK_MSTATUS_AND_RETURN_IT(status);
			status = DagUtils::queueLockAndHideVisibility(oControls[slot], dagMod);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}

		// Transform channels the animator doesn't use are locked and hidden. The IK controls keep what they move by (section 3.3).
		const std::vector<std::string> ikKeep{ "tx", "ty", "tz", "rx", "ry", "rz" };
		const std::vector<std::string> poleKeep{ "tx", "ty", "tz" };
		status = DagUtils::queueLockAndHideTransform(oControls[TwoBoneIkSlot::Ik], dagMod, ikKeep);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = DagUtils::queueLockAndHideTransform(oControls[TwoBoneIkSlot::Pole], dagMod, poleKeep);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		return DagUtils::queueLockAndHideTransform(oControls[TwoBoneIkSlot::Settings], dagMod);
	}
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

	status = Part::createAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	status = Part::createMatchAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// AFFECTS RELATIONSHIPS
	declareStandardAffects();
	Part::declareAffects();
	Part::declareMatchAffects();

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
		return Part::computeMatch(plug, data, &TwoBoneIkNode::inverse);
	}

	if (oOutputAttr == aHandleSocketMatrix)
	{
		return Part::computeHandleSockets(plug, data);
	}

	if (oOutputAttr != aControlOPM && oOutputAttr != aJointOPM && oOutputAttr != aOutputSocketMatrix)
	{
		return MStatus::kUnknownParameter;
	}

	MStatus status;
	TwoBoneIkEval eval;
	status = Part::readEvalWithTarget(data, eval);
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
		{ RigAttr::ControlSpace, TwoBoneIkSlot::Pole, static_cast<double>(PoleSpaceLabels::ParentIndex) },   // the rig's own value is always there
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

// Allocates (the contract's vectors, the warnings), which is fine for a restore. compute() never calls it.
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

	// Slots 3 to 5 have no joints: the handle and pole come from the limb's three, which are read here.
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
	// With a foot on the ikTarget, the end reaches rest * F * the handle's motion, so the handle is placed to make F land there.
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
		// The forward evaluation behind the sockets (and so an anchored Ribbon on them) reaches the target, as compute() does.
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
	status = Part::queueChannels(oControls, ikFkBlend, rest, dagMod);
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

	// The shared callback, which skips the DAG reparent for the sockets with no control and always connects matrices, or the target's.
	result.dagParentCallback = [](const RigModuleDagReparentArgs& args)
	{
		if (args.childPlug.plugIndex == TwoBoneIkSlot::IkTargetPlug)
		{
			return Part::connectIkTarget(args);
		}
		return TwoBoneIkNode::reparentChildPlugToSocket(args);
	};
	result.status = MStatus::kSuccess;
	return result;
}

bool TwoBoneIkNode::planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan)
{
	TwoBoneIkArgs ikArgs;
	if (!readModuleArgs(args, { "joints" }, ctx, ikArgs, { "controlName", "poleOffset", "settingsOffset", RigAttr::IkFkBlend, "ikOrientToWorld", "ikAlignToPole", "poleParentDefault" }))
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
