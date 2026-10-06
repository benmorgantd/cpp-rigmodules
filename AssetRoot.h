#pragma once
#include "AssetType.h"

#include <maya/MStatus.h>
#include <maya/MPxNode.h>
#include <maya/MPxCommand.h>
#include <maya/MDagModifier.h>
#include <maya/MDGModifier.h>

class AssetRootNode : public MPxNode
{
public:
	// Instance methods
	AssetRootNode();
	virtual ~AssetRootNode() override;

	// Define the compute fn
	MStatus compute(const MPlug& plug, MDataBlock& data) override;

	// Static methods
	static MStatus initialize();
	static void* creator();

public:
	// Attribute handles and Type ID
	static MTypeId id;

	// Attribute handles
	static MObject aAssetId;   // Pipe-separated identifier (e.g., "project_name|assets|my_rig")
	static MObject aAssetType; // Asset classification string
	static MObject aAssetData; // Native string attribute containing JSON payload for serialization
	static MObject aChildren;   // Message links to connected nodes
public:
	static MObject createAssetRoot(const MString& assetId, const AssetType& assetType, MDGModifier& dgMod);
};