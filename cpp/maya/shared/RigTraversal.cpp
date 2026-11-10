#include "maya/shared/RigTraversal.h"
#include "core/Constants.h"
#include "core/TemplateMirror.h"
#include "maya/ModuleRegistry.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/nodes/RigRoot.h"
#include "maya/shared/Utilities.h"

#include <algorithm>

#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MPxTransform.h>

namespace
{
	// The module that owns a control, through control.rigModule <- module.controlMessage[index].
	MStatus getControlModule(const MObject& oControl, MObject& oOutModule, unsigned int& outIndex)
	{
		const MPlug pSource = MPlug(oControl, RigControlNode::aRigModule).source();
		if (pSource.isNull())
		{
			MGlobal::displayError(MFnDependencyNode(oControl).name() + " is not connected to a rig module.");
			return MStatus::kFailure;
		}
		oOutModule = pSource.node();
		outIndex = pSource.logicalIndex();
		return MStatus::kSuccess;
	}

	// A node type that connects to a rig root's rigModules. Asset roots, rig roots and constraints are registered too, but aren't modules.
	bool isRigModuleNode(const MTypeId& typeId)
	{
		for (const ModuleDescriptor& descriptor : ModuleRegistry::all())
		{
			if (descriptor.typeId == typeId)
			{
				return descriptor.isRigModule;
			}
		}
		return false;
	}

	// The rig module that drives a joint through joint.offsetParentMatrix, or a null object if none does.
	MObject getJointModule(const MObject& oJoint)
	{
		const MPlug pSource = MPlug(oJoint, MPxTransform::offsetParentMatrix).source();
		if (pSource.isNull() || !isRigModuleNode(MFnDependencyNode(pSource.node()).typeId()))
		{
			return MObject::kNullObj;
		}
		return pSource.node();
	}

	// The rig module a joint, control or module node leads to, or a null object for any other node. The rig root
	// leads to none. Reports nothing: a control left unconnected is caught later, by the caller's walk.
	MObject getNodeModule(const MObject& oNode)
	{
		const MTypeId typeId = MFnDependencyNode(oNode).typeId();
		if (isRigModuleNode(typeId))
		{
			return oNode;
		}
		if (typeId == RigControlNode::id)
		{
			const MPlug pSource = MPlug(oNode, RigControlNode::aRigModule).source();
			return pSource.isNull() ? MObject::kNullObj : pSource.node();
		}
		if (oNode.hasFn(MFn::kJoint))
		{
			return getJointModule(oNode);
		}
		return MObject::kNullObj;
	}

	// The rig root a module belongs to, through module.rigRoot <- rigRoot.rigModules.
	MStatus getModuleRigRoot(const MObject& oModule, MObject& oOutRigRoot)
	{
		MStatus status;
		MFnDependencyNode fnModule(oModule);
		const MPlug pRigRoot = fnModule.findPlug(RigAttr::RigRoot, false, &status);
		const MPlug pSource = status ? pRigRoot.source() : MPlug();
		if (pSource.isNull())
		{
			MGlobal::displayError(fnModule.name() + " is not connected to a rig root.");
			return MStatus::kFailure;
		}
		oOutRigRoot = pSource.node();
		return MStatus::kSuccess;
	}
}

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

MStatus RigTraversal::getOwningRigRoot(const MObject& oNode, MObject& oOutRigRoot)
{
	MFnDependencyNode fnNode(oNode);
	const MTypeId typeId = fnNode.typeId();
	if (typeId == RigRootNode::id)
	{
		oOutRigRoot = oNode;
		return MStatus::kSuccess;
	}
	if (typeId == RigControlNode::id)
	{
		MObject oModule;
		unsigned int index = 0;
		MStatus status = getControlModule(oNode, oModule, index);
		if (!status)
		{
			return status;
		}
		return getModuleRigRoot(oModule, oOutRigRoot);
	}
	const MObject oModule = getNodeModule(oNode);
	if (!oModule.isNull())
	{
		return getModuleRigRoot(oModule, oOutRigRoot);
	}
	MGlobal::displayError(fnNode.name() + " is not a rig root, rig module, rig control or joint driven by a rig.");
	return MStatus::kFailure;
}

