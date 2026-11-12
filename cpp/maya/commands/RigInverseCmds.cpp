#include "maya/commands/RigInverseCmds.h"
#include "core/ControlAnimFile.h"
#include "core/JointClipFile.h"
#include "core/PoseFile.h"
#include "core/RigKeys.h"
#include "maya/shared/RigInverse.h"
#include "maya/shared/RigKeyScene.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <utility>
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

	MStatus readOptionalFrame(const MArgDatabase& argData, const char* flag, std::optional<int>& outFrame)
	{
		if (!argData.isFlagSet(flag))
		{
			return MStatus::kSuccess;
		}
		int frame = 0;
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(flag, 0, frame));
		outFrame = frame;
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

	std::optional<int> startFrame;
	std::optional<int> endFrame;
	status = readOptionalFrame(argData, kStartFrameFlag, startFrame);
	if (status)
	{
		status = readOptionalFrame(argData, kEndFrameFlag, endFrame);
	}
	if (!status)
	{
		return status;
	}
	if (startFrame.has_value() && endFrame.has_value() && *startFrame > *endFrame)
	{
		MGlobal::displayError(MString(aCommandString) + ": startFrame " + *startFrame + " is after endFrame " + *endFrame + ".");
		return MStatus::kInvalidParameter;
	}

	// The sidecar is a ControlAnim file: the control data the joints can't give (D42).
	ControlAnim sidecar;
	if (hasSidecar)
	{
		std::string text;
		status = CommandUtils::readTextFile(sidecarPath, "ControlAnim", text);
		if (!status)
		{
			return status;
		}
		ValidationReport report;
		deserializeControlAnim(text, sidecar, report);
		if (report.hasErrors())
		{
			ReportUtils::displayReport(report);
			MGlobal::displayError(MString("The sidecar failed validation, so nothing was changed: ") + sidecarPath);
			return MStatus::kFailure;
		}
	}

	JointClip clip;
	status = readJointClip(jointClipPath, clip);
	if (!status)
	{
		return status;
	}

	// Keys land on the clip's own frame numbers, so the clip must run at the scene's rate (a resample is not built).
	const double sceneFrameRate = TimeUtils::getSceneFrameRate();
	if (!TimeUtils::isSameFrameRate(clip.frameRate, sceneFrameRate))
	{
		MGlobal::displayError(MString(aCommandString) + ": the JointClip runs at " + clip.frameRate + " fps and the scene at " + sceneFrameRate
			+ " fps. Change the scene's time unit, or capture the clip again at the scene's rate.");
		return MStatus::kInvalidParameter;
	}
	if (hasSidecar && !TimeUtils::isSameFrameRate(sidecar.frameRate, clip.frameRate))
	{
		MGlobal::displayError(MString(aCommandString) + ": the sidecar runs at " + sidecar.frameRate + " fps and the JointClip at " + clip.frameRate + " fps.");
		return MStatus::kInvalidParameter;
	}

	const int clipFirst = clip.startFrame;
	const int clipLast = clip.startFrame + static_cast<int>(clip.poses.size()) - 1;
	const int first = startFrame.value_or(clipFirst);
	const int last = endFrame.value_or(clipLast);
	if (first > last || first < clipFirst || last > clipLast)
	{
		MGlobal::displayError(MString(aCommandString) + ": frames " + first + " to " + last + " are not inside the JointClip's frames " + clipFirst + " to " + clipLast + ".");
		return MStatus::kInvalidParameter;
	}

	JointClip range = clip;
	range.poses.assign(clip.poses.begin() + (first - clipFirst), clip.poses.begin() + (last - clipFirst) + 1);

	// The sidecar's curves evaluated on the clip's frames, by scratch curves that leave the scene alone.
	std::vector<PoseMap> sidecars(range.poses.size());
	if (hasSidecar)
	{
		if (sidecar.startFrame > first || sidecar.endFrame < last)
		{
			MGlobal::displayWarning(MString(aCommandString) + ": the sidecar covers frames " + sidecar.startFrame + " to " + sidecar.endFrame
				+ ", and the restore frames " + first + " to " + last + ". Outside its range its curves hold their first and last values.");
		}
		status = RigKeyScene::sampleRigKeys(scope.oRigRoot, sidecar.controls, first, last, sidecars);
		if (!status)
		{
			return status;
		}
	}

	std::vector<PoseMap> poses;
	RigInverse::ChannelOrigins origins;
	status = RigInverse::solve(scope.oRigRoot, range, sidecars, poses, &origins);
	if (!status)
	{
		return status;
	}

	// A channel the joints gave is keyed on every frame, all of a channel's keys in one call. A channel only the sidecar
	// gave keeps the sidecar's own keys, with their tangents, inside the range. A channel nothing gave is not keyed, so a
	// control with no joints and no sidecar keeps its keys.
	RigKeys keys;
	for (std::size_t p = 0; p < poses.size(); ++p)
	{
		for (const auto& [side, usages] : poses[p])
		{
			for (const auto& [usage, channels] : usages)
			{
				const auto itSide = origins.derived.find(side);
				if (itSide == origins.derived.end() || itSide->second.find(usage) == itSide->second.end())
				{
					continue;
				}
				const std::set<std::string>& derived = itSide->second.at(usage);
				for (const auto& [name, value] : channels)
				{
					if (derived.count(name) > 0)
					{
						keys[side][usage][name].keys.push_back(Key{ static_cast<double>(first) + static_cast<double>(p), value, {}, {} });
					}
				}
			}
		}
	}
	for (const auto& [side, usages] : origins.supplied)
	{
		for (const auto& [usage, channels] : usages)
		{
			for (const std::string& name : channels)
			{
				ChannelCurve curve = sidecar.controls.at(side).at(usage).at(name);
				curve.keys.erase(std::remove_if(curve.keys.begin(), curve.keys.end(), [first, last](const Key& key)
				{
					return key.frame < first || key.frame > last;
				}), curve.keys.end());
				if (!curve.keys.empty())
				{
					keys[side][usage][name] = curve;
				}
			}
		}
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

bool AnimateRigFromJointClipCmd::isUndoable() const
{
	return true;
}

MStatus AnimateRigFromJointClipCmd::redoIt()
{
	return fEdit.redo();
}

MStatus AnimateRigFromJointClipCmd::undoIt()
{
	return fEdit.undo();
}
