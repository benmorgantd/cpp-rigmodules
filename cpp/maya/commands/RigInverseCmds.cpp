#include "maya/commands/RigInverseCmds.h"
#include "core/AnimationSidecar.h"
#include "core/JointClipFile.h"
#include "core/PoseFile.h"
#include "maya/shared/RigInverse.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/Utilities.h"

#include <string>
#include <vector>

#include <maya/MArgDatabase.h>
#include <maya/MGlobal.h>
#include <maya/MStringArray.h>

namespace
{
	// Code reads flags by long name only. Short names appear only in newSyntax() (D14).
	constexpr const char* kJointPoseFlag{ "-jointPose" };
	constexpr const char* kJointClipFlag{ "-jointClip" };
	constexpr const char* kSidecarFlag{ "-sidecar" };
	constexpr const char* kModeFlag{ "-mode" };
	constexpr const char* kStartFrameFlag{ "-startFrame" };
	constexpr const char* kEndFrameFlag{ "-endFrame" };

	// mode is optional and only accepts match (D41). Reports anything else.
	MStatus readMode(const MArgDatabase& argData, const char* commandName)
	{
		if (!argData.isFlagSet(kModeFlag))
		{
			return MStatus::kSuccess;
		}
		MString mode;
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kModeFlag, 0, mode));
		if (mode != RigInverse::kMatchMode)
		{
			MGlobal::displayError(MString(commandName) + ": mode '" + mode + "' is not supported. The only mode is '" + RigInverse::kMatchMode + "'.");
			return MStatus::kInvalidParameter;
		}
		return MStatus::kSuccess;
	}

	// The optional sidecar flag's path. outHasSidecar says whether it was given.
	MStatus readSidecarPath(const MArgDatabase& argData, const char* commandName, MString& outPath, bool& outHasSidecar)
	{
		outHasSidecar = argData.isFlagSet(kSidecarFlag);
		if (!outHasSidecar)
		{
			return MStatus::kSuccess;
		}
		if (argData.getFlagArgument(kSidecarFlag, 0, outPath) != MStatus::kSuccess || outPath.length() == 0)
		{
			MGlobal::displayError(MString(commandName) + ": " + kSidecarFlag + " needs a file path.");
			return MStatus::kInvalidParameter;
		}
		return MStatus::kSuccess;
	}

	// Reads and validates a JointClip file whole, so a bad file changes nothing.
	MStatus readJointClip(const MString& path, JointClip& outClip)
	{
		std::string text;
		MStatus status = CommandUtils::readTextFile(path, "JointClip", text);
		if (!status)
		{
			return status;
		}

		ValidationReport report;
		deserializeJointClip(text, outClip, report);
		if (report.hasErrors())
		{
			ReportUtils::displayReport(report);
			MGlobal::displayError(MString("The JointClip file failed validation, so nothing was changed: ") + path);
			return MStatus::kFailure;
		}
		return MStatus::kSuccess;
	}
}

// poseRigFromJointPose ---------------------------------------------------------------------------
const char* PoseRigFromJointPoseCmd::aCommandString = "poseRigFromJointPose";

void* PoseRigFromJointPoseCmd::creator()
{
	return new PoseRigFromJointPoseCmd();
}

bool PoseRigFromJointPoseCmd::isUndoable() const
{
	return true;
}

MSyntax PoseRigFromJointPoseCmd::newSyntax()
{
	MSyntax syntax = CommandUtils::controlScopeSyntax();
	CHECK_MSTATUS(syntax.addFlag("-jp", kJointPoseFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-sc", kSidecarFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-m", kModeFlag, MSyntax::kString));
	return syntax;
}

MStatus PoseRigFromJointPoseCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	RigControlScope scope;
	MString jointPosePath;
	MString sidecarPath;
	bool hasSidecar = false;
	status = CommandUtils::readControlScope(argData, status, aCommandString, scope);
	if (status)
	{
		status = readMode(argData, aCommandString);
	}
	if (status)
	{
		status = CommandUtils::readFilePath(argData, kJointPoseFlag, aCommandString, jointPosePath);
	}
	if (status)
	{
		status = readSidecarPath(argData, aCommandString, sidecarPath, hasSidecar);
	}
	if (!status)
	{
		return status;
	}

	// The sidecar is a plain pose file.
	PoseMap sidecar;
	if (hasSidecar)
	{
		std::string text;
		status = CommandUtils::readTextFile(sidecarPath, "pose", text);
		if (!status)
		{
			return status;
		}
		ValidationReport report;
		deserializePose(text, sidecar, report);
		if (report.hasErrors())
		{
			ReportUtils::displayReport(report);
			MGlobal::displayError(MString("The sidecar failed validation, so nothing was changed: ") + sidecarPath);
			return MStatus::kFailure;
		}
	}

	JointClip clip;
	status = readJointClip(jointPosePath, clip);
	if (!status)
	{
		return status;
	}
	if (clip.poses.size() != 1)
	{
		MGlobal::displayError(MString(aCommandString) + ": the JointPose file must hold exactly one JointPose, not " + static_cast<int>(clip.poses.size()) + ".");
		return MStatus::kFailure;
	}

	std::vector<PoseMap> poses;
	status = RigInverse::solve(scope.oRigRoot, clip, { sidecar }, poses);
	if (!status)
	{
		return status;
	}

	MStringArray changedNames;
	status = RigPoseScene::setRigPose(scope, poses.front(), fDGMod, changedNames);
	if (!status)
	{
		return status;
	}

	setResult(changedNames);
	return redoIt();
}

MStatus PoseRigFromJointPoseCmd::redoIt()
{
	MStatus status = fDGMod.doIt();
	if (!status)
	{
		MGlobal::displayError(MString(aCommandString) + " failed to apply: " + status.errorString());
	}
	return status;
}

MStatus PoseRigFromJointPoseCmd::undoIt()
{
	return fDGMod.undoIt();
}

// animateRigFromJointClip ------------------------------------------------------------------------
const char* AnimateRigFromJointClipCmd::aCommandString = "animateRigFromJointClip";

void* AnimateRigFromJointClipCmd::creator()
{
	return new AnimateRigFromJointClipCmd();
}

MSyntax AnimateRigFromJointClipCmd::newSyntax()
{
	MSyntax syntax = CommandUtils::controlScopeSyntax();
	CHECK_MSTATUS(syntax.addFlag("-jc", kJointClipFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-sc", kSidecarFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-m", kModeFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-sf", kStartFrameFlag, MSyntax::kLong));
	CHECK_MSTATUS(syntax.addFlag("-ef", kEndFrameFlag, MSyntax::kLong));
	return syntax;
}

MStatus AnimateRigFromJointClipCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	RigControlScope scope;
	MString jointClipPath;
	MString sidecarPath;
	bool hasSidecar = false;
	status = CommandUtils::readControlScope(argData, status, aCommandString, scope);
	if (status)
	{
		status = readMode(argData, aCommandString);
	}
	if (status)
	{
		status = CommandUtils::readFilePath(argData, kJointClipFlag, aCommandString, jointClipPath);
	}
	if (status)
	{
		status = readSidecarPath(argData, aCommandString, sidecarPath, hasSidecar);
	}
	if (!status)
	{
		return status;
	}

	if (argData.isFlagSet(kStartFrameFlag) && argData.isFlagSet(kEndFrameFlag))
	{
		int startFrame = 0;
		int endFrame = 0;
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kStartFrameFlag, 0, startFrame));
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kEndFrameFlag, 0, endFrame));
		if (startFrame > endFrame)
		{
			MGlobal::displayError(MString(aCommandString) + ": startFrame " + startFrame + " is after endFrame " + endFrame + ".");
			return MStatus::kInvalidParameter;
		}
	}

	if (hasSidecar)
	{
		std::string text;
		status = CommandUtils::readTextFile(sidecarPath, "animation sidecar", text);
		if (!status)
		{
			return status;
		}
		AnimationSidecar sidecar;
		ValidationReport report;
		deserializeAnimationSidecar(text, sidecar, report);
		if (report.hasErrors())
		{
			ReportUtils::displayReport(report);
			MGlobal::displayError(MString("The animation sidecar failed validation, so nothing was changed: ") + sidecarPath);
			return MStatus::kFailure;
		}
	}

	JointClip clip;
	status = readJointClip(jointClipPath, clip);
	if (!status)
	{
		return status;
	}

	MGlobal::displayError(MString(aCommandString) + " is not built yet (D41 skeleton): it needs the solve and the keyframe command (D34).");
	return MStatus::kNotImplemented;
}
