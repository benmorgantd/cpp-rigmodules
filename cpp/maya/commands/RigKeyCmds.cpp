#include "maya/commands/RigKeyCmds.h"
#include "core/ControlAnimFile.h"
#include "core/RigKeys.h"
#include "maya/shared/RigKeyScene.h"
#include "maya/shared/Utilities.h"

#include <string>
#include <vector>

#include <maya/MAnimControl.h>
#include <maya/MArgDatabase.h>
#include <maya/MGlobal.h>
#include <maya/MStringArray.h>
#include <maya/MTime.h>

namespace
{
	// Code reads flags by long name only. Short names appear only in newSyntax() (D14).
	constexpr const char* kKeysFlag{ "-keys" };
	constexpr const char* kFileFlag{ "-file" };
	constexpr const char* kStartFrameFlag{ "-startFrame" };
	constexpr const char* kEndFrameFlag{ "-endFrame" };
	constexpr const char* kOffsetFlag{ "-offset" };
	constexpr const char* kMirrorFlag{ "-mirror" };
}

// getRigKeys -------------------------------------------------------------------------------------
const char* GetRigKeysCmd::aCommandString = "getRigKeys";

void* GetRigKeysCmd::creator()
{
	return new GetRigKeysCmd();
}

bool GetRigKeysCmd::isUndoable() const
{
	return false;
}

MSyntax GetRigKeysCmd::newSyntax()
{
	return CommandUtils::controlScopeSyntax();
}

MStatus GetRigKeysCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	RigControlScope scope;
	status = CommandUtils::readControlScope(argData, status, aCommandString, scope);
	if (!status)
	{
		return status;
	}

	RigKeys keys;
	MStringArray controlNames;
	status = RigKeyScene::getRigKeys(scope, keys, controlNames);
	if (!status)
	{
		return status;
	}

	setResult(MString(serializeRigKeys(keys).c_str()));
	return MStatus::kSuccess;
}

// setRigKeys -------------------------------------------------------------------------------------
const char* SetRigKeysCmd::aCommandString = "setRigKeys";

void* SetRigKeysCmd::creator()
{
	return new SetRigKeysCmd();
}

bool SetRigKeysCmd::isUndoable() const
{
	return true;
}

MSyntax SetRigKeysCmd::newSyntax()
{
	MSyntax syntax = CommandUtils::controlScopeSyntax();
	CHECK_MSTATUS(syntax.addFlag("-k", kKeysFlag, MSyntax::kString));
	return syntax;
}

MStatus SetRigKeysCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	RigControlScope scope;
	status = CommandUtils::readControlScope(argData, status, aCommandString, scope);
	if (!status)
	{
		return status;
	}

	MString text;
	if (!argData.isFlagSet(kKeysFlag) || argData.getFlagArgument(kKeysFlag, 0, text) != MStatus::kSuccess || text.length() == 0)
	{
		MGlobal::displayError(MString(aCommandString) + " needs " + kKeysFlag + ".");
		return MStatus::kInvalidParameter;
	}

	// Validate the whole string before queueing anything, so a bad one changes nothing.
	RigKeys keys;
	ValidationReport report;
	deserializeRigKeys(text.asChar(), keys, report);
	if (report.hasErrors())
	{
		ReportUtils::displayReport(report);
		MGlobal::displayError("The keys failed validation, so nothing was keyed.");
		return MStatus::kFailure;
	}

	MStringArray changedNames;
	status = RigKeyScene::setRigKeys(scope, keys, fEdit, changedNames);
	if (!status)
	{
		return status;
	}

	setResult(changedNames);
	return MStatus::kSuccess;
}

MStatus SetRigKeysCmd::redoIt()
{
	return fEdit.redo();
}

MStatus SetRigKeysCmd::undoIt()
{
	return fEdit.undo();
}

// saveRigAnimation -------------------------------------------------------------------------------
const char* SaveRigAnimationCmd::aCommandString = "saveRigAnimation";

void* SaveRigAnimationCmd::creator()
{
	return new SaveRigAnimationCmd();
}

bool SaveRigAnimationCmd::isUndoable() const
{
	return false;
}

