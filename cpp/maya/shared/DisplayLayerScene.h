#pragma once

#include <string>
#include <vector>

#include <maya/MDagModifier.h>
#include <maya/MObject.h>
#include <maya/MObjectArray.h>
#include <maya/MStatus.h>
#include <maya/MString.h>

// How a layer's members draw. The values are the layer's displayType enum attribute, so they are written as they are.
enum class DisplayLayerDisplayType : int
{
	Normal = 0,
	Template = 1,
	Reference = 2
};

// A display layer to create: its absolute name (":rig:jointsLayer"), the DAG nodes it holds, whether the layer's
// members are hidden while the timeline plays (the layer's hideOnPlayback) and how they draw (the layer's displayType).
struct DisplayLayerSpec
{
	MString name;
	MObjectArray members;
	bool hideOnPlayback{ false };
	DisplayLayerDisplayType displayType{ DisplayLayerDisplayType::Normal };
	bool visible{ true };
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
