// Internal dependencies
#include "maya/nodes/RigRoot.h"
#include "core/Constants.h"

// Maya dependencies
#include <maya/MDagModifier.h>
#include <maya/MFnAttribute.h>
#include <maya/MFnData.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnNumericData.h>
#include <maya/MFnTypedAttribute.h>
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
	MStatus status;

	// The rig root's data is strings and one number, all storable.
	auto addString = [&](MObject& aAttr, const char* name) -> MStatus
	{
		MStatus result;
		aAttr = tAttr.create(name, name, MFnData::kString, MObject::kNullObj, &result);
		CHECK_MSTATUS_AND_RETURN_IT(result);
		CHECK_MSTATUS_AND_RETURN_IT(tAttr.setStorable(true));
		return addAttribute(aAttr);
	};
	// Links to the nodes the build makes. Message attributes hold no data, so they are not storable.
	auto addMessage = [&](MObject& aAttr, const char* name, bool isArray) -> MStatus
	{
		MStatus result;
		aAttr = msgAttr.create(name, name, &result);
		CHECK_MSTATUS_AND_RETURN_IT(result);
		CHECK_MSTATUS_AND_RETURN_IT(msgAttr.setArray(isArray));
		return addAttribute(aAttr);
	};

	// The asset the rig belongs to (D29). It lived on a separate asset root node until D54 made a namespace hold one rig.
	CHECK_MSTATUS_AND_RETURN_IT(addString(aAssetId, RigAttr::AssetId));
	CHECK_MSTATUS_AND_RETURN_IT(addString(aRigName, RigAttr::RigName));
	// The input joints' skeletonHash at build (D54), so looks, weights and library files can be matched to the skeleton.
	CHECK_MSTATUS_AND_RETURN_IT(addString(aSkeletonHash, RigAttr::SkeletonHash));

	aRigVersion = nAttr.create(RigAttr::RigVersion, RigAttr::RigVersion, MFnNumericData::kLong, 0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aRigVersion));

	CHECK_MSTATUS_AND_RETURN_IT(addString(aPluginVersion, RigAttr::PluginVersion));
	// The publish id ("20261108T142012Z_bmorgan"), written by the publish step, never by the build. A string,
	// because publish ids are a time and a user, so two people publishing never collide (D29).
	CHECK_MSTATUS_AND_RETURN_IT(addString(aPublishVersion, RigAttr::PublishVersion));
	CHECK_MSTATUS_AND_RETURN_IT(addString(aRigTemplateName, RigAttr::RigTemplateName));

	CHECK_MSTATUS_AND_RETURN_IT(addMessage(aRigModules, RigAttr::RigModules, false));
	CHECK_MSTATUS_AND_RETURN_IT(addMessage(aConstraints, RigAttr::Constraints, true));
	CHECK_MSTATUS_AND_RETURN_IT(addMessage(aJointDisplayLayer, RigAttr::JointDisplayLayer, false));
	CHECK_MSTATUS_AND_RETURN_IT(addMessage(aMeshDisplayLayer, RigAttr::MeshDisplayLayer, false));
	CHECK_MSTATUS_AND_RETURN_IT(addMessage(aControlDisplayLayer, RigAttr::ControlDisplayLayer, false));
	CHECK_MSTATUS_AND_RETURN_IT(addMessage(aSelectionSets, RigAttr::SelectionSets, true));
	CHECK_MSTATUS_AND_RETURN_IT(addMessage(aAllGroup, RigAttr::AllGroup, false));
	CHECK_MSTATUS_AND_RETURN_IT(addMessage(aMeshGroup, RigAttr::MeshGroup, false));
	// The look the rig wears (D54): connects to a lookRoot node's rigRoot. None until a look is applied.
	CHECK_MSTATUS_AND_RETURN_IT(addMessage(aLook, RigAttr::Look, false));
	// The rig an attachment follows (D55). Only a record for tools: the follow itself is socket matrices connected
	// across the two rigs, so these two add nothing to evaluation.
	CHECK_MSTATUS_AND_RETURN_IT(addMessage(aParentRig, RigAttr::ParentRig, false));
	CHECK_MSTATUS_AND_RETURN_IT(addMessage(aAttachments, RigAttr::Attachments, true));

	// No attribute affects another: the node computes nothing.

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
