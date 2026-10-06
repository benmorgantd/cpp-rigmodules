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

	// FkChain
	status = plugin.registerNode(FkChainNode::aCommandString, FkChainNode::id, FkChainNode::creator, FkChainNode::initialize);
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

	status = MHWRender::MDrawRegistry::registerDrawOverrideCreator(
		RigControlNode::drawDbClassification,
		RigControlNode::drawRegistrantId,
		RigControlDrawOverride::Creator
	);

	// -- REGISTER COMMANDS --
	status = plugin.registerCommand(LayoutModuleSetupCmd::aCommandString, LayoutModuleSetupCmd::creator, LayoutModuleSetupCmd::newSyntax);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	status = plugin.registerCommand(FkChainNodeSetupCmd::aCommandString, FkChainNodeSetupCmd::creator, FkChainNodeSetupCmd::newSyntax);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	status = plugin.registerCommand(CreateRigCmd::aCommandString, CreateRigCmd::creator, CreateRigCmd::newSyntax);

	return status;
}

MStatus uninitializePlugin(MObject obj)
{
	MFnPlugin plugin(obj);
	MStatus status;
	MStatus finalStatus;

	// Deregister in reverse order of creation. There might be dependencies here
	status = plugin.deregisterNode(RigRootNode::id);
	if (!status)
	{
		status.perror("Failed to deregister rigRootNode");
		finalStatus = status;
	}

	status = plugin.deregisterNode(AssetRootNode::id);
	if (!status)
	{
		status.perror("Failed to deregister assetRootNode");
		finalStatus = status;
	}

	status = plugin.deregisterNode(LayoutModuleNode::id);


	status = plugin.deregisterNode(FkChainNode::id);
	if (!status)
	{
		status.perror("Failed to deregister fkChainNode");
		finalStatus = status;
	}

	// Shape node
	MHWRender::MDrawRegistry::deregisterDrawOverrideCreator(
		RigControlNode::drawDbClassification,
		RigControlNode::drawRegistrantId
	);
	status = plugin.deregisterNode(RigControlNode::id);

	status = plugin.deregisterCommand(LayoutModuleSetupCmd::aCommandString);
	status = plugin.deregisterCommand(FkChainNodeSetupCmd::aCommandString);
	status = plugin.deregisterCommand(CreateRigCmd::aCommandString);

	return finalStatus;
}