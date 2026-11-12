#pragma once

#include "maya/shared/RigKeyScene.h"

#include <maya/MArgList.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

// Keys on a rig's animation interface (D34). Each takes either one rig root (every control in the rig) or one or more
// controls of one rig (those only), always by name: the selection is never used. The keys are one JSON string, in the
// shape of core/RigKeys.h: { side: { usage: { channel: [[frame, value], ...] } } }, in UI time and UI units.

// cmds.getRigKeys("rig:rigRoot") or cmds.getRigKeys(*controls)
//   Returns the keys of every channel that has an animation curve, as that JSON string. A channel with no curve is left
//   out. Not undoable: it changes no scene data.
class GetRigKeysCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;
};

// cmds.setRigKeys("rig:rigRoot", keys=json) or cmds.setRigKeys(*controls, keys=json)
//   Adds the keys to the controls' channels, matched by markup, in one call: every channel gets all its keys at once, a
//   curve is made for a channel that has none, and a key replaces an existing one at the same frame. The whole string is
//   checked first, so a bad one changes nothing. A control or channel only one side has is skipped with a warning.
//   Returns the names of the controls it keyed. Undoable as one step.
class SetRigKeysCmd : public MPxCommand
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
