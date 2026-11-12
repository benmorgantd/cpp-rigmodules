#include "maya/modules/LayoutModule.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/Side.h"
#include "maya/shared/Utilities.h"

#include <string>
#include <vector>

#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MDGModifier.h>
#include <maya/MFnMatrixData.h>
#include <maya/MGlobal.h>
#include <maya/MPlug.h>
#include <maya/MTransformationMatrix.h>

// ------------------------------------------------------------------
// LayoutModuleNode Implementation
// ------------------------------------------------------------------

MTypeId LayoutModuleNode::id(0x00218C);

LayoutModuleNode::LayoutModuleNode()
{
}
LayoutModuleNode::~LayoutModuleNode()
{
}

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

	if (oOutputAttr == aControlOPM || oOutputAttr == aOutputSocketMatrix)
	{
		return computeChainOutputs(plug, data);
	}

	return MStatus::kUnknownParameter;
}

ModuleInverseResult LayoutModuleNode::inverse(const ModuleInverseContext&, const ModuleInverseInput&)
{
	return ModuleInverse::notBuilt("Layout");
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
	// Named like template modules (scene name == socket id module half), so "layout.end" resolves in commands too (D19).
	dagMod.renameNode(oLayoutModule, RigNaming::LayoutModule);

	MPlug pSide(oLayoutModule, LayoutModuleNode::aSide);
	dagMod.newPlugValueShort(pSide, getShortFromSide(Side::Center));

	result.moduleNode = oLayoutModule;

	// Rotates the control shape 90 degrees about Z (a unit quaternion).
	constexpr double kHalfSqrt2 = 0.70710678118654757;

	MObject oPreviousControl = MObject::kNullObj;

	// 2. Controls, sockets and wiring
	for (unsigned int i = 0; i < numControls; ++i)
	{
		const ControlMarkup markup = controlMarkupAt(i);
		MObject oControl = RigControlNode::createRigControl(dagMod, markup.usage.c_str(), ShapeType::Square, Side::Center, markup, MEulerRotation::kXYZ);

		// Layout is rooted at the origin, so every rest is identity.
		MStatus status = wireControl(oLayoutModule, oControl, i, MMatrix::identity, dagMod);
		if (!status)
		{
			result.status = status;
			return result;
		}

		RigModuleSocketInfo socketInfo;
		socketInfo.oTransform = oControl;
		socketInfo.mRestWorld = MMatrix::identity;
		socketInfo.outputIndex = i;

		const std::vector<std::string> socketNames = socketNamesAt(i, numControls);
		for (const std::string& socketName : socketNames)
		{
			result.parentSockets[socketName] = socketInfo;
		}
		DagPlugAndSocketUtils::setParentSocketNames(oLayoutModule, i, socketNames, dagMod);

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

	// No child plugs, so there is no dagParentCallback. The parenting pass only walks template modules, never the layout.
	result.status = MS::kSuccess;
	return result;
}

std::vector<std::string> LayoutModuleNode::socketNamesAt(unsigned int index, unsigned int count)
{
	std::vector<std::string> names{ PlugAndSocketNames::Index + std::to_string(index) };
	if (index == count - 1)
	{
		names.push_back(PlugAndSocketNames::End);
	}
	return names;
}

ModuleInterface LayoutModuleNode::makeInterface(unsigned int count)
{
	ModuleInterface moduleInterface;
	for (unsigned int i = 0; i < count; ++i)
	{
		const std::vector<std::string> names = socketNamesAt(i, count);
		moduleInterface.sockets.insert(moduleInterface.sockets.end(), names.begin(), names.end());
		moduleInterface.controls.push_back(controlMarkupAt(i));
	}
	return moduleInterface;
}

ControlMarkup LayoutModuleNode::controlMarkupAt(unsigned int index)
{
	return makeControlMarkup(SideNames::Center, std::string(RigNaming::LayoutModule) + "_" + std::to_string(index));
}