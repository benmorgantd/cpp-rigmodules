#include "maya/commands/RigDataFileCmds.h"
#include "core/Constants.h"
#include "core/LibraryFile.h"
#include "maya/shared/RigDataFileScene.h"
#include "maya/shared/Utilities.h"

#include <string>
#include <utility>
#include <vector>

#include <maya/MArgDatabase.h>
#include <maya/MGlobal.h>

namespace
{
	// Code reads flags by long name only. Short names appear only in newSyntax().
	constexpr const char* kFileFlag{ "-file" };
	constexpr const char* kMetaFlag{ "-meta" };
}

// getRigDataFileInfo -----------------------------------------------------------------------------
const char* GetRigDataFileInfoCmd::aCommandString = "getRigDataFileInfo";

void* GetRigDataFileInfoCmd::creator()
{
	return new GetRigDataFileInfoCmd();
}

bool GetRigDataFileInfoCmd::isUndoable() const
{
	return false;
}

MSyntax GetRigDataFileInfoCmd::newSyntax()
{
	MSyntax syntax;
	syntax.setObjectType(MSyntax::kStringObjects, 1, 1);
	syntax.useSelectionAsDefault(false);
	CHECK_MSTATUS(syntax.addFlag("-f", kFileFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.makeFlagMultiUse(kFileFlag));
	return syntax;
}

MStatus GetRigDataFileInfoCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	MObject oRigRoot;
	status = CommandUtils::readRigRoot(argData, status, aCommandString, oRigRoot);
	std::vector<std::string> files;
	if (status)
	{
		status = CommandUtils::readStringList(argData, kFileFlag, files);
	}
	if (!status)
	{
		return status;
	}

	// One bad file is that file's answer, not the command's failure: a library listing has to survive a broken file.
	nlohmann::ordered_json result = nlohmann::ordered_json::array();
	for (const std::string& file : files)
	{
		std::string text;
		ValidationReport report;
		if (!CommandUtils::readTextFile(file.c_str(), "data", text))
		{
			report.addError("file", "could not be read");
		}

		DataFileSummary summary;
		if (!report.hasErrors())
		{
			summarizeDataFile(text, summary, report);
		}
		std::vector<std::pair<std::string, std::string>> unmatched;
		if (!report.hasErrors())
		{
			status = RigDataFileScene::findUnmatchedControls(oRigRoot, summary.controls, unmatched);
			if (!status)
			{
				return status;
			}
		}
		result.push_back(report.hasErrors() ? dataFileErrorToJson(file, report) : dataFileInfoToJson(file, summary, unmatched));
	}

	setResult(MString(result.dump().c_str()));
	return MStatus::kSuccess;
}

// setRigDataFileMeta -----------------------------------------------------------------------------
const char* SetRigDataFileMetaCmd::aCommandString = "setRigDataFileMeta";

void* SetRigDataFileMetaCmd::creator()
{
	return new SetRigDataFileMetaCmd();
}

bool SetRigDataFileMetaCmd::isUndoable() const
{
	return false;
}

MSyntax SetRigDataFileMetaCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-f", kFileFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-mt", kMetaFlag, MSyntax::kString));
	return syntax;
}

MStatus SetRigDataFileMetaCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError(MString("Error parsing ") + aCommandString + " arguments.");
		return status;
	}

	MString path;
	status = CommandUtils::readFilePath(argData, kFileFlag, aCommandString, path);
	if (!status)
	{
		return status;
	}
	if (!argData.isFlagSet(kMetaFlag))
	{
		MGlobal::displayError(MString(aCommandString) + " needs " + kMetaFlag + ".");
		return MStatus::kInvalidParameter;
	}
	MString editText;
	CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kMetaFlag, 0, editText));

	std::string text;
	status = CommandUtils::readTextFile(path, "data", text);
	if (!status)
	{
		return status;
	}

	std::string edited;
	ValidationReport report;
	editDataFileMeta(text, editText.asChar(), edited, report);
	if (report.hasErrors())
	{
		ReportUtils::displayReport(report);
		MGlobal::displayError(MString(aCommandString) + " failed, so the file is unchanged: " + path);
		return MStatus::kFailure;
	}

	return CommandUtils::writeTextFile(path, "data", edited);
}
