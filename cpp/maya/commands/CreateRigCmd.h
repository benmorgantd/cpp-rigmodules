#pragma once

#include "maya/shared/Utilities.h"

#include <maya/MArgList.h>
#include <maya/MDagModifier.h>
#include <maya/MPxCommand.h>
#include <maya/MString.h>
#include <maya/MSyntax.h>

// Joints are always passed by full name. The selection is never used.
// cmds.createRigFromTemplate("skel:root_00_M_JNT", "skel:spine_00_M_JNT", template="C:/rigs/LayoutRig.json", namespace="rig")
// namespace is the rig's final namespace (default "rig"). The joints are moved into it.
// parentRig="hero_body:rigRoot" attaches the rig to that rig root: a template with "parent:module.socket" ids
// follows its sockets and needs one, and a template without them refuses one. The read phase lists every missing socket.
// checkMirrorPositions=True fails a mirrored module whose joint is more than 0.001 from its reflected source.
class CreateRigCmd : public MPxCommand
{
public:
	CreateRigCmd();
	virtual ~CreateRigCmd() override;

	MStatus doIt(const MArgList& args) override;
	MStatus redoIt() override;
	MStatus undoIt() override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();

	static const char* aCommandString;

private:
	MDagModifier fDagMod;
	// Redo must run fDagMod with the rig namespace current, because its node names are relative to it.
	MString fRigNamespace;
	CreatedNamespaces fCreatedNamespaces;
};
