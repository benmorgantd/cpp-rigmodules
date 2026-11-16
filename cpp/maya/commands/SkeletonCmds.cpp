#include "maya/commands/SkeletonCmds.h"
#include "core/Hash64.h"
#include "core/RigTemplateValidation.h"
#include "core/SkeletonFile.h"
#include "maya/shared/SkeletonScene.h"
#include "maya/shared/Utilities.h"

#include <maya/MArgDatabase.h>
#include <maya/MDagPathArray.h>
#include <maya/MGlobal.h>

namespace
{
	// Code reads flags by long name only. Short names appear only in newSyntax() (D14).
	constexpr const char* kFileFlag{ "-file" };
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
