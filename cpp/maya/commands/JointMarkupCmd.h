#pragma once

#include <maya/MArgDatabase.h>
#include <maya/MDGModifier.h>
#include <maya/MObjectArray.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

// Joints are always passed by name. The selection is never used.
// Create: cmds.rigJointMarkup("jointA", usage="upperArm", side="Left", export=True, skin=True)
// Edit:   cmds.rigJointMarkup("jointA", "jointB", edit=True, usage="shoulder")
// Query:  cmds.rigJointMarkup("jointA", "jointB", query=True, usage=True)   -> one value per joint, "" for unmarked joints
//         cmds.rigJointMarkup("jointA", query=True)                         -> full markup JSON per joint
// Remove: cmds.rigJointMarkup("jointA", remove=True)
class JointMarkupCmd : public MPxCommand
{
public:
	JointMarkupCmd();
	virtual ~JointMarkupCmd() override;

	MStatus doIt(const MArgList& args) override;
	MStatus redoIt() override;
	MStatus undoIt() override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;

private:
	MStatus doQuery(const MArgDatabase& argData, const MObjectArray& oJoints);
	MStatus doCreate(const MArgDatabase& argData, const MObjectArray& oJoints);
	MStatus doEdit(const MArgDatabase& argData, const MObjectArray& oJoints);
	MStatus doRemove(const MObjectArray& oJoints);

	MDGModifier fDGMod;
	bool fIsUndoable = true;
};
