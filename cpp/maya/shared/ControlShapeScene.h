#pragma once

#include "core/ControlShapeFile.h"

#include <maya/MDGModifier.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>
#include <maya/MStringArray.h>

// Scene side of control shape files: reads and writes the shape attributes of a rig's controls,
// matched by control markup (D26). Every function displays its own errors.
class ControlShapeScene
{
public:
	static ControlShape getControlShape(const MObject& oControl);
	// Queues the writes. The caller runs the modifier.
	static MStatus setControlShape(const MObject& oControl, const ControlShape& shape, MDGModifier& dgMod);

	// Every control in the rig. outControlNames lists them in traversal order.
	static MStatus getRigControlShapes(const MObject& oRigRoot, ControlShapeMap& outShapes, MStringArray& outControlNames);

	// Queues a write for every control that has an entry in shapes. outAppliedNames lists them.
	// Warns about entries with no control and controls with no entry, and applies the rest.
	static MStatus setRigControlShapes(const MObject& oRigRoot, const ControlShapeMap& shapes, MDGModifier& dgMod, MStringArray& outAppliedNames);
};
