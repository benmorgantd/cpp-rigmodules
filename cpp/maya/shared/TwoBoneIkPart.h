#pragma once

#include "core/Constants.h"
#include "core/JsonUtils.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/Side.h"
#include "maya/shared/TwoBoneIkSolve.h"
#include "maya/shared/Utilities.h"

#include <array>
#include <string>
#include <utility>

#include <maya/MArrayDataHandle.h>
#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnStringData.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MPlug.h>
#include <maya/MProfiler.h>

// The two-bone IK half of a module node (D62, design/limb.md): its attributes, its plug reads, its six slots' controls, joints, sockets
// and child plugs, and its channels. TwoBoneIkNode is this and the match outputs, and LimbNode is this and a ribbon. A Maya node
// can't inherit (RigModuleNodeBase), so the half is a template over the node, which must declare the same static attributes:
// aIkFkBlend, aStretch, aSoft, aPin (clamped 0..1), aRoll (an angle), aPoleSpace (a short, 0 or 1), aIkRestOffset (a matrix array),
// aRestBendNormal, aRestPlaneAngle, aLimbLengths, the visibility outputs aFkVisible and aIkVisible, and the match outputs aFkMatchLocal and
// aIkMatchLocal (matrix arrays) and aMatchWarnings (a string). A change here changes both
// modules: read design/limb.md first.
template <typename Node>
struct TwoBoneIkPart
{
	// Creates and adds the half's attributes, after Node::initializeSharedAttributes().
	static MStatus createAttributes()
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
			{ Node::aIkFkBlend, RigAttr::IkFkBlend, 1.0 },
			{ Node::aStretch, RigAttr::Stretch, 0.0 },
			{ Node::aSoft, RigAttr::Soft, 0.0 },
			{ Node::aPin, RigAttr::Pin, 0.0 }
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

