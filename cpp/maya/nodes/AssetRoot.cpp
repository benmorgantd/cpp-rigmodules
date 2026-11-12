// Internal dependencies
#include "maya/nodes/AssetRoot.h"
#include "core/Constants.h"
#include "maya/ModuleRegistry.h"

// Maya dependencies
#include <maya/MDGModifier.h>
#include <maya/MFnAttribute.h>
#include <maya/MFnData.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MPlug.h>

// Define unique ID (offset from RigRootNode 0x00218)
MTypeId AssetRootNode::id(0x00219);

// Static member definitions
MObject AssetRootNode::aAssetId;
MObject AssetRootNode::aChildren;

// Boilerplate constructor, destructor, and creator
AssetRootNode::AssetRootNode()
{
}
AssetRootNode::~AssetRootNode()
{
}

void* AssetRootNode::creator()
{
	return new AssetRootNode();
}

MStatus AssetRootNode::initialize()
{
	MFnTypedAttribute tAttr;
	MFnMessageAttribute msgAttr;
	MStatus status;

	// Asset ID: the asset folder path, '\' between folders (D29)
	aAssetId = tAttr.create(RigAttr::AssetId, RigAttr::AssetId, MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(aAssetId);

	// The asset's type lives in its asset.json on disk, found through the id, so the node doesn't repeat it (D29).

	// Message attributes are non-storable DG connection pins
	aChildren = msgAttr.create(RigAttr::Children, RigAttr::Children, &status);
	addAttribute(aChildren);

	// No attribute affect relationships are needed since this is a pure metadata/network node.

	return MStatus::kSuccess;
}

MStatus AssetRootNode::compute(const MPlug& plug, MDataBlock& data)
{
	// Passive network tracking node; no evaluation math required
	return MStatus::kSuccess;
}


// Master rig builder static method ------------------------------------------
MObject AssetRootNode::createAssetRoot(const MString& assetId, MDagModifier& dagMod)
{
	MObject oAssetRoot = dagMod.MDGModifier::createNode(AssetRootNode::id);

	// Set asset id
	MPlug pAssetId(oAssetRoot, AssetRootNode::aAssetId);
	if (!assetId.isEmpty())
	{
		dagMod.newPlugValueString(pAssetId, assetId);
	}

	return oAssetRoot;

}
