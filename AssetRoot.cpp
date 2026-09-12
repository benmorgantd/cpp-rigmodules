// Internal dependencies
#include "AssetRoot.h"

// Maya dependencies
#include <maya/MDataHandle.h>
#include <maya/MFnAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnData.h>
#include <maya/MPlug.h>

// Define unique ID (offset from RigRootNode 0x00218)
MTypeId AssetRootNode::id(0x00219);

// Static member definitions
MObject AssetRootNode::assetId;
MObject AssetRootNode::assetType;
MObject AssetRootNode::assetData;
MObject AssetRootNode::children;

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
	MFnMessageAttribute msgAttr;
	MStatus status;

	// Asset ID: Hierarchy path separated by pipes ("project_name|assets|my_rig")
	assetId = tAttr.create("assetId", "aid", MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(assetId);

	// Asset Type: High-level classification string ("Character", "Prop", etc.)
	assetType = tAttr.create("assetType", "at", MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(assetType);

	// Asset Data: Native string attribute storing JSON formatted payload for graph metadata
	assetData = tAttr.create("assetData", "ad", MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(assetData);

	// Message attributes are non-storable DG connection pins
	children = msgAttr.create("children", "c", &status);
	addAttribute(children);

	// No attribute affect relationships are needed since this is a pure metadata/network node.

	return MStatus::kSuccess;
}

MStatus AssetRootNode::compute(const MPlug& plug, MDataBlock& data)
{
	// Passive network tracking node; no evaluation math required
	return MStatus::kSuccess;
}

