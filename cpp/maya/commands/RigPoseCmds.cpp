#include "maya/commands/RigPoseCmds.h"
#include "core/Constants.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/Utilities.h"

#include <maya/MArgDatabase.h>
#include <maya/MGlobal.h>
#include <maya/MStringArray.h>

namespace
{
	// Code reads flags by long name only. Short names appear only in newSyntax() (D14).
	constexpr const char* kLeftToRightFlag{ "-leftToRight" };

	// The syntax of a command that takes only the rig root.
	MSyntax rigRootSyntax()
	{
		MSyntax syntax;
		syntax.setObjectType(MSyntax::kStringObjects, 1, 1);
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
	return rigRootSyntax();
}

MStatus ZeroRigControlsCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	MObject oRigRoot;
	status = CommandUtils::readRigRoot(argData, status, aCommandString, oRigRoot);
	if (!status)
	{
		return status;
	}

	MStringArray changedNames;
	status = RigPoseScene::zeroRigControls(oRigRoot, fDGMod, changedNames);
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
	MSyntax syntax = rigRootSyntax();
	CHECK_MSTATUS(syntax.addFlag("-ltr", kLeftToRightFlag, MSyntax::kBoolean));
	return syntax;
}

MStatus MirrorRigPoseCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	MObject oRigRoot;
	status = CommandUtils::readRigRoot(argData, status, aCommandString, oRigRoot);
	if (!status)
	{
		return status;
	}

	bool leftToRight = true;
	if (argData.isFlagSet(kLeftToRightFlag))
	{
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kLeftToRightFlag, 0, leftToRight));
	}

	MStringArray changedNames;
	status = RigPoseScene::mirrorRigPose(oRigRoot, leftToRight ? SideNames::Left : SideNames::Right, fDGMod, changedNames);
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
	return rigRootSyntax();
}

MStatus FlipRigPoseCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	MObject oRigRoot;
	status = CommandUtils::readRigRoot(argData, status, aCommandString, oRigRoot);
	if (!status)
	{
		return status;
	}

	MStringArray changedNames;
	status = RigPoseScene::flipRigPose(oRigRoot, fDGMod, changedNames);
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
