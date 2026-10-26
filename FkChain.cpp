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
const MString FkChainNode::aCommandString = "fkChainModule";

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
	attributeAffects(aControlRestOffset, aControlOPM);

	attributeAffects(aControlMatrix, aJointOPM);
	attributeAffects(aChildPlugParentMatrix, aJointOPM);
	attributeAffects(aControlRestOffset, aJointOPM);

	attributeAffects(aControlMatrix, aOutputSocketMatrix);
	attributeAffects(aChildPlugParentMatrix, aOutputSocketMatrix);
	attributeAffects(aControlRestOffset, aOutputSocketMatrix);

	return MStatus::kSuccess;
}

MStatus FkChainNode::compute(const MPlug& plug, MDataBlock& data)
{
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
		unsigned int elementCount = hInputDataArray.elementCount();

		for (unsigned int i = 0; i < elementCount; ++i)
		{
			hInputDataArray.jumpToElement(i);
			MDataHandle hElement = hInputDataArray.inputValue();

			MMatrix mControlLocal = hElement.child(aControlMatrix).asMatrix();
			MMatrix mControlRestOffset = hElement.child(aControlRestOffset).asMatrix();
			MMatrix mOutputOPM;

			if (plugIsJointOPM)
			{
				// we DO want to include control local transforms for joints or else they wont follow their parent controls...
				mOutputOPM = mControlLocal * mControlRestOffset * mCurrentParentWorld;
				mCurrentParentWorld = mOutputOPM; // This is already in world space
			}
			else 
			{
				// NOTE: We can't have the control's local matrix in its own opm! That causes a double transformation.
				mOutputOPM = mControlRestOffset * mCurrentParentWorld;
				// Carry world matrix forward for index i + 1
				mCurrentParentWorld = mControlLocal * mOutputOPM;
			}

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
	MObject oFkChainModule = dagMod.MDGModifier::createNode(FkChainNode::aCommandString);
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
		DagUtils::setInheritsTransforms(oJoint, false, dagMod);
		DagUtils::setSegmentScaleCompensate(oJoint, false, dagMod);

		// Make sure the joint retains its original transform and bake them again
		MMatrix mJointRest = DagUtils::bakeTransformsToOPM(jointDag);

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

		MPlug pInputRestMatrix = pControlInputDataIndex.child(aControlRestOffset);
		DagUtils::setMatrixPlug(pInputRestMatrix, mJointRest);

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

	dagMod.reparentNode(oChildTransform, oTargetSocketTransform);
	dagMod.doIt();

	// We are only doing this for the index 0 control (start)
	return connectSocketMatrixToChildPlug(oChildModule, childPlugName, oTargetModule, targetSocketName, dagMod);
}