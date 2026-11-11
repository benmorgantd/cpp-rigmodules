// Internal dependencies
#include "maya/nodes/RigRoot.h"
#include "core/Constants.h"
#include "core/RigControlFilter.h"
#include "core/RigJsonStructs.h"
#include "core/RigTemplateValidation.h"
#include "core/SelectionSets.h"
#include "core/StringUtils.h"
#include "core/TemplateMirror.h"
#include "maya/modules/LayoutModule.h"
#include "maya/nodes/AssetRoot.h"
#include "maya/nodes/Constraints.h"
#include "maya/shared/DisplayLayerScene.h"
#include "maya/shared/JointMarkupScene.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/RigTraversal.h"
#include "maya/shared/SelectionSetScene.h"
#include "maya/shared/Utilities.h"

// Standard dependencies
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// Maya dependencies
#include <maya/MDagModifier.h>
#include <maya/MDagPath.h>
#include <maya/MFnAttribute.h>
#include <maya/MFnData.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnNumericData.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MNamespace.h>
#include <maya/MObjectArray.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MString.h>
#include <maya/MTypeId.h>

// Define the unique ID
MTypeId RigRootNode::id(0x00218);

// Static member definitions
MObject RigRootNode::aRigName;
MObject RigRootNode::aRigVersion;
MObject RigRootNode::aPluginVersion;
MObject RigRootNode::aPublishVersion;
MObject RigRootNode::aRigModules;
MObject RigRootNode::aRigTemplateName;
MObject RigRootNode::aAssetRoot;
MObject RigRootNode::aChildren;
MObject RigRootNode::aConstraints;
MObject RigRootNode::aJointDisplayLayer;
MObject RigRootNode::aMeshDisplayLayer;
MObject RigRootNode::aControlDisplayLayer;
MObject RigRootNode::aSelectionSets;

