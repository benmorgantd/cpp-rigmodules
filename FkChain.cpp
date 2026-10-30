#include "FkChain.h"
#include "RigControlNode.h"
#include "RigJsonStructs.h"
#include "Utilities.h"
#include "RigModule.h"
#include "Side.h"

#include <string>
#include <nlohmann/json.hpp>

#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnCompoundAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MMatrix.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MPlug.h>
#include <maya/MDagPath.h>
#include <maya/MGlobal.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>

// ------------------------------------------------------------------
// FkChainNode Implementation
// ------------------------------------------------------------------

MTypeId FkChainNode::id(0x00218B);

FkChainNode::FkChainNode() {}
FkChainNode::~FkChainNode() {}

void* FkChainNode::creator()
{
	return new FkChainNode();
}

MStatus FkChainNode::initialize()
{
	MStatus status;
	MFnMatrixAttribute mAttr;
	MFnMessageAttribute msgAttr;
	MFnCompoundAttribute cAttr;
	MFnTypedAttribute tAttr;

	status = initializeSharedAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// AFFECTS RELATIONSHIPS
	attributeAffects(aControlMatrix, aControlOPM);
	attributeAffects(aChildPlugParentMatrix, aControlOPM);
	attributeAffects(aChildPlugParentRestMatrix, aControlOPM);
	attributeAffects(aControlRestWorld, aControlOPM);

	attributeAffects(aControlMatrix, aJointOPM);
	attributeAffects(aChildPlugParentMatrix, aJointOPM);
	attributeAffects(aChildPlugParentRestMatrix, aJointOPM);
	attributeAffects(aControlRestWorld, aJointOPM);

	attributeAffects(aControlMatrix, aOutputSocketMatrix);
	attributeAffects(aChildPlugParentMatrix, aOutputSocketMatrix);
	attributeAffects(aChildPlugParentRestMatrix, aOutputSocketMatrix);
	attributeAffects(aControlRestWorld, aOutputSocketMatrix);

	return MStatus::kSuccess;
}

