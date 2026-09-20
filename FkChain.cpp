#include "FkChain.h"
#include "RigControlNode.h"
#include "RigRoot.h"

#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnTransform.h>
#include <maya/MFnMatrixData.h>
#include <maya/MMatrix.h>
#include <maya/MDataHandle.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MArrayDataBuilder.h>
#include <maya/MPlug.h>
#include <maya/MSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MStringArray.h>
#include <maya/MArgList.h>
#include <maya/MGlobal.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>

// ------------------------------------------------------------------
// FkChainNode Implementation
// ------------------------------------------------------------------

MTypeId FkChainNode::id(0x0021B);

MObject FkChainNode::inputRestMatrix;
MObject FkChainNode::controlMatrix;
MObject FkChainNode::outputControlOPM;
MObject FkChainNode::outputJointOPM;

FkChainNode::FkChainNode() {}
FkChainNode::~FkChainNode() {}

const MString FkChainNode::commandString = "fkChainModule";

void* FkChainNode::creator()
{
	return new FkChainNode();
}

MStatus FkChainNode::initialize()
{
	MStatus status;

	status = RigModuleNodeBase::initializeBaseAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MFnMatrixAttribute mAttr;

	// 1. INPUT ARRAYS
	inputRestMatrix = mAttr.create("inputRestMatrix", "irm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setStorable(true);
	mAttr.setKeyable(true);
	addAttribute(inputRestMatrix);

	controlMatrix = mAttr.create("controlMatrix", "cm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setStorable(true);
	mAttr.setKeyable(true);
	addAttribute(controlMatrix);

	// 2. OUTPUT ARRAYS
	outputControlOPM = mAttr.create("outputControlOffsetParentMatrix", "copm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setUsesArrayDataBuilder(true);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	addAttribute(outputControlOPM);

	outputJointOPM = mAttr.create("outputJointOffsetParentMatrix", "jopm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setUsesArrayDataBuilder(true);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	addAttribute(outputJointOPM);

	// 3. AFFECTS RELATIONSHIPS
	attributeAffects(parentWorldMatrix, outputControlOPM);
	attributeAffects(inputRestMatrix, outputControlOPM);

	attributeAffects(controlMatrix, outputJointOPM);
	attributeAffects(inputRestMatrix, outputJointOPM);
	attributeAffects(parentWorldMatrix, outputJointOPM);

	attributeAffects(controlMatrix, outputSocketMatrix);
	attributeAffects(parentWorldMatrix, outputSocketMatrix);
	attributeAffects(inputRestMatrix, outputSocketMatrix);

	return MStatus::kSuccess;
}

MStatus FkChainNode::evaluateModuleSolver(const MPlug& plug, MDataBlock& data)
{
	unsigned int index = plug.isElement() ? plug.logicalIndex() : 0;

	// -------------------------------------------------------------------
	// 1. Output Control Offset Parent Matrix Array
	// -------------------------------------------------------------------
	if (plug == outputControlOPM || plug.array() == outputControlOPM)
	{
		MMatrix inputRestLocal = getInputMatrix(data, inputRestMatrix, index);
		MMatrix controlOPM = inputRestLocal;

		if (index == 0)
		{
			MMatrix parentWorld = getInputMatrix(data, parentWorldMatrix);
			MMatrix parentOffset = getInputMatrix(data, parentModuleOffset);
			controlOPM = inputRestLocal * parentOffset * parentWorld;
		}

		setOutputMatrix(data, outputControlOPM, index, controlOPM);
		data.setClean(plug);
		return MStatus::kSuccess;
	}


	// -------------------------------------------------------------------
	// 2. Output Joint Offset Parent Matrix Array
	// -------------------------------------------------------------------
	if (plug == outputJointOPM || plug.array() == outputJointOPM)
	{

		MMatrix controlLocal = getInputMatrix(data, controlMatrix, index);
		MMatrix jointOffset = getInputMatrix(data, inputRestMatrix, index);

		MMatrix jointOPM = controlLocal * jointOffset;

		if (index == 0)
		{
			// include the module parent matrix in the chain
			MMatrix parentWorld = getInputMatrix(data, parentWorldMatrix);
			MMatrix parentOffset = getInputMatrix(data, parentModuleOffset);
			jointOPM = jointOPM * parentOffset * parentWorld;
		}

		setOutputMatrix(data, outputJointOPM, index, jointOPM);
		data.setClean(plug);
		return MStatus::kSuccess;
	}
	

	// -------------------------------------------------------------------
	// 3. Output Socket Matrix Array
	// -------------------------------------------------------------------
	if (plug == outputSocketMatrix || plug.array() == outputSocketMatrix)
	{
		MMatrix mSocketWorld = getInputMatrix(data, parentWorldMatrix);
		MMatrix mParentOffset = getInputMatrix(data, parentModuleOffset);

		MArrayDataHandle hControlArray = data.inputArrayValue(controlMatrix);
		MArrayDataHandle hRestArray = data.inputArrayValue(inputRestMatrix);
		MArrayDataHandle hOutSocketArray = data.outputArrayValue(outputSocketMatrix);

		unsigned int elementCount = hControlArray.elementCount();

		// Evaluate sequentially down the chain
		for (unsigned int i = 0; i < elementCount; ++i)
		{
			hControlArray.jumpToElement(i);
			hRestArray.jumpToElement(i);

			// Jump directly to the output element slot without rebuild
			if (hOutSocketArray.jumpToElement(hControlArray.elementIndex()) == MStatus::kSuccess)
			{
				MMatrix mControlLocal = hControlArray.inputValue().asMatrix();
				MMatrix mRestLocal = hRestArray.inputValue().asMatrix();

				// Accumulate step matrix downstream
				mSocketWorld = mControlLocal * mRestLocal * mParentOffset * mSocketWorld;

				// Write directly to existing data block
				hOutSocketArray.outputValue().setMMatrix(mSocketWorld);
			}
		}

		// NOTE: because this is world space in a hierarchy, we need to calculate all sockets at once.
		hOutSocketArray.setAllClean();
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	return MStatus::kUnknownParameter;
}

// ------------------------------------------------------------------
// FkChainNode Commands
// ------------------------------------------------------------------

// FkChainNode Setup command ----------------------------------------
const MString FkChainNodeSetupCmd::commandString = "setupFkChainModule";

const char* FkChainNodeSetupCmd::kNameFlagShort = "-n";  // TODO: many of these high-level flags should be referenced from RigModule.cpp
const char* FkChainNodeSetupCmd::kNameFlagLong = "-name";
const char* FkChainNodeSetupCmd::kJointsFlagShort = "-j";
const char* FkChainNodeSetupCmd::kJointsFlagLong = "-joints";
const char* FkChainNodeSetupCmd::kParentModuleShort = "-pm";
const char* FkChainNodeSetupCmd::kParentModuleLong = "-parentModule";
const char* FkChainNodeSetupCmd::kParentSocketIndexShort = "-psi";
const char* FkChainNodeSetupCmd::kParentSocketIndexLong = "-parentSocketIndex";
const char* FkChainNodeSetupCmd::kRigRootLong = "-rigRoot";
const char* FkChainNodeSetupCmd::kRigRootShort = "-rr";


FkChainNodeSetupCmd::FkChainNodeSetupCmd() {}
FkChainNodeSetupCmd::~FkChainNodeSetupCmd() {}

void* FkChainNodeSetupCmd::creator()
{
	return new FkChainNodeSetupCmd();
}

//TODO: attribute naming conventions for matrices, function sets, etc.
// Creates the syntax for the command
MSyntax FkChainNodeSetupCmd::newSyntax()
{
	MSyntax syntax;
	syntax.addFlag(kNameFlagShort, kNameFlagLong, MSyntax::kString);
	syntax.addFlag(kJointsFlagShort, kJointsFlagLong, MSyntax::kString);
	syntax.makeFlagMultiUse(kJointsFlagShort);
	syntax.addFlag(kParentModuleShort, kParentModuleLong, MSyntax::kString);
	syntax.addFlag(kParentSocketIndexShort, kParentSocketIndexLong, MSyntax::kLong);
	syntax.addFlag(kRigRootShort, kRigRootLong, MSyntax::kString);

	return syntax;
}

// Runs the command logic
MStatus FkChainNodeSetupCmd::doIt(const MArgList& args)
{
	// Arg Gathering --------------------------------------------
	MStatus status;
	MArgDatabase argData(newSyntax(), args, &status);

	MDGModifier dgMod;
	MDagModifier dagMod;

	// Gather the passed in joint names
	// TODO: this can become a shared method
	MStringArray jointNames;
	unsigned int numJoints = 0;
	if (argData.isFlagSet(kJointsFlagShort))
	{
		// TODO: extract this syntax into a shared method for getting a list arg
		numJoints = argData.numberOfFlagUses(kJointsFlagShort);

		for (unsigned int i = 0; i < numJoints; ++i)
		{
			MArgList flagArgs;
			// Fetch the argument list for the i-th occurance of the joints flag
			status = argData.getFlagArgumentList(kJointsFlagShort, i, flagArgs);
			if (status == MStatus::kSuccess)
			{
				for (unsigned int j = 0; j < flagArgs.length(); ++j)
				{
					MString jointName = flagArgs.asString(j);
					jointNames.append(jointName);
				}
			}
		}
	}

	if (jointNames.length() == 0 || numJoints == 0)
	{
		MGlobal::displayError("At least one joint must be given with the -j flag.");
		return MStatus::kFailure;
	}

	// TODO: check module exists
	// TODO: this can become a shared method
	MString parentModuleName;
	if (argData.isFlagSet(kParentModuleShort))
	{
		argData.getFlagArgument(kParentModuleShort, 0, parentModuleName);

		MSelectionList modSelList;
		status = modSelList.add(parentModuleName);

		if (status != MStatus::kSuccess)
		{
			MGlobal::displayError("Module name " + parentModuleName + " does not exist.");
			return MStatus::kFailure;
		}
	}

	// NOTE: The socket index defaults to 0 for convenience.
	unsigned int parentModuleSocketIndex = 0;
	if (argData.isFlagSet(kParentSocketIndexShort))
	{
		argData.getFlagArgument(kParentSocketIndexShort, 0, parentModuleSocketIndex);
	}

	// Shared command actions which read the args and do things  -------------

	// TODO: run a shared RigModule method here which will do base-level shared functions.
	MObject moduleObj = RigModuleNodeBase::createAndNameModule(argData, dgMod, FkChainNodeSetupCmd::kNameFlagShort);

	// Command Action --------------------------------------------------------

	// Get module plugs
	MFnDependencyNode moduleFn(moduleObj);
	MPlug inputRestMatrix = moduleFn.findPlug("inputRestMatrix", false);
	MPlug controlMatrix = moduleFn.findPlug("controlMatrix", false);
	MPlug outputControlOPM = moduleFn.findPlug("outputControlOffsetParentMatrix", false);
	MPlug outputJointOPM = moduleFn.findPlug("outputJointOffsetParentMatrix", false);
	
	// TODO: wire this module to the given rig root


	MObject previousControlTransform = MObject::kNullObj;
	MMatrix mFirstControlWorld = MMatrix::identity;

	for (unsigned int i = 0; i < numJoints; ++i)
	{
		// Bake the joint's transforms to its offset parent matrix (jointOPM = parentInverseMatrix * worldMatrix)
		// Get the joint's dag path and confirm it exists
		MSelectionList sel;
		status = sel.add(jointNames[i]);

		if (status == MStatus::kFailure)
		{
			MGlobal::displayError("Given joint " + jointNames[i] + " does not exist.");
			return MStatus::kFailure;
		}

		MDagPath jointDag;
		status = sel.getDagPath(0, jointDag);

		MFnDagNode jointFnDag(jointDag);
		MPlug jointOpmPlug = jointFnDag.findPlug("offsetParentMatrix", false);
		MPlug jointParentInvMatPlug = jointFnDag.findPlug("parentInverseMatrix", false);
		MMatrix mJointWorld = jointDag.inclusiveMatrix();

		// Fetch the parent inverse matrix value
		MObject parentInvObj;
		jointParentInvMatPlug.elementByLogicalIndex(0).getValue(parentInvObj);
		MFnMatrixData parentInvData(parentInvObj);
		MMatrix mJointParentInv = parentInvData.matrix();

		// Set the joint's offset parent matrix to the parent inverse * the world
		MMatrix jointOffsetParentMatrix = mJointWorld * mJointParentInv;
		MFnMatrixData matrixData;
		MObject opmMatrixDataObject = matrixData.create(jointOffsetParentMatrix);
		jointOpmPlug.setValue(opmMatrixDataObject);

		// Zero the joint's transforms (its transforms are now baked into its offset parent matrix)
		MFnTransform jointFnTrans(jointDag);
		jointFnTrans.set(MTransformationMatrix::identity);

		// Set the input rest matrix to the joint's OPM value
		MPlug inputRestPlug = inputRestMatrix.elementByLogicalIndex(i);
		inputRestPlug.setValue(opmMatrixDataObject);

		// Create the control transform
		MObject controlTransform = RigModuleNodeBase::createRigControl(moduleObj, dagMod, jointNames[i]);

		// Parent the control transform to the previous parent if there is one
		if (previousControlTransform != MObject::kNullObj)
		{
			dagMod.reparentNode(controlTransform, previousControlTransform);
		}
		dagMod.doIt();
		previousControlTransform = controlTransform;

		// Get the control's matrix plugs
		MFnDependencyNode controlMFn(controlTransform);
		MPlug controlMatrixPlug = controlMFn.findPlug("matrix", false);
		MPlug controlOpmPlug = controlMFn.findPlug("offsetParentMatrix", false);

		// Connect the control's matrix to the module's controlMatrix input at this index
		dgMod.connect(controlMatrixPlug, controlMatrix.elementByLogicalIndex(i));
		// Connect the output control opm to the control opm
		dgMod.connect(outputControlOPM.elementByLogicalIndex(i), controlOpmPlug);
		// Connect the output joint opm to the joint opm
		dgMod.connect(outputJointOPM.elementByLogicalIndex(i), jointOpmPlug);

		if (i == 0)
		{
			dgMod.doIt();
			MSelectionList sel;
			sel.add(controlTransform);
			MDagPath firstControlDag;
			sel.getDagPath(0, firstControlDag);
			mFirstControlWorld = firstControlDag.inclusiveMatrix();
		}
	}

	// Wire parent to the module and maintain offset
	// TODO: move to shared method
	if (parentModuleName.isEmpty() != true)
	{
		// Wire the parent module to the new module
		MObject parentModuleMObj;
		MSelectionList modSelList;
		modSelList.add(parentModuleName);
		modSelList.getDependNode(0, parentModuleMObj);
		MFnDependencyNode parentModuleFn(parentModuleMObj);
		MPlug childModulesPlug = parentModuleFn.findPlug("childModules", false);
		unsigned int numElements = childModulesPlug.numElements();

		MPlug parentModulePlug = moduleFn.findPlug("parentModule", false);
		// TODO: is this the correct way to connect to the next index?
		dgMod.connect(childModulesPlug.elementByLogicalIndex(numElements), parentModulePlug);

		// Wire the parent's socket matrix index to the child
		MPlug parentSocketMatrix = parentModuleFn.findPlug("outputSocketMatrix", false).elementByLogicalIndex(parentModuleSocketIndex);
		MPlug childParentMatrix = moduleFn.findPlug("parentWorldMatrix", false);
		dgMod.connect(parentSocketMatrix, childParentMatrix);

		// Compute offset --------------------------
		MObject parentSocketObj;
		parentSocketMatrix.getValue(parentSocketObj);
		MFnMatrixData parentSocketData(parentSocketObj);
		MMatrix mParentSocketWorld = parentSocketData.matrix();

		MMatrix mParentModuleOffset = mFirstControlWorld * mParentSocketWorld.inverse();

		// Set the parentModuleOffset plug value
		MPlug parentModuleOffset = moduleFn.findPlug("parentModuleOffset", false);
		MFnMatrixData parentModuleOffsetData;
		MObject parentModuleOffsetObj = parentModuleOffsetData.create(mParentModuleOffset);
		parentModuleOffset.setValue(parentModuleOffsetObj);
	}

	// Wire this module to the rig root
	FkChainNode::connectModuleToRigRoot(argData, dgMod, moduleObj);

	status = dgMod.doIt();

	// Set command return value (we may need to define MResultType)
	MString finalModuleName = moduleFn.name();
	MStringArray result;
	result.append(finalModuleName);
	setResult(result);

	return status;
}
