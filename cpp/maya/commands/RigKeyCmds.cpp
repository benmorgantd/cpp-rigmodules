#include "maya/commands/RigKeyCmds.h"
#include "core/RigKeys.h"
#include "maya/shared/RigKeyScene.h"
#include "maya/shared/Utilities.h"

#include <string>

#include <maya/MArgDatabase.h>
#include <maya/MGlobal.h>
#include <maya/MStringArray.h>

namespace
{
	// Code reads flags by long name only. Short names appear only in newSyntax() (D14).
	constexpr const char* kKeysFlag{ "-keys" };
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
