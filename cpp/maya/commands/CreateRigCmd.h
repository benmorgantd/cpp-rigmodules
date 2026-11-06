#pragma once

#include <maya/MArgList.h>
#include <maya/MDagModifier.h>
#include <maya/MPxCommand.h>
#include <maya/MString.h>
#include <maya/MSyntax.h>

// Joints are always passed by full name. The selection is never used.
// cmds.createRigFromTemplate("skel:root_jnt", "skel:spine_jnt", template="C:/rigs/LayoutRig.json", namespace="rig")
// namespace is the rig's final namespace (default "rig"). The joints are moved into it.
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
};
