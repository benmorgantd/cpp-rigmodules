#pragma once

#include <maya/MArgDatabase.h>
#include <maya/MDGModifier.h>
#include <maya/MObjectArray.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

// Joints are always passed by name. The selection is never used.
// Create: rigJointMarkup -usage "upperArm" -side "Left" -export true -skin true jointA
// Edit:   rigJointMarkup -edit -usage "shoulder" jointA jointB
// Query:  rigJointMarkup -query -usage jointA jointB   -> one value per joint, "" for unmarked joints
//         rigJointMarkup -query jointA                 -> full markup JSON per joint
// Remove: rigJointMarkup -remove jointA
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
