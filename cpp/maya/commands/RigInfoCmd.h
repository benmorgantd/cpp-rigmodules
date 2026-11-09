#pragma once

#include <maya/MArgList.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

// cmds.getRigInfo("hero:arm_ctrl_l")
//   -> JSON text: the rig root, asset root, asset id, variant (rigName), namespace and version stamps of the rig
//   that a rig root, rig module or rig control belongs to (D29, D30). The node is passed by name, never the
//   selection. Query only, so not undoable. Keys are in RigInfoKey and RigAttr.
class GetRigInfoCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;
};
