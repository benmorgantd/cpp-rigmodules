#pragma once

#include "core/PoseFile.h"
#include "core/RigKeys.h"
#include "maya/shared/RigTraversal.h"

#include <vector>

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
	// within the frames the call covers. Each key then gets the tangents it names, and the curve its flags (weighted
	// and the infinities, so a curve the data gives no flags goes back to unweighted and constant). Enum, bool and
	// integer channels key stepped whatever the tangents say. The whole call is checked first, so an error changes
	// nothing. A control or channel the keys have and the
	// rig lacks is skipped with a warning (a control outside a selection scope is not one). outChangedNames lists the
	// controls that were keyed. The edit has run when this returns, and the caller keeps it for undo and redo.
	static MStatus setRigKeys(const RigControlScope& scope, const RigKeys& keys, RigKeyEdit& edit, MStringArray& outChangedNames);

	// The keys as they go onto the other side (D42): each entry moves to its mirror control, and a channel the mirror
	// negates (a Center control's by its flip axis, any other by its mirror mode) has its values and fixed tangent
	// angles negated. It is a flip: both sides' entries move, so a rig keyed on one side only gets keys on the other.
	// An entry whose control has no mirror, or that the rig lacks, is skipped with a warning.
	static MStatus mirrorRigKeys(const RigControlScope& scope, const RigKeys& keys, RigKeys& outMirrored);

	// The keys' curves evaluated on every whole frame from firstFrame to lastFrame, as one pose per frame (UI units), for
	// the channels the rig has. It builds scratch animCurves with the same keys, tangents and flags and deletes them, so
	// the scene is unchanged and nothing goes on the undo queue. A channel the rig lacks is left out.
	static MStatus sampleRigKeys(const MObject& oRigRoot, const RigKeys& keys, int firstFrame, int lastFrame, std::vector<PoseMap>& outSamples);
};
