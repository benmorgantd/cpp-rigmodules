#pragma once

#include <maya/MDagModifier.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

// Renames joints, rig controls, rig modules and rig constraints to the naming convention (D58):
// <camelCaseName>_##_<L|R|M>_<JNT|CTRL|MOD|CON|GEO|MRK>. The nodes are the ones passed, else the selection. The index counts up in
// that order, so the first node gets startIndex (0 by default), the next one more.
// Rename: cmds.renameRigNodes("a", "b", "c", base="upperArm", side="Left")  -> ["upperArm_00_L_JNT", "upperArm_01_L_JNT", ...]
//         cmds.renameRigNodes("a", "b", base="finger", startIndex=3)        -> finger_03_..., finger_04_...
//         cmds.renameRigNodes(base="spine")                                 -> the selection, in selection order
// Renumber: leave base out and each node keeps its own base: cmds.renameRigNodes("spine_03_M_JNT", "spine_07_M_JNT") -> spine_00_M_JNT, spine_01_M_JNT
// The side (Left, Right or Center) defaults to the node's current side, else Center. The kind comes from the node's type.
// Query:  cmds.renameRigNodes("upperArm_00_L_JNT", query=True)  -> one JSON object per name, "" when it is not in the convention:
//         {"base": "upperArm", "index": 0, "side": "Left", "kind": "Joint"}
// All names are checked before anything changes: a name in use by another node fails, and so does a repeated target.
// The selection's order is only known when Maya tracks it (cmds.selectPref(trackSelectionOrder=True)), so the command fails on
// a selection of more than one node while it is off.
class RenameRigNodesCmd : public MPxCommand
{
public:
	RenameRigNodesCmd();
	virtual ~RenameRigNodesCmd() override;

	MStatus doIt(const MArgList& args) override;
	MStatus redoIt() override;
	MStatus undoIt() override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;

private:
	MDagModifier fDagMod;
	bool fIsUndoable = true;
};
