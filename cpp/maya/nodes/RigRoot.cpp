// Internal dependencies
#include "maya/nodes/RigRoot.h"
#include "core/AssetId.h"
#include "core/Constants.h"
#include "core/Hash64.h"
#include "core/NamingConvention.h"
#include "core/RigControlFilter.h"
#include "core/RigJsonStructs.h"
#include "core/RigTemplateValidation.h"
#include "core/SelectionSets.h"
#include "core/SkeletonFile.h"
#include "core/StringUtils.h"
#include "core/TemplateMirror.h"
#include "maya/modules/LayoutModule.h"
#include "maya/nodes/Constraints.h"
#include "maya/shared/DisplayLayerScene.h"
#include "maya/shared/JointMarkupScene.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/RigTraversal.h"
#include "maya/shared/SelectionSetScene.h"
#include "maya/shared/SkeletonScene.h"
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
#include <maya/MFnDagNode.h>
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
#include <maya/MString.h>
#include <maya/MTypeId.h>

// Define the unique ID
MTypeId RigRootNode::id(0x00218);

// Static member definitions
MObject RigRootNode::aAssetId;
MObject RigRootNode::aRigName;
MObject RigRootNode::aSkeletonHash;
MObject RigRootNode::aRigVersion;
MObject RigRootNode::aPluginVersion;
MObject RigRootNode::aPublishVersion;
MObject RigRootNode::aRigModules;
MObject RigRootNode::aRigTemplateName;
MObject RigRootNode::aChildren;
MObject RigRootNode::aConstraints;
MObject RigRootNode::aJointDisplayLayer;
MObject RigRootNode::aMeshDisplayLayer;
MObject RigRootNode::aControlDisplayLayer;
MObject RigRootNode::aSelectionSets;
MObject RigRootNode::aAllGroup;
MObject RigRootNode::aMeshGroup;
MObject RigRootNode::aLook;
MObject RigRootNode::aParentRig;
MObject RigRootNode::aAttachments;

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

	// The asset the rig belongs to (D29). It lived on a separate asset root node until D54 made a namespace hold one rig.
	aAssetId = tAttr.create(RigAttr::AssetId, RigAttr::AssetId, MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(aAssetId);

	aRigName = tAttr.create(RigAttr::RigName, RigAttr::RigName, MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(aRigName);

	// The input joints' skeletonHash at build (D54), so looks, weights and library files can be matched to the skeleton.
	aSkeletonHash = tAttr.create(RigAttr::SkeletonHash, RigAttr::SkeletonHash, MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(aSkeletonHash);

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

	aAllGroup = msgAttr.create(RigAttr::AllGroup, RigAttr::AllGroup, &status);
	addAttribute(aAllGroup);

	aMeshGroup = msgAttr.create(RigAttr::MeshGroup, RigAttr::MeshGroup, &status);
	addAttribute(aMeshGroup);

	// The look the rig wears (D54): connects to a lookRoot node's rigRoot. None until a look is applied.
	aLook = msgAttr.create(RigAttr::Look, RigAttr::Look, &status);
	addAttribute(aLook);

	// The rig an attachment follows (D55). Only a record for tools: the follow itself is socket matrices connected
	// across the two rigs, so these two add nothing to evaluation.
	aParentRig = msgAttr.create(RigAttr::ParentRig, RigAttr::ParentRig, &status);
	addAttribute(aParentRig);

	aAttachments = msgAttr.create(RigAttr::Attachments, RigAttr::Attachments, &status);
	msgAttr.setArray(true);
	addAttribute(aAttachments);

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
    const MString& assetId,
    const MString& name,
    const MString& skeletonHash,
    int version,
    const MString& templateName,
    MDagModifier& dagMod)
{
    MObject oRigRoot = dagMod.MDGModifier::createNode(RigRootNode::id);

    MPlug pAssetId(oRigRoot, RigRootNode::aAssetId);
    dagMod.newPlugValueString(pAssetId, assetId);

    if (!name.isEmpty())
    {
        MPlug pRigName(oRigRoot, RigRootNode::aRigName);
        dagMod.newPlugValueString(pRigName, name);
    }

    MPlug pSkeletonHash(oRigRoot, RigRootNode::aSkeletonHash);
    dagMod.newPlugValueString(pSkeletonHash, skeletonHash);

    MPlug pRigVersion(oRigRoot, RigRootNode::aRigVersion);
    dagMod.newPlugValueInt(pRigVersion, version);

    MPlug pPluginVersion(oRigRoot, RigRootNode::aPluginVersion);
    dagMod.newPlugValueString(pPluginVersion, PluginInfo::Version);

    if (!templateName.isEmpty())
    {
        MPlug pTemplate(oRigRoot, RigRootNode::aRigTemplateName);
        dagMod.newPlugValueString(pTemplate, templateName);
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
		checkName(DagUtils::getAbsoluteName(rigNamespace, RigNaming::AllGroup), MObject::kNullObj, RigNaming::AllGroup);
		checkName(DagUtils::getAbsoluteName(rigNamespace, RigNaming::MeshGroup), MObject::kNullObj, RigNaming::MeshGroup);
		for (std::size_t i = 0; i < modules.size(); ++i)
		{
			checkName(DagUtils::getAbsoluteName(rigNamespace, modules[i].name.c_str()), MObject::kNullObj,
				RigTemplateValidation::modulePath(i, modules[i]));
		}
		for (unsigned int i = 0; i < joints.length(); ++i)
		{
			const MFnDependencyNode fnJoint(joints[i].node());

			// The controls take their names from their joints, so a joint name outside the convention is an error (D58).
			NamingConvention::Name parsedName;
			const std::string jointName = MNamespace::stripNamespaceFromName(fnJoint.name()).asChar();
			if (!NamingConvention::parse(jointName, parsedName) || parsedName.kind != NamingConvention::Kind::Joint)
			{
				report.addError(joints[i].fullPathName().asChar(), "joint name '" + jointName + "' must be " + NamingConvention::Form
					+ " ending in " + NamingConvention::kindToken(NamingConvention::Kind::Joint) + ". Rename it with cmds.renameRigNodes");
			}
			checkName(DagUtils::getAbsoluteName(rigNamespace, MNamespace::stripNamespaceFromName(fnJoint.name())), joints[i].node(),
				joints[i].fullPathName().asChar());
		}
	}

	// A namespace holds one rig (D54), so its display layers, selection sets and groups, named per namespace, are the
	// rig's own. Fails when the namespace already holds a rig root, whatever its asset or rig name.
	void checkNamespaceFree(const MString& rigNamespace, ValidationReport& report)
	{
		const MString absoluteNamespace = MString(":") + rigNamespace;
		if (!MNamespace::namespaceExists(absoluteNamespace))
		{
			return;
		}

		MStatus status;
		const MObjectArray namespaceNodes = MNamespace::getNamespaceObjects(absoluteNamespace, false, &status);
		if (!status)
		{
			report.addError(absoluteNamespace.asChar(), "could not list the namespace's nodes: " + std::string(status.errorString().asChar()));
			return;
		}

		for (unsigned int i = 0; i < namespaceNodes.length(); ++i)
		{
			const MFnDependencyNode fnNode(namespaceNodes[i]);
			if (fnNode.typeId() == RigRootNode::id)
			{
				report.addError(absoluteNamespace.asChar(), std::string("already holds the rig ") + fnNode.name().asChar()
					+ ". A namespace holds one rig: pass another namespace");
			}
		}
	}

	// Read phase. A template with "parent:" ids is an attachment and needs the rig it follows, and a rig that is not
	// an attachment refuses one. Every socket the template takes is looked up in the parent's namespace and every
	// missing one is reported, so one attempt lists them all (D55). outParentNamespace is the parent's, ":" form
	// aside, empty when there is no parent.
	void checkParentRig(
		const RigBuildInputs& inputs,
		const RigRootData& rigData,
		const MString& rigNamespace,
		MString& outParentNamespace,
		ValidationReport& report)
	{
		const std::vector<std::string> socketIds = RigTemplateValidation::parentSocketIds(rigData);
		if (inputs.oParentRigRoot.isNull())
		{
			if (!socketIds.empty())
			{
				report.addError(ParentRigId::Prefix, "the template takes sockets from a parent rig (" + StringUtils::joinNames(socketIds)
					+ "), so it needs a parentRig");
			}
			return;
		}
		if (socketIds.empty())
		{
			report.addError(RigAttr::ParentRig, std::string("the template has no '") + ParentRigId::Prefix + "' ids, so it does not attach to a rig");
			return;
		}

		const MFnDependencyNode fnParent(inputs.oParentRigRoot);
		if (fnParent.typeId() != RigRootNode::id)
		{
			report.addError(RigAttr::ParentRig, std::string(fnParent.name().asChar()) + " is not a rig root");
			return;
		}

		const MString absoluteParentNamespace = DagUtils::getAbsoluteNamespace(inputs.oParentRigRoot);
		if (absoluteParentNamespace == MString(":") + rigNamespace)
		{
			report.addError(RigAttr::ParentRig, "the parent rig is in the namespace this rig builds into. An attachment is its own rig, in its own namespace");
			return;
		}
		outParentNamespace = absoluteParentNamespace == ":" ? MString() : absoluteParentNamespace;

		for (const std::string& socketId : socketIds)
		{
			MObject oModule;
			RigModuleSocketInfo socketInfo;
			std::string error;
			if (!RigModuleNodeBase::findSocketById(outParentNamespace, socketId, oModule, socketInfo, error))
			{
				report.addError(std::string(ParentRigId::Prefix) + socketId, error + " in the parent rig " + fnParent.name().asChar());
			}
		}
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

	// Read phase. Reads the scene, never writes it. Fills plans (layout first) as far as validation gets, the namespace
	// the rig goes into (the default needs the template's rigName) and the input joints' skeleton hash.
	void planRig(
		const RigBuildInputs& inputs,
		RigRootData& rigData,
		MString& outRigNamespace,
		MString& outParentNamespace,
		MString& outSkeletonHash,
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
		outRigNamespace = inputs.rigNamespace.has_value()
			? *inputs.rigNamespace
			: MString((AssetId::name(inputs.assetId) + "_" + rigData.rigName).c_str());
		checkNamespaceFree(outRigNamespace, report);
		checkParentRig(inputs, rigData, outRigNamespace, outParentNamespace, report);
		RigTemplateValidation::validateModuleNames(rigData.rigModules, report);
		checkTargetNamesFree(outRigNamespace, rigData.rigModules, inputs.joints, report);

		// Module plans claim joints from this set, so bad markup stops here instead of surfacing as missing joints.
		const std::size_t errorCountBeforeMarkup = report.errors.size();
		MarkedJointSet markedJoints;
		JointMarkupScene::collectMarkedJoints(inputs.joints, markedJoints, report);
		if (report.errors.size() > errorCountBeforeMarkup)
		{
			return;
		}

		// Read before the build moves anything, so the stamp is the skeleton as it was handed in. readSkeleton repeats
		// collectMarkedJoints' checks, which have passed, so the hash has one home and one reader (D54).
		Skeleton skeleton;
		SkeletonScene::readSkeleton(inputs.joints, skeleton, report);
		if (report.errors.size() > errorCountBeforeMarkup)
		{
			return;
		}
		outSkeletonHash = Hash64::format(skeletonHash(skeleton)).c_str();

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

	// Pass 2. Every child plug gets a parent: its parentStructure entry, or layout_00_M_MOD.end when the template does not list it.
	// The template was validated, so a failed lookup here means a module built something its plan did not declare.
	// A "parent:" entry takes a socket of the rig this one is attached to (D55): the matrix connects across, and the DAG
	// is left alone.
	MStatus parentModuleControls(
		const RigModuleMap& rigModuleMap,
		const std::vector<RigModuleData>& modules,
		const MString& parentNamespace,
		MDagModifier& dagMod)
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
				// An internal plug takes the parent of the plug it follows (settings follows start), never its own entry.
				auto it = moduleData.parentStructure.find(childPlugInfo.followsPlug.empty() ? plugName : childPlugInfo.followsPlug);
				if (it == moduleData.parentStructure.end() && !childPlugInfo.defaultsToPlug.empty())
				{
					it = moduleData.parentStructure.find(childPlugInfo.defaultsToPlug);
				}
				const std::string& socketIdentifier = it != moduleData.parentStructure.end() ? it->second : defaultSocket;

				const RigModuleBuildResult* pParentModule = nullptr;
				const RigModuleSocketInfo* pSocket = nullptr;
				const bool isCrossRig = RigTemplateValidation::isParentSocketId(socketIdentifier);
				MObject oParentRigModule;
				RigModuleSocketInfo parentRigSocket;
				bool found = false;
				if (isCrossRig)
				{
					std::string error;
					found = RigModuleNodeBase::findSocketById(
						parentNamespace, RigTemplateValidation::stripParentPrefix(socketIdentifier), oParentRigModule, parentRigSocket, error);
					pSocket = &parentRigSocket;
				}
				else
				{
					found = RigModuleNodeBase::lookupParentSocket(rigModuleMap, socketIdentifier, pParentModule, pSocket);
				}
				if (!found)
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
					isCrossRig ? oParentRigModule : pParentModule->moduleNode,
					dagMod,
					!isCrossRig
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
		const MString& rigNamespace,
		const MObject& oRigRoot,
		MDagModifier& dagMod)
	{
		MObjectArray oControls;
		MStatus status = RigTraversal::getRigControls(oRigRoot, RigControlFilter{}, oControls);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		std::vector<DisplayLayerSpec> specs(3);
		specs[0].name = DagUtils::getAbsoluteName(rigNamespace, RigNaming::JointLayer);
		for (unsigned int i = 0; i < inputs.joints.length(); ++i)
		{
			specs[0].members.append(inputs.joints[i].node());
		}
		specs[1].name = DagUtils::getAbsoluteName(rigNamespace, RigNaming::MeshLayer);
		// The model is not selectable while animating, so it can't be picked by mistake.
		specs[1].displayType = DisplayLayerDisplayType::Reference;
		specs[2].name = DagUtils::getAbsoluteName(rigNamespace, RigNaming::ControlLayer);
		specs[2].members = oControls;
		// Not drawing the controls during playback is the cheapest way to skip their custom draw override (D37).
		specs[2].hideOnPlayback = true;

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
		const MString& rigNamespace,
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
				spec.name = DagUtils::getAbsoluteName(rigNamespace, set.name.c_str());
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

	// Write phase, after the first doIt. Locks every control's rotate order, which its module set when it created
	// the control (D40). Queued as MEL because MDagModifier has no lock call, and so that undo and redo replay it.
	MStatus queueRotateOrderLocks(const MObject& oRigRoot, MDagModifier& dagMod)
	{
		MObjectArray oControls;
		MStatus status = RigTraversal::getRigControls(oRigRoot, RigControlFilter{}, oControls);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		for (unsigned int i = 0; i < oControls.length(); ++i)
		{
			const MFnDagNode fnControl(oControls[i], &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			status = dagMod.commandToExecute(MString("setAttr -lock true ") + MelUtils::quoted(fnControl.fullPathName() + ".rotateOrder"));
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		return dagMod.doIt();
	}

	// Write phase, after the first doIt. Makes rig_all, the parent of the rig's topmost joints and controls, and mesh_grp
	// under it, for the model import to fill (D43). Each goes on its own message attribute of the rig root. Their
	// transforms are locked and hidden, leaving visibility. Inheritance is off for joints and controls, so reparenting
	// them under an identity group changes no result.
	MStatus queueRigGroups(
		const RigBuildInputs& inputs,
		const MString& rigNamespace,
		const MObject& oRigRoot,
		MDagModifier& dagMod)
	{
		MStatus status;
		MObject oAll = dagMod.createNode("transform", MObject::kNullObj, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = dagMod.renameNode(oAll, DagUtils::getAbsoluteName(rigNamespace, RigNaming::AllGroup));
		CHECK_MSTATUS_AND_RETURN_IT(status);
		MObject oMeshGroup = dagMod.createNode("transform", oAll, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = dagMod.renameNode(oMeshGroup, DagUtils::getAbsoluteName(rigNamespace, RigNaming::MeshGroup));
		CHECK_MSTATUS_AND_RETURN_IT(status);

		// Joints and controls are measured apart: a joint is topmost among the input joints, a control among the controls.
		MObjectArray oJoints;
		for (unsigned int i = 0; i < inputs.joints.length(); ++i)
		{
			oJoints.append(inputs.joints[i].node());
		}
		MObjectArray oControls;
		status = RigTraversal::getRigControls(oRigRoot, RigControlFilter{}, oControls);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		MObjectArray oTopmost = DagUtils::getTopmostNodes(oJoints);
		const MObjectArray oTopControls = DagUtils::getTopmostNodes(oControls);
		for (unsigned int i = 0; i < oTopControls.length(); ++i)
		{
			oTopmost.append(oTopControls[i]);
		}
		for (unsigned int i = 0; i < oTopmost.length(); ++i)
		{
			status = dagMod.reparentNode(oTopmost[i], oAll);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}

		const MObject oGroups[] = { oAll, oMeshGroup };
		const MObject oAttrs[] = { RigRootNode::aAllGroup, RigRootNode::aMeshGroup };
		for (int i = 0; i < 2; ++i)
		{
			MPlug pMessage = MFnDependencyNode(oGroups[i]).findPlug("message", false);
			status = dagMod.connect(pMessage, MPlug(oRigRoot, oAttrs[i]));
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		status = dagMod.doIt();
		CHECK_MSTATUS_AND_RETURN_IT(status);

		// After the doIt, because the lock commands name the transforms by path.
		for (const MObject& oGroup : oGroups)
		{
			status = DagUtils::queueLockAndHideTransform(oGroup, dagMod);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		return dagMod.doIt();
	}

	// Write phase, after the first doIt. Records the parent rig: its attachments array to this rig root's parentRig.
	MStatus queueParentRigLink(const RigBuildInputs& inputs, const MObject& oRigRoot, MDagModifier& dagMod)
	{
		if (inputs.oParentRigRoot.isNull())
		{
			return MStatus::kSuccess;
		}

		MPlug pAttachments(inputs.oParentRigRoot, RigRootNode::aAttachments);
		MPlug pSlot = pAttachments.elementByLogicalIndex(pAttachments.evaluateNumElements());
		MStatus status = dagMod.connect(pSlot, MPlug(oRigRoot, RigRootNode::aParentRig));
		CHECK_MSTATUS_AND_RETURN_IT(status);
		return dagMod.doIt();
	}

	// Write phase, after the constraints. A control the parent rig drives alone is hidden, locked and left at its rest
	// values (a new control is already there), so keys left on it can't surprise an animator later (D55).
	MStatus queueDrivenControlLocks(
		const std::vector<RigConstraintData>& constraints,
		const RigModuleMap& rigModuleMap,
		MDagModifier& dagMod)
	{
		bool queued = false;
		for (const RigConstraintData& constraint : constraints)
		{
			if (!RigTemplateValidation::isDrivenByParent(constraint))
			{
				continue;
			}

			const RigModuleBuildResult* pModule = nullptr;
			const RigModuleChildPlugInfo* pChildPlug = nullptr;
			if (!RigModuleNodeBase::lookupChildPlug(rigModuleMap, constraint.target, pModule, pChildPlug) || pChildPlug->oTransform.isNull())
			{
				MGlobal::displayError(MString("Constraint target has no control to lock: ") + constraint.target.c_str());
				return MStatus::kFailure;
			}

			MStatus status = DagUtils::queueLockAndHideTransform(pChildPlug->oTransform, dagMod);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			status = DagUtils::queueHideAndLockVisibility(pChildPlug->oTransform, dagMod);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			queued = true;
		}
		return queued ? dagMod.doIt() : MStatus::kSuccess;
	}

	// Write phase. Runs only on a clean report. Constraints come after the first doIt because they add an enum
	// attribute to controls that must exist by then.
	MStatus buildRig(
		const RigBuildInputs& inputs,
		const MString& rigNamespace,
		const MString& skeletonHashText,
		const MString& parentNamespace,
		const RigRootData& rigData,
		const std::vector<ModulePlan>& plans,
		MDagModifier& dagMod)
	{
		MStatus status;

		MObject oRigRoot = RigRootNode::createRigRoot(
			inputs.assetId.c_str(),
			rigData.rigName.c_str(),
			skeletonHashText,
			rigData.rigVersion,
			rigData.rigTemplateName.c_str(),
			dagMod
		);

		// Pass 1, layout included.
		RigModuleMap rigModuleMap;
		status = RigModuleNodeBase::buildModules(plans, oRigRoot, dagMod, rigModuleMap);
		if (!status)
		{
			return status;
		}

		status = parentModuleControls(rigModuleMap, rigData.rigModules, parentNamespace, dagMod);
		if (!status)
		{
			return status;
		}

		status = queueJointMoves(inputs.joints, rigNamespace, dagMod);
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

		status = queueDisplayLayers(inputs, rigNamespace, oRigRoot, dagMod);
		if (!status)
		{
			MGlobal::displayError("Failed building the rig's display layers.");
			dagMod.undoIt();
			return status;
		}

		status = queueSelectionSets(inputs, rigNamespace, oRigRoot, dagMod);
		if (!status)
		{
			MGlobal::displayError("Failed building the rig's selection sets.");
			dagMod.undoIt();
			return status;
		}

		status = queueRotateOrderLocks(oRigRoot, dagMod);
		if (!status)
		{
			MGlobal::displayError("Failed locking the controls' rotate orders.");
			dagMod.undoIt();
			return status;
		}

		status = queueRigGroups(inputs, rigNamespace, oRigRoot, dagMod);
		if (!status)
		{
			MGlobal::displayError("Failed building the rig's groups.");
			dagMod.undoIt();
			return status;
		}

		if (!rigData.rigConstraints.empty())
		{
			status = RigConstraintNode::buildFromTemplate(rigData.rigConstraints, rigModuleMap, parentNamespace, oRigRoot, dagMod);
			if (!status)
			{
				MGlobal::displayError("Failed building rig constraints.");
				dagMod.undoIt();
				return status;
			}

			status = queueDrivenControlLocks(rigData.rigConstraints, rigModuleMap, dagMod);
			if (!status)
			{
				MGlobal::displayError("Failed locking the controls the parent rig drives.");
				dagMod.undoIt();
				return status;
			}
		}

		status = queueParentRigLink(inputs, oRigRoot, dagMod);
		if (!status)
		{
			MGlobal::displayError("Failed recording the parent rig.");
			dagMod.undoIt();
			return status;
		}

		return MStatus::kSuccess;
	}
}

MStatus RigRootNode::createRig(const RigBuildInputs& inputs, MDagModifier& dagMod, MString& outRigNamespace)
{
	// Read phase: nothing touches the scene until the whole template validates, so a bad template leaves nothing behind.
	RigRootData rigData;
	MString rigNamespace;
	MString parentNamespace;
	MString skeletonHashText;
	std::vector<ModulePlan> plans;
	ValidationReport report;
	planRig(inputs, rigData, rigNamespace, parentNamespace, skeletonHashText, plans, report);

	ReportUtils::displayReport(report);
	if (report.hasErrors())
	{
		MGlobal::displayError(MString("Rig template failed validation with ") + static_cast<int>(report.errors.size())
			+ " error(s). Nothing was built: " + inputs.templatePath);
		return MStatus::kFailure;
	}

	// Write phase.
	const ScopedRigNamespace rigNamespaceScope(rigNamespace);
	MStatus status = rigNamespaceScope.status();
	if (!status)
	{
		return status;
	}

	try
	{
		status = buildRig(inputs, rigNamespace, skeletonHashText, parentNamespace, rigData, plans, dagMod);
	}
	catch (const std::exception& e)
	{
		MGlobal::displayError(MString("Exception raised when building rig: ") + e.what());
		dagMod.undoIt();
		status = MStatus::kFailure;
	}

	if (status)
	{
		outRigNamespace = rigNamespace;
		MGlobal::displayInfo("Rig successfully created from template: " + inputs.templatePath);
	}
	return status;
}
