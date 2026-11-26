#include "maya/commands/TwoBoneIkCmds.h"
#include "core/Constants.h"
#include "core/ControlMarkup.h"
#include "core/JsonUtils.h"
#include "core/PoseFile.h"
#include "maya/modules/LimbModule.h"
#include "maya/modules/ReverseFootModule.h"
#include "maya/modules/TwoBoneIkModule.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/RigTraversal.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <string>
#include <vector>

#include <maya/MAngle.h>
#include <maya/MArgDatabase.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MObjectArray.h>
#include <maya/MPlug.h>
#include <maya/MStringArray.h>

namespace
{
	// Code reads flags by long name only. Short names appear only in newSyntax() (D14).
	constexpr const char* kBlendFlag{ "-blend" };

	// The TwoBoneIk module the object belongs to: the module node, or any of its controls. Reports anything else.
	MStatus readLimbModule(const MString& name, const char* commandName, MObject& oOutModule)
	{
		MObject oNode;
		if (!DagUtils::getMObjectFromName(name, oNode))
		{
			MGlobal::displayError(MString(commandName) + ": node not found: " + name);
			return MStatus::kFailure;
		}
		// A joint leads to its module too, but the command takes a control or the module (D52 section 7.2). A Limb matches the same
		// way: its first six slots are a two-bone limb (D62), and its ribbon rides the joints.
		oOutModule = oNode.hasFn(MFn::kJoint) ? MObject::kNullObj : RigTraversal::getNodeModule(oNode);
		const MTypeId moduleType = oOutModule.isNull() ? MTypeId() : MFnDependencyNode(oOutModule).typeId();
		if (oOutModule.isNull() || (moduleType != TwoBoneIkNode::id && moduleType != LimbNode::id))
		{
			MGlobal::displayError(MString(commandName) + ": " + name + " is not a TwoBoneIk or Limb module or one of its controls.");
			return MStatus::kFailure;
		}
		return MStatus::kSuccess;
	}

	// The match matrix at a slot, pulled from the module. elementByLogicalIndex makes the element the module writes into.
	MMatrix readMatchLocal(const MObject& oModule, const char* attrName, unsigned int slot)
	{
		const MPlug pArray = MFnDependencyNode(oModule).findPlug(attrName, false);
		return DagUtils::getMatrixFromPlug(pArray.elementByLogicalIndex(slot));
	}

	// A matched local into the pose, as channel values in the control's own rotate order and nearest its current rotation. What the channels
	// can't hold is a warning.
	MStatus addMatchedLocal(const MObject& oControl, const MMatrix& mLocal, PoseMap& pose, std::vector<std::string>& warnings)
	{
		ControlMarkup markup;
		std::vector<RigPoseScene::Channel> channels;
		MStatus status = RigTraversal::getControlMarkup(oControl, markup);
		if (status)
		{
			status = RigPoseScene::getControlChannels(oControl, channels);
		}
		CHECK_MSTATUS_AND_RETURN_IT(status);

		std::vector<double> values(channels.size(), 0.0);
		MEulerRotation rotation = RigPoseScene::getControlRotation(oControl);
		const RigPoseScene::LocalFit fit = RigPoseScene::fitLocalToChannels(oControl, channels, mLocal, rotation, values);

		const std::string label = DagUtils::getPartialPathName(oControl).asChar();
		if (fit.hasShear)
		{
			warnings.push_back(label + " has shear in its matched pose, which its channels can't hold. Position and rotation match, the shape may not.");
		}
		if (fit.leavesChannels)
		{
			warnings.push_back(label + ": locked or driven channels kept their values, so the control may not match the pose.");
		}
		PoseChannels& poseChannels = pose[markup.side][markup.usage];
		for (const std::size_t i : fit.written)
		{
			poseChannels.emplace_back(channels[i].info.name, RigPoseScene::toUiUnits(channels[i], values[i]));
		}
		return MStatus::kSuccess;
	}

