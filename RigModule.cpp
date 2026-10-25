#include "RigModule.h"
#include "RigJsonStructs.h"
#include "FkChain.h"
#include "unordered_map"

#include <maya/MDataBlock.h>
#include <maya/MMatrix.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MArrayDataBuilder.h>
#include <maya/MGlobal.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>
#include <maya/MFnMatrixData.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnCompoundAttribute.h>

// NOTE that RigModuleNodeBase can NOT define any static node attributes!

RigModuleNodeBase::RigModuleNodeBase() {}
RigModuleNodeBase::~RigModuleNodeBase() {}

MStatus RigModuleNodeBase::compute(const MPlug& plug, MDataBlock& data) 
{ 
	return MStatus::kSuccess; 
}

MStatus RigModuleNodeBase::initializeSharedAttributes(
	MObject& aModuleData, 
	MObject& aRigRoot, 
	MObject& aRigControls, 
	MObject& aOutputSocketMatrix, 
	MObject& aSide)
{
	MStatus status;

	// ---- Initialize Shared Attributes ----
	MFnTypedAttribute tAttr;
	MFnMatrixAttribute mAttr;
	MFnMessageAttribute msgAttr;
	MFnEnumAttribute eAttr;
	MFnNumericAttribute nAttr;
	MFnCompoundAttribute cAttr;

	// 1. Metadata Attributes
	aModuleData = tAttr.create("moduleData", "md", MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(aModuleData);

	// 2. Network Message Plugs
	aRigRoot = msgAttr.create("rigRoot", "rr", &status);
	addAttribute(aRigRoot);

	aRigControls = msgAttr.create("rigControls", "ctrls", &status);
	addAttribute(aRigControls);

	// 3. Output Socket Matrix Array
	aOutputSocketMatrix = mAttr.create("outputSocketMatrix", "soc", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setUsesArrayDataBuilder(true);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	mAttr.setWorldSpace(true);
	addAttribute(aOutputSocketMatrix);

	// Side
	aSide = eAttr.create("side", "sd", 0);
	eAttr.addField("Center", 0);
	eAttr.addField("Left", 1);
	eAttr.addField("Right", 2);
	eAttr.setKeyable(false);
	eAttr.setStorable(true);
	addAttribute(aSide);

	if (status != MStatus::kSuccess)
	{
		MGlobal::displayError("Failure creating base attributes for module: " + status.errorString());
		return status;
	}

	return status;

	// ---------------------------------------------
}

// Matrix Calculation Helpers --------------------------------------------
MMatrix RigModuleNodeBase::computeOffsetParentMatrix(const MMatrix& mControlWorld, const MMatrix& mDagParentWorld)
{
	// M_OPM = M_ControlWorld * M_DAGParentWorld_Inverse
	return mControlWorld * mDagParentWorld.inverse();
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
MStatus RigModuleNodeBase::connectModuleToRigRoot(MObject& oRigRoot, MDGModifier& dgMod, const MObject& oModule, MObject& aRigRoot)
{
	MStatus status;
	MFnDependencyNode rigRootFn(oRigRoot);
	MPlug pRigModules = rigRootFn.findPlug("rm", false);

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

	status = dgMod.connect(pRigModules, pRigRoot);
	return status;
}

MObject RigModuleNodeBase::findPlugOrSocketTransform(const std::unordered_map<std::string, MObject>& socketPairs, const std::string& name)
{
	auto it = socketPairs.find(name);
	if (it != socketPairs.end())
	{
		return it->second;
	}
	return MObject::kNullObj;
};

// First Pass Module Builder ------------------------------------------
std::unordered_map<std::string, RigModuleBuildResult> RigModuleNodeBase::buildModules(
	const std::vector<RigModuleData>& flatModuleList,
	MObject oRigRoot,
	MDGModifier& dgMod,
	MDagModifier& dagMod)
{
	//MObjectArray oRigModules;
	std::unordered_map<std::string, RigModuleBuildResult> rigModuleMap;

	// 5. Traverse and Build Module Hierarchy Recursively
	for (unsigned int i = 0; i < flatModuleList.size(); ++i)
	{
		// Ensure our mods are up to date before we build the next module.
		// This means that when we query attributes from previous modules we will have them declared and set.

		RigModuleData moduleData = flatModuleList[i];
		RigModuleBuildResult creationResult;

		// 1. Branching creation logic based on moduleType
		if (moduleData.moduleType == "FkChain")
		{
			// Call the static creation method for FkChain
			creationResult = FkChainNode::createModule(
				moduleData,
				oRigRoot,
				dgMod,
				dagMod
			);
		}
		// TODO: aimmodulee has to implement buildresult.
		//else if (moduleData.moduleType == "AimModule")
		//{
		//	creationResult = AimModuleNode::createModule(
		//		moduleData,
		//		oRigRoot,
		//		dgMod,
		//		dagMod
		//	);
		//}
		// Future module types (e.g., IkChain, SplineRibbon) branch here...
		else
		{
			MGlobal::displayError(MString("Unknown module type: ") + moduleData.moduleType.c_str());
			return std::unordered_map<std::string, RigModuleBuildResult>();
		}
		if (!creationResult.moduleNode.isNull())
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

MStatus RigModuleNodeBase::connectParentMatrixToChildPlug(
	MPlug pChildPlugs,
	const MObject& oTargetSocketTransform,
	const MString& targetChildPlugName,
	MDGModifier& dgMod
)
{
	MStatus status;

	const unsigned int numElements = pChildPlugs.numElements(&status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	bool matchFound = false;

	for (unsigned int i = 0; i < numElements; ++i)
	{
		MPlug pChildPlugElement = pChildPlugs.elementByLogicalIndex(i, &status);
		if (!status || pChildPlugElement.isNull())
		{
			continue;
		}

		MPlug pChildPlugName = pChildPlugElement.child(FkChainNode::aChildPlugName);
		MString childPlugName = pChildPlugName.asString();

		if (childPlugName == targetChildPlugName)
		{
			MPlug pChildPlugParentMatrix = pChildPlugElement.child(FkChainNode::aChildPlugParentMatrix);

			MFnDependencyNode fnParentTransform(oTargetSocketTransform, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);

			MPlug pParentMatrix = fnParentTransform.findPlug("worldMatrix", false, &status).elementByLogicalIndex(0, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);

			status = dgMod.connect(pParentMatrix, pChildPlugParentMatrix);
			CHECK_MSTATUS_AND_RETURN_IT(status);

			matchFound = true;
			break;
		}
	}

	if (!matchFound)
	{
		MGlobal::displayError("Child plug name '" + targetChildPlugName + "' was not found on the module.");
		return MS::kFailure;
	}

	return MS::kSuccess;
}



