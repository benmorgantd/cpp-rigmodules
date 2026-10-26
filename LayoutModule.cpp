#include "LayoutModule.h"
#include "RigControlNode.h"
#include "RigModule.h"
#include "Side.h"

#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnCompoundAttribute.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>
#include <maya/MGlobal.h>
#include <maya/MFnMatrixData.h>


// ------------------------------------------------------------------
// LayoutModuleNode Implementation
// ------------------------------------------------------------------

MTypeId LayoutModuleNode::id(0x00218C);
const MString LayoutModuleNode::aCommmandString = "layoutModule";

LayoutModuleNode::LayoutModuleNode() {}
LayoutModuleNode::~LayoutModuleNode() {}

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
	MFnCompoundAttribute cAttr;

	// TODO: the compound and array attrs for control data, opm, messages could become shared attributes.
	status = initializeSharedAttributes();

	// AFFECTS RELATIONSHIPS
	attributeAffects(aControlMatrix, aControlOPM);
	attributeAffects(aControlMatrix, aOutputSocketMatrix);

	return MStatus::kSuccess;
}

// In the case of this node, there is only one output to compute, the outputSocketMatrix.
// TODO: the solver for this module is not evaluating correctly
MStatus LayoutModuleNode::compute(const MPlug& plug, MDataBlock& data)
{
	unsigned int index = plug.isElement() ? plug.logicalIndex() : 0;

	if (plug.attribute() == aControlOPM)
	{
		MArrayDataHandle hInputDataArray = data.inputArrayValue(aControlInputData);
		MArrayDataHandle hOutputOPMArray = data.outputArrayValue(aControlOPM);

		// Base incoming matrix (e.g. from upstream socket or asset root)
		// TODO: if we had a module parent for layout it would go here
		MMatrix mCurrentParentWorld = MMatrix::identity;

		unsigned int elementCount = hInputDataArray.elementCount();

		for (unsigned int i = 0; i < elementCount; ++i)
		{
			hInputDataArray.jumpToElement(i);
			MDataHandle hElement = hInputDataArray.inputValue();

			MMatrix mControlLocal = hElement.child(aControlMatrix).asMatrix();
			// NOTE: We can't have the control's local matrix in its opm! That causes a double transformation.
			MMatrix mControlOPM = mCurrentParentWorld;

			// Write to output array in-place
			hOutputOPMArray.jumpToElement(i);
			hOutputOPMArray.outputValue().setMMatrix(mControlOPM);

			// Carry world matrix forward for index i + 1
			MMatrix mControlWorld = mControlLocal * mControlOPM;
			mCurrentParentWorld = mControlWorld;
		}

		hOutputOPMArray.setAllClean();
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	return MStatus::kUnknownParameter;
}

RigModuleBuildResult LayoutModuleNode::createModule(
	MObject& oRigRoot,
	MDagModifier& dagMod,
	const unsigned short& numControls)
{
	RigModuleBuildResult result;

	MGlobal::displayInfo("Creating Layout module.");

	// 1. Create layout module node
	MObject oLayoutModule = dagMod.MDGModifier::createNode(LayoutModuleNode::aCommmandString);
	dagMod.renameNode(oLayoutModule, "layoutModule"); // TODO: namespaces
	dagMod.doIt();

	MPlug pSide(oLayoutModule, LayoutModuleNode::aSide);
	dagMod.newPlugValueShort(pSide, getShortFromSide(Side::Center));

	result.moduleNode = oLayoutModule;

	MObject oPreviousControlNode = MObject::kNullObj;

	// 2. Build layout controls hierarchy and populate in-memory socket registry
	for (unsigned int i = 0; i < numControls; ++i)
	{
		MString controlName = MString("layout_") + std::to_string(i).c_str();
		MObject oControl = RigControlNode::createRigControl(oLayoutModule, dagMod, controlName);

		MFnDependencyNode fnControl(oControl);
		MPlug pShapeType(oControl, RigControlNode::aShapeType);
		dagMod.newPlugValueShort(pShapeType, ShapeType::Square);

		// Connect control message
		MPlug pControlMessages(oLayoutModule, LayoutModuleNode::aControlMessage);
		MPlug pControlMessageIndex = pControlMessages.elementByLogicalIndex(i);
		MPlug pControlRigModule(oControl, RigControlNode::aRigModule);
		dagMod.connect(pControlMessageIndex, pControlRigModule);

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
		dagMod.newPlugValue(pShapeTransform, oShapeTransform);

		// Wire control matrix to module controlMatrix array input
		MPlug pControlInputData(oLayoutModule, LayoutModuleNode::aControlInputData);
		MPlug pControlInputDataIndex = pControlInputData.elementByLogicalIndex(i);
		MPlug pControlOutputMatrix(oControl, RigControlNode::matrix);

		MPlug pModuleInputMatrix = pControlInputDataIndex.child(LayoutModuleNode::aControlMatrix);
		dagMod.connect(pControlOutputMatrix, pModuleInputMatrix);

		// Connect to OPM for control
		MPlug pModuleOPM(oLayoutModule, LayoutModuleNode::aControlOPM);
		MPlug pModuleOPMIndex = pModuleOPM.elementByLogicalIndex(i);
		MPlug pControlOPM(oControl, RigControlNode::offsetParentMatrix);
		dagMod.connect(pModuleOPMIndex, pControlOPM);

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
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dagMod, oLayoutModule, aRigRoot);
	}

	dagMod.doIt();

	result.status = MS::kSuccess;
	return result;
}
