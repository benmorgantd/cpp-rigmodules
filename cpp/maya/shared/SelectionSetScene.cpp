#include "maya/shared/SelectionSetScene.h"
#include "core/Constants.h"
#include "maya/shared/Utilities.h"

#include <maya/MFn.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MPlug.h>

namespace
{
	// Membership the way the sets command makes it: a DAG node's instObjGroups[0] feeds dagSetMembers, and a nested
	// set's message feeds dnSetMembers.
	constexpr const char* kInstObjGroupsAttr = "instObjGroups";
	constexpr const char* kDagMembersAttr = "dagSetMembers";
	constexpr const char* kDnMembersAttr = "dnSetMembers";
}

MStatus SelectionSetScene::replaceSets(const std::vector<SelectionSetSpec>& specs, MDagModifier& dagMod, MObjectArray& outSets)
{
	outSets.clear();

	// Every delete is queued before any create, so the creates see the names as free.
	for (const SelectionSetSpec& spec : specs)
	{
		MObject oExisting;
		if (DagUtils::getMObjectFromName(spec.name, oExisting) && oExisting.hasFn(MFn::kSet))
		{
			MStatus status = dagMod.deleteNode(oExisting);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
	}

	for (const SelectionSetSpec& spec : specs)
	{
		MStatus status;
		MObject oSet = dagMod.MDGModifier::createNode("objectSet", &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = dagMod.renameNode(oSet, spec.name);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		MFnTypedAttribute fnMarkup;
		MObject oMarkup = fnMarkup.create(RigAttr::SetMarkup, RigAttr::SetMarkup, MFnData::kString, MObject::kNullObj, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = dagMod.addAttribute(oSet, oMarkup);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		outSets.append(oSet);
	}

	// The attributes and nodes must exist before plugs on them can be built.
	MStatus status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	for (std::size_t s = 0; s < specs.size(); ++s)
	{
		const SelectionSetSpec& spec = specs[s];
		MFnDependencyNode fnSet(outSets[static_cast<unsigned int>(s)]);

		MPlug pMarkup = fnSet.findPlug(RigAttr::SetMarkup, false, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = dagMod.newPlugValueString(pMarkup, spec.markup);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		MPlug pDagMembers = fnSet.findPlug(kDagMembersAttr, false, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		for (unsigned int m = 0; m < spec.members.length(); ++m)
		{
			MPlug pInstance = MFnDependencyNode(spec.members[m]).findPlug(kInstObjGroupsAttr, false, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			status = dagMod.connect(pInstance.elementByLogicalIndex(0), pDagMembers.elementByLogicalIndex(m));
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}

		MPlug pDnMembers = fnSet.findPlug(kDnMembersAttr, false, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		for (unsigned int c = 0; c < spec.children.size(); ++c)
		{
			MPlug pChildMessage = MFnDependencyNode(outSets[spec.children[c]]).findPlug("message", false, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			status = dagMod.connect(pChildMessage, pDnMembers.elementByLogicalIndex(c));
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
	}
	return MStatus::kSuccess;
}