		Node::aRoll = uAttr.create(RigAttr::Roll, RigAttr::Roll, MFnUnitAttribute::kAngle, 0.0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(uAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(Node::aRoll));

		Node::aPoleSpace = nAttr.create(RigAttr::PoleSpace, RigAttr::PoleSpace, MFnNumericData::kShort, 0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setMin(0));
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setMax(1));
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(Node::aPoleSpace));

		// Stored at build, like Aim's aimRestOffset.
		Node::aIkRestOffset = mAttr.create(RigAttr::IkRestOffset, RigAttr::IkRestOffset, MFnMatrixAttribute::kDouble, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(mAttr.setArray(true));
		CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(Node::aIkRestOffset));

		Node::aRestBendNormal = nAttr.create(RigAttr::RestBendNormal, RigAttr::RestBendNormal, MFnNumericData::k3Double, 0.0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(Node::aRestBendNormal));

		Node::aRestPlaneAngle = nAttr.create(RigAttr::RestPlaneAngle, RigAttr::RestPlaneAngle, MFnNumericData::kDouble, 0.0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(Node::aRestPlaneAngle));

		Node::aLimbLengths = nAttr.create(RigAttr::LimbLengths, RigAttr::LimbLengths, MFnNumericData::k2Double, 0.0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(Node::aLimbLengths));

		// Outputs that drive the controls' visibility, a function of the blend alone.
		for (MObject* pAttr : { &Node::aFkVisible, &Node::aIkVisible })
		{
			const char* name = pAttr == &Node::aFkVisible ? RigAttr::FkVisible : RigAttr::IkVisible;
			*pAttr = nAttr.create(name, name, MFnNumericData::kDouble, 1.0, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			CHECK_MSTATUS_AND_RETURN_IT(nAttr.setWritable(false));
			CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(false));
			CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(*pAttr));
		}
		return MStatus::kSuccess;
	}

	// The half's attributeAffects, after Node::declareStandardAffects().
	static void declareAffects()
	{
		// Every setting changes the solved joints and the sockets that carry them. The pole's space also moves the pole control.
		for (const MObject& oInput : { Node::aIkFkBlend, Node::aStretch, Node::aSoft, Node::aPin, Node::aRoll, Node::aPoleSpace, Node::aIkRestOffset,
			Node::aRestBendNormal, Node::aRestPlaneAngle, Node::aLimbLengths })
		{
			MPxNode::attributeAffects(oInput, Node::aJointOPM);
			MPxNode::attributeAffects(oInput, Node::aOutputSocketMatrix);
		}
		MPxNode::attributeAffects(Node::aPoleSpace, Node::aControlOPM);
		MPxNode::attributeAffects(Node::aIkFkBlend, Node::aFkVisible);
		MPxNode::attributeAffects(Node::aIkFkBlend, Node::aIkVisible);
	}

	// The match outputs (D52 section 4.1), pulled only by setRigIkFkBlend and never in playback: the control locals that reproduce the current
	// pose on each side, by slot (aFkMatchLocal 0 to 2, aIkMatchLocal 3 and 4), and what the match can't do, a JSON list of messages. The node
	// declares aFkMatchLocal and aIkMatchLocal (matrix arrays) and aMatchWarnings (a string). Call after createAttributes().
	static MStatus createMatchAttributes()
	{
		MStatus status;
		MFnMatrixAttribute mAttr;
		MFnTypedAttribute tAttr;
		// Maya computes an output only when something pulls it, and nothing is connected to these.
		for (MObject* pAttr : { &Node::aFkMatchLocal, &Node::aIkMatchLocal })
		{
			const char* name = pAttr == &Node::aFkMatchLocal ? RigAttr::FkMatchLocal : RigAttr::IkMatchLocal;
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
		Node::aMatchWarnings = tAttr.create(RigAttr::MatchWarnings, RigAttr::MatchWarnings, MFnData::kString, oEmptyList, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(tAttr.setWritable(false));
		CHECK_MSTATUS_AND_RETURN_IT(tAttr.setStorable(false));
		return MPxNode::addAttribute(Node::aMatchWarnings);
	}

	// The match outputs read everything the pose does: the controls, the child plugs' parents and every two-bone setting.
	static void declareMatchAffects()
	{
		for (const MObject& oInput : { Node::aControlMatrix, Node::aControlRestWorld, Node::aChildPlugParentMatrix, Node::aChildPlugParentRestMatrix,
			Node::aIkFkBlend, Node::aStretch, Node::aSoft, Node::aPin, Node::aRoll, Node::aPoleSpace, Node::aIkRestOffset, Node::aRestBendNormal,
			Node::aRestPlaneAngle, Node::aLimbLengths })
		{
			MPxNode::attributeAffects(oInput, Node::aFkMatchLocal);
			MPxNode::attributeAffects(oInput, Node::aIkMatchLocal);
			MPxNode::attributeAffects(oInput, Node::aMatchWarnings);
		}
	}

	// The one branch of compute() that allocates (the inverse's vectors and warnings), against D36's rule. It runs only when a tool pulls a
	// match output: playback never does, and bench (D52 phase 8) checks that. The inverse is the restore's, run on the joints this evaluation
	// solved, so what a match command writes is what a restore of the same pose would. `inverse` is the two-bone inverse (TwoBoneIkNode::inverse):
	// a Limb matches its limb joints the same way, and only the first six slots are matched.
	// mTargetDelta is a Limb's ikTarget motion since rest when the target is connected (D74): the joints reach it, as the Limb's compute
	// does, and the inverse places the handle so the foot's motion F (the target's motion over the handle's) lands on them, so a match with
	// a raised foot puts no pop in the ankle.
	static MStatus computeMatch(const MPlug& plug, MDataBlock& data, ModuleInverseFn inverse, const std::optional<MMatrix>& mTargetDelta = std::nullopt)
	{
		MProfilingScope profile(ProfilerUtils::category(), MProfiler::kColorC_L2, "twoBoneIk computeMatch");   // no such event in a playback profile shows the branch never ran
		MStatus status;
		TwoBoneIkEval eval;
		status = readEval(data, eval);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		if (mTargetDelta.has_value() && eval.blend > 0.0)
		{
			eval.hasTarget = true;
			eval.mTargetDelta = *mTargetDelta;
		}
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

		MArrayDataHandle hFkMatch = data.outputArrayValue(Node::aFkMatchLocal, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		MArrayDataHandle hIkMatch = data.outputArrayValue(Node::aIkMatchLocal, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		for (unsigned int slot = 0; slot < TwoBoneIkSlot::Settings && slot < result.controlLocal.size(); ++slot)
		{
			if (!result.controlLocal[slot].has_value())
			{
				continue;
			}
			Node::writeOutputMatrix(slot < TwoBoneIkSlot::LimbCount ? hFkMatch : hIkMatch, slot, *result.controlLocal[slot]);
		}

		data.outputValue(Node::aMatchWarnings).setString(JsonUtils::writeJsonString(result.warnings).c_str());

		const MObject oNode = plug.node();
		Node::commitOutput(data, oNode, Node::aFkMatchLocal, hFkMatch);
		Node::commitOutput(data, oNode, Node::aIkMatchLocal, hIkMatch);
		data.setClean(MPlug(oNode, Node::aMatchWarnings));
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	// Reads the limb's plugs. compute() writes every output array from one evaluation of this.
	static MStatus readEval(MDataBlock& data, TwoBoneIkEval& eval)
	{
		MStatus status;

		MArrayDataHandle hControlInputData = data.inputArrayValue(Node::aControlInputData, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		MArrayDataHandle hChildPlugs = data.inputArrayValue(Node::aChildPlugs, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
		{
			status = Node::readControlInput(hControlInputData, slot, eval.mLocal[slot], eval.mRest[slot]);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		eval.mUpperDelta = Node::readChildPlugParentDelta(hChildPlugs, TwoBoneIkSlot::Upper);
		eval.mIkDelta = Node::readChildPlugParentDelta(hChildPlugs, TwoBoneIkSlot::Ik);
		eval.mSettingsDelta = Node::readChildPlugParentDelta(hChildPlugs, TwoBoneIkSlot::Settings);
		eval.poleFollowsIk = data.inputValue(Node::aPoleSpace).asShort() == 1;
		if (!eval.poleFollowsIk)
		{
			eval.mPoleParentDelta = Node::readChildPlugParentDelta(hChildPlugs, TwoBoneIkSlot::Pole);
		}

		// The settings are read at every blend: the match outputs run the inverse at blend 0 too, with these values.
		eval.blend = data.inputValue(Node::aIkFkBlend).asDouble();
		eval.stretch = data.inputValue(Node::aStretch).asDouble();
		eval.soft = data.inputValue(Node::aSoft).asDouble();
		eval.pin = data.inputValue(Node::aPin).asDouble();
		eval.rollRadians = data.inputValue(Node::aRoll).asAngle().asRadians();
		if (eval.blend > 0.0)
		{
			const double2& lengths = data.inputValue(Node::aLimbLengths).asDouble2();
			eval.rest.length1 = lengths[0];
			eval.rest.length2 = lengths[1];
			eval.rest.restPlaneAngle = data.inputValue(Node::aRestPlaneAngle).asDouble();
			eval.rest.restBendNormal = data.inputValue(Node::aRestBendNormal).asVector();

			MArrayDataHandle hRestOffset = data.inputArrayValue(Node::aIkRestOffset, &status);
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

	// Per slot: bake and wire the joint, create and wire the control, name the sockets and child plug. Then the rest data the solve
	// reads. oControls gets the six controls, which the caller needs for the channels.
	static MStatus createSlots(
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
				status = Node::wireJoint(oModule, input.oJoint, slot, input.mRestWorld, dagMod);
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
			status = Node::wireControl(oModule, oControls[slot], slot, input.mRestWorld, dagMod);
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

		// The rest data the solve reads.
		for (unsigned int i = 0; i < TwoBoneIkSlot::LimbCount; ++i)
		{
			MPlug pOffset = MPlug(oModule, Node::aIkRestOffset).elementByLogicalIndex(i);
			DagUtils::setMatrixPlug(pOffset, rest.ikRestOffset[i], dagMod);
		}
		MPlug pBendNormal(oModule, Node::aRestBendNormal);
		dagMod.newPlugValueDouble(pBendNormal.child(0), rest.restBendNormal.x);
		dagMod.newPlugValueDouble(pBendNormal.child(1), rest.restBendNormal.y);
		dagMod.newPlugValueDouble(pBendNormal.child(2), rest.restBendNormal.z);
		dagMod.newPlugValueDouble(MPlug(oModule, Node::aRestPlaneAngle), rest.restPlaneAngle);
		MPlug pLengths(oModule, Node::aLimbLengths);
		dagMod.newPlugValueDouble(pLengths.child(0), rest.length1);
		dagMod.newPlugValueDouble(pLengths.child(1), rest.length2);
		return MStatus::kSuccess;
	}

	// Queues the half's channels on its controls. The controls must exist before an attribute can be added to them, and the attributes
	// before they can be wired, as in RigConstraintNode::createConstraint, so the build runs the modifier between this and wireChannels.
	static MStatus queueChannels(const MObject (&oControls)[TwoBoneIkSlot::Count], double ikFkBlend, MDagModifier& dagMod)
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
		return RigPoseScene::addEnumChannel(oControls[TwoBoneIkSlot::Pole], RigAttr::ControlSpace, { PoleSpaceLabels::Parent, PoleSpaceLabels::Ik }, 0, false, dagMod);
	}

	// Wires the channels queueChannels added to the module, drives the controls' visibility, and locks and hides the transform
	// channels the animator doesn't use.
	static MStatus wireChannels(const MObject& oModule, const MObject (&oControls)[TwoBoneIkSlot::Count], MDagModifier& dagMod)
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
			{ TwoBoneIkSlot::Settings, RigAttr::IkFkBlend, Node::aIkFkBlend },
			{ TwoBoneIkSlot::Settings, RigAttr::Stretch, Node::aStretch },
			{ TwoBoneIkSlot::Settings, RigAttr::Soft, Node::aSoft },
			{ TwoBoneIkSlot::Ik, RigAttr::Roll, Node::aRoll },
			{ TwoBoneIkSlot::Pole, RigAttr::Pin, Node::aPin },
			{ TwoBoneIkSlot::Pole, RigAttr::ControlSpace, Node::aPoleSpace }
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
			{ TwoBoneIkSlot::Lower, Node::aFkVisible }, { TwoBoneIkSlot::End, Node::aFkVisible },
			{ TwoBoneIkSlot::Ik, Node::aIkVisible }, { TwoBoneIkSlot::Pole, Node::aIkVisible }
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
};
