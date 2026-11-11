#pragma once

#include <string>
#include <vector>

#include <maya/MDagModifier.h>
#include <maya/MObject.h>
#include <maya/MObjectArray.h>
#include <maya/MStatus.h>
#include <maya/MString.h>

// A display layer to create: its absolute name (":rig:jointsLayer") and the DAG nodes it holds.
struct DisplayLayerSpec
{
	MString name;
	MObjectArray members;
};

class DisplayLayerScene
{
public:
	// Queues, for each spec in order, deleting a display layer already holding that name, then creating the layer
	// and putting its members in it. Nothing runs until the caller's dagMod.doIt, and undo restores the old layers.
	// A member in another layer is moved out of it, as the Layer Editor does. The layer is registered with the
	// layer manager, so it shows in the Layer Editor. outLayers holds the new layers in spec order.
	static MStatus replaceLayers(const std::vector<DisplayLayerSpec>& specs, MDagModifier& dagMod, MObjectArray& outLayers);
};
