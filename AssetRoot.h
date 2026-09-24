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
	static MObject assetId;   // Pipe-separated identifier (e.g., "project_name|assets|my_rig")
	static MObject assetType; // Asset classification string
	static MObject assetData; // Native string attribute containing JSON payload for serialization
	static MObject children;   // Message links to connected nodes
public: 
	static MObject createAssetRoot(const MString& assetId, const AssetType& assetType, MDGModifier& dgMod);
	static MStatus createRig(const MString& jsonFilePath);
};


/// <summary>
/// Main function to run for creating rigs.
/// </summary>
class CreateRigCmd : public MPxCommand
{
public:
	CreateRigCmd();
	virtual ~CreateRigCmd() override;

	MStatus doIt(const MArgList& args) override;
	MStatus redoIt() override;
	MStatus undoIt() override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();

	// Command registration attribute
	static const char* commandString;

private:
	static const char* kFilePathFlagShort;
	static const char* kFilePathFlagLong;

	MDGModifier fDgMod;
	MDagModifier fDagMod;
};