#include "maya/commands/RigPoseCmds.h"
#include "core/Constants.h"
#include "core/PoseBlend.h"
#include "core/PoseFile.h"
#include "maya/shared/RigDataFileScene.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/Utilities.h"

#include <cmath>
#include <string>

#include <maya/MArgDatabase.h>
#include <maya/MGlobal.h>
#include <maya/MStringArray.h>

namespace
{
	// Code reads flags by long name only. Short names appear only in newSyntax() (D14).
	constexpr const char* kLeftToRightFlag{ "-leftToRight" };
	constexpr const char* kFileFlag{ "-file" };
	constexpr const char* kMetaFlag{ "-meta" };
	constexpr const char* kMirrorFlag{ "-mirror" };
	constexpr const char* kBlendFlag{ "-blend" };
	constexpr const char* kBlendReferenceFlag{ "-blendReference" };

	// The syntax of a command that takes a control scope, with the pose file flag.
	MSyntax fileScopeSyntax()
	{
		MSyntax syntax = CommandUtils::controlScopeSyntax();
		CHECK_MSTATUS(syntax.addFlag("-f", kFileFlag, MSyntax::kString));
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

	// Reads and validates a whole pose file before anything is queued, so a bad file changes nothing.
	MStatus readPoseFile(const MString& path, PoseMap& outPose, LibraryMeta* pMeta = nullptr)
	{
		std::string text;
		MStatus status = CommandUtils::readTextFile(path, "pose", text);
		if (!status)
		{
			return status;
		}

		ValidationReport report;
		deserializePose(text, outPose, report, pMeta);
		if (report.hasErrors())
		{
			ReportUtils::displayReport(report);
			MGlobal::displayError(MString("The pose file failed validation, so nothing was loaded: ") + path);
			return MStatus::kFailure;
		}
		return MStatus::kSuccess;
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
	return CommandUtils::controlScopeSyntax();
}

MStatus ZeroRigControlsCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	RigControlScope scope;
	status = CommandUtils::readControlScope(argData, status, aCommandString, scope);
	if (!status)
	{
		return status;
	}

	MStringArray changedNames;
	status = RigPoseScene::zeroRigControls(scope, fDGMod, changedNames);
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
	MSyntax syntax = CommandUtils::controlScopeSyntax();
	CHECK_MSTATUS(syntax.addFlag("-ltr", kLeftToRightFlag, MSyntax::kBoolean));
	return syntax;
}

MStatus MirrorRigPoseCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	RigControlScope scope;
	status = CommandUtils::readControlScope(argData, status, aCommandString, scope);
	if (!status)
	{
		return status;
	}

	// Without the flag, the scene function picks the source side (see RigPoseScene::mirrorRigPose).
	std::string sourceSide;
	if (argData.isFlagSet(kLeftToRightFlag))
	{
		bool leftToRight = true;
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kLeftToRightFlag, 0, leftToRight));
		sourceSide = leftToRight ? SideNames::Left : SideNames::Right;
	}

	MStringArray changedNames;
	status = RigPoseScene::mirrorRigPose(scope, sourceSide, fDGMod, changedNames);
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
	return CommandUtils::controlScopeSyntax();
}

MStatus FlipRigPoseCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	RigControlScope scope;
	status = CommandUtils::readControlScope(argData, status, aCommandString, scope);
	if (!status)
	{
		return status;
	}

	MStringArray changedNames;
	status = RigPoseScene::flipRigPose(scope, fDGMod, changedNames);
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

// saveRigPose ------------------------------------------------------------------------------------
const char* SaveRigPoseCmd::aCommandString = "saveRigPose";

void* SaveRigPoseCmd::creator()
{
	return new SaveRigPoseCmd();
}

bool SaveRigPoseCmd::isUndoable() const
{
	return false;
}

MSyntax SaveRigPoseCmd::newSyntax()
{
	MSyntax syntax = fileScopeSyntax();
	CHECK_MSTATUS(syntax.addFlag("-mt", kMetaFlag, MSyntax::kString));
	return syntax;
}

