#pragma once

#include <maya/MArgList.h>
#include <maya/MDGModifier.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

// Nodes are always passed by name. The selection is never used.
// cmds.setRigIkFkBlend("settingsControl", blend=1.0)   (any control or the module node of a TwoBoneIk limb; D52 section 7.2)
//   -> the controls whose values changed. Writes the control values that reproduce the limb's current pose on both
//   sides (the module's fkMatchLocal and ikMatchLocal), then sets ikFkBlend to blend (default 1, IK), so the switch
//   doesn't pop at blend 0 and 1. Warns when the IK side can't match the pose or a channel is locked or driven. Sets no
//   keys. Undoable as one step. A raw setAttr on ikFkBlend does no match.
class SetRigIkFkBlendCmd : public MPxCommand
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
	MDGModifier fDGMod;
};
