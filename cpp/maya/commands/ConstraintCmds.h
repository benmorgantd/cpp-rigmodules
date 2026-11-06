#pragma once

#include <maya/MArgList.h>
#include <maya/MDagModifier.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

// Nodes are always passed by name. The selection is never used.
// cmds.createRigParentConstraint("rigControl", driver=["module.socket", ...], label=["Space", ...], initial=n, name="n")
//   -> the constraint node name. label is given once per driver or not at all. Default name: <control>_space.
//   Driver modules and an unqualified name resolve in the control's namespace (D19).
class CreateParentConstraintCmd : public MPxCommand
{
public:
	CreateParentConstraintCmd();
	virtual ~CreateParentConstraintCmd() override;

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

// cmds.setRigConstraintSpace("constraintNode", space=n)
// cmds.setRigConstraintSpace("constraintNode", label="Space")   (exactly one of space or label)
class SetConstraintSpaceCmd : public MPxCommand
{
public:
	SetConstraintSpaceCmd();
	virtual ~SetConstraintSpaceCmd() override;

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
