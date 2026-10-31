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
#include <maya/MMatrixArray.h>
#include <maya/MSelectionList.h>

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
	const MObjectArray& oJoints,
	const MMatrixArray& mJointWorldRests,
	MObject& oRigRoot,
	MDagModifier& dagMod)
{
	RigModuleBuildResult result;

	// Instantiate module node in DG
	MObject oFkChainModule = dagMod.MDGModifier::createNode(FkChainNode::id);
	dagMod.renameNode(oFkChainModule, moduleName);

	MPlug pSide(oFkChainModule, FkChainNode::aSide);
	dagMod.newPlugValueShort(pSide, getShortFromSide(side));
	result.moduleNode = oFkChainModule;

	MFnDependencyNode moduleFn(oFkChainModule);
	MPlug pControlInputData(oFkChainModule, FkChainNode::aControlInputData);
	MPlug pOutputControlOPM(oFkChainModule, FkChainNode::aControlOPM);
	MPlug pOutputJointOPM(oFkChainModule, FkChainNode::aJointOPM);
	MPlug pControlMessage(oFkChainModule, FkChainNode::aControlMessage);

	MObject oPreviousControl;
	unsigned int numJoints = oJoints.length();

	for (unsigned int i = 0; i < numJoints; ++i)
	{
		// Turn off inherits transform for all joints so their OPM can be in world space
		MObject oJoint = oJoints[i];
		MMatrix mJointRestWorld = mJointWorldRests[i];

		// Make sure the joint retains its original transform and bake them again
		DagUtils::setInheritsTransforms(oJoint, false, dagMod);
		DagUtils::setSegmentScaleCompensate(oJoint, false, dagMod);
		result.status = DagUtils::bakeJointTransformsToOPM(oJoint, mJointRestWorld, dagMod);

		// Create control and flush modifier
		MFnTransform fnJoint(oJoint);
		MObject oControlTransform = RigControlNode::createRigControl(oFkChainModule, dagMod, fnJoint.name(), shapeType, side);

		const std::string indexName = "index_" + std::to_string(i);
		MString socketNames = indexName.c_str();

		RigModuleSocketInfo socketInfo;
		socketInfo.oTransform = oControlTransform;
		socketInfo.mRestWorld = mJointRestWorld;
		socketInfo.outputIndex = i;
		result.parentSockets[indexName] = socketInfo;

		if (i == 0)
		{
			// Only the first control in an FK chain can be parented to another module.
			result.parentSockets["start"] = socketInfo;;
			result.childPlugs["start"] = RigModuleChildPlugInfo{ oControlTransform, 0 };
			socketNames += ";start";

			DagPlugAndSocketUtils::setChildPlugName(oFkChainModule, 0, "start", dagMod);
		}

		if (i == numJoints - 1)
		{
			result.parentSockets["end"] = socketInfo;
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

		// Output connection setup
		// Connect the control opm output to the control's opm
		MPlug pControlOpm(oControlTransform, RigControlNode::offsetParentMatrix);
		dagMod.connect(pOutputControlOPM.elementByLogicalIndex(i), pControlOpm);

		MPlug pJointOpm = fnJoint.findPlug("offsetParentMatrix", false);
		dagMod.connect(pOutputJointOPM.elementByLogicalIndex(i), pJointOpm);
		
		// Update persistent path for next iteration
		oPreviousControl = oControlTransform;
	}

	if (!oRigRoot.isNull())
	{
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dagMod, oFkChainModule, aRigRoot);
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

	MObjectArray oJoints;
	MMatrixArray mWorldMatrices;

	for (const std::string& jointName : fkArgs.joints)
	{
		MString jointNameStr(jointName.c_str());
		MSelectionList sel;
		sel.add(jointNameStr);
		MObject oJoint;
		sel.getDependNode(0, oJoint);

		if (oJoint.isNull())
		{
			MGlobal::displayError(MString("Joint does not exist in scene: ") + jointNameStr);
			return result;
		}
		oJoints.append(oJoint);

		// Get the world matrix and append it
		MStatus status;
		MDagPath jointDag;
		MDagPath::getAPathTo(oJoint, jointDag);
		mWorldMatrices.append(jointDag.inclusiveMatrix());
	}

	return FkChainNode::createModule(
		ctx.moduleData.name.c_str(),
		getSideFromString(ctx.moduleData.side),
		getShapeTypeFromString(ctx.moduleData.shapeType),
		oJoints,
		mWorldMatrices,
		ctx.oRigRoot, 
		ctx.dagMod);
}

MStatus FkChainNode::reparentChildPlugs(RigModuleDagReparentArgs args)
{
	MStatus status = args.dagMod.reparentNode(args.childPlug.oTransform, args.parentSocket.oTransform);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	return connectSocketMatrixToChildPlug(
		args.oChildModuleNode,
		args.childPlug.plugIndex,
		args.oParentTargetModuleNode,
		args.parentSocket.outputIndex,
		args.parentSocket.mRestWorld,
		args.dagMod
	);
}