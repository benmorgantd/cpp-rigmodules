// Internal dependencies
#include "AssetRoot.h"
#include "RigRoot.h"
#include "AssetType.h"
#include "RigJsonStructs.h"
#include "RigModule.h"
#include "LayoutModule.h"

// Maya dependencies
#include <maya/MFnAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnData.h>
#include <maya/MPlug.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>
#include <maya/MGlobal.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>
#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>

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
	MFnEnumAttribute eAttr;
	MFnMessageAttribute msgAttr;
	MStatus status;

	// Asset ID: Hierarchy path separated by pipes ("project_name|assets|my_rig")
	assetId = tAttr.create("assetId", "aid", MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(assetId);

	// Asset Type: High-level classification string ("Character", "Prop", etc.)
	assetType = eAttr.create("assetType", "at", 0);
	eAttr.addField("Prop", 0);
	eAttr.addField("Character", 1);
	eAttr.addField("Head", 2);
	eAttr.addField("Vehicle", 3);
	eAttr.addField("Weapon", 4);
	eAttr.addField("VFX", 5);
	eAttr.addField("Light", 6);
	eAttr.addField("Environment", 7);
	eAttr.addField("Other", 8);
	eAttr.setStorable(true);
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


// Master rig builder static method ------------------------------------------
MObject AssetRootNode::createAssetRoot(const MString& assetId, const AssetType& assetType, MDGModifier& dgMod)
{
	MObject oAssetRoot = dgMod.createNode("assetRootNode");  // TODO: node names should be variables.

	// Set asset id
	MPlug pAssetId(oAssetRoot, AssetRootNode::assetId);
	if (!assetId.isEmpty())
	{
		dgMod.newPlugValueString(pAssetId, assetId);
	}

	// Set asset type
	short assetTypeShort = getShortFromAssetType(assetType);
	MPlug pAssetType(oAssetRoot, AssetRootNode::assetType);
	dgMod.newPlugValueShort(pAssetType, assetTypeShort);

	return oAssetRoot;

}

MStatus AssetRootNode::createRig(const MString& jsonFilePath)
{
    MStatus status;

    // 1. Parse JSON File into C++ Structs
    RigRootData rigData;
    MString parseErr;
    if (!loadRigTemplate(jsonFilePath, rigData, parseErr))
    {
        MGlobal::displayError(parseErr);
        return MStatus::kFailure;
    }

    MDGModifier dgMod;
    MDagModifier dagMod;

    // 2. Instantiate AssetRootNode
    // (Assuming assetType conversion and assetId mapping relative to MAYA_PROJECTS_ROOT)
    AssetType assetType = getAssetTypeFromString(rigData.rigType.c_str());
    MObject oAssetRoot = AssetRootNode::createAssetRoot(rigData.rigName.c_str(), assetType, dgMod);

    // 3. Instantiate RigRootNode
    MObject oRigRoot = RigRootNode::createRigRoot(
        rigData.rigName.c_str(),
        rigData.rigType.c_str(),
        rigData.rigVersion,
        jsonFilePath,
        oAssetRoot,
        dgMod
    );

    // 4. Create Implicit Base "Layout" Module
    // The Layout module acts as the fallback parent socket for all top-level modules.
    MObject oLayoutModule = MObject::kNullObj;
    oLayoutModule = LayoutModuleNode::createModule(oRigRoot, dgMod, dagMod, 3);

    // 5. Traverse and Build Module Hierarchy Recursively
    for (const RigModuleData& topLevelModule : rigData.rigModules)
    {
        // Top-level modules have no JSON parent, so they attach to oLayoutModule
        RigModuleNodeBase::buildModuleRecursive(
            topLevelModule,
            oRigRoot,
            oLayoutModule,
            dgMod,
            dagMod
        );
    }

    // 6. Execute all DG and DAG modifications atomically in Maya
    status = dgMod.doIt();
    if (!status)
    {
        MGlobal::displayError("Failed executing DG Modifiers during rig creation.");
        return status;
    }

    status = dagMod.doIt();
    if (!status)
    {
        MGlobal::displayError("Failed executing DAG Modifiers during rig creation.");
        return status;
    }

    MGlobal::displayInfo("Rig successfully created from template: " + jsonFilePath);
    return MStatus::kSuccess;
}


// Creat Rig Command ---------------------------------
const char* CreateRigCmd::commandString = "createRigFromTemplate";
const char* CreateRigCmd::kFilePathFlagShort = "-f";
const char* CreateRigCmd::kFilePathFlagLong = "-filePath";

CreateRigCmd::CreateRigCmd()
{
}

CreateRigCmd::~CreateRigCmd()
{
}

void* CreateRigCmd::creator()
{
    return new CreateRigCmd();
}

bool CreateRigCmd::isUndoable() const
{
    return true;
}

MSyntax CreateRigCmd::newSyntax()
{
    MSyntax syntax;
    syntax.addFlag(kFilePathFlagShort, kFilePathFlagLong, MSyntax::kString);
    return syntax;
}

MStatus CreateRigCmd::doIt(const MArgList& args)
{
    MStatus status;
    MArgDatabase argData(syntax(), args, &status);
    if (!status)
    {
        MGlobal::displayError("Error parsing command arguments.");
        return status;
    }

    MString jsonFilePath;
    if (argData.isFlagSet(kFilePathFlagShort))
    {
        argData.getFlagArgument(kFilePathFlagShort, 0, jsonFilePath);
    }
    else
    {
        MGlobal::displayError("Missing required flag: -filePath / -f");
        return MStatus::kFailure;
    }

    // Delegate creation directly to AssetRootNode entry method
    return AssetRootNode::createRig(jsonFilePath);
}

MStatus CreateRigCmd::redoIt()
{
    return MStatus::kSuccess;
}

MStatus CreateRigCmd::undoIt()
{
    return MStatus::kSuccess;
}