bool RigTraversal::isRigNode(const MObject& oNode)
{
	return MFnDependencyNode(oNode).typeId() == RigRootNode::id || !getNodeModule(oNode).isNull();
}

MStatus RigTraversal::getAssetRoot(const MObject& oRigRoot, MObject& oOutAssetRoot)
{
	const MPlug pSource = MPlug(oRigRoot, RigRootNode::aAssetRoot).source();
	if (pSource.isNull())
	{
		MGlobal::displayError(MFnDependencyNode(oRigRoot).name() + " is not connected to an asset root.");
		return MStatus::kFailure;
	}
	oOutAssetRoot = pSource.node();
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

MStatus RigTraversal::getControlRestWorld(const MObject& oControl, MMatrix& mOutRestWorld)
{
	MObject oModule;
	unsigned int index = 0;
	MStatus status = getControlModule(oControl, oModule, index);
	if (!status)
	{
		return status;
	}

	MFnDependencyNode fnModule(oModule);
	MPlug pControlData = fnModule.findPlug(RigAttr::ControlInputData, false, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MObject oRestWorldAttr = fnModule.attribute(RigAttr::ControlRestWorld, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MPlug pRestWorld = pControlData.elementByLogicalIndex(index, &status).child(oRestWorldAttr, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	mOutRestWorld = DagUtils::getMatrixFromPlug(pRestWorld);
	return MStatus::kSuccess;
}

MStatus RigTraversal::getJointRestWorld(const MDagPath& jointPath, MMatrix& mOutRestWorld)
{
	MStatus status;
	const MObject oJoint = jointPath.node();
	const MObject oModule = getJointModule(oJoint);
	if (oModule.isNull())
	{
		mOutRestWorld = jointPath.inclusiveMatrix(&status);
		return status;
	}

	MFnDependencyNode fnModule(oModule);
	MPlug pRestWorld = fnModule.findPlug(RigAttr::JointRestWorld, false, &status);
	if (!status)
	{
		MGlobal::displayError(fnModule.name() + " drives " + jointPath.partialPathName() + " but stores no joint rest. Rebuild the rig.");
		return status;
	}
	const unsigned int index = MPlug(oJoint, MPxTransform::offsetParentMatrix).source().logicalIndex();
	mOutRestWorld = DagUtils::getMatrixFromPlug(pRestWorld.elementByLogicalIndex(index));
	return MStatus::kSuccess;
}

MStatus RigTraversal::getMirrorControl(const MObject& oControl, MObject& oOutMirror)
{
	ControlMarkup markup;
	MStatus status = getControlMarkup(oControl, markup);
	if (!status)
	{
		return status;
	}

	const std::string mirrorSide = TemplateMirror::swapSide(markup.side);
	if (mirrorSide == markup.side)
	{
		oOutMirror = oControl;   // Center
		return MStatus::kSuccess;
	}

	MObject oRigRoot;
	status = getOwningRigRoot(oControl, oRigRoot);
	if (!status)
	{
		return status;
	}

	RigControlFilter filter;
	filter.sides.push_back(mirrorSide);
	filter.usages.push_back(markup.usage);
	MObjectArray oMatches;
	status = getRigControls(oRigRoot, filter, oMatches);
	if (!status)
	{
		return status;
	}
	if (oMatches.length() != 1)
	{
		// Control markup is unique per rig (D25), so more than one match can't happen.
		MGlobal::displayError(MFnDependencyNode(oControl).name() + " has no mirror: no control in its rig has side "
			+ mirrorSide.c_str() + ", usage " + markup.usage.c_str() + ".");
		return MStatus::kFailure;
	}
	oOutMirror = oMatches[0];
	return MStatus::kSuccess;
}
