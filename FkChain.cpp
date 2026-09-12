#include "FkChain.h"

#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnTransform.h>
#include <maya/MFnMatrixData.h>
#include <maya/MMatrix.h>
#include <maya/MDataHandle.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MArrayDataBuilder.h>
#include <maya/MPlug.h>
#include <maya/MSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MStringArray.h>
#include <maya/MArgList.h>
#include <maya/MGlobal.h>

// ------------------------------------------------------------------
// FkChainNode Implementation
// ------------------------------------------------------------------

MTypeId FkChainNode::id(0x0021B);

MObject FkChainNode::inputRestMatrix;
MObject FkChainNode::controlMatrix;
MObject FkChainNode::outputControlOPM;
MObject FkChainNode::outputJointOPM;

FkChainNode::FkChainNode() {}
FkChainNode::~FkChainNode() {}

const MString FkChainNode::commandString = "fkChainModule";

void* FkChainNode::creator()
{
	return new FkChainNode();
}

MStatus FkChainNode::initialize()
{
	MStatus status;

	status = RigModuleNodeBase::initializeBaseAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MFnMatrixAttribute mAttr;

	// 1. INPUT ARRAYS
	inputRestMatrix = mAttr.create("inputRestMatrix", "irm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setStorable(true);
	mAttr.setKeyable(true);
	addAttribute(inputRestMatrix);

	controlMatrix = mAttr.create("controlMatrix", "cm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setStorable(true);
	mAttr.setKeyable(true);
	addAttribute(controlMatrix);

	// 2. OUTPUT ARRAYS
	outputControlOPM = mAttr.create("outputControlOffsetParentMatrix", "copm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setUsesArrayDataBuilder(true);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	addAttribute(outputControlOPM);

	outputJointOPM = mAttr.create("outputJointOffsetParentMatrix", "jopm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setUsesArrayDataBuilder(true);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	addAttribute(outputJointOPM);

	// 3. AFFECTS RELATIONSHIPS
	attributeAffects(parentWorldMatrix, outputControlOPM);
	attributeAffects(inputRestMatrix, outputControlOPM);

	attributeAffects(controlMatrix, outputJointOPM);
	attributeAffects(inputRestMatrix, outputJointOPM);
	attributeAffects(parentWorldMatrix, outputJointOPM);

	attributeAffects(controlMatrix, outputSocketMatrix);

	return MStatus::kSuccess;
}

MStatus FkChainNode::evaluateModuleSolver(const MPlug& plug, MDataBlock& data)
{
	unsigned int index = plug.isElement() ? plug.logicalIndex() : 0;

	// -------------------------------------------------------------------
	// 1. Output Control Offset Parent Matrix Array
	// -------------------------------------------------------------------
	if (plug == outputControlOPM || plug.array() == outputControlOPM)
	{
		MMatrix inputRestLocal = getInputMatrix(data, inputRestMatrix, index);
		MMatrix controlOPM;

		if (index == 0)
		{
			MMatrix parentWorld = getInputMatrix(data, parentWorldMatrix);
			controlOPM = inputRestLocal * parentWorld;
		}
		else
		{
			controlOPM = inputRestLocal;
		}

		setOutputMatrix(data, outputControlOPM, index, controlOPM);
		data.setClean(plug);
		return MStatus::kSuccess;
	}


	// -------------------------------------------------------------------
	// 2. Output Joint Offset Parent Matrix Array
	// -------------------------------------------------------------------
	if (plug == outputJointOPM || plug.array() == outputJointOPM)
	{

		MMatrix controlLocal = getInputMatrix(data, controlMatrix, index);
		MMatrix jointOffset = getInputMatrix(data, inputRestMatrix, index);

		MGlobal::displayInfo(MString("Processing jointOPM at index ") + index);
		MGlobal::displayInfo(MString("Control local matrix: ") + controlLocal[3][0] + MString(" ") + controlLocal[3][1] + MString(" ") + controlLocal[3][2]);

		MMatrix jointOPM;

		if (index == 0)
		{
			// include the module parent matrix in the chain
			MMatrix parentWorld = getInputMatrix(data, parentWorldMatrix);
			jointOPM = controlLocal * jointOffset * parentWorld;
		}
		else
		{
			jointOPM = controlLocal * jointOffset;
		}

		setOutputMatrix(data, outputJointOPM, index, jointOPM);
		data.setClean(plug);
		return MStatus::kSuccess;
	}
	
	// -------------------------------------------------------------------
	// 3. Output Socket Matrix Array
	// -------------------------------------------------------------------
	// TODO: this right now is just outputting local matrices. we may need to multiply up the hierarchy
	// and ensure these are world-space positions.
	if (plug == outputSocketMatrix || plug.array() == outputSocketMatrix)
	{
		MMatrix controlLocal = getInputMatrix(data, controlMatrix, index);
		setOutputMatrix(data, outputSocketMatrix, index, controlLocal);
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	return MStatus::kUnknownParameter;
}
