#pragma once

#include <maya/MStatus.h>
#include <maya/MPxNode.h>

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
	static MObject assetId;   // Pipe-separated identifier (e.g., "project_name|assets|my_rig")
	static MObject assetType; // Asset classification string
	static MObject assetData; // Native string attribute containing JSON payload for serialization
	static MObject children;   // Message links to connected nodes
};