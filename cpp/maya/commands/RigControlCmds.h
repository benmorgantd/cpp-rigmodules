#pragma once

#include <maya/MArgList.h>
#include <maya/MDGModifier.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

// The rig root is always passed by name. The selection is never used.

// cmds.getRigControls("rig:rigRoot", moduleType=["FkChain"], side=["Left"], usage=["upperArm"])
//   -> control names. Each flag is optional and multi-use. Values of one flag are alternatives, and every
//   flag given must match. Query only, so not undoable.
class GetRigControlsCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;
};

// cmds.saveRigControlShapes("rig:rigRoot", file="C:/assets/hero/shapes.json")
//   -> the names of the controls saved. Writes a file and changes nothing in the scene, so not undoable.
class SaveRigControlShapesCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;
};

// cmds.loadRigControlShapes("rig:rigRoot", file="C:/assets/hero/shapes.json")
//   -> the names of the controls changed. Undoable. A file that fails validation changes nothing.
class LoadRigControlShapesCmd : public MPxCommand
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

// cmds.getRigMirrorControl("rig:arm_ctrl_l") -> "arm_ctrl_r"
//   The control in the same rig with the same usage and the other side, found by control markup (D28).
//   A Center control returns itself. Query only, so not undoable.
class GetRigMirrorControlCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;
};

// cmds.mirrorRigControlShapes("rig:rigRoot", leftToRight=True)
//   -> the names of the controls changed. Reflects every Left control's shape across world X = 0 onto its Right
//   mirror (or Right onto Left with leftToRight=False), using rest world matrices (D28). Undoable.
class MirrorRigControlShapesCmd : public MPxCommand
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
