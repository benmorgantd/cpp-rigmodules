#include "RigModule.h"
#include "RigJsonStructs.h"
#include "FkChain.h"
#include "AimModule.h"

#include <maya/MFnTypedAttribute.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnData.h>
#include <maya/MDataBlock.h>
#include <maya/MMatrix.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MArrayDataBuilder.h>
#include <maya/MGlobal.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>
#include <maya/MFnMatrixData.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MStringArray.h>
#include <maya/MObjectArray.h>

// Static attribute definitions
MObject RigModuleNodeBase::moduleData;
MObject RigModuleNodeBase::aRigRoot;
MObject RigModuleNodeBase::aOutputSocketMatrix;
MObject RigModuleNodeBase::aRigControls;
MObject RigModuleNodeBase::aSide;

RigModuleNodeBase::RigModuleNodeBase()
{
}

RigModuleNodeBase::~RigModuleNodeBase()
{
}

MStatus RigModuleNodeBase::compute(const MPlug& plug, MDataBlock& data)
{
	return evaluateModuleSolver(plug, data);
}

MStatus RigModuleNodeBase::initializeBaseAttributes()
{
	MFnTypedAttribute tAttr;
	MFnMatrixAttribute mAttr;
	MFnMessageAttribute msgAttr;
	MFnEnumAttribute eAttr;
	MFnNumericAttribute nAttr;
	MStatus status;

	// 1. Metadata Attributes
	moduleData = tAttr.create("moduleData", "md", MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(moduleData);

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

	return MStatus::kSuccess;
}

// Matrix Calculation Helpers --------------------------------------------

MMatrix RigModuleNodeBase::computeOffsetParentMatrix(const MMatrix& mControlWorld, const MMatrix& mDagParentWorld)
{
	// M_OPM = M_ControlWorld * M_DAGParentWorld_Inverse
	return mControlWorld * mDagParentWorld.inverse();
}

int RigModuleNodeBase::resolveSocketIndex(const MString& key, unsigned int totalElements)
{
	if (key == "start" || key == "0" || key == "socketDefault")
	{
		return 0;
	}
	if (key == "end" || key == "-1")
	{
		return (totalElements > 0) ? static_cast<int>(totalElements) - 1 : 0;
	}

	int idx = key.asInt();
	if (idx < 0 && totalElements > 0)
	{
		idx = static_cast<int>(totalElements) + idx;
	}
	return (idx >= 0) ? idx : 0;
}

bool RigModuleNodeBase::parseSocketIdentifier(const MString& fullIdentifier, MString& outModuleName, MString& outSocketName)
{
	MStringArray tokens;
	if (fullIdentifier.split('.', tokens) == MS::kSuccess && tokens.length() == 2)
	{
		outModuleName = tokens[0];
		outSocketName = tokens[1];
		return true;
	}

	// Single token represents a standalone node or root control
	outModuleName = "";
	outSocketName = fullIdentifier;
	return false;
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

MMatrix getMatrixFromPlug(const MPlug& pMatrixPlug)
{
	MMatrix matrixValue;

	MObject dataObj;
	MStatus status = pMatrixPlug.getValue(dataObj);

	if (status && !dataObj.isNull() && dataObj.hasFn(MFn::kMatrixData))
	{
		MFnMatrixData fnMatrixData(dataObj);
		matrixValue = fnMatrixData.matrix();
	}

	return matrixValue;
}

MMatrix RigModuleNodeBase::getInputMatrix(MDataBlock& data, const MObject& attr)
{
	MDataHandle hData = data.inputValue(attr);
	return hData.asMatrix();
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
MStatus RigModuleNodeBase::connectModuleToRigRoot(MObject& oRigRoot, MDGModifier& dgMod, const MObject& oModule)
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
	MPlug pRigRoot(oModule, RigModuleNodeBase::aRigRoot);

	if (pRigRoot.isNull())
	{
		MGlobal::displayError("Failed to find rigRoot plug on the module object.");
		return MStatus::kFailure;
	}

	status = dgMod.connect(pRigModules, pRigRoot);
	return status;
}


// First Pass Module Builder ------------------------------------------
MObjectArray RigModuleNodeBase::buildModules(
	const std::vector<RigModuleData>& flatModuleList,
	MObject oRigRoot,
	MDGModifier& dgMod,
	MDagModifier& dagMod)
{
	MObjectArray oRigModules;

	// 5. Traverse and Build Module Hierarchy Recursively
	for (unsigned int i = 0; i < flatModuleList.size(); ++i)
	{
		// Ensure our mods are up to date before we build the next module.
		// This means that when we query attributes from previous modules we will have them declared and set.

		RigModuleData moduleData = flatModuleList[i];
		MObject oModule;

		// 1. Branching creation logic based on moduleType
		if (moduleData.moduleType == "FkChain")
		{
			// Call the static creation method for FkChain
			oModule = FkChainNode::createModule(
				moduleData,
				oRigRoot,
				dgMod,
				dagMod
			);
		}
		else if (moduleData.moduleType == "AimModule")
		{
			oModule = AimModuleNode::createModule(
				moduleData,
				oRigRoot,
				dgMod,
				dagMod
			);
		}
		// Future module types (e.g., IkChain, SplineRibbon) branch here...
		else
		{
			MGlobal::displayError(MString("Unknown module type: ") + moduleData.moduleType.c_str());
			return MObjectArray();
		}
		if (!oModule.isNull())
		{
			oRigModules.append(oModule);
		}
		else
		{
			MGlobal::displayError("Error creating a rig module. Null returned.");
			return MObjectArray();
		}
	}
	
	// TODO: this could return a map of module names to their MObject.
	return oRigModules;
}

// 2nd pass function for parenting the controls to their respective dag and matrix parents from any other module.
void RigModuleNodeBase::parentModuleControls(const MObjectArray& rigModules, const std::vector<RigModuleData>& flatModuleList, MDGModifier& dgMod, MDagModifier& dagMod)
{

	/*
	Go through child plugs and find the mobject related to the parent socket. Make the parent relationship.
	*/

	// Ensure rig module object array and module list from json are the same size.
	if (rigModules.length() != static_cast<unsigned int>(flatModuleList.size()))
	{
		MGlobal::displayError("Unable to parent module controls because object array and module list from json are not equal lengths.");
		return;
	}

	for (unsigned int i = 0; i < flatModuleList.size(); ++i)
	{
		MObject oModule = rigModules[i];
		RigModuleData moduleJsonData = flatModuleList[i];

		// TODO: Get parent data, parent controls accordingly... 
	}
}