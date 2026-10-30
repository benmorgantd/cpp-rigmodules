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
#include <maya/MFnMatrixData.h>

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
	bool plugIsControlOPM = plug.attribute() == aControlOPM;
	bool plugIsJointOPM = plug.attribute() == aJointOPM;
	bool plugIsOutputSocketMatrix = plug.attribute() == aOutputSocketMatrix;

	if (plugIsControlOPM || plugIsJointOPM || plugIsOutputSocketMatrix)
	{
		MArrayDataHandle hInputDataArray = data.inputArrayValue(aControlInputData);
		MArrayDataHandle hOutputArray = data.outputArrayValue(
			plugIsJointOPM ? aJointOPM : (plugIsControlOPM ? aControlOPM : aOutputSocketMatrix));

		// Socket live world and its world at the time of parenting. Both identity if unparented.
		MMatrix mParentWorld = getInputCompoundMatrix(data, aChildPlugs, aChildPlugParentMatrix, 0);
		MMatrix mParentRest = getInputCompoundMatrix(data, aChildPlugs, aChildPlugParentRestMatrix, 0);

		const unsigned int elementCount = hInputDataArray.elementCount();

		for (unsigned int i = 0; i < elementCount; ++i)
		{
			hInputDataArray.jumpToElement(i);
			MDataHandle hElement = hInputDataArray.inputValue();

			const MMatrix mControlLocal = hElement.child(aControlMatrix).asMatrix();
			const MMatrix mControlRestWorld = hElement.child(aControlRestWorld).asMatrix();

			// Motion of the parent since rest. Identity at rest.
			const MMatrix mParentDelta = mParentRest.inverse() * mParentWorld;

			// World-space OPM: the control's rest carried by the parent's motion. No local in here.
			const MMatrix mControlOPM = mControlRestWorld * mParentDelta;
			const MMatrix mControlWorld = mControlLocal * mControlOPM;

			// Carry state: identical for every output plug.
			mParentWorld = mControlWorld;
			mParentRest = mControlRestWorld;

			// Only the written value depends on the plug.
			const MMatrix& mOut = plugIsControlOPM ? mControlOPM : mControlWorld;

			if (hOutputArray.jumpToElement(i) == MStatus::kSuccess)
			{
				hOutputArray.outputValue().setMMatrix(mOut);
			}
		}

		hOutputArray.setAllClean();
		data.setClean(plug);
		return MStatus::kSuccess;
	}
	/*
	unsigned int index = plug.isElement() ? plug.logicalIndex() : 0;
	bool plugIsControlOPM = plug.attribute() == aControlOPM;
	bool plugIsJointOPM = plug.attribute() == aJointOPM;
	bool plugIsOutputSocketMatrix = plug.attribute() == aOutputSocketMatrix;

	// -------------------------------------------------------------------
	// 1. Output Control Offset Parent Matrix Array OR Joint OPM array (the logic is the same)
	// -------------------------------------------------------------------
	if (plugIsControlOPM || plugIsJointOPM || plugIsOutputSocketMatrix)
	{
		MArrayDataHandle hInputDataArray = data.inputArrayValue(aControlInputData);
		// the logic is the same for control and joint opm array
		MArrayDataHandle hOutputOPMArray = plugIsJointOPM ? data.outputArrayValue(aJointOPM) : data.outputArrayValue(aControlOPM);
		MArrayDataHandle hOutputSocketMatrixArray = data.outputArrayValue(aOutputSocketMatrix);

		MMatrix mCurrentParentWorld = getInputCompoundMatrix(data, aChildPlugs, aChildPlugParentMatrix, 0);
		MMatrix mCurrentParentWorldRest = getInputCompoundMatrix(data, aChildPlugs, aChildPlugParentRestMatrix, 0);
		unsigned int elementCount = hInputDataArray.elementCount();

		for (unsigned int i = 0; i < elementCount; ++i)
		{
			hInputDataArray.jumpToElement(i);
			MDataHandle hElement = hInputDataArray.inputValue();

			MMatrix mControlLocal = hElement.child(aControlMatrix).asMatrix();
			MMatrix mControlWorldRest = hElement.child(aControlRestWorld).asMatrix();
			MMatrix mOutputOPM;

			// TODO: our issue with the jump is here in the compute. 
			// When dealing with world spaces, we're double-applying transformations.
			// The end transform OPM has to be in world space, but we also have to apply the parent's in world space. 

			MMatrix mParentDelta = mCurrentParentWorldRest.inverse() * mCurrentParentWorld;
			MMatrix mWorldOffset = mControlWorldRest * mParentDelta;

			if (plugIsJointOPM)
			{
				// we DO want to include control local transforms for joints or else they wont follow their parent controls...
				mOutputOPM = mControlLocal * mWorldOffset;
			}
			else
			{
				// NOTE: We can't have the control's local matrix in its own opm! That causes a double transformation.
				mOutputOPM = mWorldOffset;
			}

			// Carry world matrix forward for index i + 1
			mCurrentParentWorld = mControlLocal * mOutputOPM;
			// Make sure we also update the world rest so we know the delta
			mCurrentParentWorldRest = mWorldOffset;

			if (plugIsOutputSocketMatrix)
			{
				hOutputSocketMatrixArray.jumpToElement(i);
				hOutputSocketMatrixArray.outputValue().setMMatrix(mControlLocal * mOutputOPM);
			}
			else
			{
				// Write to output array in-place
				hOutputOPMArray.jumpToElement(i);
				hOutputOPMArray.outputValue().setMMatrix(mOutputOPM);
			}
		}

		if (plugIsOutputSocketMatrix)
		{
			hOutputSocketMatrixArray.setAllClean();
		}
		else
		{
			hOutputOPMArray.setAllClean();
		}
		
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	return MStatus::kUnknownParameter;
	*/
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

		if (i == 0)
		{
			result.parentSockets[std::string("start")] = oControlTransform;
			result.parentSockets[std::string("index_0")] = oControlTransform;
			
			result.childPlugs[std::string("start")] = oControlTransform;  // only the first control can be parented in a fkchain
			result.childPlugs[std::string("index_0")] = oControlTransform;

			// Compound, separated by semicolons
			MString plugAndSocketName = MString("start;") + "index_" + std::to_string(i).c_str();

			// Only the first in the chain can be a child
			MPlug pChildPlugs(oFkChainModule, aChildPlugs);
			MPlug pChildPlugsElement = pChildPlugs.elementByLogicalIndex(0);
			MPlug pChildPlugsElementName = pChildPlugsElement.child(aChildPlugName);
			dagMod.newPlugValueString(pChildPlugsElementName, plugAndSocketName);
			dagMod.setPlugLockState(pChildPlugsElementName, true);

			// Also add to socket names
			MPlug pSocketNames(oFkChainModule, FkChainNode::aSocketNames);
			MPlug pSocketNamesElement = pSocketNames.elementByLogicalIndex(i);
			dagMod.newPlugValueString(pSocketNamesElement, plugAndSocketName);
			dagMod.setPlugLockState(pSocketNamesElement, true);

		}
		else if (i == numJoints - 1)
		{
			result.parentSockets[std::string("end")] = oControlTransform;

			MPlug pSocketNames(oFkChainModule, FkChainNode::aSocketNames);
			MPlug pSocketNamesElement = pSocketNames.elementByLogicalIndex(i);
			dagMod.newPlugValueString(pSocketNamesElement, MString("end;") + "index_" + std::to_string(i).c_str());
			dagMod.setPlugLockState(pSocketNamesElement, true);
		}
		else
		{
			result.parentSockets["index_" + std::to_string(i)] = oControlTransform;

			MPlug pSocketNames(oFkChainModule, FkChainNode::aSocketNames);
			MPlug pSocketNamesElement = pSocketNames.elementByLogicalIndex(i);
			dagMod.newPlugValueString(pSocketNamesElement, MString("index_") + std::to_string(i).c_str());
			dagMod.setPlugLockState(pSocketNamesElement, true);
		}


		// 5. Setup Input connections for the control
		MPlug pControlInputDataIndex = pControlInputData.elementByLogicalIndex(i);

		MFnDependencyNode fnControl(oControlTransform);
		MPlug pControlLocalMatrix(oControlTransform, RigControlNode::matrix);
		MPlug pControlInputMatrix = pControlInputDataIndex.child(aControlMatrix);
		dagMod.connect(pControlLocalMatrix, pControlInputMatrix);

		// Store the control rest world matrix and never touch it
		MPlug pControlRestWorld = pControlInputDataIndex.child(aControlRestWorld);
		DagUtils::setMatrixPlug(pControlRestWorld, mJointRestWorld, dagMod);

		// Connect the message attribute
		MPlug pControlMessages(oFkChainModule, FkChainNode::aControlMessage);
		MPlug pControlMessageIndex = pControlMessages.elementByLogicalIndex(i);
		MPlug pControlRigModule(oControlTransform, RigControlNode::aRigModule);
		dagMod.connect(pControlMessageIndex, pControlRigModule);

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

	// TODO: all this is broken and needs fixin'

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

	MDagPath dagParent;
	MDagPath::getAPathTo(oTargetSocketTransform, dagParent);
	MMatrix mParentOrigWorld = dagParent.inclusiveMatrix();

	// We are only doing this for the index 0 control (start)
	status = connectSocketMatrixToChildPlug(oChildModule, childPlugName, oTargetModule, targetSocketName, dagMod);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	return status;
}