// Boilerplate constructor, destructor, and creator
RigRootNode::RigRootNode()
{
}
RigRootNode::~RigRootNode()
{
}

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

	// TODO: replace with constants references.
	aRigVersion = nAttr.create(RigAttr::RigVersion, RigAttr::RigVersion, MFnNumericData::kLong, 0, &status);
	nAttr.setStorable(true);
	addAttribute(aRigVersion);

	aPluginVersion = tAttr.create(RigAttr::PluginVersion, RigAttr::PluginVersion, MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(aPluginVersion);

	// The publish id ("20261108T142012Z_bmorgan"), written by the publish step, never by the build. A string,
	// because publish ids are a time and a user, so two people publishing never collide (D29).
	aPublishVersion = tAttr.create(RigAttr::PublishVersion, RigAttr::PublishVersion, MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(aPublishVersion);

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

	aJointDisplayLayer = msgAttr.create(RigAttr::JointDisplayLayer, RigAttr::JointDisplayLayer, &status);
	addAttribute(aJointDisplayLayer);

	aMeshDisplayLayer = msgAttr.create(RigAttr::MeshDisplayLayer, RigAttr::MeshDisplayLayer, &status);
	addAttribute(aMeshDisplayLayer);

	aControlDisplayLayer = msgAttr.create(RigAttr::ControlDisplayLayer, RigAttr::ControlDisplayLayer, &status);
	addAttribute(aControlDisplayLayer);

	aSelectionSets = msgAttr.create(RigAttr::SelectionSets, RigAttr::SelectionSets, &status);
	msgAttr.setArray(true);
	addAttribute(aSelectionSets);

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

    MPlug pRigVersion(oRigRoot, RigRootNode::aRigVersion);
    dagMod.newPlugValueInt(pRigVersion, version);

    MPlug pPluginVersion(oRigRoot, RigRootNode::aPluginVersion);
    dagMod.newPlugValueString(pPluginVersion, PluginInfo::Version);

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

	// renameNode silently makes a clashing name unique, so a module would no longer match its template name
	// and a joint would no longer match its skeleton name. Checks each target name against the scene and the others.
	void checkTargetNamesFree(
		const MString& rigNamespace,
		const std::vector<RigModuleData>& modules,
		const MDagPathArray& joints,
		ValidationReport& report)
	{
		std::unordered_map<std::string, std::string> takenBy;   // target name -> the "where" that claimed it

		// oSelf is the node being renamed. A joint already in the rig namespace already holds its target name.
		auto checkName = [&](const MString& targetName, const MObject& oSelf, const std::string& where)
		{
			const std::string target = targetName.asChar();
			auto [it, inserted] = takenBy.emplace(target, where);
			if (!inserted)
			{
				report.addError(where, "would be named '" + target + "', the same as " + it->second);
				return;
			}

			MObject oExisting;
			if (DagUtils::getMObjectFromName(targetName, oExisting) == MStatus::kSuccess && oExisting != oSelf)
			{
				report.addError(where, "a node named '" + target + "' already exists in the scene");
			}
		};

		checkName(DagUtils::getAbsoluteName(rigNamespace, RigNaming::LayoutModule), MObject::kNullObj, RigNaming::LayoutModule);
		for (std::size_t i = 0; i < modules.size(); ++i)
		{
			checkName(DagUtils::getAbsoluteName(rigNamespace, modules[i].name.c_str()), MObject::kNullObj,
				RigTemplateValidation::modulePath(i, modules[i]));
		}
		for (unsigned int i = 0; i < joints.length(); ++i)
		{
			const MFnDependencyNode fnJoint(joints[i].node());
			checkName(DagUtils::getAbsoluteName(rigNamespace, MNamespace::stripNamespaceFromName(fnJoint.name())), joints[i].node(),
				joints[i].fullPathName().asChar());
		}
	}

	// The asset owns the namespace, so the rig namespace holds at most one asset root, shared by every rig variant
	// built into it (D29). Returns the existing one, or a null MObject when the build should create it.
	MObject findAssetRoot(const RigBuildInputs& inputs, const RigRootData& rigData, ValidationReport& report)
	{
		const MString absoluteNamespace = MString(":") + inputs.rigNamespace;
		if (!MNamespace::namespaceExists(absoluteNamespace))
		{
			return MObject::kNullObj;
		}

		MStatus status;
		const MObjectArray namespaceNodes = MNamespace::getNamespaceObjects(absoluteNamespace, false, &status);
		if (!status)
		{
			report.addError(absoluteNamespace.asChar(), "could not list the namespace's nodes: " + std::string(status.errorString().asChar()));
			return MObject::kNullObj;
		}

		std::vector<MObject> assetRoots;
		for (unsigned int i = 0; i < namespaceNodes.length(); ++i)
		{
			if (MFnDependencyNode(namespaceNodes[i]).typeId() == AssetRootNode::id)
			{
				assetRoots.push_back(namespaceNodes[i]);
			}
		}
		if (assetRoots.empty())
		{
			return MObject::kNullObj;
		}
		if (assetRoots.size() > 1)
		{
			report.addError(absoluteNamespace.asChar(), "holds " + std::to_string(assetRoots.size())
				+ " asset roots. A namespace belongs to one asset");
			return MObject::kNullObj;
		}

		MObject oAssetRoot = assetRoots[0];
		const MFnDependencyNode fnAssetRoot(oAssetRoot);
		const std::string existingId = MPlug(oAssetRoot, AssetRootNode::aAssetId).asString().asChar();
		if (!StringUtils::equalsIgnoreCase(existingId, inputs.assetId))
		{
			report.addError(RigAttr::AssetId, "'" + inputs.assetId + "' does not match '" + existingId + "' on "
				+ fnAssetRoot.name().asChar() + ", which owns the namespace");
		}

		MPlugArray pRigRoots;
		MPlug(oAssetRoot, AssetRootNode::aChildren).connectedTo(pRigRoots, false, true);
		for (unsigned int i = 0; i < pRigRoots.length(); ++i)
		{
			MObject oRigRoot = pRigRoots[i].node();
			if (MFnDependencyNode(oRigRoot).typeId() != RigRootNode::id)
			{
				continue;
			}
			if (MPlug(oRigRoot, RigRootNode::aRigName).asString() == rigData.rigName.c_str())
			{
				report.addError(TemplateKey::RigName, "'" + rigData.rigName + "' is already built for this asset by "
					+ MFnDependencyNode(oRigRoot).name().asChar());
			}
		}
		return oAssetRoot;
	}

	// Write phase. Keeps each joint's name and changes only its namespace. Names were checked free in the read phase.
	MStatus queueJointMoves(const MDagPathArray& joints, const MString& rigNamespace, MDagModifier& dagMod)
	{
		for (unsigned int i = 0; i < joints.length(); ++i)
		{
			MObject oJoint = joints[i].node();
			const MFnDependencyNode fnJoint(oJoint);
			const MString targetName = DagUtils::getAbsoluteName(rigNamespace, MNamespace::stripNamespaceFromName(fnJoint.name()));
			if (fnJoint.absoluteName() == targetName)
			{
				continue;
			}

			MStatus status = dagMod.renameNode(oJoint, targetName);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		return MStatus::kSuccess;
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
		const RigBuildInputs& inputs,
		RigRootData& rigData,
		MObject& oExistingAssetRoot,
		std::vector<ModulePlan>& plans,
		ValidationReport& report)
	{
		if (!loadRigTemplate(inputs.templatePath.asChar(), rigData, report))
		{
			return;
		}

		// Mirrors first, so every check below and the build see them as ordinary entries (D28).
		TemplateMirror::expandMirrors(rigData, report);
		RigTemplateValidation::validateHeader(rigData, report);
		oExistingAssetRoot = findAssetRoot(inputs, rigData, report);
		RigTemplateValidation::validateModuleNames(rigData.rigModules, report);
		checkTargetNamesFree(inputs.rigNamespace, rigData.rigModules, inputs.joints, report);

		// Module plans claim joints from this set, so bad markup stops here instead of surfacing as missing joints.
		const std::size_t errorCountBeforeMarkup = report.errors.size();
		MarkedJointSet markedJoints;
		JointMarkupScene::collectMarkedJoints(inputs.joints, markedJoints, report);
		if (report.errors.size() > errorCountBeforeMarkup)
		{
			return;
		}

		plans.push_back(makeLayoutPlan());
		for (ModulePlan& plan : RigModuleNodeBase::planModules(rigData.rigModules, markedJoints, inputs.checkMirrorPositions, report))
		{
			plans.push_back(std::move(plan));
		}

		ModuleInterfaceMap interfaces;
		for (const ModulePlan& plan : plans)
		{
			interfaces.emplace(plan.name, plan.moduleInterface);
		}
		RigTemplateValidation::validateConnections(rigData, interfaces, report);
		RigTemplateValidation::validateControlMarkup(rigData, interfaces, report);

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

				RigModuleDagReparentArgs args
				{
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

	// Write phase, after the first doIt because the controls are found through the built modules. Replaces the rig's
	// three layers (D38): the input joints and every control, plus an empty mesh layer (the model is imported after the build, D38). Each layer
	// goes on its own message attribute of the rig root.
	MStatus queueDisplayLayers(
		const RigBuildInputs& inputs,
		const MObject& oRigRoot,
		MDagModifier& dagMod)
	{
		MObjectArray oControls;
		MStatus status = RigTraversal::getRigControls(oRigRoot, RigControlFilter{}, oControls);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		std::vector<DisplayLayerSpec> specs(3);
		specs[0].name = DagUtils::getAbsoluteName(inputs.rigNamespace, RigNaming::JointLayer);
		for (unsigned int i = 0; i < inputs.joints.length(); ++i)
		{
			specs[0].members.append(inputs.joints[i].node());
		}
		specs[1].name = DagUtils::getAbsoluteName(inputs.rigNamespace, RigNaming::MeshLayer);
		specs[2].name = DagUtils::getAbsoluteName(inputs.rigNamespace, RigNaming::ControlLayer);
		specs[2].members = oControls;

		MObjectArray oLayers;
		status = DisplayLayerScene::replaceLayers(specs, dagMod, oLayers);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		const MObject oSingleAttrs[] = { RigRootNode::aJointDisplayLayer, RigRootNode::aMeshDisplayLayer, RigRootNode::aControlDisplayLayer };
		for (unsigned int i = 0; i < oLayers.length(); ++i)
		{
			MPlug pMessage = MFnDependencyNode(oLayers[i]).findPlug("message", false);
			status = dagMod.connect(pMessage, MPlug(oRigRoot, oSingleAttrs[i]));
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		return dagMod.doIt();
	}

	// Write phase, after the display layers. Replaces the rig's selection sets (D39): one hierarchy for the controls
	// and one for the skin joints, each with a Left, Right and Center set and a set per module. Each set goes on
	// RigRoot.selectionSets. SelectionSets::planSets lays them out, and this turns its indices into scene nodes.
	MStatus queueSelectionSets(
		const RigBuildInputs& inputs,
		const MObject& oRigRoot,
		MDagModifier& dagMod)
	{
		MObjectArray oControls;
		std::vector<SelectionSets::Item> controlItems;
		MObjectArray oModules;
		MStatus status = RigTraversal::getRigModules(oRigRoot, oModules);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		for (unsigned int m = 0; m < oModules.length(); ++m)
		{
			MObjectArray oModuleControls;
			status = RigTraversal::getModuleControls(oModules[m], oModuleControls);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			for (unsigned int c = 0; c < oModuleControls.length(); ++c)
			{
				ControlMarkup markup;
				status = RigTraversal::getControlMarkup(oModuleControls[c], markup);
				CHECK_MSTATUS_AND_RETURN_IT(status);
				oControls.append(oModuleControls[c]);
				controlItems.push_back({ MNamespace::stripNamespaceFromName(MFnDependencyNode(oModules[m]).name()).asChar(), markup.side });
			}
		}

		MObjectArray oSkinJoints;
		std::vector<SelectionSets::Item> skinItems;
		for (unsigned int j = 0; j < inputs.joints.length(); ++j)
		{
			const MObject oJoint = inputs.joints[j].node();
			bool hasMarkup = false;
			JointMarkup markup;
			status = JointMarkupScene::readMarkup(oJoint, hasMarkup, markup);
			CHECK_MSTATUS_AND_RETURN_IT(status);

			const MObject oModule = RigTraversal::getJointModule(oJoint);
			const SelectionSets::Item item{ oModule.isNull() ? std::string() : MNamespace::stripNamespaceFromName(MFnDependencyNode(oModule).name()).asChar(), markup.side };
			if (markup.isSkinned)
			{
				oSkinJoints.append(oJoint);
				skinItems.push_back(item);
			}
		}

		struct Group
		{
			const char* kind;
			const MObjectArray& nodes;
			const std::vector<SelectionSets::Item>& items;
		};
		const Group groups[] =
		{
			{ SelectionSetKind::Controls, oControls, controlItems },
			{ SelectionSetKind::Joints, oSkinJoints, skinItems }
		};

		std::vector<SelectionSetSpec> specs;
		for (const Group& group : groups)
		{
			const unsigned int first = static_cast<unsigned int>(specs.size());
			for (const SelectionSets::Set& set : SelectionSets::planSets(group.kind, group.items))
			{
				SelectionSetSpec spec;
				spec.name = DagUtils::getAbsoluteName(inputs.rigNamespace, set.name.c_str());
				spec.markup = set.markup.c_str();
				for (const std::size_t item : set.items)
				{
					spec.members.append(group.nodes[static_cast<unsigned int>(item)]);
				}
				for (const std::size_t child : set.children)
				{
					spec.children.push_back(first + static_cast<unsigned int>(child));
				}
				specs.push_back(spec);
			}
		}

		MObjectArray oSets;
		status = SelectionSetScene::replaceSets(specs, dagMod, oSets);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		MPlug pSets(oRigRoot, RigRootNode::aSelectionSets);
		for (unsigned int i = 0; i < oSets.length(); ++i)
		{
			MPlug pMessage = MFnDependencyNode(oSets[i]).findPlug("message", false);
			status = dagMod.connect(pMessage, pSets.elementByLogicalIndex(i));
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		return dagMod.doIt();
	}

	// Write phase. Runs only on a clean report. Constraints come after the first doIt because they add an enum
	// attribute to controls that must exist by then.
	MStatus buildRig(
		const RigBuildInputs& inputs,
		const RigRootData& rigData,
		MObject oAssetRoot,
		const std::vector<ModulePlan>& plans,
		MDagModifier& dagMod)
	{
		MStatus status;

		if (oAssetRoot.isNull())
		{
			oAssetRoot = AssetRootNode::createAssetRoot(inputs.assetId.c_str(), dagMod);
		}

		MObject oRigRoot = RigRootNode::createRigRoot(
			rigData.rigName.c_str(),
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

		status = queueJointMoves(inputs.joints, inputs.rigNamespace, dagMod);
		if (!status)
		{
			MGlobal::displayError("Failed queueing the input joint moves into the rig namespace.");
			return status;
		}

		// Execute the DAG modifier once for every module, the parenting pass and the joint moves.
		status = dagMod.doIt();
		if (!status)
		{
			MGlobal::displayError("Failed executing DAG Modifiers during rig creation: " + status.errorString());
			return status;
		}

		status = queueDisplayLayers(inputs, oRigRoot, dagMod);
		if (!status)
		{
			MGlobal::displayError("Failed building the rig's display layers.");
			dagMod.undoIt();
			return status;
		}

		status = queueSelectionSets(inputs, oRigRoot, dagMod);
		if (!status)
		{
			MGlobal::displayError("Failed building the rig's selection sets.");
			dagMod.undoIt();
			return status;
		}

		if (!rigData.rigConstraints.empty())
		{
			status = RigConstraintNode::buildFromTemplate(rigData.rigConstraints, rigModuleMap, oRigRoot, dagMod);
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

MStatus RigRootNode::createRig(const RigBuildInputs& inputs, MDagModifier& dagMod)
{
	// Read phase: nothing touches the scene until the whole template validates, so a bad template leaves nothing behind.
	RigRootData rigData;
	MObject oAssetRoot;
	std::vector<ModulePlan> plans;
	ValidationReport report;
	planRig(inputs, rigData, oAssetRoot, plans, report);

	ReportUtils::displayReport(report);
	if (report.hasErrors())
	{
		MGlobal::displayError(MString("Rig template failed validation with ") + static_cast<int>(report.errors.size())
			+ " error(s). Nothing was built: " + inputs.templatePath);
		return MStatus::kFailure;
	}

	// Write phase.
	const ScopedRigNamespace rigNamespaceScope(inputs.rigNamespace);
	MStatus status = rigNamespaceScope.status();
	if (!status)
	{
		return status;
	}

	try
	{
		status = buildRig(inputs, rigData, oAssetRoot, plans, dagMod);
	}
	catch (const std::exception& e)
	{
		MGlobal::displayError(MString("Exception raised when building rig: ") + e.what());
		dagMod.undoIt();
		status = MStatus::kFailure;
	}

	if (status)
	{
		MGlobal::displayInfo("Rig successfully created from template: " + inputs.templatePath);
	}
	return status;
}
