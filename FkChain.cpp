#include "FkChain.h"
#include "RigControlNode.h"
#include "RigJsonStructs.h"
#include "Utilities.h"
#include "RigModule.h"

#include <string>

#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnCompoundAttribute.h>
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

MObject FkChainNode::aControlInputData;
MObject FkChainNode::aControlMatrix;
MObject FkChainNode::aControlRestOffset;
MObject FkChainNode::aControlParentMatrix;

MObject FkChainNode::aControlOPM;
MObject FkChainNode::aJointOPM;

MObject FkChainNode::aControlMessage;

// Shared Attributes (Although shared, these need to be unique MObjects for each module)
MObject FkChainNode::aModuleData;
MObject FkChainNode::aRigControls;
MObject FkChainNode::aRigRoot;
MObject FkChainNode::aOutputSocketMatrix;
MObject FkChainNode::aSide;

FkChainNode::FkChainNode() {}
FkChainNode::~FkChainNode() {}

const MString FkChainNode::aCommandString = "fkChainModule";

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

	status = FkChainNode::initializeSharedAttributes(aModuleData, aRigRoot, aRigControls, aOutputSocketMatrix, aSide);

	if (status != MStatus::kSuccess)
	{
		return MStatus::kFailure;
	}

	// INPUT ATTRIBUTES
	// Control compound input attr sub attrs
	aControlMatrix = mAttr.create("localMatrix", "localMatrix", MFnMatrixAttribute::kDouble, &status);
	aControlParentMatrix = mAttr.create("parentMatrix", "parentMatrix", MFnMatrixAttribute::kDouble, &status);
	aControlRestOffset = mAttr.create("restOffset", "restOffset", MFnMatrixAttribute::kDouble, &status);
	mAttr.setStorable(true);

	aControlInputData = cAttr.create("controlData", "controlData", &status);

	cAttr.addChild(aControlMatrix);
	cAttr.addChild(aControlParentMatrix);
	cAttr.addChild(aControlRestOffset);

	cAttr.setArray(true);
	cAttr.setUsesArrayDataBuilder(true);
	addAttribute(aControlInputData);

	// Control message attr connection
	aControlMessage = msgAttr.create("controlMessage", "controlMessage", &status);
	msgAttr.setArray(true);
	addAttribute(aControlMessage);

	// OUTPUT ATTRIBUTES
	// Control output array attr
	aControlOPM = mAttr.create("controlOPM", "controlOPM", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	mAttr.setUsesArrayDataBuilder(true);
	addAttribute(aControlOPM);

	aJointOPM = mAttr.create("jointOPM", "jointOPM", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	mAttr.setUsesArrayDataBuilder(true);
	addAttribute(aJointOPM);

	if (status != MStatus::kSuccess)
	{
		MGlobal::displayError(MString("There was an error in FkChain initialize: ") + status.errorString());
		return MStatus::kFailure;
	}

	// AFFECTS RELATIONSHIPS
	attributeAffects(aControlMatrix, aControlOPM);
	attributeAffects(aControlParentMatrix, aControlOPM);
	attributeAffects(aControlRestOffset, aControlOPM);

	attributeAffects(aControlMatrix, aJointOPM);
	attributeAffects(aControlParentMatrix, aJointOPM);
	attributeAffects(aControlRestOffset, aJointOPM);

	//attributeAffects(aControlMatrix, aOutputSocketMatrix);
	//attributeAffects(aControlParentMatrix, aOutputSocketMatrix);
	//attributeAffects(aControlRestOffset, aOutputSocketMatrix);

	return MStatus::kSuccess;
}

MStatus FkChainNode::compute(const MPlug& plug, MDataBlock& data)
{
	unsigned int index = plug.isElement() ? plug.logicalIndex() : 0;

	// -------------------------------------------------------------------
	// 1. Output Control Offset Parent Matrix Array OR Joint OPM array (the logic is the same)
	// -------------------------------------------------------------------
	if (plug.attribute() == aControlOPM || plug.attribute() == aJointOPM)
	{
		MArrayDataHandle hInputDataArray = data.inputArrayValue(aControlInputData);
		// the logic is the same for control and joint opm array
		MArrayDataHandle hOutputOPMArray = plug.attribute() == aControlOPM ? data.outputArrayValue(aControlOPM) : data.outputArrayValue(aJointOPM);

		// Base incoming matrix (e.g. from upstream socket or asset root)
		MMatrix mCurrentParentWorld = getInputCompoundMatrix(data, aControlInputData, aControlParentMatrix, 0);

		unsigned int elementCount = hInputDataArray.elementCount();

		for (unsigned int i = 0; i < elementCount; ++i)
		{
			hInputDataArray.jumpToElement(i);
			MDataHandle hElement = hInputDataArray.inputValue();

			MMatrix mControlLocal = hElement.child(aControlMatrix).asMatrix();
			MMatrix mControlRestOffset = hElement.child(aControlRestOffset).asMatrix();
			// NOTE: We can't have the control's local matrix in its opm! That causes a double transformation.
			MMatrix mControlOPM = mControlRestOffset * mCurrentParentWorld;

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

RigModuleBuildResult FkChainNode::createModule(
	const MString& moduleName,
	const MDagPathArray& jointDags,
	MObject oRigRoot,
	MDGModifier& dgMod,
	MDagModifier& dagMod)
{
	RigModuleBuildResult result;

	// Instantiate module node in DG
	MObject oModule = dgMod.createNode("fkChainModule");
	dgMod.renameNode(oModule, moduleName);
	dgMod.doIt();

	result.moduleNode = oModule;

	MFnDependencyNode moduleFn(oModule);
	MPlug pControlInputData(oModule, FkChainNode::aControlInputData);
	MPlug pOutputControlOPM(oModule, FkChainNode::aControlOPM);
	MPlug pOutputJointOPM(oModule, FkChainNode::aJointOPM);
	MPlug pControlMessage(oModule, FkChainNode::aControlMessage);

	MObject oPreviousControl;
	unsigned int numJoints = jointDags.length();

	for (unsigned int i = 0; i < numJoints; ++i)
	{
		MDagPath jointDag = jointDags[i];

		MMatrix jointOriginalWorldMatrix = jointDag.inclusiveMatrix();

		// Turn off inherits transform for all joints so their OPM can be in world space
		MObject oJoint = jointDag.node();
		DagUtils::setInheritsTransforms(oJoint, false, dagMod);

		// Make sure the joint retains its original transform and bake them again
		MMatrix mJointRest = DagUtils::bakeTransformsToOPM(jointDag);

		// After toggling inherits transform, the joint will have moved. We have to now set its matrix back and bake that to the OPM

		// Create control and flush modifier
		MObject oControlTransform = RigControlNode::createRigControl(oModule, dagMod, jointDag.partialPathName());
		dagMod.doIt();

		if (i == 0)
		{
			result.parentSockets[std::string("start")] = oControlTransform;
			result.childPlugs[std::string("start")] = oControlTransform;  // only the first control can be parented in a fkchain
		}
		else if (i == numJoints - 1)
		{
			result.parentSockets[std::string("end")] = oControlTransform;
		}
		result.parentSockets["index_" + std::to_string(i)] = oControlTransform;
		
		// 5. Setup Input connections for the control
		MPlug pControlInputDataIndex = pControlInputData.elementByLogicalIndex(i);

		MFnDependencyNode fnControl(oControlTransform);
		MPlug pControlLocalMatrix(oControlTransform, RigControlNode::matrix);
		MPlug pControlInputMatrix = pControlInputDataIndex.child(aControlMatrix);
		dgMod.connect(pControlLocalMatrix, pControlInputMatrix);

		MPlug pInputRestMatrix = pControlInputDataIndex.child(aControlRestOffset);
		DagUtils::setMatrixPlug(pInputRestMatrix, mJointRest);

		// Connect the message attribute
		MPlug pControlMessages(oModule, FkChainNode::aControlMessage);
		MPlug pControlMessageIndex = pControlMessages.elementByLogicalIndex(i);
		MPlug pControlRigModule(oControlTransform, RigControlNode::aRigModule);
		dgMod.connect(pControlMessageIndex, pControlRigModule);

		// 4. Connect previous control (i-1) to current module slot (i)
		if (!oPreviousControl.isNull())
		{
			dagMod.reparentNode(oControlTransform, oPreviousControl);

			MPlug pParentMatrix = pControlInputDataIndex.child(aControlParentMatrix);
			MPlug pPreviousMatrix(oPreviousControl, RigControlNode::matrix);  // TODO: world matrix?
			dgMod.connect(pPreviousMatrix, pParentMatrix);
		}
		dagMod.doIt();

		// Output connection setup
		// Connect the control opm output to the control's opm
		MPlug pControlOpm(oControlTransform, RigControlNode::offsetParentMatrix);
		dgMod.connect(pOutputControlOPM.elementByLogicalIndex(i), pControlOpm);

		MFnTransform fnJoint(oJoint);
		MPlug pJointOpm = fnJoint.findPlug("offsetParentMatrix", false);
		dgMod.connect(pOutputJointOPM.elementByLogicalIndex(i), pJointOpm);
		
		dgMod.doIt();

		// Update persistent path for next iteration
		oPreviousControl = oControlTransform;
	}

	if (!oRigRoot.isNull())
	{
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dgMod, oModule, aRigRoot);
		dgMod.doIt();
	}

	// Tie this module's reparent logic to the result callback called during creation.
	result.dagParentCallback = FkChainNode::reparentChildPlugs;

	result.status = MS::kSuccess;
	return result;
}

RigModuleBuildResult FkChainNode::createModule(
	const RigModuleData& moduleData,
	MObject rigRoot,
	MDGModifier& dgMod,
	MDagModifier& dagMod)
{
	RigModuleBuildResult result;

	// 1. Resolve string joint names from RigModuleData into MDagPath instances
	const auto* fkArgs = std::get_if<FkModuleArgs>(&moduleData.moduleArgs);
	if (!fkArgs)
	{
		MGlobal::displayError(MString("Invalid moduleArgs variant type for FkChainModule: ") + moduleData.name.c_str());
		result.status = MStatus::kFailure;
		return result;
	}

	MDagPathArray jointDags;
	for (const std::string& jointName : fkArgs->joints)
	{
		MString jointNameStr(jointName.c_str());
		MDagPath jointDag = DagUtils::getDagPathFromName(jointNameStr);

		if (!jointDag.node().isNull())
		{
			jointDags.append(jointDag);
		}
		else
		{
			MGlobal::displayError(MString("Joint does not exist in scene: ") + jointNameStr);
		}
	}

	// 2. Delegate to the flattened arguments overload
	result = FkChainNode::createModule(
		moduleData.name.c_str(),
		jointDags,
		rigRoot,
		dgMod,
		dagMod
	);

	if (!result)
	{
		MGlobal::displayError(MString("Failed to build FkChain module: ") + moduleData.name.c_str());
	}

	return result;
}

MStatus FkChainNode::reparentChildPlugs(RigModuleDagReparentArgs args)
{
	if (args.childPlugName != "start")
	{
		MGlobal::displayError("The child plug name must be \'start\'");
		return MStatus::kFailure;
	}

	MDGModifier& dgMod = args.dgMod;
	MDagModifier& dagMod = args.dagMod;
	MObject& oChildTransform = args.oChildPlugTransform;
	MObject& oTargetSocketTrasform = args.oParentTargetTransform;
	MObject& oChildModule = args.oChildModuleNode;

	MFnDependencyNode fnChildTransform(oChildTransform);
	MPlug pChildWorldMatrix = fnChildTransform.findPlug("worldMatrix", false).elementByLogicalIndex(0);

	MMatrix mChildWorldMatrix = DagUtils::getMatrixFromPlug(pChildWorldMatrix);

	// Then do the actual reparenting but BEFORE we set the plug value
	dagMod.reparentNode(oChildTransform, oTargetSocketTrasform);
	dagMod.doIt();

	// We are only doing this for the index 0 control (start)
	// In other cases, we could traverse the message connection to find the control's index.
	MPlug pModuleControlInputData(oChildModule, FkChainNode::aControlInputData);
	MPlug pModuleControlInputDataIndex = pModuleControlInputData.elementByLogicalIndex(0);
	MPlug pControlRestOffset = pModuleControlInputDataIndex.child(aControlRestOffset);
	MPlug pControlParentMatrix = pModuleControlInputDataIndex.child(aControlParentMatrix);

	MFnDependencyNode fnParentTransform(oTargetSocketTrasform);
	MPlug pParentMatrix = fnParentTransform.findPlug("worldMatrix", false).elementByLogicalIndex(0);
	dgMod.connect(pParentMatrix, pControlParentMatrix);

	return MStatus::kSuccess;
}