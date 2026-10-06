// Internal dependencies
#include "AssetRoot.h"
#include "AssetType.h"

// Maya dependencies
#include <maya/MFnAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnData.h>
#include <maya/MPlug.h>
#include <maya/MDGModifier.h>

// Define unique ID (offset from RigRootNode 0x00218)
MTypeId AssetRootNode::id(0x00219);

// Static member definitions
MObject AssetRootNode::aAssetId;
MObject AssetRootNode::aAssetType;
MObject AssetRootNode::aAssetData;
MObject AssetRootNode::aChildren;

// Boilerplate constructor, destructor, and creator
AssetRootNode::AssetRootNode() {}
AssetRootNode::~AssetRootNode() {}

void* AssetRootNode::creator()
{
	return new AssetRootNode();
}

MStatus AssetRootNode::initialize()
{
	MFnTypedAttribute tAttr;
	MFnEnumAttribute eAttr;
	MFnMessageAttribute msgAttr;
	MStatus status;

	// Asset ID: Hierarchy path separated by pipes ("project_name|assets|my_rig")
	aAssetId = tAttr.create("assetId", "aid", MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(aAssetId);

	// Asset Type: High-level classification string 
	aAssetType = eAttr.create("assetType", "at", 0);
	eAttr.addField("Mesh", 0);
	eAttr.addField("Rig", 1);
	eAttr.addField("Other", 2);
	eAttr.setStorable(true);
	addAttribute(aAssetType);

	// Asset Data: Native string attribute storing JSON formatted payload for graph metadata
	aAssetData = tAttr.create("assetData", "ad", MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(aAssetData);

	// Message attributes are non-storable DG connection pins
	aChildren = msgAttr.create("children", "c", &status);
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
MObject AssetRootNode::createAssetRoot(const MString& assetId, const AssetType& assetType, MDGModifier& dgMod)
{
	MObject oAssetRoot = dgMod.createNode("assetRootNode");  // TODO: node names should be variables.

	// Set asset id
	MPlug pAssetId(oAssetRoot, AssetRootNode::aAssetId);
	if (!assetId.isEmpty())
	{
		dgMod.newPlugValueString(pAssetId, assetId);
	}

	// Set asset type
	short assetTypeShort = getShortFromAssetType(assetType);
	MPlug pAssetType(oAssetRoot, AssetRootNode::aAssetType);
	dgMod.newPlugValueShort(pAssetType, assetTypeShort);

	return oAssetRoot;

}
