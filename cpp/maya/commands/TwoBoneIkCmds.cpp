#include "maya/commands/TwoBoneIkCmds.h"
#include "core/Constants.h"
#include "core/ControlMarkup.h"
#include "core/JsonUtils.h"
#include "core/PoseFile.h"
#include "maya/modules/TwoBoneIkModule.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/RigTraversal.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <string>
#include <vector>

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
		// A joint leads to its module too, but the command takes a control or the module (D52 section 7.2).
		oOutModule = oNode.hasFn(MFn::kJoint) ? MObject::kNullObj : RigTraversal::getNodeModule(oNode);
		if (oOutModule.isNull() || MFnDependencyNode(oOutModule).typeId() != TwoBoneIkNode::id)
		{
			MGlobal::displayError(MString(commandName) + ": " + name + " is not a TwoBoneIk module or one of its controls.");
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
	if (scope.oControls.length() != TwoBoneIkSlot::Count)
	{
		MGlobal::displayError(MString(aCommandString) + ": " + MFnDependencyNode(oModule).name() + " has " + scope.oControls.length()
			+ " controls, not the " + TwoBoneIkSlot::Count + " a TwoBoneIk limb has.");
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
		const MObject& oControl = scope.oControls[slot];
		ControlMarkup markup;
		std::vector<RigPoseScene::Channel> channels;
		status = RigTraversal::getControlMarkup(oControl, markup);
		if (status)
		{
			status = RigPoseScene::getControlChannels(oControl, channels);
		}
		CHECK_MSTATUS_AND_RETURN_IT(status);

		const MMatrix mLocal = readMatchLocal(oModule, slot < TwoBoneIkSlot::LimbCount ? RigAttr::FkMatchLocal : RigAttr::IkMatchLocal, slot);
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
