#include "RigModule.h"

#include <maya/MFnTypedAttribute.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnData.h>
#include <maya/MDataBlock.h>
#include <maya/MMatrix.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MArrayDataBuilder.h>
#include <maya/MGlobal.h>

// Static attribute definitions
MObject RigModuleNodeBase::moduleData;
MObject RigModuleNodeBase::rigRoot;
MObject RigModuleNodeBase::parentModule;
MObject RigModuleNodeBase::childModules;
MObject RigModuleNodeBase::parentWorldMatrix;
MObject RigModuleNodeBase::outputSocketMatrix;

RigModuleNodeBase::RigModuleNodeBase() {}
RigModuleNodeBase::~RigModuleNodeBase() {}

MStatus RigModuleNodeBase::compute(const MPlug& plug, MDataBlock& data)
{
	return evaluateModuleSolver(plug, data);
}

MStatus RigModuleNodeBase::initializeBaseAttributes()
{
	MFnTypedAttribute tAttr;
	MFnMatrixAttribute mAttr;
	MFnMessageAttribute msgAttr;
	MStatus status;

	// 1. Metadata Attributes
	moduleData = tAttr.create("moduleData", "md", MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(moduleData);

	// 2. Network Message Plugs
	rigRoot = msgAttr.create("rigRoot", "rr", &status);
	addAttribute(rigRoot);

	parentModule = msgAttr.create("parentModule", "pm", &status);
	addAttribute(parentModule);

	childModules = msgAttr.create("childModules", "cmods", &status);
	msgAttr.setArray(true);
	addAttribute(childModules);

	// 3. Base Driving Input Matrix (Scalar)
	parentWorldMatrix = mAttr.create("parentWorldMatrix", "pwm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setStorable(true);
	mAttr.setKeyable(true);
	mAttr.setWorldSpace(true);
	addAttribute(parentWorldMatrix);

	// 4. Output Socket Matrix Array (The ONLY matrix array attribute across the framework)
	outputSocketMatrix = mAttr.create("outputSocketMatrix", "soc", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setUsesArrayDataBuilder(true);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	mAttr.setWorldSpace(true);
	addAttribute(outputSocketMatrix);

	return MStatus::kSuccess;
}

// Shared functions
MMatrix RigModuleNodeBase::getInputMatrix(MDataBlock& data, const MObject& attr, unsigned int idx)
{
	MArrayDataHandle hArray = data.inputArrayValue(attr);
	if (hArray.jumpToElement(idx) == MS::kSuccess) 
	{
		// Do NOT re-aquire hArray here because we've just moved it to the desired plug.
		return hArray.inputValue().asMatrix();
	}
	else 
	{
		MGlobal::displayError("erro in getting array element");
	}
	return MMatrix::identity;
};

MMatrix RigModuleNodeBase::getInputMatrix(MDataBlock& data, const MObject& attr)
{
	MDataHandle hData = data.inputValue(attr);
	return hData.asMatrix();
};

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
};