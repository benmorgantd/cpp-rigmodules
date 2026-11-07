#include "maya/commands/RigControlCmds.h"
#include "core/Constants.h"
#include "core/ControlShapeFile.h"
#include "core/RigControlFilter.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/ControlShapeScene.h"
#include "maya/shared/RigTraversal.h"
#include "maya/shared/Utilities.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <maya/MArgDatabase.h>
#include <maya/MDagPath.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
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

	// The one rig root object every command here takes.
	MStatus readRigRoot(const MArgDatabase& argData, MStatus parseStatus, const char* commandName, MObject& oOutRigRoot)
	{
		if (!parseStatus)
		{
			MGlobal::displayError(MString("Error parsing ") + commandName + " arguments.");
			return parseStatus;
		}

		MStringArray objects;
		MStatus status = argData.getObjects(objects);
		if (!status || objects.length() != 1)
		{
			MGlobal::displayError(MString(commandName) + " takes exactly one rig root.");
			return MStatus::kFailure;
		}
		return RigTraversal::getRigRoot(objects[0], oOutRigRoot);
	}

	MStatus readMultiUseStrings(const MArgDatabase& argData, const char* flag, std::vector<std::string>& outValues)
	{
		const unsigned int uses = argData.numberOfFlagUses(flag);
		for (unsigned int i = 0; i < uses; ++i)
		{
			MArgList flagArgs;
			MStatus status = argData.getFlagArgumentList(flag, i, flagArgs);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			outValues.push_back(flagArgs.asString(0).asChar());
		}
		return MStatus::kSuccess;
	}

	MStatus readFilePath(const MArgDatabase& argData, const char* commandName, MString& outPath)
	{
		if (!argData.isFlagSet(kFileFlag) || argData.getFlagArgument(kFileFlag, 0, outPath) != MStatus::kSuccess || outPath.length() == 0)
		{
			MGlobal::displayError(MString(commandName) + " needs -file.");
			return MStatus::kFailure;
		}
		return MStatus::kSuccess;
	}

	MString controlName(const MObject& oControl)
	{
		MDagPath dagPath;
		MFnDagNode(oControl).getPath(dagPath);
		return dagPath.partialPathName();
	}
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
	status = readRigRoot(argData, status, aCommandString, oRigRoot);
	if (!status)
	{
		return status;
	}

	RigControlFilter filter;
	CHECK_MSTATUS_AND_RETURN_IT(readMultiUseStrings(argData, kModuleTypeFlag, filter.moduleTypes));
	CHECK_MSTATUS_AND_RETURN_IT(readMultiUseStrings(argData, kSideFlag, filter.sides));
	CHECK_MSTATUS_AND_RETURN_IT(readMultiUseStrings(argData, kUsageFlag, filter.usages));

	MObjectArray oControls;
	status = RigTraversal::getRigControls(oRigRoot, filter, oControls);
	if (!status)
	{
		return status;
	}

	MStringArray names;
	for (unsigned int i = 0; i < oControls.length(); ++i)
	{
		names.append(controlName(oControls[i]));
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
	status = readRigRoot(argData, status, aCommandString, oRigRoot);
	if (!status)
	{
		return status;
	}

	MString path;
	status = readFilePath(argData, aCommandString, path);
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

	std::ofstream file(path.asChar(), std::ios::out | std::ios::trunc);
	file << serializeControlShapes(shapes);
	if (!file)
	{
		MGlobal::displayError(MString("Could not write the shape file: ") + path);
		return MStatus::kFailure;
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
	status = readRigRoot(argData, status, aCommandString, oRigRoot);
	if (!status)
	{
		return status;
	}

	MString path;
	status = readFilePath(argData, aCommandString, path);
	if (!status)
	{
		return status;
	}

	std::ifstream file(path.asChar());
	if (!file)
	{
		MGlobal::displayError(MString("Could not read the shape file: ") + path);
		return MStatus::kFailure;
	}
	std::stringstream text;
	text << file.rdbuf();

	// Validate the whole file before queueing anything, so a bad file changes nothing.
	ControlShapeMap shapes;
	ValidationReport report;
	deserializeControlShapes(text.str(), shapes, report);
	if (report.hasErrors())
	{
		for (const std::string& error : report.errors)
		{
			MGlobal::displayError(error.c_str());
		}
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
	MStringArray objects;
	if (!status || argData.getObjects(objects) != MStatus::kSuccess || objects.length() != 1)
	{
		MGlobal::displayError(MString(aCommandString) + " takes exactly one control.");
		return MStatus::kFailure;
	}

	MObject oControl;
	if (DagUtils::getMObjectFromName(objects[0], oControl) != MStatus::kSuccess
		|| MFnDependencyNode(oControl).typeId() != RigControlNode::id)
	{
		MGlobal::displayError(MString("Not a rig control: ") + objects[0]);
		return MStatus::kFailure;
	}

	MObject oMirror;
	status = RigTraversal::getMirrorControl(oControl, oMirror);
	if (!status)
	{
		return status;
	}
	setResult(controlName(oMirror));
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
	status = readRigRoot(argData, status, aCommandString, oRigRoot);
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
