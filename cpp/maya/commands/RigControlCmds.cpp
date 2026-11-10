#include "maya/commands/RigControlCmds.h"
#include "core/Constants.h"
#include "core/ControlChannels.h"
#include "core/ControlMarkup.h"
#include "core/ControlShapeFile.h"
#include "core/RigControlFilter.h"
#include "maya/shared/ControlShapeScene.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/RigTraversal.h"
#include "maya/shared/Utilities.h"

#include <string>
#include <vector>

#include <maya/MArgDatabase.h>
#include <maya/MGlobal.h>
#include <maya/MObjectArray.h>
#include <maya/MStringArray.h>

namespace
{
	// Code reads flags by long name only. Short names appear only in newSyntax() (D14).
	constexpr const char* kModuleTypeFlag{ "-moduleType" };
	constexpr const char* kSideFlag{ "-side" };
	constexpr const char* kUsageFlag{ "-usage" };
	constexpr const char* kFileFlag{ "-file" };
	constexpr const char* kLeftToRightFlag{ "-leftToRight" };
}

// getRigControls ---------------------------------------------------------------------------------
const char* GetRigControlsCmd::aCommandString = "getRigControls";

void* GetRigControlsCmd::creator()
{
	return new GetRigControlsCmd();
}

bool GetRigControlsCmd::isUndoable() const
{
	return false;
}

