// A mll can only have one initialize/uninitialize entry point to avoid memory corruption and crashes on build. 
#include <maya/MDrawRegistry.h>
#include <maya/MFnPlugin.h>  // only include MFnPlugin in the file that has the plugin declaration to avoid build errors.
#include <maya/MGlobal.h>
#include <maya/MStatus.h>

#include <string>
#include <vector>

// Include headers for all custom nodes
#include "core/ControlShapes.h"
#include "maya/CommandRegistry.h"
#include "maya/ModuleRegistry.h"
#include "maya/shared/Utilities.h"

// Plugin Registration
MStatus initializePlugin(MObject obj)
{
	MStatus status;

	// A control shape that doesn't triangulate would draw wrong in every rig, so refuse to load instead.
	std::string shapeError;
	if (!validateShapeLibrary(shapeError))
	{
		MGlobal::displayError(MString("RigModules control shape library is invalid: ") + shapeError.c_str());
		return MStatus::kFailure;
	}

	// Register our plugin
	MFnPlugin plugin(obj, PluginInfo::Vendor, PluginInfo::Version, "Any");

	ProfilerUtils::addCategory();

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

	// -- REGISTER COMMANDS --
	for (const CommandDescriptor& descriptor : CommandRegistry::all())
	{
		status = plugin.registerCommand(descriptor.name, descriptor.creator, descriptor.syntax);
		if (!status)
		{
			status.perror(MString("Failed to register command ") + descriptor.name);
			return status;
		}
	}

	// Last: Maya doesn't call uninitializePlugin after a failed load, so nothing may return early after the
	// callback is registered. The control draw override reads it.
	status = ActiveSelection::start();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	return status;
}

MStatus uninitializePlugin(MObject obj)
{
	MStatus status;
	MFnPlugin plugin(obj);

	// First, before any early return: a callback left registered would call into the unloaded .mll.
	ActiveSelection::stop();

	// 1. Deregister VP2 Draw Override FIRST (before node type is removed) to avoid crashes.
	status = MHWRender::MDrawRegistry::deregisterDrawOverrideCreator(
		RigControlNode::drawDbClassification,
		RigControlNode::drawRegistrantId
	);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// -- Deregister Commands --
	const std::vector<CommandDescriptor>& commands = CommandRegistry::all();
	for (auto it = commands.rbegin(); it != commands.rend(); ++it)
	{
		plugin.deregisterCommand(it->name);
	}

	// 3. Deregister node types (reverse order)
	status = plugin.deregisterNode(RigControlNode::id);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	const std::vector<ModuleDescriptor>& descriptors = ModuleRegistry::all();
	for (auto it = descriptors.rbegin(); it != descriptors.rend(); ++it)
	{
		plugin.deregisterNode(it->typeId);
	}

	ProfilerUtils::removeCategory();

	return status;
}