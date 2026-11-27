#pragma once

#include <maya/MArgList.h>
#include <maya/MDagModifier.h>
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

// cmds.getRigMirrorControl("rig:arm_00_L_CTRL") -> "arm_00_R_CTRL"
//   The control in the same rig with the same usage and the other side, found by control markup.
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

// cmds.getRigControlChannels("rig:arm_00_L_CTRL", "rig:arm_00_R_CTRL")
//   -> a JSON object: control name -> its channels in channel box order, each with name, type, rest, min, max and
//   mirror (see core/ControlChannels.h). Controls are passed by name, one or more. Query only, so not undoable.
class GetRigControlChannelsCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;
};

// cmds.getRigControlInfo("rig:arm_00_L_CTRL", "rig:arm_00_R_CTRL")
//   -> a JSON object: control name -> moduleType, module, side, usage and mirror (the mirror control's name, null when
//   it has none), keys in ControlInfoKey. Controls are passed by name, one or more. Query only, so not undoable.
class GetRigControlInfoCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;
};

// cmds.getRigControlSpace("rig:arm_00_L_CTRL")
//   -> a JSON object {"constraint": ..., "module": ..., "labels": [...], "current": n}, keys in ControlSpaceKey,
//   or an empty string for a control with no space switch. A constraint space names the constraint and a
//   module-owned space (the TwoBoneIk pole's) names the module, the other being empty. Exactly one control, by name.
//   Query only, so not undoable.
class GetRigControlSpaceCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;
};

// cmds.setRigControlSpace("rig:arm_00_L_CTRL", space=1) or cmds.setRigControlSpace("rig:arm_00_L_CTRL", label="Chest")
//   Switches the control to that space, either kind, and keeps its world matrix. Exactly one of space (the
//   index in getRigControlSpace's labels) or label. Undoable. A locked or driven channel keeps its value and warns.
class SetRigControlSpaceCmd : public MPxCommand
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
	MDagModifier fDagMod;
};

// cmds.mirrorRigControlShapes("rig:rigRoot", leftToRight=True)
//   -> the names of the controls changed. Reflects every Left control's shape across world X = 0 onto its Right
//   mirror (or Right onto Left with leftToRight=False), using rest world matrices. Undoable.
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
