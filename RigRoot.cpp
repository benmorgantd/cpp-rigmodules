// Internal dependencies
#include "RigRoot.h"

// Maya dependencies
#include <maya/MDataHandle.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnData.h>
#include <maya/MFnNumericData.h>
#include <maya/MPlug.h>
#include <maya/MSelectionList.h>
#include <maya/MGlobal.h>
#include <maya/MDGModifier.h>

// Define the unique ID
MTypeId RigRootNode::id(0x00218);

// Static member definitions
MObject RigRootNode::rigName;
MObject RigRootNode::rigType;
MObject RigRootNode::rigVersion;
MObject RigRootNode::rigModules;
MObject RigRootNode::rigTemplateName;
MObject RigRootNode::assetRoot;
MObject RigRootNode::children;

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
	MStatus status;

	rigName = tAttr.create("rigName", "rn", MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(rigName);

	rigType = tAttr.create("rigType", "rt", MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(rigType);

	rigVersion = nAttr.create("rigVersion", "rv", MFnNumericData::kLong, 0, &status);
	nAttr.setStorable(true);
	addAttribute(rigVersion);

	rigTemplateName = tAttr.create("rigTemplateName", "rtn", MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(rigTemplateName);

	// Message attributes can't be storable since they don't contain data
	rigModules = msgAttr.create("rigModules", "rm", &status);
	addAttribute(rigModules);

	assetRoot = msgAttr.create("assetRoot", "ar", &status);
	addAttribute(assetRoot);

	children = msgAttr.create("children", "c", &status);
	addAttribute(children);

	// In the case of this node, there are no attribute affect relationships.

	return MStatus::kSuccess;
}


MStatus RigRootNode::compute(const MPlug& plug, MDataBlock& data)
{
	// The node really has nothing to compute when attributes change, so we can just return kSuccess.
	return MStatus::kSuccess;
}

// Public shared methods