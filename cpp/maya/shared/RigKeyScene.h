#pragma once

#include "core/RigKeys.h"
#include "maya/shared/RigTraversal.h"

#include <maya/MAnimCurveChange.h>
#include <maya/MDGModifier.h>
#include <maya/MStatus.h>
#include <maya/MStringArray.h>

// The undoable writes of one keying call: the animCurve nodes it creates (through the modifier, so undo deletes them)
// and the keys it adds to every curve, new or existing (through the change, so undo removes them). One per command.
struct RigKeyEdit
{
	MDGModifier dgMod;
	MAnimCurveChange curveChange;

	// Runs the edit again after an undo. The first run is RigKeyScene::setRigKeys itself.
	MStatus redo();
	MStatus undo();
};

// Scene side of keying a rig's control channels (D34): the keys a rig has, and every key of a call written at once.
// A channel is the same thing as in RigPoseScene: a keyable attribute a tool may write. Every function displays its own
// errors.
class RigKeyScene
{
public:
	// The keys of every channel of every control in the scope that has an animCurve, by control markup. Channels
	// without keys are left out. outControlNames lists the controls read, in scope order.
	static MStatus getRigKeys(const RigControlScope& scope, RigKeys& outKeys, MStringArray& outControlNames);

	// Adds the keys to the channels of the controls in the scope, matched by markup, creating a curve for a channel that
	// has none. Each channel gets all its keys in one MFnAnimCurve::addKeys call, and a key replaces an existing one
	// within the frames the call covers. Enum, bool and integer channels key stepped, the others use the global default
	// tangents. The whole call is checked first, so an error changes nothing. A control or channel the keys have and the
	// rig lacks is skipped with a warning (a control outside a selection scope is not one). outChangedNames lists the
	// controls that were keyed. The edit has run when this returns, and the caller keeps it for undo and redo.
	static MStatus setRigKeys(const RigControlScope& scope, const RigKeys& keys, RigKeyEdit& edit, MStringArray& outChangedNames);
};
