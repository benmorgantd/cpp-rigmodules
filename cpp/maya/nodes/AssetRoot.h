#pragma once
#include <maya/MDagModifier.h>
#include <maya/MPxNode.h>
#include <maya/MStatus.h>

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
	static MObject aAssetId;   // Pipe-separated folder path below RIGGING_PROJECT_DIR (D29), e.g. "shooter|main|characters|hero"
	static MObject aChildren;   // Message links to connected nodes
public:
	static MObject createAssetRoot(const MString& assetId, MDagModifier& dagMod);
};