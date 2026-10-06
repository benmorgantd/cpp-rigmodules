// Internal dependencies
#include "RigRoot.h"
#include "AssetRoot.h"
#include "LayoutModule.h"
#include "AssetType.h"
#include "RigJsonStructs.h"
#include "RigModule.h"

// Standard dependencies
#include <filesystem>

// Maya dependencies
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnData.h>
#include <maya/MFnNumericData.h>
#include <maya/MPlug.h>
#include <maya/MDGModifier.h>
#include <maya/MGlobal.h>
#include <maya/MSyntax.h>
#include <maya/MArgDatabase.h>
#include <maya/MString.h>

// Define the unique ID
MTypeId RigRootNode::id(0x00218);

// Static member definitions
MObject RigRootNode::aRigName;
MObject RigRootNode::aRigType;
MObject RigRootNode::aRigVersion;
MObject RigRootNode::aRigModules;
MObject RigRootNode::aRigTemplateName;
MObject RigRootNode::aAssetRoot;
MObject RigRootNode::aChildren;

// Boilerplate constructor, destructor, and creator
RigRootNode::RigRootNode() {}
RigRootNode::~ RigRootNode() {}

void* RigRootNode::creator()
{
	return new RigRootNode();
}


MStatus RigRootNode::initialize()
{
	MFnNumericAttribute nAttr;
	MFnTypedAttribute tAttr;
	MFnMessageAttribute msgAttr;
    MFnEnumAttribute eAttr;
	MStatus status;

	aRigName = tAttr.create("rigName", "rn", MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(aRigName);

	aRigType = eAttr.create("rigType", "rt", 0);
    eAttr.addField("Prop", 0);
    eAttr.addField("Character", 1);
    eAttr.addField("Vehicle", 2);
    eAttr.addField("Head", 3);
    eAttr.addField("Weapon", 4);
    eAttr.addField("VFX", 5);
    eAttr.addField("Light", 6);
    eAttr.addField("Environment", 7);
    eAttr.addField("Other", 8);
	tAttr.setStorable(true);
	addAttribute(aRigType);

	aRigVersion = nAttr.create("rigVersion", "rv", MFnNumericData::kLong, 0, &status);
	nAttr.setStorable(true);
	addAttribute(aRigVersion);

	aRigTemplateName = tAttr.create("rigTemplateName", "rtn", MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(aRigTemplateName);

	// Message attributes can't be storable since they don't contain data
	aRigModules = msgAttr.create("rigModules", "rm", &status);
	addAttribute(aRigModules);

	aAssetRoot = msgAttr.create("assetRoot", "ar", &status);
	addAttribute(aAssetRoot);

	aChildren = msgAttr.create("children", "c", &status);
	addAttribute(aChildren);

	// In the case of this node, there are no attribute affect relationships.

	return MStatus::kSuccess;
}


MStatus RigRootNode::compute(const MPlug& plug, MDataBlock& data)
{
	// The node really has nothing to compute when attributes change, so we can just return kSuccess.
	return MStatus::kSuccess;
}

// Public shared methods
MObject RigRootNode::createRigRoot(
    const MString& name,
    const MString& type,
    int version,
    const MString& templateName,
    MObject oAssetRoot,
    MDGModifier& dgMod)
{
    // 1. Create the DG node (using a variable string for node type)
    MString nodeTypeName = "rigRootNode";
    MObject oRigRoot = dgMod.createNode(nodeTypeName);

    // 2. Set string and numeric attributes
    if (!name.isEmpty())
    {
        MPlug pRigName(oRigRoot, RigRootNode::aRigName);
        dgMod.newPlugValueString(pRigName, name);
    }

    if (!type.isEmpty())
    {
        MPlug pRigType(oRigRoot, RigRootNode::aRigType);  // TODO: rigType enum should be what our AssetType enum currently is
        dgMod.newPlugValueString(pRigType, type);
    }

    MPlug pRigVersion(oRigRoot, RigRootNode::aRigVersion);
    dgMod.newPlugValueInt(pRigVersion, version);

    if (!templateName.isEmpty())
    {
        MPlug pTemplate(oRigRoot, RigRootNode::aRigTemplateName);
        dgMod.newPlugValueString(pTemplate, templateName);
    }

    // 3. Wire network plug: RigRootNode.assetRoot -> AssetRootNode
    if (!oAssetRoot.isNull())
    {
        MPlug pAssetRootPlug(oRigRoot, RigRootNode::aAssetRoot);
        MPlug pRigRootPlug(oAssetRoot, AssetRootNode::aChildren);
        if (!pAssetRootPlug.isNull() && !pRigRootPlug.isNull())
        { 
            dgMod.connect(pRigRootPlug, pAssetRootPlug);
        }
        else
        {
            MGlobal::displayError("Failed to wire rig root to asset root.");
        }
        
    }

    return oRigRoot;
}

MStatus RigRootNode::createRig(const MString& jsonFilePath)  // TODO: optionally pass in the asset root. If none, we create one.
{
    MStatus status;

    // 1. Parse JSON File into C++ Structs
    AssetRootData assetData;
    RigRootData rigData;
    MString parseErr;
    if (!loadRigTemplate(jsonFilePath, assetData, rigData, parseErr))
    {
        MGlobal::displayError(parseErr);
        return MStatus::kFailure;
    }

    MDGModifier dgMod;
    MDagModifier dagMod;

    // 2. Instantiate AssetRootNode
    // (Assuming assetType conversion and assetId mapping relative to MAYA_PROJECTS_ROOT)
    AssetType assetType = getAssetTypeFromString(assetData.assetType);  // TODO: there's some issue here with this string
    // TODO: optionally create the asset root.
    MObject oAssetRoot = AssetRootNode::createAssetRoot(rigData.rigTemplateName.c_str(), assetType, dgMod);

    // 3. Instantiate RigRootNode
    MObject oRigRoot = RigRootNode::createRigRoot(
        rigData.rigTemplateName.c_str(),
        rigData.rigType.c_str(),
        rigData.rigVersion,
        jsonFilePath,
        oAssetRoot,
        dgMod
    );

    // 4. Create Implicit Base "Layout" Module
    // The Layout module acts as the fallback parent socket for all top-level modules.
    MObject oLayoutModule = MObject::kNullObj;
    // TODO: pass in status
    oLayoutModule = LayoutModuleNode::createModule(oRigRoot, dgMod, dagMod, 3);

    // 5. Traverse and Build Module Hierarchy Recursively
    for (unsigned int i = 0; i < rigData.rigModules.size(); ++i)//const RigModuleData& topLevelModule : rigData.rigModules)
    {
        // Ensure our mods are up to date before we build the next module.
        // This means that when we query attributes from previous modules we will have them declared and set.
        dgMod.doIt();
        dagMod.doIt();

        const RigModuleData topLevelModule = rigData.rigModules[i];
        // Top-level modules have no module parent, so they attach to oLayoutModule.
        // The other parents are determined from their hierarchy in the JSON
        RigModuleNodeBase::buildModuleRecursive(
            topLevelModule,
            oRigRoot,
            oLayoutModule,
            dgMod,
            dagMod
        );
    }

    // 6. Execute all DG and DAG mods
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
const char* CreateRigCmd::aCommandString = "createRigFromTemplate";

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
    syntax.addArg(MSyntax::kString);
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

    MString jsonFilePath = argData.commandArgumentString(0);

    if (jsonFilePath.isEmpty() || !std::filesystem::exists(jsonFilePath.asChar()))
    {
        MGlobal::displayError("Please enter a valid json rig template filepath.");
        return MStatus::kFailure;
    }

    // Delegate creation directly to AssetRootNode entry method
    return RigRootNode::createRig(jsonFilePath);
}

MStatus CreateRigCmd::redoIt()
{
    return MStatus::kSuccess;
}

MStatus CreateRigCmd::undoIt()
{
    return MStatus::kSuccess;
}