	// A foot that follows the limb's blend (D74), matched as the limb is: its toe FK control from fkMatchLocal, and toeTap on the foot control
	// from ikMatchToeTap, with the foot's other channels kept. Adds the foot's controls to the scope. The foot's matchWarnings are its IK
	// side's, so they are reported only for a switch toward IK.
	MStatus addMatchedFoot(const MObject& oFoot, double blend, RigControlScope& scope, PoseMap& pose, std::vector<std::string>& warnings)
	{
		const MString footName = MFnDependencyNode(oFoot).name();
		MObjectArray oFootControls;
		MStatus status = RigTraversal::getModuleControls(oFoot, oFootControls);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		if (oFootControls.length() != ReverseFootSlot::ControlCount)
		{
			warnings.push_back(std::string(footName.asChar()) + " has " + std::to_string(oFootControls.length()) + " controls, not the "
				+ std::to_string(ReverseFootSlot::ControlCount) + " a ReverseFoot has, so its toe was not matched.");
			return MStatus::kSuccess;
		}
		for (unsigned int i = 0; i < oFootControls.length(); ++i)
		{
			scope.oControls.append(oFootControls[i]);
		}

		status = addMatchedLocal(oFootControls[ReverseFootSlot::ToeFk], readMatchLocal(oFoot, RigAttr::FkMatchLocal, ReverseFootSlot::ToeFk), pose, warnings);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		const MObject& oFootControl = oFootControls[ReverseFootSlot::Foot];
		ControlMarkup markup;
		std::vector<RigPoseScene::Channel> channels;
		status = RigTraversal::getControlMarkup(oFootControl, markup);
		if (status)
		{
			status = RigPoseScene::getControlChannels(oFootControl, channels);
		}
		CHECK_MSTATUS_AND_RETURN_IT(status);
		const auto toeTap = std::find_if(channels.begin(), channels.end(), [](const RigPoseScene::Channel& channel)
		{
			return channel.info.name == RigAttr::ToeTap;
		});
		const MFnDependencyNode fnFoot(oFoot);
		if (toeTap != channels.end())
		{
			const double radians = fnFoot.findPlug(RigAttr::IkMatchToeTap, false).asMAngle().asRadians();
			pose[markup.side][markup.usage].emplace_back(RigAttr::ToeTap, RigPoseScene::toUiUnits(*toeTap, radians));
		}
		else
		{
			warnings.push_back(DagUtils::getPartialPathName(oFootControl).asChar() + std::string(": ") + RigAttr::ToeTap
				+ " is locked or driven, so the IK toe was not matched.");
		}

		if (blend > 0.0)
		{
			std::vector<std::string> footWarnings;
			std::string error;
			const MString warningText = fnFoot.findPlug(RigAttr::MatchWarnings, false).asString();
			if (!JsonUtils::readJsonString(std::string(warningText.asChar()), footWarnings, error))
			{
				MGlobal::displayError(footName + "'s " + RigAttr::MatchWarnings + " is not a JSON list: " + error.c_str());
				return MStatus::kFailure;
			}
			for (const std::string& warning : footWarnings)
			{
				warnings.push_back(std::string(footName.asChar()) + ": " + warning);
			}
		}
		return MStatus::kSuccess;
	}
}

const char* SetRigIkFkBlendCmd::aCommandString = "setRigIkFkBlend";

void* SetRigIkFkBlendCmd::creator()
{
	return new SetRigIkFkBlendCmd();
}

bool SetRigIkFkBlendCmd::isUndoable() const
{
	return true;
}

