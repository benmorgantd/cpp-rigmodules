#pragma once

#include <maya/MArgList.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

// Library file queries and edits (D51): what a pose file or a ControlAnim file holds, without loading it onto a rig.

// cmds.getRigDataFileInfo("rig:rigRoot", file=["C:/lib/fist.controlpose.json", "C:/lib/walk.controlanim.json"])
//   -> JSON text, a list with one object per file in the order given: the file, its kind ("pose" or "animation"),
//   formatVersion, meta, for an animation frameRate, startFrame and endFrame, controlCount, channelCount and
//   unmatchedControls (the [side, usage] of every file control the rig has no control for). A file that can't be read
//   or fails validation gives {file, error}, and the others still answer. Query only: it changes nothing and is not
//   undoable. Keys are in DataFileInfoKey.
class GetRigDataFileInfoCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;
};

// cmds.setRigDataFileMeta(file="C:/lib/fist.controlpose.json", meta='{"tags": ["hands"], "status": "review"}')
//   Edits the meta of a pose file or a ControlAnim file and nothing else: each key in meta replaces that field, and an
//   empty string or list clears it. The rig's own fields (assetId, variant, pluginVersion, created) can't be set. An
//   older file is written at the current format version. The file is not changed when anything fails. The caller
//   versions and checks out the file first, as for a save. Not undoable (it edits a file).
class SetRigDataFileMetaCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;
};