MSyntax GetRigControlsCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-mt", kModuleTypeFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.makeFlagMultiUse(kModuleTypeFlag));
	CHECK_MSTATUS(syntax.addFlag("-sd", kSideFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.makeFlagMultiUse(kSideFlag));
	CHECK_MSTATUS(syntax.addFlag("-us", kUsageFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.makeFlagMultiUse(kUsageFlag));
	syntax.setObjectType(MSyntax::kStringObjects, 1, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus GetRigControlsCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	MObject oRigRoot;
	status = CommandUtils::readRigRoot(argData, status, aCommandString, oRigRoot);
	if (!status)
	{
		return status;
	}

	RigControlFilter filter;
	CHECK_MSTATUS_AND_RETURN_IT(CommandUtils::readStringList(argData, kModuleTypeFlag, filter.moduleTypes));
	CHECK_MSTATUS_AND_RETURN_IT(CommandUtils::readStringList(argData, kSideFlag, filter.sides));
	CHECK_MSTATUS_AND_RETURN_IT(CommandUtils::readStringList(argData, kUsageFlag, filter.usages));

	MObjectArray oControls;
	status = RigTraversal::getRigControls(oRigRoot, filter, oControls);
	if (!status)
	{
		return status;
	}

	MStringArray names;
	for (unsigned int i = 0; i < oControls.length(); ++i)
	{
		names.append(DagUtils::getPartialPathName(oControls[i]));
	}
	setResult(names);
	return MStatus::kSuccess;
}

// saveRigControlShapes ---------------------------------------------------------------------------
const char* SaveRigControlShapesCmd::aCommandString = "saveRigControlShapes";

void* SaveRigControlShapesCmd::creator()
{
	return new SaveRigControlShapesCmd();
}

bool SaveRigControlShapesCmd::isUndoable() const
{
	return false;
}

MSyntax SaveRigControlShapesCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-f", kFileFlag, MSyntax::kString));
	syntax.setObjectType(MSyntax::kStringObjects, 1, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus SaveRigControlShapesCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	MObject oRigRoot;
	status = CommandUtils::readRigRoot(argData, status, aCommandString, oRigRoot);
	if (!status)
	{
		return status;
	}

	MString path;
	status = CommandUtils::readFilePath(argData, kFileFlag, aCommandString, path);
	if (!status)
	{
		return status;
	}

	ControlShapeMap shapes;
	MStringArray controlNames;
	status = ControlShapeScene::getRigControlShapes(oRigRoot, shapes, controlNames);
	if (!status)
	{
		return status;
	}

	status = CommandUtils::writeTextFile(path, "shape", serializeControlShapes(shapes));
	if (!status)
	{
		return status;
	}

	setResult(controlNames);
	return MStatus::kSuccess;
}

// loadRigControlShapes ---------------------------------------------------------------------------
const char* LoadRigControlShapesCmd::aCommandString = "loadRigControlShapes";

void* LoadRigControlShapesCmd::creator()
{
	return new LoadRigControlShapesCmd();
}

bool LoadRigControlShapesCmd::isUndoable() const
{
	return true;
}

MSyntax LoadRigControlShapesCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-f", kFileFlag, MSyntax::kString));
	syntax.setObjectType(MSyntax::kStringObjects, 1, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus LoadRigControlShapesCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	MObject oRigRoot;
	status = CommandUtils::readRigRoot(argData, status, aCommandString, oRigRoot);
	if (!status)
	{
		return status;
	}

	MString path;
	status = CommandUtils::readFilePath(argData, kFileFlag, aCommandString, path);
	if (!status)
	{
		return status;
	}

	std::string text;
	status = CommandUtils::readTextFile(path, "shape", text);
	if (!status)
	{
		return status;
	}

	// Validate the whole file before queueing anything, so a bad file changes nothing.
	ControlShapeMap shapes;
	ValidationReport report;
	deserializeControlShapes(text, shapes, report);
	if (report.hasErrors())
	{
		ReportUtils::displayReport(report);
		MGlobal::displayError(MString("The shape file failed validation, so nothing was loaded: ") + path);
		return MStatus::kFailure;
	}

	MStringArray appliedNames;
	status = ControlShapeScene::setRigControlShapes(oRigRoot, shapes, fDGMod, appliedNames);
	if (!status)
	{
		return status;
	}

	setResult(appliedNames);
	return redoIt();
}

MStatus LoadRigControlShapesCmd::redoIt()
{
	MStatus status = fDGMod.doIt();
	if (!status)
	{
		MGlobal::displayError(MString("loadRigControlShapes failed to apply: ") + status.errorString());
	}
	return status;
}

MStatus LoadRigControlShapesCmd::undoIt()
{
	return fDGMod.undoIt();
}

// getRigMirrorControl ----------------------------------------------------------------------------
const char* GetRigMirrorControlCmd::aCommandString = "getRigMirrorControl";

void* GetRigMirrorControlCmd::creator()
{
	return new GetRigMirrorControlCmd();
}

bool GetRigMirrorControlCmd::isUndoable() const
{
	return false;
}

MSyntax GetRigMirrorControlCmd::newSyntax()
{
	MSyntax syntax;
	syntax.setObjectType(MSyntax::kStringObjects, 1, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus GetRigMirrorControlCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError(MString("Error parsing ") + aCommandString + " arguments.");
		return status;
	}

	MString controlName;
	status = CommandUtils::readSingleObject(argData, aCommandString, "control", controlName);
	if (!status)
	{
		return status;
	}

	MObject oControl;
	status = RigTraversal::getRigControl(controlName, oControl);
	if (!status)
	{
		return status;
	}

	MObject oMirror;
	status = RigTraversal::getMirrorControl(oControl, oMirror);
	if (!status)
	{
		return status;
	}
	setResult(DagUtils::getPartialPathName(oMirror));
	return MStatus::kSuccess;
}

// getRigControlChannels --------------------------------------------------------------------------
const char* GetRigControlChannelsCmd::aCommandString = "getRigControlChannels";

void* GetRigControlChannelsCmd::creator()
{
	return new GetRigControlChannelsCmd();
}

bool GetRigControlChannelsCmd::isUndoable() const
{
	return false;
}

MSyntax GetRigControlChannelsCmd::newSyntax()
{
	MSyntax syntax;
	syntax.setObjectType(MSyntax::kStringObjects, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus GetRigControlChannelsCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError(MString("Error parsing ") + aCommandString + " arguments.");
		return status;
	}

	MStringArray controlNames;
	status = argData.getObjects(controlNames);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	ControlChannelList result;
	for (unsigned int i = 0; i < controlNames.length(); ++i)
	{
		MObject oControl;
		status = RigTraversal::getRigControl(controlNames[i], oControl);
		if (!status)
		{
			return status;
		}

		std::vector<RigPoseScene::Channel> channels;
		status = RigPoseScene::getControlChannels(oControl, channels);
		if (!status)
		{
			return status;
		}

		std::vector<ControlChannel> infos;
		for (const RigPoseScene::Channel& channel : channels)
		{
			infos.push_back(channel.info);
		}
		result.push_back({ DagUtils::getPartialPathName(oControl).asChar(), infos });
	}

	setResult(MString(serializeControlChannels(result).c_str()));
	return MStatus::kSuccess;
}

// getRigControlInfo -----------------------------------------------------------------------------
const char* GetRigControlInfoCmd::aCommandString = "getRigControlInfo";

void* GetRigControlInfoCmd::creator()
{
	return new GetRigControlInfoCmd();
}

bool GetRigControlInfoCmd::isUndoable() const
{
	return false;
}

MSyntax GetRigControlInfoCmd::newSyntax()
{
	MSyntax syntax;
	syntax.setObjectType(MSyntax::kStringObjects, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus GetRigControlInfoCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError(MString("Error parsing ") + aCommandString + " arguments.");
		return status;
	}

	MStringArray controlNames;
	status = argData.getObjects(controlNames);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	ControlInfoList result;
	for (unsigned int i = 0; i < controlNames.length(); ++i)
	{
		MObject oControl;
		status = RigTraversal::getRigControl(controlNames[i], oControl);
		ControlInfo info;
		if (status)
		{
			status = RigTraversal::getControlInfo(oControl, info);
		}
		if (!status)
		{
			return status;
		}
		result.push_back({ DagUtils::getPartialPathName(oControl).asChar(), info });
	}

	setResult(MString(serializeControlInfo(result).c_str()));
	return MStatus::kSuccess;
}

// mirrorRigControlShapes -------------------------------------------------------------------------
const char* MirrorRigControlShapesCmd::aCommandString = "mirrorRigControlShapes";

void* MirrorRigControlShapesCmd::creator()
{
	return new MirrorRigControlShapesCmd();
}

bool MirrorRigControlShapesCmd::isUndoable() const
{
	return true;
}

MSyntax MirrorRigControlShapesCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-ltr", kLeftToRightFlag, MSyntax::kBoolean));
	syntax.setObjectType(MSyntax::kStringObjects, 1, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus MirrorRigControlShapesCmd::doIt(const MArgList& args)
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
	status = ControlShapeScene::mirrorRigControlShapes(oRigRoot, leftToRight ? SideNames::Left : SideNames::Right, fDGMod, changedNames);
	if (!status)
	{
		return status;
	}

	setResult(changedNames);
	return redoIt();
}

MStatus MirrorRigControlShapesCmd::redoIt()
{
	MStatus status = fDGMod.doIt();
	if (!status)
	{
		MGlobal::displayError(MString("mirrorRigControlShapes failed to apply: ") + status.errorString());
	}
	return status;
}

MStatus MirrorRigControlShapesCmd::undoIt()
{
	return fDGMod.undoIt();
}