MSyntax SaveRigAnimationCmd::newSyntax()
{
	MSyntax syntax = CommandUtils::controlScopeSyntax();
	CHECK_MSTATUS(syntax.addFlag("-f", kFileFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-sf", kStartFrameFlag, MSyntax::kDouble));
	CHECK_MSTATUS(syntax.addFlag("-ef", kEndFrameFlag, MSyntax::kDouble));
	return syntax;
}

MStatus SaveRigAnimationCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	RigControlScope scope;
	MString path;
	status = CommandUtils::readControlScope(argData, status, aCommandString, scope);
	if (status)
	{
		status = CommandUtils::readFilePath(argData, kFileFlag, aCommandString, path);
	}
	if (!status)
	{
		return status;
	}

	// The range defaults to the playback range, like the time slider.
	ControlAnim anim;
	anim.frameRate = TimeUtils::getSceneFrameRate();
	anim.startFrame = MAnimControl::minTime().as(MTime::uiUnit());
	anim.endFrame = MAnimControl::maxTime().as(MTime::uiUnit());
	if (argData.isFlagSet(kStartFrameFlag))
	{
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kStartFrameFlag, 0, anim.startFrame));
	}
	if (argData.isFlagSet(kEndFrameFlag))
	{
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kEndFrameFlag, 0, anim.endFrame));
	}
	if (anim.endFrame < anim.startFrame)
	{
		MGlobal::displayError(MString(aCommandString) + ": " + kEndFrameFlag + " " + anim.endFrame + " is before " + kStartFrameFlag + " " + anim.startFrame + ".");
		return MStatus::kInvalidParameter;
	}

	RigKeys keys;
	MStringArray controlNames;
	status = RigKeyScene::getRigKeys(scope, keys, controlNames);
	if (!status)
	{
		return status;
	}

	// Only the keys inside the range go in, and a channel with none is left out.
	for (auto& [side, usages] : keys)
	{
		for (auto& [usage, channels] : usages)
		{
			for (auto& [channel, curve] : channels)
			{
				std::vector<Key> inRange;
				for (const Key& key : curve.keys)
				{
					if (key.frame >= anim.startFrame && key.frame <= anim.endFrame)
					{
						inRange.push_back(key);
					}
				}
				curve.keys = inRange;
			}
		}
	}
	anim.controls = keys;

	status = CommandUtils::writeTextFile(path, "ControlAnim", serializeControlAnim(anim));
	if (!status)
	{
		return status;
	}

	setResult(controlNames);
	return MStatus::kSuccess;
}

// loadRigAnimation -------------------------------------------------------------------------------
const char* LoadRigAnimationCmd::aCommandString = "loadRigAnimation";

void* LoadRigAnimationCmd::creator()
{
	return new LoadRigAnimationCmd();
}

bool LoadRigAnimationCmd::isUndoable() const
{
	return true;
}

MSyntax LoadRigAnimationCmd::newSyntax()
{
	MSyntax syntax = CommandUtils::controlScopeSyntax();
	CHECK_MSTATUS(syntax.addFlag("-f", kFileFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-o", kOffsetFlag, MSyntax::kDouble));
	CHECK_MSTATUS(syntax.addFlag("-m", kMirrorFlag, MSyntax::kBoolean));
	return syntax;
}

MStatus LoadRigAnimationCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	RigControlScope scope;
	MString path;
	status = CommandUtils::readControlScope(argData, status, aCommandString, scope);
	if (status)
	{
		status = CommandUtils::readFilePath(argData, kFileFlag, aCommandString, path);
	}
	if (!status)
	{
		return status;
	}

	double offset = 0.0;
	bool isMirrored = false;
	if (argData.isFlagSet(kOffsetFlag))
	{
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kOffsetFlag, 0, offset));
	}
	if (argData.isFlagSet(kMirrorFlag))
	{
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kMirrorFlag, 0, isMirrored));
	}

	std::string text;
	status = CommandUtils::readTextFile(path, "ControlAnim", text);
	if (!status)
	{
		return status;
	}

	// Validate the whole file before queueing anything, so a bad file changes nothing.
	ControlAnim anim;
	ValidationReport report;
	deserializeControlAnim(text, anim, report);
	if (report.hasErrors())
	{
		ReportUtils::displayReport(report);
		MGlobal::displayError(MString("The ControlAnim file failed validation, so nothing was keyed: ") + path);
		return MStatus::kFailure;
	}
	if (!TimeUtils::isSameFrameRate(anim.frameRate, TimeUtils::getSceneFrameRate()))
	{
		MGlobal::displayError(MString(aCommandString) + ": the animation runs at " + anim.frameRate + " fps and the scene at "
			+ TimeUtils::getSceneFrameRate() + " fps. Change the scene's time unit first.");
		return MStatus::kInvalidParameter;
	}

	RigKeys keys = anim.controls;
	if (isMirrored)
	{
		status = RigKeyScene::mirrorRigKeys(scope, anim.controls, keys);
		if (!status)
		{
			return status;
		}
	}
	offsetKeys(keys, offset);

	MStringArray changedNames;
	status = RigKeyScene::setRigKeys(scope, keys, fEdit, changedNames);
	if (!status)
	{
		return status;
	}

	setResult(changedNames);
	return MStatus::kSuccess;
}

MStatus LoadRigAnimationCmd::redoIt()
{
	return fEdit.redo();
}

MStatus LoadRigAnimationCmd::undoIt()
{
	return fEdit.undo();
}
