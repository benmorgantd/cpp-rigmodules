#include "maya/commands/RigPoseCmds.h"
#include "core/Constants.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/Utilities.h"

#include <string>

#include <maya/MArgDatabase.h>
#include <maya/MGlobal.h>
#include <maya/MStringArray.h>

namespace
{
	// Code reads flags by long name only. Short names appear only in newSyntax() (D14).
	constexpr const char* kLeftToRightFlag{ "-leftToRight" };

	// The syntax of a command that takes a rig root or one or more controls.
	MSyntax controlScopeSyntax()
	{
		MSyntax syntax;
		syntax.setObjectType(MSyntax::kStringObjects, 1);
		syntax.useSelectionAsDefault(false);
		return syntax;
	}

	// Runs the queued writes, reporting a failure under the command's name.
	MStatus applyPose(MDGModifier& dgMod, const char* commandName)
	{
		MStatus status = dgMod.doIt();
		if (!status)
		{
			MGlobal::displayError(MString(commandName) + " failed to apply: " + status.errorString());
		}
		return status;
	}
}

// zeroRigControls --------------------------------------------------------------------------------
const char* ZeroRigControlsCmd::aCommandString = "zeroRigControls";

void* ZeroRigControlsCmd::creator()
{
	return new ZeroRigControlsCmd();
}

bool ZeroRigControlsCmd::isUndoable() const
{
	return true;
}

MSyntax ZeroRigControlsCmd::newSyntax()
{
	return controlScopeSyntax();
}

MStatus ZeroRigControlsCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	RigControlScope scope;
	status = CommandUtils::readControlScope(argData, status, aCommandString, scope);
	if (!status)
	{
		return status;
	}

	MStringArray changedNames;
	status = RigPoseScene::zeroRigControls(scope, fDGMod, changedNames);
	if (!status)
	{
		return status;
	}

	setResult(changedNames);
	return redoIt();
}

MStatus ZeroRigControlsCmd::redoIt()
{
	return applyPose(fDGMod, aCommandString);
}

MStatus ZeroRigControlsCmd::undoIt()
{
	return fDGMod.undoIt();
}

// mirrorRigPose ----------------------------------------------------------------------------------
const char* MirrorRigPoseCmd::aCommandString = "mirrorRigPose";

void* MirrorRigPoseCmd::creator()
{
	return new MirrorRigPoseCmd();
}

bool MirrorRigPoseCmd::isUndoable() const
{
	return true;
}

MSyntax MirrorRigPoseCmd::newSyntax()
{
	MSyntax syntax = controlScopeSyntax();
	CHECK_MSTATUS(syntax.addFlag("-ltr", kLeftToRightFlag, MSyntax::kBoolean));
	return syntax;
}

MStatus MirrorRigPoseCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	RigControlScope scope;
	status = CommandUtils::readControlScope(argData, status, aCommandString, scope);
	if (!status)
	{
		return status;
	}

	// Without the flag, the scene function picks the source side (see RigPoseScene::mirrorRigPose).
	std::string sourceSide;
	if (argData.isFlagSet(kLeftToRightFlag))
	{
		bool leftToRight = true;
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kLeftToRightFlag, 0, leftToRight));
		sourceSide = leftToRight ? SideNames::Left : SideNames::Right;
	}

	MStringArray changedNames;
	status = RigPoseScene::mirrorRigPose(scope, sourceSide, fDGMod, changedNames);
	if (!status)
	{
		return status;
	}

	setResult(changedNames);
	return redoIt();
}

MStatus MirrorRigPoseCmd::redoIt()
{
	return applyPose(fDGMod, aCommandString);
}

MStatus MirrorRigPoseCmd::undoIt()
{
	return fDGMod.undoIt();
}

// flipRigPose ------------------------------------------------------------------------------------
const char* FlipRigPoseCmd::aCommandString = "flipRigPose";

void* FlipRigPoseCmd::creator()
{
	return new FlipRigPoseCmd();
}

bool FlipRigPoseCmd::isUndoable() const
{
	return true;
}

MSyntax FlipRigPoseCmd::newSyntax()
{
	return controlScopeSyntax();
}

MStatus FlipRigPoseCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	RigControlScope scope;
	status = CommandUtils::readControlScope(argData, status, aCommandString, scope);
	if (!status)
	{
		return status;
	}

	MStringArray changedNames;
	status = RigPoseScene::flipRigPose(scope, fDGMod, changedNames);
	if (!status)
	{
		return status;
	}

	setResult(changedNames);
	return redoIt();
}

MStatus FlipRigPoseCmd::redoIt()
{
	return applyPose(fDGMod, aCommandString);
}

MStatus FlipRigPoseCmd::undoIt()
{
	return fDGMod.undoIt();
}
