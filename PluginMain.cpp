// A mll can only have one initialize/uninitialize entry point to avoid memory corruption and crashes on build. 
#include <maya/MStatus.h>
#include <maya/MFnPlugin.h>  // only include MFnPlugin in the file that has the plugin declaration to avoid build errors.
#include <maya/MDrawRegistry.h>

#include <vector>

// Include headers for all custom nodes
#include "ModuleRegistry.h"
#include "AssetRoot.h"
#include "RigRoot.h"
#include "RigControlNode.h"
#include "RigModule.h"

// Plugin Registration
MStatus initializePlugin(MObject obj)
{
	MStatus status;

	// Register our plugin
	MFnPlugin plugin(obj, "Ben Morgan", "0.1", "Any");

	// -- RIG MODULE NODES --
	// This loop is possible because we have the ModuleRegistry. It makes it so that when we add a new module we don't have to touch so many files.
	for (const ModuleDescriptor& descriptor : ModuleRegistry::all())
	{
		status = plugin.registerNode(descriptor.nodeName, descriptor.typeId, descriptor.creator, descriptor.initialize);
		if (!status)
		{
			status.perror(MString("Failed to register ") + descriptor.nodeName);
			return status;
		}
	}
	
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

	// TODO: we could do a command registry too.
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

	const std::vector<ModuleDescriptor>& descriptors = ModuleRegistry::all();
	for (auto it = descriptors.rbegin(); it != descriptors.rend(); ++it)
	{
		plugin.deregisterNode(it->typeId);
	}

	return status;
}