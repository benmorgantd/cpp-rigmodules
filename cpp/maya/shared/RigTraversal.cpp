#include "maya/shared/RigTraversal.h"
#include "core/Constants.h"
#include "maya/ModuleRegistry.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/nodes/RigRoot.h"
#include "maya/shared/Utilities.h"

#include <algorithm>

#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>

MStatus RigTraversal::getRigRoot(const MString& name, MObject& oOutRigRoot)
{
	MStatus status = DagUtils::getMObjectFromName(name, oOutRigRoot);
	if (!status)
	{
		MGlobal::displayError(MString("Rig root not found: ") + name);
		return status;
	}

	if (MFnDependencyNode(oOutRigRoot).typeId() != RigRootNode::id)
	{
		MGlobal::displayError(MString("Not a rig root: ") + name);
		oOutRigRoot = MObject::kNullObj;
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus RigTraversal::getRigModules(const MObject& oRigRoot, MObjectArray& oOutModules)
{
	MStatus status;
	MFnDependencyNode fnRigRoot(oRigRoot);
	MPlug pRigModules = fnRigRoot.findPlug(RigAttr::RigModules, false, &status);
	if (!status)
	{
		MGlobal::displayError(fnRigRoot.name() + " has no " + RigAttr::RigModules + " attribute.");
		return status;
	}

	MPlugArray pDestinations;
	pRigModules.connectedTo(pDestinations, false, true);

	std::vector<MObject> modules;
	for (unsigned int i = 0; i < pDestinations.length(); ++i)
	{
		modules.push_back(pDestinations[i].node());
	}
	// Connection order is not something a user controls or sees, so sort by name instead.
	std::sort(modules.begin(), modules.end(), [](const MObject& a, const MObject& b)
	{
		return std::string(MFnDependencyNode(a).name().asChar()) < std::string(MFnDependencyNode(b).name().asChar());
	});

	oOutModules.clear();
	for (const MObject& oModule : modules)
	{
		oOutModules.append(oModule);
	}
	return MStatus::kSuccess;
}

MStatus RigTraversal::getModuleControls(const MObject& oModule, MObjectArray& oOutControls)
{
	oOutControls.clear();

	MFnDependencyNode fnModule(oModule);
	if (!fnModule.hasAttribute(RigAttr::ControlMessage))
	{
		return MStatus::kSuccess;   // a module type without controls
	}

	MStatus status;
	MPlug pControlMessage = fnModule.findPlug(RigAttr::ControlMessage, false, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// Physical walk: a logical lookup would create sparse elements. Physical order follows the logical indices.
	for (unsigned int e = 0; e < pControlMessage.numElements(); ++e)
	{
		MPlugArray pDestinations;
		pControlMessage.elementByPhysicalIndex(e).connectedTo(pDestinations, false, true);
		for (unsigned int i = 0; i < pDestinations.length(); ++i)
		{
			MObject oControl = pDestinations[i].node();
			if (MFnDependencyNode(oControl).typeId() == RigControlNode::id)
			{
				oOutControls.append(oControl);
			}
		}
	}
	return MStatus::kSuccess;
}

std::string RigTraversal::getModuleType(const MObject& oModule)
{
	const MTypeId typeId = MFnDependencyNode(oModule).typeId();
	for (const ModuleDescriptor& descriptor : ModuleRegistry::all())
	{
		if (descriptor.typeId == typeId)
		{
			return descriptor.moduleType;
		}
	}
	return std::string();
}

MStatus RigTraversal::getControlMarkup(const MObject& oControl, ControlMarkup& outMarkup)
{
	MFnDependencyNode fnControl(oControl);
	const MString text = MPlug(oControl, RigControlNode::aControlMarkup).asString();
	if (text.length() == 0)
	{
		MGlobal::displayError(fnControl.name() + " has no control markup. Rebuild the rig from its template.");
		return MStatus::kFailure;
	}

	std::string error;
	if (!parseControlMarkup(text.asChar(), outMarkup, error))
	{
		MGlobal::displayError(fnControl.name() + " has invalid control markup: " + error.c_str());
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

std::vector<std::string> RigTraversal::getKnownModuleTypes()
{
	std::vector<std::string> moduleTypes;
	for (const ModuleDescriptor& descriptor : ModuleRegistry::all())
	{
		if (descriptor.isRigModule)
		{
			moduleTypes.push_back(descriptor.moduleType);
		}
	}
	return moduleTypes;
}

MStatus RigTraversal::getRigControls(const MObject& oRigRoot, const RigControlFilter& filter, MObjectArray& oOutControls)
{
	oOutControls.clear();

	std::string error;
	if (!validateRigControlFilter(filter, getKnownModuleTypes(), error))
	{
		MGlobal::displayError(error.c_str());
		return MStatus::kFailure;
	}

	MObjectArray oModules;
	MStatus status = getRigModules(oRigRoot, oModules);
	if (!status)
	{
		return status;
	}

	for (unsigned int m = 0; m < oModules.length(); ++m)
	{
		const std::string moduleType = getModuleType(oModules[m]);

		MObjectArray oControls;
		status = getModuleControls(oModules[m], oControls);
		if (!status)
		{
			return status;
		}

		for (unsigned int c = 0; c < oControls.length(); ++c)
		{
			ControlMarkup markup;
			status = getControlMarkup(oControls[c], markup);
			if (!status)
			{
				return status;
			}
			if (filter.matches(moduleType, markup))
			{
				oOutControls.append(oControls[c]);
			}
		}
	}
	return MStatus::kSuccess;
}
