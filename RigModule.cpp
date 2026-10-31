#include "RigModule.h"
#include "RigJsonStructs.h"
#include "ModuleRegistry.h"
#include "Constants.h"

#include <unordered_map>
#include <string>

#include <maya/MDataBlock.h>
#include <maya/MMatrix.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MArrayDataBuilder.h>
#include <maya/MGlobal.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>
#include <maya/MFnDependencyNode.h>

// NOTE that RigModuleNodeBase can NOT define any static node attributes!

RigModuleNodeBase::RigModuleNodeBase() {}
RigModuleNodeBase::~RigModuleNodeBase() {}

MStatus RigModuleNodeBase::compute(const MPlug& plug, MDataBlock& data) 
{ 
	return MStatus::kSuccess; 
}


// DataBlock Handles Access --------------------------------------------
MMatrix RigModuleNodeBase::getInputMatrix(MDataBlock& data, const MObject& attr, unsigned int idx)
{
	MStatus status;
	MArrayDataHandle hArray = data.inputArrayValue(attr, &status);
	if (status == MStatus::kSuccess && hArray.jumpToElement(idx) == MS::kSuccess)
	{
		return hArray.inputValue().asMatrix();
	}
	MGlobal::displayError("Error retrieving array matrix element at index.");
	return MMatrix::identity;
}

MMatrix RigModuleNodeBase::getInputMatrix(MDataBlock& data, const MObject& attr)
{
	MDataHandle hData = data.inputValue(attr);
	return hData.asMatrix();
}

MMatrix RigModuleNodeBase::getInputCompoundMatrix(
	MDataBlock& data,
	const MObject& aParentCompound,
	const MObject& aChildAttr,
	unsigned int logicalIndex)
{
	MStatus status;
	MArrayDataHandle hCompoundArray = data.inputArrayValue(aParentCompound, &status);

	if (status == MStatus::kSuccess && hCompoundArray.jumpToElement(logicalIndex) == MStatus::kSuccess)
	{
		MDataHandle hElement = hCompoundArray.inputValue();
		MDataHandle hChild = hElement.child(aChildAttr);
		return hChild.asMatrix();
	}

	// Return identity if the element is not found or uninitialized
	return MMatrix::identity;
}

void RigModuleNodeBase::setOutputMatrix(MDataBlock& data, const MObject& attr, unsigned int idx, const MMatrix& mat)
{
	MArrayDataHandle hArray = data.outputArrayValue(attr);
	if (hArray.jumpToElement(idx) == MS::kSuccess)
	{
		hArray.outputValue().setMMatrix(mat);
	}
	else
	{
		MArrayDataBuilder builder(&data, attr, 1);
		MDataHandle hElement = builder.addElement(idx);
		hElement.setMMatrix(mat);
		hArray.set(builder);
	}
}

void RigModuleNodeBase::setOutputMatrix(MDataBlock& data, const MObject& attr, const MMatrix& mat)
{
	MDataHandle hOutput = data.outputValue(attr);
	hOutput.setMMatrix(mat);
}

// Networking functionality --------------------------------------------
MStatus RigModuleNodeBase::connectModuleToRigRoot(MObject& oRigRoot, MDagModifier& dagMod, const MObject& oModule, MObject& aRigRoot)
{
	MStatus status;
	MFnDependencyNode rigRootFn(oRigRoot);
	MPlug pRigModules = rigRootFn.findPlug(RigAttr::RigModules, false);

	if (pRigModules.isNull())
	{
		MGlobal::displayError("Failed to find rig modules plug on given rig root object.");
		return MStatus::kFailure;
	}

	MFnDependencyNode moduleFn(oModule);
	MPlug pRigRoot(oModule, aRigRoot);

	if (pRigRoot.isNull())
	{
		MGlobal::displayError("Failed to find rigRoot plug on the module object.");
		return MStatus::kFailure;
	}

	status = dagMod.connect(pRigModules, pRigRoot);
	return status;
}

// First Pass Module Builder ------------------------------------------
std::unordered_map<std::string, RigModuleBuildResult> RigModuleNodeBase::buildModules(
	const std::vector<RigModuleData>& flatModuleList,
	MObject oRigRoot,
	MDagModifier& dagMod)
{
	std::unordered_map<std::string, RigModuleBuildResult> rigModuleMap;

	// 5. Traverse and Build Modules from the flat list. Parenting between module controls will happen in the second pass.
	for (unsigned int i = 0; i < flatModuleList.size(); ++i)
	{
		RigModuleData moduleData = flatModuleList[i];

		// Not completely sure what this is doing with the pointers.
		const ModuleDescriptor* pDescriptor = ModuleRegistry::find(moduleData.moduleType);
		if (!pDescriptor || !pDescriptor->build)
		{
			MGlobal::displayError(MString("Unknown or non-buildable module type: ") + moduleData.moduleType.c_str());
			return std::unordered_map<std::string, RigModuleBuildResult>();
		}
		MGlobal::displayInfo(MString("Building module '") + moduleData.name.c_str() + "' of type " + moduleData.moduleType.c_str());

		BuildContext ctx{ moduleData, oRigRoot, dagMod };
		RigModuleBuildResult creationResult = pDescriptor->build(moduleData.moduleArgs, ctx);

		if (!creationResult.moduleNode.isNull() && creationResult.status == MStatus::kSuccess)
		{
			rigModuleMap[moduleData.name] = creationResult;
		}
		else
		{
			MGlobal::displayError("Error creating a rig module. Null returned.");
			return std::unordered_map<std::string, RigModuleBuildResult>();
		}
	}
	
	return rigModuleMap;
}



