#include "FkChain.h"
#include "RigControlNode.h"
#include "RigJsonStructs.h"
#include "Utilities.h"
#include "RigModule.h"

#include <string>

#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnDagNode.h>
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

MObject FkChainNode::aInputRestMatrix;
MObject FkChainNode::aControlMatrix;
MObject FkChainNode::aOutputJointOPM;
MObject FkChainNode::aOutputControlOPM;

// Shared Attributes (Although shared, these need to be unique MObjects for each module)
MObject FkChainNode::aModuleData;
MObject FkChainNode::aRigControls;
MObject FkChainNode::aRigRoot;
MObject FkChainNode::aOutputSocketMatrix;
MObject FkChainNode::aSide;

// Attrs for this node
MObject FkChainNode::aDagParentMatrix;

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

	status = FkChainNode::initializeSharedAttributes(aModuleData, aRigRoot, aRigControls, aOutputSocketMatrix, aSide);

	if (status != MStatus::kSuccess)
	{
		return MStatus::kFailure;
	}

	// INPUT ATTRIBUTES
	aInputRestMatrix = mAttr.create("inputRestMatrix", "irm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setStorable(true);
	mAttr.setKeyable(true);
	addAttribute(aInputRestMatrix);

	aControlMatrix = mAttr.create("controlMatrix", "cm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setStorable(true);
	mAttr.setKeyable(true);
	addAttribute(aControlMatrix);

	aDagParentMatrix = mAttr.create("dagParentMatrix", "dpm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setStorable(true);
	mAttr.setKeyable(true);  // are these needed? 
	addAttribute(aDagParentMatrix);

	// OUTPUT ATTRIBUTES
	aOutputJointOPM = mAttr.create("outputJointOPM", "jopm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setUsesArrayDataBuilder(true);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	addAttribute(aOutputJointOPM);

	aOutputControlOPM = mAttr.create("outputControlOPM", "copm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setUsesArrayDataBuilder(true);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	addAttribute(aOutputControlOPM);

	if (status != MStatus::kSuccess)
	{
		MGlobal::displayError(MString("There was an error in FkChain initialize: ") + status.errorString());
		return MStatus::kFailure;
	}

	// AFFECTS RELATIONSHIPS
	attributeAffects(aInputRestMatrix, aOutputControlOPM);
	attributeAffects(aDagParentMatrix, aOutputControlOPM);

	attributeAffects(aDagParentMatrix, aOutputJointOPM);
	attributeAffects(aControlMatrix, aOutputJointOPM);
	attributeAffects(aInputRestMatrix, aOutputJointOPM);

	attributeAffects(aDagParentMatrix, aOutputSocketMatrix);
	attributeAffects(aControlMatrix, aOutputSocketMatrix);
	attributeAffects(aInputRestMatrix, aOutputSocketMatrix);

	return MStatus::kSuccess;
}

MStatus FkChainNode::compute(const MPlug& plug, MDataBlock& data)
{
	unsigned int index = plug.isElement() ? plug.logicalIndex() : 0;

	// -------------------------------------------------------------------
	// 1. Output Control Offset Parent Matrix Array
	// -------------------------------------------------------------------
	if (plug.attribute() == aOutputControlOPM)
	{
		// If aOutputControlOPM plug is never getting dirtied, why is that? 
		MMatrix inputRestLocal = getInputMatrix(data, aInputRestMatrix, index);

		// Our control's matrix is already going to be affected by its DAG relationship to the parent. 
		// Therefore we don't have to include that parent matrix in this, or else we'd double-transform.
		// By default, our control's offset parent matrix is just its baked starting offset.
		MMatrix controlOPM = inputRestLocal;

		setOutputMatrix(data, aOutputControlOPM, index, controlOPM);
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	// -------------------------------------------------------------------
	// 2. Output Joint Offset Parent Matrix Array
	// -------------------------------------------------------------------
	if (plug.attribute() == aOutputJointOPM)
	{
		MMatrix controlLocal = getInputMatrix(data, aControlMatrix, index);
		MMatrix jointOffset = getInputMatrix(data, aInputRestMatrix, index);

		MMatrix jointOPM;

		if (index == 0)
		{
			MMatrix parentMatrix = getInputMatrix(data, aDagParentMatrix);

			jointOPM = controlLocal * jointOffset * parentMatrix;
		}
		else
		{
			jointOPM = controlLocal * jointOffset;
		}

		setOutputMatrix(data, aOutputJointOPM, index, jointOPM);
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	// -------------------------------------------------------------------
	// 3. Output Socket Matrix Array
	// -------------------------------------------------------------------
	if (plug.attribute() == aOutputSocketMatrix)
	{
		// Establish the base world space entering the module chain
		// Local Step * Parent World (Row-Major)
		MArrayDataHandle hControlArray = data.inputArrayValue(aControlMatrix);
		MArrayDataHandle hOutSocketArray = data.outputArrayValue(aOutputSocketMatrix);

		MMatrix mCurrentWorld = MMatrix::identity;

		unsigned int elementCount = hControlArray.elementCount();

		// Evaluate sequentially down the chain
		for (unsigned int i = 0; i < elementCount; ++i)
		{
			MMatrix mControlLocal = getInputMatrix(data, aControlMatrix, i);
			MMatrix mRestLocal = getInputMatrix(data, aInputRestMatrix, i);

			// 1. Combine local control and rest matrices for this step
			MMatrix mLocalStep = mControlLocal * mRestLocal;

			// 2. Transform local step into current world space
			MMatrix mSocketWorld = mLocalStep * mCurrentWorld;

			setOutputMatrix(data, aOutputSocketMatrix, i, mSocketWorld);

			// 3. Update accumulated world matrix for the next child downstream
			mCurrentWorld = mSocketWorld;
		}

		// NOTE: because this is world space in a hierarchy, we need to calculate all sockets at once.
		hOutSocketArray.setAllClean();
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
	MPlug pInputRestMatrix = moduleFn.findPlug("inputRestMatrix", false);
	MPlug pControlMatrix = moduleFn.findPlug("controlMatrix", false);
	MPlug pOutputControlOPM = moduleFn.findPlug("outputControlOPM", false);
	MPlug pOutputJointOPM = moduleFn.findPlug("outputJointOPM", false);

	MObject oPreviousControl;
	unsigned int numJoints = jointDags.length();

	for (unsigned int i = 0; i < numJoints; ++i)
	{
		MDagPath jointDag = jointDags[i];

		// Bake rest matrices
		MMatrix mJointRest = DagUtils::bakeTransformsToOPM(jointDag);

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

		// 4. Connect previous control (i-1) to current module slot (i)
		if (!oPreviousControl.isNull())
		{
			dagMod.reparentNode(oControlTransform, oPreviousControl);
		}
		dagMod.doIt();

		// 5. Connect local control & joint matrices for index i
		MFnDependencyNode fnControl(oControlTransform);
		MPlug pControlLocalMatrix = fnControl.findPlug("matrix", false);
		dgMod.connect(pControlLocalMatrix, pControlMatrix.elementByLogicalIndex(i));

		MPlug pControlOpm = fnControl.findPlug("offsetParentMatrix", false);
		dgMod.connect(pOutputControlOPM.elementByLogicalIndex(i), pControlOpm);

		MPlug pInputRestMatrixIndex = pInputRestMatrix.elementByLogicalIndex(i);
		DagUtils::setMatrixPlug(pInputRestMatrixIndex, mJointRest);

		MFnDagNode jointFnDag(jointDag);
		MPlug pJointOpm = jointFnDag.findPlug("offsetParentMatrix", false);
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

	// TODO: we will want to do the dag parenting, update offset parent matrices, and connect parent matrices to the module.
	MGlobal::displayInfo("In fkchain reparent function");

	MDagModifier& dagMod = args.dagMod;
	MObject& oChildTransform = args.oChildPlugTransform;
	MObject& oTargetSocketTrasform = args.oParentTargetTransform;
	MObject& oChildModule = args.oChildModuleNode;

	dagMod.reparentNode(oChildTransform, oTargetSocketTrasform);


	// Connect the parent socket transform's world matrix to the module's dagParentMatrix
	MFnDependencyNode fnParentTransform(oTargetSocketTrasform);
	MPlug pParentWorldMatrix = fnParentTransform.findPlug("worldMatrix", false).elementByLogicalIndex(0);
	MPlug pDagParentMatarix(oChildModule, FkChainNode::aDagParentMatrix);
	MDGModifier& dgMod = args.dgMod;

	dgMod.connect(pParentWorldMatrix, pDagParentMatarix);

	return MStatus::kSuccess;
}