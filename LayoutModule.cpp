#include "LayoutModule.h"
#include "RigControlNode.h"
#include "Side.h"
#include "RigModule.h"

#include "vector"
#include "utility"

#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>
#include <maya/MGlobal.h>
#include <maya/MFnMatrixData.h>


// ------------------------------------------------------------------
// LayoutModuleNode Implementation
// ------------------------------------------------------------------

MTypeId LayoutModuleNode::id(0x00218C);

MObject LayoutModuleNode::aControlMatrix;
// Shared Attributes (Although shared, these need to be unique MObjects for each module)
MObject LayoutModuleNode::aModuleData;
MObject LayoutModuleNode::aRigControls;
MObject LayoutModuleNode::aRigRoot;
MObject LayoutModuleNode::aOutputSocketMatrix;
MObject LayoutModuleNode::aSide;

LayoutModuleNode::LayoutModuleNode() {}
LayoutModuleNode::~LayoutModuleNode() {}

const MString LayoutModuleNode::commandString = "layoutModule";

void* LayoutModuleNode::creator()
{
	return new LayoutModuleNode();
}

MStatus LayoutModuleNode::initialize()
{
	MStatus status;

	// TODO: this module does not really need all of these attributes. 
	MFnMatrixAttribute mAttr;
	MFnMessageAttribute msgAttr;
	MFnEnumAttribute eAttr;
	MFnNumericAttribute nAttr;

	status = RigModuleNodeBase::initializeSharedAttributes(aModuleData, aRigRoot, aRigControls, aOutputSocketMatrix, aSide);

	// 1. INPUT ARRAYS
	aControlMatrix = mAttr.create("controlMatrix", "cm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setStorable(true);
	mAttr.setKeyable(true);
	addAttribute(aControlMatrix);

	// 3. AFFECTS RELATIONSHIPS
	attributeAffects(aControlMatrix, aOutputSocketMatrix);

	return MStatus::kSuccess;
}

// In the case of this node, there is only one output to compute, the outputSocketMatrix.
// TODO: the solver for this module is not evaluating correctly
MStatus LayoutModuleNode::compute(const MPlug& plug, MDataBlock& data)
{
	// TODO: child modules should attach to the last index of the layout module. Make "-1" the default.
	// TODO: there are world-space calculation issues here.
	if (plug == aOutputSocketMatrix || plug.array() == aOutputSocketMatrix)
	{
		MArrayDataHandle hControlArray = data.inputArrayValue(aControlMatrix);
		MArrayDataHandle hOutSocketArray = data.outputArrayValue(aOutputSocketMatrix);

		// Start with identity
		MMatrix mCurrentWorld = MMatrix::identity;
		unsigned int elementCount = hControlArray.elementCount();

		// Sequential downstream accumulation loop
		for (unsigned int i = 0; i < elementCount; ++i)
		{
			hControlArray.jumpToElement(i);
			MMatrix mControlLocal = hControlArray.inputValue().asMatrix();
			// Maya Row-Major Order: Local Step * Current Accumulated World Matrix
			MMatrix mSocketWorld = mControlLocal * mCurrentWorld;

			RigModuleNodeBase::setOutputMatrix(data, aOutputSocketMatrix, i, mSocketWorld);

			mCurrentWorld = mSocketWorld;
		}

		// Clean the entire output array block at once
		hOutSocketArray.setAllClean();
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	return MStatus::kUnknownParameter;
}

RigModuleBuildResult LayoutModuleNode::createModule(
	MObject& oRigRoot,
	MDGModifier& dgMod,
	MDagModifier& dagMod,
	const unsigned short numControls)
{
	RigModuleBuildResult result;

	MGlobal::displayInfo("Creating Layout module.");

	// 1. Create layout module node
	MObject oLayoutModule = dgMod.createNode(LayoutModuleNode::commandString);
	dgMod.renameNode(oLayoutModule, "layoutModule"); // TODO: namespaces
	dgMod.doIt();

	result.moduleNode = oLayoutModule;

	MObject oPreviousControlNode = MObject::kNullObj;

	// 2. Build layout controls hierarchy and populate in-memory socket registry
	for (unsigned int i = 0; i < numControls; ++i)
	{
		MString controlName = MString("layout_") + std::to_string(i).c_str();
		MObject oControl = RigControlNode::createRigControl(oLayoutModule, dagMod, controlName);

		MFnDependencyNode fnControl(oControl);
		MPlug pShapeType(oControl, RigControlNode::aShapeType);
		dgMod.newPlugValueShort(pShapeType, ShapeType::Square);

		// Record the "end" socket on the final layout control
		if (i == numControls - 1)
		{
			result.parentSockets[std::string("end")] = oControl;
		}

		// Also record indexed socket handles for explicit index targeting
		result.parentSockets[std::string("index_") + std::to_string(i).c_str()] = oControl;

		// Set decreasing scale using shape transform matrix
		MPlug pShapeTransform(oControl, RigControlNode::aShapeTransform);
		MTransformationMatrix mShapeTransform;

		double scaleValue = 1.0 - (0.1 * i);
		const double matrixScale[3] = { scaleValue, scaleValue, scaleValue };
		mShapeTransform.setScale(matrixScale, MSpace::kObject);

		// Orient control shape (90-degree rotation about Z axis)
		mShapeTransform.setRotationQuaternion(0.0, 0.0, 0.7011, 0.7011);

		MFnMatrixData fnShapeTransform;
		MObject oShapeTransform = fnShapeTransform.create(mShapeTransform.asMatrix());
		dgMod.newPlugValue(pShapeTransform, oShapeTransform);

		// Wire control matrix to module controlMatrix array input
		MPlug pControlOutputMatrix(oControl, RigControlNode::matrix);
		MPlug pModuleInputMatrix(oLayoutModule, LayoutModuleNode::aControlMatrix);
		dgMod.connect(pControlOutputMatrix, pModuleInputMatrix.elementByLogicalIndex(i));

		// Parent sequentially in DAG
		if (oPreviousControlNode != MObject::kNullObj)
		{
			dagMod.reparentNode(oControl, oPreviousControlNode);
		}
		dagMod.doIt();
		oPreviousControlNode = oControl;
	}

	// 3. Connect to RigRoot network plug
	if (!oRigRoot.isNull())
	{
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dgMod, oLayoutModule, aRigRoot);
	}

	dgMod.doIt();
	dagMod.doIt();

	result.status = MS::kSuccess;
	return result;
}
