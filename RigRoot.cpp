// Internal dependencies
#include "RigRoot.h"
#include "AssetRoot.h"
#include "LayoutModule.h"
#include "AssetType.h"
#include "RigJsonStructs.h"
#include "RigModule.h"
#include "RigType.h"
#include "Constraints.h"
#include "Constants.h"
#include "RigTemplateValidation.h"

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
#include <maya/MSelectionList.h>

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
MObject RigRootNode::aConstraints;

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

	aRigName = tAttr.create(RigAttr::RigName, RigAttr::RigName, MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(aRigName);

	aRigType = eAttr.create(RigAttr::RigType, RigAttr::RigType, 0);
	for (short i = 0; i < RigAttr::RigTypes::All.size(); ++i)
	{
		eAttr.addField(RigAttr::RigTypes::All[i], i);
	}
	addAttribute(aRigType);

	// TODO: replace with constants references.
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

	aConstraints = msgAttr.create(RigAttr::Constraints, RigAttr::Constraints, &status);
	msgAttr.setArray(true);
	addAttribute(aConstraints);

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

namespace
{
	// Fixed until templates can configure the layout.
	constexpr unsigned short kLayoutControlCount = 3;
	const char* const kRigNamespace = "rig";

	void displayReport(const ValidationReport& report)
	{
		for (const std::string& warning : report.warnings)
		{
			MGlobal::displayWarning(warning.c_str());
		}
		for (const std::string& error : report.errors)
		{
			MGlobal::displayError(error.c_str());
		}
	}

	// renameNode silently makes a clashing name unique, and the module would no longer match its template name.
	void checkModuleNamesFree(const std::vector<RigModuleData>& modules, ValidationReport& report)
	{
		for (std::size_t i = 0; i < modules.size(); ++i)
		{
			const MString nodeName = MString(":") + kRigNamespace + ":" + modules[i].name.c_str();
			MSelectionList existing;
			if (existing.add(nodeName) == MStatus::kSuccess)
			{
				report.addError(RigTemplateValidation::modulePath(i, modules[i].name),
					std::string("a node named '") + nodeName.asChar() + "' already exists in the scene");
			}
		}
	}

	// The layout goes through the same plan/build path as template modules, so pass 2 and constraints find it by name.
	ModulePlan makeLayoutPlan()
	{
		ModulePlan plan;
		plan.name = RigNaming::LayoutModule;
		plan.moduleInterface = LayoutModuleNode::makeInterface(kLayoutControlCount);
		plan.build = [](MObject& oRigRoot, MDagModifier& dagMod)
		{
			return LayoutModuleNode::createModule(oRigRoot, dagMod, kLayoutControlCount);
		};
		return plan;
	}

	// Read phase. Reads the scene, never writes it. Fills plans (layout first) as far as validation gets.
	void planRig(
		const MString& jsonFilePath,
		const MString& jointNamespace,
		AssetRootData& assetData,
		RigRootData& rigData,
		std::vector<ModulePlan>& plans,
		ValidationReport& report)
	{
		if (!loadRigTemplate(jsonFilePath.asChar(), assetData, rigData, report))
		{
			return;
		}

		RigTemplateValidation::validateHeader(rigData, assetData, report);
		RigTemplateValidation::validateModuleNames(rigData.rigModules, report);
		checkModuleNamesFree(rigData.rigModules, report);

		// collectMarkedJoints displays its own errors.
		MarkedJointSet markedJoints;
		if (!JointMarkupScene::collectMarkedJoints(jointNamespace, markedJoints))
		{
			report.addError("joint markup", std::string("could not read joint markup in namespace '") + jointNamespace.asChar() + "'");
			return;
		}

		plans.push_back(makeLayoutPlan());
		for (ModulePlan& plan : RigModuleNodeBase::planModules(rigData.rigModules, markedJoints, report))
		{
			plans.push_back(std::move(plan));
		}

		ModuleInterfaceMap interfaces;
		for (const ModulePlan& plan : plans)
		{
			interfaces.emplace(plan.name, plan.moduleInterface);
		}
		RigTemplateValidation::validateConnections(rigData, interfaces, report);

		for (const std::string& jointName : markedJoints.index.unclaimedExportedJoints())
		{
			report.addWarning(jointName, "marked for export, but no module drives it, so it stays at rest");
		}
	}

	// Pass 2. Every child plug gets a parent: its parentStructure entry, or layout.end when the template does not list it.
	// The template was validated, so a failed lookup here means a module built something its plan did not declare.
	MStatus parentModuleControls(const RigModuleMap& rigModuleMap, const std::vector<RigModuleData>& modules, MDagModifier& dagMod)
	{
		const std::string defaultSocket = std::string(RigNaming::LayoutModule) + "." + PlugAndSocketNames::End;

		for (const RigModuleData& moduleData : modules)
		{
			const RigModuleBuildResult* pModule = RigModuleNodeBase::findInfo(rigModuleMap, moduleData.name);
			if (pModule == nullptr)
			{
				MGlobal::displayError(MString("Module missing from the build map: ") + moduleData.name.c_str());
				return MStatus::kFailure;
			}

			for (const auto& [plugName, childPlugInfo] : pModule->childPlugs)
			{
				auto it = moduleData.parentStructure.find(plugName);
				const std::string& socketIdentifier = it != moduleData.parentStructure.end() ? it->second : defaultSocket;

				const RigModuleBuildResult* pParentModule = nullptr;
				const RigModuleSocketInfo* pSocket = nullptr;
				if (!RigModuleNodeBase::lookupParentSocket(rigModuleMap, socketIdentifier, pParentModule, pSocket))
				{
					MGlobal::displayError(MString("Socket '") + socketIdentifier.c_str() + "' not found for child plug '"
						+ moduleData.name.c_str() + "." + plugName.c_str() + "'.");
					return MStatus::kFailure;
				}

				if (!pModule->dagParentCallback)
				{
					MGlobal::displayError(MString("Module has child plugs but no dag parenting function: ") + moduleData.name.c_str());
					return MStatus::kFailure;
				}

				RigModuleDagReparentArgs args{
					childPlugInfo,
					*pSocket,
					pModule->moduleNode,
					pParentModule->moduleNode,
					dagMod
				};

				MStatus status = pModule->dagParentCallback(args);
				if (!status)
				{
					MGlobal::displayError(MString("Failed to do dag parenting for module ") + moduleData.name.c_str());
					return status;
				}
			}
		}
		return MStatus::kSuccess;
	}

	// Write phase. Runs only on a clean report. Constraints come after the first doIt because they add an enum
	// attribute to controls that must exist by then.
	MStatus buildRig(const RigRootData& rigData, const AssetRootData& assetData, const std::vector<ModulePlan>& plans, MDagModifier& dagMod)
	{
		MStatus status;

		// TODO: optionally create the asset root, and pass an MStatus so its creation can be checked.
		std::string assetTypeName = assetData.assetType;
		MObject oAssetRoot = AssetRootNode::createAssetRoot(rigData.rigTemplateName.c_str(), getAssetTypeFromString(assetTypeName), dagMod);

		MObject oRigRoot = RigRootNode::createRigRoot(
			rigData.rigName.c_str(),
			rigData.rigType.c_str(),
			rigData.rigVersion,
			rigData.rigTemplateName.c_str(),
			oAssetRoot,
			dagMod
		);

		// Pass 1, layout included.
		RigModuleMap rigModuleMap;
		status = RigModuleNodeBase::buildModules(plans, oRigRoot, dagMod, rigModuleMap);
		if (!status)
		{
			return status;
		}

		status = parentModuleControls(rigModuleMap, rigData.rigModules, dagMod);
		if (!status)
		{
			return status;
		}

		// Execute the DAG modifier once for every module and the parenting pass.
		status = dagMod.doIt();
		if (!status)
		{
			MGlobal::displayError("Failed executing DAG Modifiers during rig creation: " + status.errorString());
			return status;
		}

		if (!rigData.rigConstraints.empty())
		{
			status = ParentConstraintNode::buildFromTemplate(rigData.rigConstraints, rigModuleMap, oRigRoot, dagMod);
			if (!status)
			{
				MGlobal::displayError("Failed building rig constraints.");
				dagMod.undoIt();
				return status;
			}
		}

		return MStatus::kSuccess;
	}
}

// TODO: optionally pass in the asset root. If none, we create one.
MStatus RigRootNode::createRig(const MString& jsonFilePath, const MString& jointNamespace, MDagModifier& dagMod)
{
	// Read phase: nothing touches the scene until the whole template validates, so a bad template leaves nothing behind.
	AssetRootData assetData;
	RigRootData rigData;
	std::vector<ModulePlan> plans;
	ValidationReport report;
	planRig(jsonFilePath, jointNamespace, assetData, rigData, plans, report);

	displayReport(report);
	if (report.hasErrors())
	{
		MGlobal::displayError(MString("Rig template failed validation with ") + static_cast<int>(report.errors.size())
			+ " error(s). Nothing was built: " + jsonFilePath);
		return MStatus::kFailure;
	}

	// Write phase.
	const MString originalNamespace = MNamespace::currentNamespace();
	MNamespace::addNamespace(kRigNamespace);
	// TODO: we will have to move input joints into the namespace also
	MNamespace::setCurrentNamespace(kRigNamespace);

	MStatus status;
	try
	{
		status = buildRig(rigData, assetData, plans, dagMod);
	}
	catch (const std::exception& e)
	{
		MGlobal::displayError(MString("Exception raised when building rig: ") + e.what());
		dagMod.undoIt();
		status = MStatus::kFailure;
	}

	MNamespace::setCurrentNamespace(originalNamespace);
	if (status)
	{
		MGlobal::displayInfo("Rig successfully created from template: " + jsonFilePath);
	}
	return status;
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

// Read by long name only. MSyntax::addFlag requires a short name of at most 3 letters, so it appears only here.
const char* CreateRigCmd::kNamespaceFlag = "-namespace";

MSyntax CreateRigCmd::newSyntax()
{
    MSyntax syntax;
    syntax.addArg(MSyntax::kString);
    CHECK_MSTATUS(syntax.addFlag("-ns", kNamespaceFlag, MSyntax::kString));
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

    MString jointNamespace(":");
    if (argData.isFlagSet(kNamespaceFlag))
    {
        status = argData.getFlagArgument(kNamespaceFlag, 0, jointNamespace);
        CHECK_MSTATUS_AND_RETURN_IT(status);
    }

    // Delegate creation directly to AssetRootNode entry method
    status = RigRootNode::createRig(jsonFilePath, jointNamespace, fDagMod);
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