MSyntax SetRigIkFkBlendCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-b", kBlendFlag, MSyntax::kDouble));
	syntax.setObjectType(MSyntax::kStringObjects, 1, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus SetRigIkFkBlendCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError(MString("Error parsing ") + aCommandString + " arguments.");
		return status;
	}

	MString objectName;
	status = CommandUtils::readSingleObject(argData, aCommandString, "control or module", objectName);
	if (!status)
	{
		return status;
	}

	double blend = 1.0;
	if (argData.isFlagSet(kBlendFlag))
	{
		status = argData.getFlagArgument(kBlendFlag, 0, blend);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	if (!(blend >= 0.0 && blend <= 1.0))
	{
		MGlobal::displayError(MString(aCommandString) + ": " + kBlendFlag + " is " + blend + ", and must be from 0 (FK) to 1 (IK).");
		return MStatus::kInvalidParameter;
	}

	MObject oModule;
	status = readLimbModule(objectName, aCommandString, oModule);
	if (!status)
	{
		return status;
	}
	RigControlScope scope;
	scope.oRigRoot = RigTraversal::findModuleRigRoot(oModule);
	if (scope.oRigRoot.isNull())
	{
		MGlobal::displayError(MString(aCommandString) + ": " + MFnDependencyNode(oModule).name() + " is not connected to a rig root.");
		return MStatus::kFailure;
	}
	status = RigTraversal::getModuleControls(oModule, scope.oControls);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	const bool isLimb = MFnDependencyNode(oModule).typeId() == LimbNode::id;
	if (isLimb ? scope.oControls.length() < TwoBoneIkSlot::Count : scope.oControls.length() != TwoBoneIkSlot::Count)
	{
		MGlobal::displayError(MString(aCommandString) + ": " + MFnDependencyNode(oModule).name() + " has " + scope.oControls.length()
			+ " controls, not the " + (isLimb ? "at least " : "") + TwoBoneIkSlot::Count + " a " + (isLimb ? "Limb" : "TwoBoneIk limb") + " has.");
		return MStatus::kFailure;
	}

	// 1. Pull the match outputs: the control locals that reproduce the pose as it is, on each side.
	std::vector<std::string> warnings;
	std::string error;
	const MString warningText = MFnDependencyNode(oModule).findPlug(RigAttr::MatchWarnings, false).asString();
	if (!JsonUtils::readJsonString(std::string(warningText.asChar()), warnings, error))
	{
		MGlobal::displayError(MString(aCommandString) + ": the module's " + RigAttr::MatchWarnings + " is not a JSON list: " + error.c_str());
		return MStatus::kFailure;
	}

	// 2. Each local to channel values, in the control's own rotate order and nearest its current rotation.
	PoseMap pose;
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::Settings; ++slot)
	{
		const MMatrix mLocal = readMatchLocal(oModule, slot < TwoBoneIkSlot::LimbCount ? RigAttr::FkMatchLocal : RigAttr::IkMatchLocal, slot);
		status = addMatchedLocal(scope.oControls[slot], mLocal, pose, warnings);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}

	// 2b. The feet that follow this limb's blend (D74). Their match outputs are pulled here, before any write, from the pose as it is.
	MObjectArray oFollowers;
	status = RigTraversal::getBlendFollowers(oModule, oFollowers);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	for (unsigned int i = 0; i < oFollowers.length(); ++i)
	{
		if (MFnDependencyNode(oFollowers[i]).typeId() == ReverseFootModuleNode::id)
		{
			status = addMatchedFoot(oFollowers[i], blend, scope, pose, warnings);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
	}

	// 3. The blend, on the settings control.
	{
		const MObject& oSettings = scope.oControls[TwoBoneIkSlot::Settings];
		ControlMarkup markup;
		std::vector<RigPoseScene::Channel> channels;
		status = RigTraversal::getControlMarkup(oSettings, markup);
		if (status)
		{
			status = RigPoseScene::getControlChannels(oSettings, channels);
		}
		CHECK_MSTATUS_AND_RETURN_IT(status);

		const bool canSetBlend = std::any_of(channels.begin(), channels.end(), [](const RigPoseScene::Channel& channel)
		{
			return channel.info.name == RigAttr::IkFkBlend;
		});
		if (canSetBlend)
		{
			pose[markup.side][markup.usage].emplace_back(RigAttr::IkFkBlend, blend);
		}
		else
		{
			warnings.push_back(DagUtils::getPartialPathName(oSettings).asChar() + std::string(": ") + RigAttr::IkFkBlend
				+ " is locked or driven, so the blend was not changed.");
		}
	}

	// One modifier holds every write, so undo is one step. setRigPose skips a channel already at its value.
	MStringArray changedNames;
	status = RigPoseScene::setRigPose(scope, pose, fDGMod, changedNames);
	if (!status)
	{
		return status;
	}
	for (const std::string& warning : warnings)
	{
		MGlobal::displayWarning(MString(aCommandString) + ": " + warning.c_str());
	}

	setResult(changedNames);
	return redoIt();
}

MStatus SetRigIkFkBlendCmd::redoIt()
{
	const MStatus status = fDGMod.doIt();
	if (!status)
	{
		MGlobal::displayError(MString(aCommandString) + " failed to apply: " + status.errorString());
	}
	return status;
}

MStatus SetRigIkFkBlendCmd::undoIt()
{
	return fDGMod.undoIt();
}
