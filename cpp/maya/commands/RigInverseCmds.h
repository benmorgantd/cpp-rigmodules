#pragma once

#include "maya/shared/RigKeyScene.h"

#include <maya/MArgList.h>
#include <maya/MDGModifier.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

// Set a rig's control values from joints (D41). Both take either one rig root (every control in the rig) or one or more
// controls of one rig (those only), always by name: the selection is never used. They set values on a rig that
// already exists. Both validate everything first and write nothing on any error.

// cmds.poseRigFromJointPose("rig:rigRoot", jointPose=path, sidecar=path, mode="match")
//   jointPose: a JointClip file (JointClipFile.h) with exactly one JointPose. Required.
//   sidecar: a pose file (PoseFile.h) with the control values the joints can't give. Optional.
//   mode: "match", the only mode. Optional.
//   Sets values like loadRigPose and keys nothing. Returns the names of the controls it changed. Undoable.
class PoseRigFromJointPoseCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	MStatus redoIt() override;
	MStatus undoIt() override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;

private:
	MDGModifier fDGMod;
};

// cmds.animateRigFromJointClip("rig:rigRoot", jointClip=path, sidecar=path, mode="match", startFrame=1, endFrame=100)
//   jointClip: a JointClip file. Required.
//   sidecar: an animation sidecar file (AnimationSidecar.h). Optional.
//   mode: "match", the only mode. Optional.
//   startFrame, endFrame: the range of the clip to write, the whole clip by default. Optional.
//   Keys every channel it writes on every frame of the range, all of a channel's keys in one call (RigKeyScene), on the
//   clip's own frame numbers. The clip must run at the scene's frame rate. Returns the names of the controls it keyed.
//   Undoable as one step.
class AnimateRigFromJointClipCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	MStatus redoIt() override;
	MStatus undoIt() override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;

private:
	RigKeyEdit fEdit;
};
