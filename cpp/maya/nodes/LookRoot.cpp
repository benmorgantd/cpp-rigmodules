#include "maya/nodes/LookRoot.h"
#include "core/Constants.h"

#include <maya/MFnData.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnTypedAttribute.h>

// 0x00219 is retired (the old assetRootModule), so the next free id after 0x00218F is 0x002190.
MTypeId LookRootNode::id(0x002190);

MObject LookRootNode::aAssetId;
MObject LookRootNode::aLookName;
MObject LookRootNode::aPublishVersion;
MObject LookRootNode::aRigRoot;
MObject LookRootNode::aMembers;

void* LookRootNode::creator()
{
	return new LookRootNode();
}

MStatus LookRootNode::initialize()
{
	MFnTypedAttribute tAttr;
	MFnMessageAttribute msgAttr;
	MStatus status;

	// The look's own asset, which can differ from the rig's: a look may come from another asset.
	aAssetId = tAttr.create(RigAttr::AssetId, RigAttr::AssetId, MFnData::kString, MObject::kNullObj, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	tAttr.setStorable(true);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aAssetId));

	aLookName = tAttr.create(RigAttr::LookName, RigAttr::LookName, MFnData::kString, MObject::kNullObj, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	tAttr.setStorable(true);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aLookName));

	aPublishVersion = tAttr.create(RigAttr::PublishVersion, RigAttr::PublishVersion, MFnData::kString, MObject::kNullObj, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	tAttr.setStorable(true);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aPublishVersion));

	aRigRoot = msgAttr.create(RigAttr::RigRoot, RigAttr::RigRoot, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aRigRoot));

	aMembers = msgAttr.create(RigAttr::LookMembers, RigAttr::LookMembers, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	msgAttr.setArray(true);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aMembers));

	// No inputs and no outputs, so there are no attribute affects relationships.
	return MStatus::kSuccess;
}
