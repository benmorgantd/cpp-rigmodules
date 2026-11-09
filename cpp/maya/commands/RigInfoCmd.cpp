#include "maya/commands/RigInfoCmd.h"
#include "core/Constants.h"
#include "core/JsonUtils.h"
#include "maya/nodes/AssetRoot.h"
#include "maya/nodes/RigRoot.h"
#include "maya/shared/RigTraversal.h"
#include "maya/shared/Utilities.h"

#include <nlohmann/json.hpp>

#include <maya/MArgDatabase.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MPlug.h>
#include <maya/MStringArray.h>

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
	MStringArray objects;
	if (!status || argData.getObjects(objects) != MStatus::kSuccess || objects.length() != 1)
	{
		MGlobal::displayError(MString(aCommandString) + " takes exactly one rig node.");
		return MStatus::kFailure;
	}

	MObject oNode;
	status = DagUtils::getMObjectFromName(objects[0], oNode);
	if (!status)
	{
		MGlobal::displayError(MString("Node not found: ") + objects[0]);
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
	MObject oAssetRoot;
	status = RigTraversal::getAssetRoot(oRigRoot, oAssetRoot);
	if (!status)
	{
		return status;
	}

	// Rig and asset roots are DG nodes, so their names are unique without a DAG path.
	const MFnDependencyNode fnRigRoot(oRigRoot);
	nlohmann::json info;
	info[RigAttr::RigRoot] = fnRigRoot.name().asChar();
	info[RigAttr::AssetRoot] = MFnDependencyNode(oAssetRoot).name().asChar();
	info[RigAttr::AssetId] = MPlug(oAssetRoot, AssetRootNode::aAssetId).asString().asChar();
	info[RigAttr::RigName] = MPlug(oRigRoot, RigRootNode::aRigName).asString().asChar();
	info[RigAttr::RigVersion] = MPlug(oRigRoot, RigRootNode::aRigVersion).asInt();
	info[RigAttr::PluginVersion] = MPlug(oRigRoot, RigRootNode::aPluginVersion).asString().asChar();
	info[RigAttr::PublishVersion] = MPlug(oRigRoot, RigRootNode::aPublishVersion).asString().asChar();
	info[RigInfoKey::Namespace] = fnRigRoot.parentNamespace().asChar();

	setResult(MString(JsonUtils::writeJsonString(info).c_str()));
	return MStatus::kSuccess;
}
