// Internal dependencies
#include "RigRoot.h"
#include "AssetRoot.h"
#include "LayoutModule.h"
#include "AssetType.h"
#include "RigJsonStructs.h"
#include "RigModule.h"
#include "RigType.h"

// Standard dependencies
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <stdexcept>
#include <string>

// Maya dependencies
#include <maya/MDagModifier.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnData.h>
#include <maya/MFnNumericData.h>
#include <maya/MPlug.h>
#include <maya/MGlobal.h>
#include <maya/MSyntax.h>
#include <maya/MArgDatabase.h>
#include <maya/MString.h>
#include <maya/MNamespace.h>
#include <maya/MTypeId.h>

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

	aRigName = tAttr.create("rigName", "rigName", MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(aRigName);

	aRigType = eAttr.create("rigType", "rigType", 0);
    eAttr.addField("Prop", 0);
    eAttr.addField("Character", 1);
    eAttr.addField("Vehicle", 2);
    eAttr.addField("Head", 3);
    eAttr.addField("Weapon", 4);
    eAttr.addField("VFX", 5);
    eAttr.addField("Light", 6);
    eAttr.addField("Environment", 7);
    eAttr.addField("Other", 8);
	eAttr.setStorable(true);
	addAttribute(aRigType);

	aRigVersion = nAttr.create("rigVersion", "rigVersion", MFnNumericData::kLong, 0, &status);
	nAttr.setStorable(true);
	addAttribute(aRigVersion);

	aRigTemplateName = tAttr.create("rigTemplateName", "rigTemplateName", MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(aRigTemplateName);

	// Message attributes can't be storable since they don't contain data
	aRigModules = msgAttr.create("rigModules", "rigModules", &status);
	addAttribute(aRigModules);

	aAssetRoot = msgAttr.create("assetRoot", "assetRoot", &status);
	addAttribute(aAssetRoot);

	aChildren = msgAttr.create("children", "cildren", &status);
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
    MDagModifier& dagMod)
{
    // 1. Create the DG node (using a variable string for node type)
    MObject oRigRoot = dagMod.MDGModifier::createNode(RigRootNode::id);

    // 2. Set string and numeric attributes
    if (!name.isEmpty())
    {
        MPlug pRigName(oRigRoot, RigRootNode::aRigName);
        dagMod.newPlugValueString(pRigName, name);
    }

    if (!type.isEmpty())
    {
		RigType rigType = getRigTypeFromString(type.asChar());
		short rigTypeShort = getShortFromRigType(rigType);
        MPlug pRigType(oRigRoot, RigRootNode::aRigType);
		dagMod.newPlugValueShort(pRigType, rigTypeShort);
    }

    MPlug pRigVersion(oRigRoot, RigRootNode::aRigVersion);
    dagMod.newPlugValueInt(pRigVersion, version);

    if (!templateName.isEmpty())
    {
        MPlug pTemplate(oRigRoot, RigRootNode::aRigTemplateName);
        dagMod.newPlugValueString(pTemplate, templateName);
    }

    // 3. Wire network plug: RigRootNode.assetRoot -> AssetRootNode
    if (!oAssetRoot.isNull())
    {
        MPlug pAssetRootPlug(oRigRoot, RigRootNode::aAssetRoot);
        MPlug pRigRootPlug(oAssetRoot, AssetRootNode::aChildren);

        if (!pAssetRootPlug.isNull() && !pRigRootPlug.isNull())
        { 
            dagMod.connect(pRigRootPlug, pAssetRootPlug);
        }
        else
        {
            MGlobal::displayError("Failed to wire rig root to asset root.");
        }
    }

    return oRigRoot;
}

// TODO: optionally pass in the asset root. If none, we create one.
MStatus RigRootNode::createRig(const MString& jsonFilePath, MDagModifier& dagMod)
{
    MStatus status;
	MString originalNamespace = MNamespace::currentNamespace();

	try
	{
		MNamespace::addNamespace("rig");
		// TODO: we will have to move input joints into the namespace also
		MNamespace::setCurrentNamespace("rig");  

		// 1. Parse JSON File into C++ Structs
		AssetRootData assetData;
		RigRootData rigData;
		MString parseErr;

		if (!loadRigTemplate(jsonFilePath, assetData, rigData, parseErr))
		{
			MGlobal::displayError(parseErr);
			dagMod.undoIt();
			return MStatus::kFailure;
		}

		// 2. Instantiate AssetRootNode
		// (Assuming assetType conversion and assetId mapping relative to MAYA_PROJECTS_ROOT)
		AssetType assetType = getAssetTypeFromString(assetData.assetType);
		// TODO: optionally create the asset root.
		// TODO: we have no asset id to pass in because that's not supposed to be apart of json data (it should not be tied to assets)
		// TODO: we need to pass in an MStatus reference so we can see if this succeeded.
		MObject oAssetRoot = AssetRootNode::createAssetRoot(rigData.rigTemplateName.c_str(), assetType, dagMod);

		// 3. Create RigRootNode
		MObject oRigRoot = RigRootNode::createRigRoot(
			rigData.rigTemplateName.c_str(),
			rigData.rigType.c_str(),
			rigData.rigVersion,
			rigData.rigTemplateName.c_str(),
			oAssetRoot,
			dagMod
		);

		// 4. Create Implicit Base "Layout" Module
		// The Layout module acts as the fallback parent socket for all top-level modules.
		RigModuleBuildResult layoutModuleBuildResult = LayoutModuleNode::createModule(oRigRoot, dagMod, 3);
		status = layoutModuleBuildResult.status;
		if (!status)
		{
			MGlobal::displayError(MString("Failure building layout module: ") + status.errorString());
			return status;
		}
		
		// 5. Build all our modules, then run a control parenting pass.
		std::unordered_map<std::string, RigModuleBuildResult> rigModuleMap;

		// Build the remainder of the modules.
		rigModuleMap = RigModuleNodeBase::buildModules(rigData.rigModules, oRigRoot, dagMod);

		// Append the layout module to the rig module map
		rigModuleMap["layout"] = layoutModuleBuildResult;

		// Parent the controls together.
		parentModuleControls(rigModuleMap, rigData.rigModules, dagMod);

		// 6. Execute DAG mod ONE time. For this to work, we have to do the read phase previously so that we know what transforms are.
		status = dagMod.doIt();
		if (!status)
		{
			MGlobal::displayError("Failed executing DAG Modifiers during rig creation: " + status.errorString());
			return status;
		}

		MGlobal::displayInfo("Rig successfully created from template: " + jsonFilePath);
	}
	catch (const std::runtime_error& e)
	{
		MGlobal::displayError(MString("Exception raised when building rig: ") + e.what());
		// Undo if we hit an exception.
		dagMod.undoIt();
	}
	
	MNamespace::setCurrentNamespace(originalNamespace);
    return MStatus::kSuccess;
}

bool RigRootNode::parseSocketIdentifier(const MString& fullIdentifier, std::string& outModuleName, std::string& outSocketName)
{
	MStringArray tokens;
	if (fullIdentifier.split('.', tokens) == MS::kSuccess && tokens.length() == 2)
	{
		outModuleName = tokens[0].asChar();
		outSocketName = tokens[1].asChar();
		return true;
	}

	// Single token represents a standalone node or root control
	outModuleName = "";
	outSocketName = fullIdentifier.asChar();
	return false;
}

void RigRootNode::parentModuleControls(
	const std::unordered_map<std::string, RigModuleBuildResult>& rigModuleMap,
	const std::vector<RigModuleData>& flatModuleList,
	MDagModifier& dagMod)
{
	// 1. Verify layout module exists in build map
	auto layoutIt = rigModuleMap.find("layout");
	if (layoutIt == rigModuleMap.end())
	{
		MGlobal::displayError("Unable to parent module controls: 'layout' module not found in build registry.");
		return;
	}
	const RigModuleBuildResult& layoutResult = layoutIt->second;
	MObject oLayoutModule = layoutResult.moduleNode;

	// Ensure build map (including 'layout') matches flat JSON list length.
	// We subtract one because we added Layout during build.
	if ((rigModuleMap.size() - 1) != flatModuleList.size())
	{
		MGlobal::displayError("Unable to parent module controls because object map and module list from json are not equal lengths.");
		return;
	}

	// Default fallback parent socket (Layout module "end"). Can be nullptr, checked where it is used.
	const RigModuleSocketInfo* pLayoutEndSocket = RigModuleNodeBase::findInfo(layoutResult.parentSockets, "end");

	// 2. Process parenting for each module defined in JSON
	for (unsigned int i = 0; i < flatModuleList.size(); ++i)
	{
		const RigModuleData& moduleJsonData = flatModuleList[i];
		const std::string& moduleName = moduleJsonData.name;

		auto moduleIt = rigModuleMap.find(moduleName);
		if (moduleIt == rigModuleMap.end())
		{
			MGlobal::displayError(MString("Module not found in build map: ") + moduleName.c_str());
			continue;
		}

		const RigModuleBuildResult& buildResult = moduleIt->second;
		MObject oModule = buildResult.moduleNode;

		// Every path below ends in the module-specific callback stored in the build result.
		if (!buildResult.dagParentCallback)
		{
			MGlobal::displayInfo(MString("Module did not define dag parenting function: ") + moduleName.c_str());
			continue;
		}

		// Case A: Default parenting (No explicit parentStructure defined) -> Parent to Layout "end"
		if (moduleJsonData.parentStructure.empty())
		{
			MGlobal::displayInfo(MString("Rig module ") + moduleName.c_str() + " did not define any dag parents. Defaulting to parenting all child plugs to the Layout module.");

			if (pLayoutEndSocket == nullptr)
			{
				MGlobal::displayError("Layout module 'end' socket is invalid. Cannot default parent controls.");
				continue;
			}

			for (const auto& [plugName, childPlugInfo] : buildResult.childPlugs)
			{
				if (childPlugInfo.oTransform.isNull())
				{
					continue;
				}

				RigModuleDagReparentArgs args{
					childPlugInfo,
					*pLayoutEndSocket,
					oModule,
					oLayoutModule,
					dagMod
				};

				MStatus reparentResult = buildResult.dagParentCallback(args);
				if (!reparentResult)
				{
					MGlobal::displayError(MString("Failed to do dag parenting for module ") + moduleName.c_str());
				}
			}
		}
		// Case B: Explicit parent/child map specified in JSON template
		else
		{
			for (const auto& [childPlugName, parentTargetIdentifier] : moduleJsonData.parentStructure)
			{
				const RigModuleChildPlugInfo* pChildInfo = RigModuleNodeBase::findInfo(buildResult.childPlugs, childPlugName);
				if (pChildInfo == nullptr)
				{
					MGlobal::displayError(MString("Child plug '") + childPlugName.c_str() + "' not found in module '" + moduleName.c_str() + "'.");
					continue;
				}

				// Parse "moduleName.socketName" target string
				std::string targetModuleName, targetSocketName;
				parseSocketIdentifier(parentTargetIdentifier.c_str(), targetModuleName, targetSocketName);

				MObject oParentTargetModule = MObject::kNullObj;
				const RigModuleSocketInfo* pSocketInfo = nullptr;

				if (targetModuleName.empty() || targetModuleName == "layout")
				{
					if (targetModuleName.empty())
					{
						MGlobal::displayWarning("Invalid socket name given, defaulting to layout.");
					}

					oParentTargetModule = oLayoutModule;
					pSocketInfo = RigModuleNodeBase::findInfo(layoutResult.parentSockets, targetSocketName);
				}
				else
				{
					auto targetModuleIt = rigModuleMap.find(targetModuleName);
					if (targetModuleIt == rigModuleMap.end())
					{
						MGlobal::displayError(MString("Target parent module '") + targetModuleName.c_str() + "' not found in build map.");
						continue;
					}

					oParentTargetModule = targetModuleIt->second.moduleNode;
					pSocketInfo = RigModuleNodeBase::findInfo(targetModuleIt->second.parentSockets, targetSocketName);
				}

				if (pSocketInfo == nullptr)
				{
					MGlobal::displayError(MString("Target parent socket '") + targetSocketName.c_str() + "' not found on module '" + targetModuleName.c_str() + "'.");
					continue;
				}

				RigModuleDagReparentArgs args{
					*pChildInfo,
					*pSocketInfo,
					oModule,
					oParentTargetModule,
					dagMod
				};

				MStatus reparentResult = buildResult.dagParentCallback(args);
				if (!reparentResult)
				{
					MGlobal::displayError(MString("Failed to do dag parenting for module ") + moduleName.c_str());
				}
			}
		}
	}
}


// Create Rig Command ---------------------------------
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
    status = RigRootNode::createRig(jsonFilePath, fDagMod);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	return redoIt();
}

MStatus CreateRigCmd::redoIt()
{
	MStatus status = fDagMod.doIt();
	if (!status)
	{
		MGlobal::displayError("Failed executing DAG modifier during rig creation: " + status.errorString());
		return status;
	}

	MGlobal::displayInfo("Rig successfully created.");
	return MStatus::kSuccess;
}

MStatus CreateRigCmd::undoIt()
{
	MGlobal::displayInfo("Undoing creating the rig.");
	return fDagMod.undoIt();
}