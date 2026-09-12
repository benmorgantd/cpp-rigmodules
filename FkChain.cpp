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
	attributeAffects(parentWorldMatrix, outputSocketMatrix);
	attributeAffects(inputRestMatrix, outputSocketMatrix);

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
		MMatrix controlOPM = inputRestLocal;

		if (index == 0)
		{
			MMatrix parentWorld = getInputMatrix(data, parentWorldMatrix);
			controlOPM = inputRestLocal * parentWorld;
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

		MMatrix jointOPM = controlLocal * jointOffset;

		if (index == 0)
		{
			// include the module parent matrix in the chain
			MMatrix parentWorld = getInputMatrix(data, parentWorldMatrix);
			jointOPM *= parentWorld;
		}

		setOutputMatrix(data, outputJointOPM, index, jointOPM);
		data.setClean(plug);
		return MStatus::kSuccess;
	}
	
	// -------------------------------------------------------------------
	// 3. Output Socket Matrix Array
	// -------------------------------------------------------------------
	if (plug == outputSocketMatrix || plug.array() == outputSocketMatrix)
	{
		MMatrix mSocketWorld = getInputMatrix(data, parentWorldMatrix);

		MArrayDataHandle hControlArray = data.inputArrayValue(controlMatrix);
		MArrayDataHandle hRestArray = data.inputArrayValue(inputRestMatrix);
		MArrayDataHandle hOutSocketArray = data.outputArrayValue(outputSocketMatrix);

		unsigned int elementCount = hControlArray.elementCount();

		// Evaluate sequentially down the chain
		for (unsigned int i = 0; i < elementCount; ++i)
		{
			hControlArray.jumpToElement(i);
			hRestArray.jumpToElement(i);

			// Jump directly to the output element slot without rebuild
			if (hOutSocketArray.jumpToElement(hControlArray.elementIndex()) == MStatus::kSuccess)
			{
				MMatrix mControlLocal = hControlArray.inputValue().asMatrix();
				MMatrix mRestLocal = hRestArray.inputValue().asMatrix();

				// Accumulate step matrix downstream
				mSocketWorld = mControlLocal * mRestLocal * mSocketWorld;

				// Write directly to existing data block
				hOutSocketArray.outputValue().setMMatrix(mSocketWorld);
			}
		}

		// NOTE: because this is world space in a hierarchy, we need to calculate all at once
		hOutSocketArray.setAllClean();
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	return MStatus::kUnknownParameter;
}
