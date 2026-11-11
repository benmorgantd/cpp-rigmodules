#pragma once

#include <maya/MArgList.h>
#include <maya/MDGModifier.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

// Pose tools on a rig's animation interface (D34). Each takes either one rig root (every control in the rig) or one or
// more controls of one rig (those only), always by name: the selection is never used. Each command sets values like
// cmds.setAttr and keys nothing, returns the names of the controls it changed, and is undoable.

// cmds.zeroRigControls("rig:rigRoot") or cmds.zeroRigControls(*controls)
//   Every channel of each control goes to its rest (the attribute default), which is the build pose.
class ZeroRigControlsCmd : public MPxCommand
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

// cmds.mirrorRigPose("rig:rigRoot", leftToRight=True) or cmds.mirrorRigPose(*controls, leftToRight=True)
//   Each source side control's values go onto its mirror: copied, or negated for a custom channel that says so.
//   Center controls and the source side keep their values. leftToRight picks the source side. Without it, a rig
//   root mirrors from Left, and controls mirror from their side when they all share one, and from Left otherwise.
class MirrorRigPoseCmd : public MPxCommand
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

// cmds.flipRigPose("rig:rigRoot") or cmds.flipRigPose(*controls)
//   Each Left or Right control swaps values with its mirror (a pair given from both ends swaps once), and each
//   Center control is reflected across the world YZ plane.
class FlipRigPoseCmd : public MPxCommand
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

// cmds.saveRigPose("rig:rigRoot", file=path) or cmds.saveRigPose(*controls, file=path)
//   Writes the channel values of every control to a pose file, keyed by control markup (PoseFile.h). Returns the names
//   of the controls saved. Not undoable: it changes no scene data.
class SaveRigPoseCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;
};

// cmds.loadRigPose("rig:rigRoot", file=path) or cmds.loadRigPose(*controls, file=path)
//   Applies a pose file to the controls, matched by markup. The whole file is checked first, so a bad file changes
//   nothing. A control or channel only one side has is skipped with a warning. Returns the names of the controls
//   that changed. Sets values and keys nothing.
class LoadRigPoseCmd : public MPxCommand
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
