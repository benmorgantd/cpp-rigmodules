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

// cmds.saveRigAnimation("rig:rigRoot", file=path, startFrame=n, endFrame=n) or cmds.saveRigAnimation(*controls, ...)
//   Writes the keys of the controls' channels as a ControlAnim file (core/ControlAnimFile.h, D42): the keys with their
//   tangents and each curve's flags, by control markup. Only the keys from startFrame to endFrame go in, and both
//   default to the scene's playback range. A channel with no key in the range is left out. The file records the
//   range and the scene's frame rate. Returns the names of the controls it read. Not undoable: it changes no scene
//   data. Keying the rig at two frames first (rigmodules.pose.key_rig) saves a pose that is an animation.
class SaveRigAnimationCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;
};

// cmds.loadRigAnimation("rig:rigRoot", file=path, offset=n, mirror=False) or cmds.loadRigAnimation(*controls, ...)
//   Keys the controls' channels from a ControlAnim file, matched by markup, as setRigKeys does (one undo step, a key
//   replaces an existing one at its frame, a control the file lacks keeps its keys). A newer or damaged file changes
//   nothing, and so does one whose frame rate isn't the scene's. offset (default 0) moves every key by that many
//   frames. mirror=True puts each side's keys on the other, negating the channels that mirror negates and the fixed
//   tangent angles with them, and flips Center controls. Returns the names of the controls it keyed.
class LoadRigAnimationCmd : public MPxCommand
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
