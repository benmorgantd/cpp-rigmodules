#pragma once

#include "core/ControlChannels.h"

#include <maya/MDagModifier.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>

// A control's space switch, whichever kind it is (D52 section 4.4): a rigConstraint space (D19, its drivers are the
// labels) or a module-owned space (the module reads the control's `space` enum itself, as TwoBoneIk does for its pole).
// getRigControlSpace and setRigControlSpace are thin commands over these two.
class ControlSpaceScene
{
public:
	// The control's space: the constraint or the module that owns it, the labels in order and the current one. False for
	// a control with no space switch. Read only.
	static bool getControlSpace(const MObject& oControl, ControlSpace& outSpace);

	// Switches the control to the space at index newSpace and keeps its world matrix (D27's formula, through
	// RigPoseScene::matchControlToNewDelta). Queues into dagMod and runs it, so a command's redoIt can run it again.
	// Reports and fails for a control with no space switch, or an index with no space.
	static MStatus setControlSpace(const MObject& oControl, unsigned int newSpace, MDagModifier& dagMod);
};
