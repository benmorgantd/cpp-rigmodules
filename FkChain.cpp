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
#include <maya/MFnTransform.h>

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
	status = initializeSharedAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// AFFECTS RELATIONSHIPS
	// Every control/child-plug input affects control OPM, joint OPM and socket matrix outputs.
	declareStandardAffects();

	return MStatus::kSuccess;
}

MStatus FkChainNode::compute(const MPlug& plug, MDataBlock& data)
{
	const MObject oOutputAttr = plug.attribute();

	// Control OPM is the only output that excludes the control's local matrix.
	// Joint OPM and socket matrix both want the control's world matrix.
	if (oOutputAttr == aControlOPM)
	{
		return computeChainOutput(plug, data, false);
	}

	if (oOutputAttr == aJointOPM || oOutputAttr == aOutputSocketMatrix)
	{
		return computeChainOutput(plug, data, true);
	}

	return MStatus::kUnknownParameter;
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
		// The module drives the joint's world transform through its OPM, so nothing may be inherited.
		DagUtils::setInheritsTransforms(oJoint, false, dagMod);
		DagUtils::setSegmentScaleCompensate(oJoint, false, dagMod);
		MStatus bakeStatus = DagUtils::bakeJointTransformsToOPM(oJoint, mJointRestWorld, dagMod);

		if (!bakeStatus)
		{
			result.status = bakeStatus;
			return result;
		}

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
			result.parentSockets["start"] = socketInfo;
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

		MStatus wireStatus = wireControl(oFkChainModule, oControlTransform, i, mJointRestWorld, dagMod);
		if (!wireStatus)
		{
			result.status = wireStatus;
			return result;
		}

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
	result.dagParentCallback = FkChainNode::reparentChildPlugToSocket;
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

	if (fkArgs.joints.empty())
	{
		MGlobal::displayError(MString("FkChain needs at least one joint: ") + ctx.moduleData.name.c_str());
		return result;
	}

	MObjectArray oJoints;
	MMatrixArray mWorldMatrices;

	for (const std::string& jointName : fkArgs.joints)
	{
		MObject oJoint;
		MMatrix mWorld;
		MStatus status = DagUtils::getJointAndWorldMatrix(jointName.c_str(), oJoint, mWorld);
		if (!status)
		{
			return result;  // helper already reported which joint failed
		}

		oJoints.append(oJoint);
		mWorldMatrices.append(mWorld);
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
