#include "LayoutModule.h"
#include "RigControlNode.h"
#include "RigModule.h"
#include "Side.h"
#include "Utilities.h"

#include <string>

#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>
#include <maya/MFnMatrixData.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MPlug.h>
#include <maya/MDataBlock.h>
#include <maya/MGlobal.h>

// ------------------------------------------------------------------
// LayoutModuleNode Implementation
// ------------------------------------------------------------------

MTypeId LayoutModuleNode::id(0x00218C);

LayoutModuleNode::LayoutModuleNode() {}
LayoutModuleNode::~LayoutModuleNode() {}

void* LayoutModuleNode::creator()
{
	return new LayoutModuleNode();
}

MStatus LayoutModuleNode::initialize()
{
	MStatus status = initializeSharedAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	declareControlAffects();

	return MStatus::kSuccess;
}

// A layout is a chain of controls with identity rests and no parent: OPM[i] is the world of control i-1.
// That is the FK chain solve, so the compute is just dispatch.
MStatus LayoutModuleNode::compute(const MPlug& plug, MDataBlock& data)
{
	const MObject oOutputAttr = plug.attribute();

	if (oOutputAttr == aControlOPM)
	{
		return computeChainOutput(plug, data, false);
	}

	if (oOutputAttr == aOutputSocketMatrix)
	{
		return computeChainOutput(plug, data, true);
	}

	return MStatus::kUnknownParameter;
}

RigModuleBuildResult LayoutModuleNode::createModule(
	MObject& oRigRoot,
	MDagModifier& dagMod,
	const unsigned short& numControls)
{
	RigModuleBuildResult result;

	if (numControls == 0)
	{
		MGlobal::displayError("Layout module needs at least one control.");
		return result;
	}

	// 1. Layout module node
	MObject oLayoutModule = dagMod.MDGModifier::createNode(LayoutModuleNode::id);
	dagMod.renameNode(oLayoutModule, "layoutModule");

	MPlug pSide(oLayoutModule, LayoutModuleNode::aSide);
	dagMod.newPlugValueShort(pSide, getShortFromSide(Side::Center));

	result.moduleNode = oLayoutModule;

	// Rotates the control shape 90 degrees about Z (a unit quaternion).
	constexpr double kHalfSqrt2 = 0.70710678118654757;

	MObject oPreviousControl = MObject::kNullObj;

	// 2. Controls, sockets and wiring
	for (unsigned int i = 0; i < numControls; ++i)
	{
		MString controlName = MString("layout_") + std::to_string(i).c_str();
		MObject oControl = RigControlNode::createRigControl(oLayoutModule, dagMod, controlName, ShapeType::Square, Side::Center);

		// Layout is rooted at the origin, so every rest is identity.
		MStatus status = wireControl(oLayoutModule, oControl, i, MMatrix::identity, dagMod);
		if (!status)
		{
			result.status = status;
			return result;
		}

		// Every control is a socket, like the FK chain. The last one is also "end".
		const std::string indexName = "index_" + std::to_string(i);
		MString socketNames = indexName.c_str();

		RigModuleSocketInfo socketInfo;
		socketInfo.oTransform = oControl;
		socketInfo.mRestWorld = MMatrix::identity;
		socketInfo.outputIndex = i;
		result.parentSockets[indexName] = socketInfo;

		if (i == numControls - 1)
		{
			result.parentSockets["end"] = socketInfo;
			socketNames += ";end";
		}

		DagPlugAndSocketUtils::setParentSocketName(oLayoutModule, i, socketNames.asChar(), dagMod);

		// Decreasing scale, rotated about Z, through the shape transform
		const double scaleValue = 1.0 - (0.1 * i);
		const double matrixScale[3] = { scaleValue, scaleValue, scaleValue };

		MTransformationMatrix tShapeTransform;
		tShapeTransform.setScale(matrixScale, MSpace::kObject);
		tShapeTransform.setRotationQuaternion(0.0, 0.0, kHalfSqrt2, kHalfSqrt2);

		MFnMatrixData fnShapeTransform;
		MObject oShapeTransform = fnShapeTransform.create(tShapeTransform.asMatrix());
		MPlug pShapeTransform(oControl, RigControlNode::aShapeTransform);
		dagMod.newPlugValue(pShapeTransform, oShapeTransform);

		// Parent sequentially in the DAG
		if (!oPreviousControl.isNull())
		{
			dagMod.reparentNode(oControl, oPreviousControl);
		}
		oPreviousControl = oControl;
	}

	// 3. Network
	if (!oRigRoot.isNull())
	{
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dagMod, oLayoutModule, aRigRoot);
	}

	// No child plugs, so there is no dagParentCallback. parentModuleControls already skips modules without one.
	result.status = MS::kSuccess;
	return result;
}