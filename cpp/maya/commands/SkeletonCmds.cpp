#include "maya/commands/SkeletonCmds.h"
#include "core/Hash64.h"
#include "core/RigTemplateValidation.h"
#include "core/SkeletonFile.h"
#include "core/StringUtils.h"
#include "maya/shared/SkeletonScene.h"
#include "maya/shared/Utilities.h"

#include <maya/MArgDatabase.h>
#include <maya/MDagPathArray.h>
#include <maya/MGlobal.h>
#include <maya/MNamespace.h>
#include <maya/MObjectArray.h>
#include <maya/MStringArray.h>

namespace
{
	// Code reads flags by long name only. Short names appear only in newSyntax().
	constexpr const char* kFileFlag{ "-file" };
	constexpr const char* kNamespaceFlag{ "-namespace" };
}

const char* SaveRigSkeletonCmd::aCommandString = "saveRigSkeleton";

void* SaveRigSkeletonCmd::creator()
{
	return new SaveRigSkeletonCmd();
}

bool SaveRigSkeletonCmd::isUndoable() const
{
	return false;
}

MSyntax SaveRigSkeletonCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-f", kFileFlag, MSyntax::kString));
	syntax.setObjectType(MSyntax::kStringObjects, 0);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus SaveRigSkeletonCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError(MString("Error parsing ") + aCommandString + " arguments.");
		return status;
	}

	MDagPathArray joints;
	status = CommandUtils::readJoints(argData, aCommandString, joints);
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

	Skeleton skeleton;
	ValidationReport report;
	SkeletonScene::readSkeleton(joints, skeleton, report);
	if (report.hasErrors())
	{
		ReportUtils::displayReport(report);
		MGlobal::displayError(MString(aCommandString) + ": nothing was written.");
		return MStatus::kFailure;
	}

	status = CommandUtils::writeTextFile(path, "skeleton", serializeSkeleton(skeleton));
	if (!status)
	{
		return status;
	}

	setResult(MString(Hash64::format(skeletonHash(skeleton)).c_str()));
	return MStatus::kSuccess;
}

// loadRigSkeleton --------------------------------------------------------------------------------
const char* LoadRigSkeletonCmd::aCommandString = "loadRigSkeleton";

void* LoadRigSkeletonCmd::creator()
{
	return new LoadRigSkeletonCmd();
}

bool LoadRigSkeletonCmd::isUndoable() const
{
	return true;
}

MSyntax LoadRigSkeletonCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-f", kFileFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-ns", kNamespaceFlag, MSyntax::kString));
	return syntax;
}

MStatus LoadRigSkeletonCmd::doIt(const MArgList& args)
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

	// Colons at the ends are dropped, so ":rig", "rig:" and "rig" mean the same namespace, and ":" is the root.
	MString namespaceName = MNamespace::currentNamespace();
	if (argData.isFlagSet(kNamespaceFlag))
	{
		status = argData.getFlagArgument(kNamespaceFlag, 0, namespaceName);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	fNamespace = StringUtils::trim(namespaceName.asChar(), ':').c_str();

	std::string text;
	status = CommandUtils::readTextFile(path, "skeleton", text);
	if (!status)
	{
		return status;
	}

	// Validate the whole file before creating anything, so a bad file changes nothing.
	Skeleton skeleton;
	ValidationReport report;
	deserializeSkeleton(text, skeleton, report);
	ReportUtils::displayReport(report);
	if (report.hasErrors())
	{
		MGlobal::displayError(MString("The skeleton file failed validation, so nothing was loaded: ") + path);
		return MStatus::kFailure;
	}

	MObjectArray oJoints;
	fCreatedNamespaces.captureBefore();
	{
		ScopedRigNamespace scopedNamespace(fNamespace);
		status = scopedNamespace.status();
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = SkeletonScene::createSkeleton(skeleton, fDagMod, oJoints);
		if (!status)
		{
			return status;
		}
	}
	fCreatedNamespaces.recordCreated();

	MStringArray names;
	for (unsigned int i = 0; i < oJoints.length(); ++i)
	{
		names.append(DagUtils::getPartialPathName(oJoints[i]));
	}
	setResult(names);
	return MStatus::kSuccess;
}

MStatus LoadRigSkeletonCmd::redoIt()
{
	// Nodes are created relative to the current namespace, so the redo needs it current again.
	ScopedRigNamespace scopedNamespace(fNamespace);
	MStatus status = scopedNamespace.status();
	CHECK_MSTATUS_AND_RETURN_IT(status);
	return fDagMod.doIt();
}

MStatus LoadRigSkeletonCmd::undoIt()
{
	const MStatus status = fDagMod.undoIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);
	fCreatedNamespaces.removeCreated();
	return status;
}
