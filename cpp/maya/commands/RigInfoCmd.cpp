#include "maya/commands/RigInfoCmd.h"
#include "core/Constants.h"
#include "core/JsonUtils.h"
#include "maya/nodes/LookRoot.h"
#include "maya/nodes/RigRoot.h"
#include "maya/shared/RigTraversal.h"
#include "maya/shared/Utilities.h"

#include <nlohmann/json.hpp>

#include <maya/MArgDatabase.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>

const char* GetRigInfoCmd::aCommandString = "getRigInfo";

void* GetRigInfoCmd::creator()
{
	return new GetRigInfoCmd();
}

bool GetRigInfoCmd::isUndoable() const
{
	return false;
}

MSyntax GetRigInfoCmd::newSyntax()
{
	MSyntax syntax;
	syntax.setObjectType(MSyntax::kStringObjects, 1, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus GetRigInfoCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError(MString("Error parsing ") + aCommandString + " arguments.");
		return status;
	}

	MString nodeName;
	status = CommandUtils::readSingleObject(argData, aCommandString, "rig node", nodeName);
	if (!status)
	{
		return status;
	}

	MObject oNode;
	status = DagUtils::getMObjectFromName(nodeName, oNode);
	if (!status)
	{
		MGlobal::displayError(MString("Node not found: ") + nodeName);
		return status;
	}

	// A node outside every rig is an ordinary answer, not an error, so a selection callback can ask about anything.
	if (!RigTraversal::isRigNode(oNode))
	{
		setResult(MString());
		return MStatus::kSuccess;
	}

	MObject oRigRoot;
	status = RigTraversal::getOwningRigRoot(oNode, oRigRoot);
	if (!status)
	{
		return status;
	}
	// The rig root is a DG node, so its name is unique without a DAG path.
	const MFnDependencyNode fnRigRoot(oRigRoot);
	nlohmann::json info;
	info[RigAttr::RigRoot] = fnRigRoot.name().asChar();
	info[RigAttr::AssetId] = MPlug(oRigRoot, RigRootNode::aAssetId).asString().asChar();
	info[RigAttr::RigName] = MPlug(oRigRoot, RigRootNode::aRigName).asString().asChar();
	info[RigAttr::SkeletonHash] = MPlug(oRigRoot, RigRootNode::aSkeletonHash).asString().asChar();
	info[RigAttr::RigVersion] = MPlug(oRigRoot, RigRootNode::aRigVersion).asInt();
	info[RigAttr::PluginVersion] = MPlug(oRigRoot, RigRootNode::aPluginVersion).asString().asChar();
	info[RigAttr::PublishVersion] = MPlug(oRigRoot, RigRootNode::aPublishVersion).asString().asChar();
	info[RigInfoKey::Namespace] = fnRigRoot.parentNamespace().asChar();

	// The look the rig wears (D54), null for a rig that has had none applied.
	const MObject oLookRoot = RigTraversal::findLookRoot(oRigRoot);
	if (oLookRoot.isNull())
	{
		info[RigInfoKey::Look] = nullptr;
	}
	else
	{
		nlohmann::json look;
		look[RigInfoKey::LookAssetId] = MPlug(oLookRoot, LookRootNode::aAssetId).asString().asChar();
		look[RigInfoKey::LookName] = MPlug(oLookRoot, LookRootNode::aLookName).asString().asChar();
		look[RigInfoKey::LookPublishVersion] = MPlug(oLookRoot, LookRootNode::aPublishVersion).asString().asChar();
		info[RigInfoKey::Look] = look;
	}

	// The rig an attachment follows (D55), null for a rig that is not attached.
	const MPlug pParentRig(oRigRoot, RigRootNode::aParentRig);
	MPlugArray parentSources;
	pParentRig.connectedTo(parentSources, true, false);
	info[RigInfoKey::ParentRigRoot] = nullptr;
	if (parentSources.length() > 0)
	{
		info[RigInfoKey::ParentRigRoot] = MFnDependencyNode(parentSources[0].node()).name().asChar();
	}

	setResult(MString(JsonUtils::writeJsonString(info).c_str()));
	return MStatus::kSuccess;
}