MStatus SaveRigPoseCmd::doIt(const MArgList& args)
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

	PoseMap pose;
	MStringArray controlNames;
	status = RigPoseScene::getRigPose(scope, pose, controlNames);
	if (!status)
	{
		return status;
	}

	LibraryMeta meta;
	status = RigDataFileScene::readSaveMeta(argData, kMetaFlag, aCommandString, scope.oRigRoot, meta);
	if (!status)
	{
		return status;
	}

	status = CommandUtils::writeTextFile(path, "pose", serializePose(pose, meta));
	if (!status)
	{
		return status;
	}

	setResult(controlNames);
	return MStatus::kSuccess;
}

// loadRigPose ------------------------------------------------------------------------------------
const char* LoadRigPoseCmd::aCommandString = "loadRigPose";

void* LoadRigPoseCmd::creator()
{
	return new LoadRigPoseCmd();
}

bool LoadRigPoseCmd::isUndoable() const
{
	return true;
}

MSyntax LoadRigPoseCmd::newSyntax()
{
	MSyntax syntax = fileScopeSyntax();
	CHECK_MSTATUS(syntax.addFlag("-m", kMirrorFlag, MSyntax::kBoolean));
	CHECK_MSTATUS(syntax.addFlag("-b", kBlendFlag, MSyntax::kDouble));
	CHECK_MSTATUS(syntax.addFlag("-br", kBlendReferenceFlag, MSyntax::kString));
	return syntax;
}

MStatus LoadRigPoseCmd::doIt(const MArgList& args)
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

	bool isMirrored = false;
	if (argData.isFlagSet(kMirrorFlag))
	{
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kMirrorFlag, 0, isMirrored));
	}

	// Any finite weight from 0 up is valid (D72): the results are clamped to each channel's range, and the UI sets
	// the practical top.
	const bool isBlended = argData.isFlagSet(kBlendFlag);
	double blend = 1.0;
	if (isBlended)
	{
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kBlendFlag, 0, blend));
		if (!std::isfinite(blend) || blend < 0.0)
		{
			MGlobal::displayError(MString(aCommandString) + ": the blend weight must be a finite number from 0 up.");
			return MStatus::kFailure;
		}
	}
	MString referencePath;
	if (argData.isFlagSet(kBlendReferenceFlag))
	{
		if (!isBlended)
		{
			MGlobal::displayError(MString(aCommandString) + ": blendReference needs blend.");
			return MStatus::kFailure;
		}
		status = CommandUtils::readFilePath(argData, kBlendReferenceFlag, aCommandString, referencePath);
		if (!status)
		{
			return status;
		}
	}

	PoseMap pose;
	LibraryMeta fileMeta;
	status = readPoseFile(path, pose, &fileMeta);
	if (!status)
	{
		return status;
	}
	RigDataFileScene::warnIfMarkersDiffer(scope.oRigRoot, fileMeta, aCommandString);

	// The mirror is made on the file's data, before any write, so the rig is written once (D51).
	if (isMirrored)
	{
		PoseMap mirrored;
		status = RigPoseScene::mirrorPoseData(scope, pose, mirrored);
		if (!status)
		{
			return status;
		}
		pose = mirrored;
	}

	// The blend starts from the reference file when there is one, so every call of a slider drag starts from the same
	// pose instead of compounding on the last call's result (D72). Without one it starts from the scope's current pose.
	if (isBlended)
	{
		PoseMap current;
		PoseChannelInfo channels;
		MStringArray readNames;
		status = RigPoseScene::getRigPose(scope, current, readNames, &channels);
		if (!status)
		{
			return status;
		}

		PoseMap reference;
		if (referencePath.length() > 0)
		{
			status = readPoseFile(referencePath, reference);
			if (!status)
			{
				return status;
			}
		}
		else
		{
			reference = current;
		}

		PoseMap blended;
		blendPoses(reference, pose, channels, blend, blended);
		pose = blended;
	}

	MStringArray changedNames;
	status = RigPoseScene::setRigPose(scope, pose, fDGMod, changedNames);
	if (!status)
	{
		return status;
	}

	setResult(changedNames);
	return redoIt();
}

MStatus LoadRigPoseCmd::redoIt()
{
	return applyPose(fDGMod, aCommandString);
}

MStatus LoadRigPoseCmd::undoIt()
{
	return fDGMod.undoIt();
}
