// A mll can only have one initialize/uninitialize entry point to avoid memory corruption and crashes on build. 
#include <maya/MStatus.h>
#include <maya/MFnPlugin.h>  // only include MFnPlugin in the file that has the plugin declaration to avoid build errors.
#include <maya/MDrawRegistry.h>

// Include headers for all custom nodes
#include "RigRoot.h"
#include "AssetRoot.h"
#include "LayoutModule.h"
#include "FkChain.h"
#include "RigControlNode.h"
#include "AimModule.h"

// Plugin Registration
MStatus initializePlugin(MObject obj)
{
	MStatus status;

	// Register our plugin
	MFnPlugin plugin(obj, "Ben Morgan", "0.1", "Any");

	// Asset root node
	status = plugin.registerNode("assetRootNode", AssetRootNode::id, AssetRootNode::creator, AssetRootNode::initialize);
	if (!status)
	{
		status.perror("Failed to register assetRootNode");
		return status;
	}

	// Rig root node
	status = plugin.registerNode("rigRootNode", RigRootNode::id, RigRootNode::creator, RigRootNode::initialize);
	if (!status)
	{
		status.perror("Failed to register rigRootNode");
		return status;
	}

	// -- RIG MODULE NODES --
	// LayoutModule
	status = plugin.registerNode(LayoutModuleNode::commandString, LayoutModuleNode::id, LayoutModuleNode::creator, LayoutModuleNode::initialize);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// FkChain
	status = plugin.registerNode(FkChainNode::aCommandString, FkChainNode::id, FkChainNode::creator, FkChainNode::initialize);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	status = plugin.registerNode(AimModuleNode::aCommandString, AimModuleNode::id, AimModuleNode::creator, AimModuleNode::initialize);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// Shape node
	status = plugin.registerTransform(
		"rigControlNode",
		RigControlNode::id,
		RigControlNode::creator,
		RigControlNode::initialize,
		MPxTransformationMatrix::creator,
		MPxTransformationMatrix::baseTransformationMatrixId,
		&RigControlNode::drawDbClassification
	);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	status = MHWRender::MDrawRegistry::registerDrawOverrideCreator(
		RigControlNode::drawDbClassification,
		RigControlNode::drawRegistrantId,
		RigControlDrawOverride::Creator
	);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// -- REGISTER COMMANDS --
	status = plugin.registerCommand(CreateRigCmd::aCommandString, CreateRigCmd::creator, CreateRigCmd::newSyntax);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	return status;
}

MStatus uninitializePlugin(MObject obj)
{
	MStatus status;
	MFnPlugin plugin(obj);

	// 1. Deregister VP2 Draw Override FIRST (before node type is removed) to avoid crashes.
	status = MHWRender::MDrawRegistry::deregisterDrawOverrideCreator(
		RigControlNode::drawDbClassification,
		RigControlNode::drawRegistrantId
	);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// 2. Deregister custom command
	status = plugin.deregisterCommand(CreateRigCmd::aCommandString);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// 3. Deregister node types (reverse order)
	status = plugin.deregisterNode(RigControlNode::id);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	status = plugin.deregisterNode(AimModuleNode::id);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	status = plugin.deregisterNode(FkChainNode::id);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	status = plugin.deregisterNode(LayoutModuleNode::id);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	status = plugin.deregisterNode(RigRootNode::id);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	plugin.deregisterNode(AssetRootNode::id);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	return status;
}