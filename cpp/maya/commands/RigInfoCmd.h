#pragma once

#include <maya/MArgList.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

// cmds.getRigInfo("hero:arm_00_L_CTRL")
//   -> JSON text: the rig root, asset id, rigName, skeletonHash, namespace and version stamps of the rig
//   that a rig root, rig module, rig control or rig-driven joint belongs to (D29, D30). "" for any other node,
//   with no error. The node is passed by name, never the selection. Query only, so not undoable. Keys are in
//   RigInfoKey and RigAttr. "look" is null, or the look the rig wears (D54): assetId, name, publishVersion.
class GetRigInfoCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;
};