MStatus FkChainNode::compute(const MPlug& plug, MDataBlock& data)
{
	unsigned int index = plug.isElement() ? plug.logicalIndex() : 0;
	const MObject oOutputAttr = plug.attribute();
	bool plugIsControlOPM = oOutputAttr == aControlOPM;
	bool plugIsJointOPM = oOutputAttr == aJointOPM;
	bool plugIsOutputSocketMatrix = oOutputAttr == aOutputSocketMatrix;

	if (!plugIsControlOPM && !plugIsJointOPM && !plugIsOutputSocketMatrix)
	{
		return MStatus::kUnknownParameter;
	}

	MStatus status;
	MArrayDataHandle hControlInputData = data.inputArrayValue(aControlInputData, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MArrayDataHandle hOutputData = data.outputArrayValue(oOutputAttr, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// Control OPM is the only output that excludes the control's local matrix.
	// Joint OPM and socket matrix both want the control's world matrix.
	status = solveFkChain(data, hControlInputData, hOutputData, 0, !plugIsControlOPM);
	CHECK_MSTATUS_AND_RETURN_IT(status);
		
	hOutputData.setAllClean();
	data.setClean(plug);
	return MStatus::kSuccess;
}

RigModuleBuildResult FkChainNode::createModule(
	const MString& moduleName,
	const Side& side,
	const ShapeType& shapeType,
	const MDagPathArray& jointDags,
	MObject oRigRoot,
	MDagModifier& dagMod)
{
	RigModuleBuildResult result;

	// Instantiate module node in DG
	MObject oFkChainModule = dagMod.MDGModifier::createNode(FkChainNode::id);
	dagMod.renameNode(oFkChainModule, moduleName);
	dagMod.doIt();

	MPlug pSide(oFkChainModule, FkChainNode::aSide);

	dagMod.newPlugValueShort(pSide, getShortFromSide(side));
	dagMod.doIt(); // write the side to the module so that future created controls get that.

	result.moduleNode = oFkChainModule;

	MFnDependencyNode moduleFn(oFkChainModule);
	MPlug pControlInputData(oFkChainModule, FkChainNode::aControlInputData);
	MPlug pOutputControlOPM(oFkChainModule, FkChainNode::aControlOPM);
	MPlug pOutputJointOPM(oFkChainModule, FkChainNode::aJointOPM);
	MPlug pControlMessage(oFkChainModule, FkChainNode::aControlMessage);

	MObject oPreviousControl;
	unsigned int numJoints = jointDags.length();

	for (unsigned int i = 0; i < numJoints; ++i)
	{
		MDagPath jointDag = jointDags[i];

		// Turn off inherits transform for all joints so their OPM can be in world space
		MObject oJoint = jointDag.node();

		// Make sure the joint retains its original transform and bake them again
		MMatrix mJointRestLocal = DagUtils::bakeTransformsToOPM(jointDag);
		MMatrix mJointRestWorld = jointDag.inclusiveMatrix();

		DagUtils::setInheritsTransforms(oJoint, false, dagMod);
		DagUtils::setSegmentScaleCompensate(oJoint, false, dagMod);
		dagMod.doIt();

		// After toggling inherits transform, the joint will have moved. We have to now set its matrix back and bake that to the OPM

		// Create control and flush modifier
		MObject oControlTransform = RigControlNode::createRigControl(oFkChainModule, dagMod, jointDag.partialPathName(), shapeType);
		dagMod.doIt();

		const std::string indexName = "index_" + std::to_string(i);
		MString socketNames = indexName.c_str();
		result.parentSockets[indexName] = oControlTransform;

		if (i == 0)
		{
			// Only the first control in an FK chain can be parented to another module.
			result.parentSockets["start"] = oControlTransform;
			result.childPlugs["start"] = oControlTransform;
			socketNames += ";start";

			DagPlugAndSocketUtils::setChildPlugName(oFkChainModule, 0, "start", dagMod);
		}

		if (i == numJoints - 1)
		{
			result.parentSockets["end"] = oControlTransform;
			socketNames += ";end";
		}

		DagPlugAndSocketUtils::setParentSocketName(oFkChainModule, i, socketNames.asChar(), dagMod);

		// 5. Setup Input connections for the control
		connectControlInputMatrix(oFkChainModule, oControlTransform, i, dagMod);
		// Store the control rest world matrix and never touch it
		setControlRestMatrix(oFkChainModule, mJointRestWorld, i, dagMod);

		// Connect the message attribute
		connectControlToRigModule(oFkChainModule, oControlTransform, i, dagMod);

		// 4. Connect previous control (i-1) to current module slot (i)
		if (!oPreviousControl.isNull())
		{
			dagMod.reparentNode(oControlTransform, oPreviousControl);		
		}
		dagMod.doIt();

		// Output connection setup
		// Connect the control opm output to the control's opm
		MPlug pControlOpm(oControlTransform, RigControlNode::offsetParentMatrix);
		dagMod.connect(pOutputControlOPM.elementByLogicalIndex(i), pControlOpm);

		MFnTransform fnJoint(oJoint);
		MPlug pJointOpm = fnJoint.findPlug("offsetParentMatrix", false);
		dagMod.connect(pOutputJointOPM.elementByLogicalIndex(i), pJointOpm);
		
		dagMod.doIt();

		// Update persistent path for next iteration
		oPreviousControl = oControlTransform;
	}

	if (!oRigRoot.isNull())
	{
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dagMod, oFkChainModule, aRigRoot);
		dagMod.doIt();
	}

	// Tie this module's reparent logic to the result callback called during creation.
	result.dagParentCallback = FkChainNode::reparentChildPlugs;
	result.status = MS::kSuccess;
	return result;
}

RigModuleBuildResult FkChainNode::buildFromJson(const nlohmann::json& args, BuildContext& ctx)
{
	RigModuleBuildResult result;
	FkModuleArgs fkArgs;

	try
	{
		// Version migration goes here: branch on ctx.moduleData.moduleVersion
		// and rewrite old arg layouts into the current FkModuleArgs shape.
		fkArgs = args.get<FkModuleArgs>();
	}
	catch (const nlohmann::json::exception& e)
	{
		MGlobal::displayError(MString("Invalid FkChain args for ") + ctx.moduleData.name.c_str() + ": " + e.what());
		return result;   // status defaults to kFailure
	}

	MDagPathArray jointDags;
	for (const std::string& jointName : fkArgs.joints)
	{
		MString jointNameStr(jointName.c_str());
		MDagPath jointDag = DagUtils::getDagPathFromName(jointNameStr);

		if (jointDag.node().isNull())
		{
			MGlobal::displayError(MString("Joint does not exist in scene: ") + jointNameStr);
			return result;
		}
		jointDags.append(jointDag);
	}

	return FkChainNode::createModule(
		ctx.moduleData.name.c_str(), 
		getSideFromString(ctx.moduleData.side),
		getShapeTypeFromString(ctx.moduleData.shapeType),
		jointDags, 
		ctx.oRigRoot, 
		ctx.dagMod);
}

MStatus FkChainNode::reparentChildPlugs(RigModuleDagReparentArgs args)
{
	MStatus status;

	if (args.childPlugName != "start")
	{
		MGlobal::displayError("The child plug name must be \'start\'");
		return MStatus::kFailure;
	}

	MDagModifier& dagMod = args.dagMod;
	MString targetSocketName = args.targetSocketName.c_str();
	MObject& oTargetModule = args.oParentTargetModuleNode;
	MObject& oChildModule = args.oChildModuleNode;
	MString childPlugName = args.childPlugName.c_str();
	MObject& oChildTransform = args.oChildPlugTransform;
	MObject& oTargetSocketTransform = args.oParentTargetTransform;

	status = dagMod.reparentNode(oChildTransform, oTargetSocketTransform);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// We are only doing this for the index 0 control (start)
	status = connectSocketMatrixToChildPlug(oChildModule, childPlugName, oTargetModule, targetSocketName, dagMod);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	return status;
}