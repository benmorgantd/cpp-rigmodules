#pragma once

#include <maya/MArgList.h>
#include <maya/MDagModifier.h>
#include <maya/MPxCommand.h>
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MSyntax.h>

// The temporary capsule display of a rig's push outs (D79, design/push-out.md section 5). The logic is
// maya/shared/PushOutScene; these commands parse flags and delegate.

// cmds.showRigPushOuts(namespace="skel")
//   -> the display nodes it made, one per rigPushOut node of the namespace (the current one when left out)
//   Each display is a blue capsule at the push out's stored offset that follows the capsule's joint live. Move and rotate it,
//   and change its radius and halfLength attributes: none of this changes the rig. Fails when a display is already there.
//   Undoable.
class ShowRigPushOutsCmd : public MPxCommand
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
	MString fNamespace;   // without colons at its ends, "" for the root
};

// cmds.hideRigPushOuts(namespace="skel")
//   -> the names of the displays it deleted
//   Deletes every display of the namespace and the utility nodes behind it. Undoable.
class HideRigPushOutsCmd : public MPxCommand
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

// cmds.getRigPushOutCapsule(display)
//   -> JSON text, the template's `capsule` object for that display: {"follow": ["Center", "head"], "radius": 6.0,
//   "halfLength": 10.0, "offset": {"translate": [0, 4, 3], "rotate": [0, 0, 0]}}, the offset being the display's place
//   relative to the joint it follows. Paste it into a collision of the template. Fails for a display that is scaled. Not
//   undoable: it changes nothing.
class GetRigPushOutCapsuleCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;
};
