#pragma once

#include <maya/MArgList.h>
#include <maya/MDGModifier.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

// Pose tools on a rig's animation interface (D34). The rig root is always passed by name. The selection is never
// used. Each command sets values like cmds.setAttr and keys nothing, returns the names of the controls it changed,
// and is undoable.

// cmds.zeroRigControls("rig:rigRoot")
//   Every channel of every control goes to its rest (the attribute default), which is the build pose.
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

// cmds.mirrorRigPose("rig:rigRoot", leftToRight=True)
//   Every Left control's values go onto its Right mirror (or Right onto Left with leftToRight=False): copied, or
//   negated for a custom channel that says so. Center controls and the source side keep their values.
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

// cmds.flipRigPose("rig:rigRoot")
//   Left and Right controls swap values, and every Center control is reflected across the world YZ plane.
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
