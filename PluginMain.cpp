// A mll can only have one initialize/uninitialize entry point to avoid memory corruption and crashes on build. 
#include <maya/MStatus.h>
#include <maya/MFnPlugin.h>  // only include MFnPlugin in the file that has the plugin declaration to avoid build errors.

// Include headers for all custom nodes
#include "RigRoot.h"
#include "AssetRoot.h"
#include "SingleJointFK.h"
#include "FkChain.h"

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
	// SingleJointFK
	status = plugin.registerNode(SingleJointFKNode::commandString, SingleJointFKNode::id, SingleJointFKNode::creator, SingleJointFKNode::initialize);
	if (!status)
	{
		status.perror("Failed to register singleJointFKNode");
		return status;
	}

	// FkChain
	status = plugin.registerNode(FkChainNode::commandString, FkChainNode::id, FkChainNode::creator, FkChainNode::initialize);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// -- REGISTER COMMANDS --
	status = plugin.registerCommand(SingleJointFKCmd::commandString, SingleJointFKCmd::creator, SingleJointFKCmd::newSyntax);
	CHECK_MSTATUS_AND_RETURN_IT(status);

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

	status = plugin.deregisterNode(SingleJointFKNode::id);
	if (!status)
	{
		status.perror("Failed to deregister singleJointFKNode");
		finalStatus = status;
	}

	status = plugin.deregisterNode(FkChainNode::id);
	if (!status)
	{
		status.perror("Failed to deregister fkChainNode");
		finalStatus = status;
	}

	status = plugin.deregisterCommand(SingleJointFKCmd::commandString);

	return finalStatus;
}