#include "FkChain.h"
#include "RigControlNode.h"
#include "RigJsonStructs.h"

#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnTransform.h>
#include <maya/MFnMatrixData.h>
#include <maya/MMatrix.h>
#include <maya/MDataHandle.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MPlug.h>
#include <maya/MSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MStringArray.h>
#include <maya/MArgList.h>
#include <maya/MGlobal.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>
#include <maya/MArgDatabase.h>

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

MObject FkChainNode::createModule(
	const MString& moduleName,
	const MDagPathArray& jointDags,
	MObject oParentModule,
	unsigned int parentModuleSocketIndex,
	MObject oRigRoot,
	MDGModifier& dgMod,
	MDagModifier& dagMod,
	MStatus* status)
{
	MStatus localStat;

	// 1. Create the FkChainModule node
	MObject oModule = dgMod.createNode("fkChainModule");
	dgMod.renameNode(oModule, moduleName);

	// Get module plugs
	MFnDependencyNode moduleFn(oModule);
	MPlug inputRestMatrix(oModule, FkChainNode::inputRestMatrix);
	MPlug controlMatrix(oModule, FkChainNode::controlMatrix);
	// TODO: use this style for other findPlug calls.
	MPlug outputControlOPM = moduleFn.findPlug("outputControlOffsetParentMatrix", false);
	MPlug outputJointOPM = moduleFn.findPlug("outputJointOffsetParentMatrix", false);

	MObject previousControlTransform = MObject::kNullObj;
	MMatrix mFirstControlWorld = MMatrix::identity;
	unsigned int numJoints = jointDags.length();

	for (unsigned int i = 0; i < numJoints; ++i)
	{
		MDagPath jointDag = jointDags[i];

		MFnDagNode jointFnDag(jointDag);
		MPlug jointOpmPlug = jointFnDag.findPlug("offsetParentMatrix", false);
		MPlug jointParentInvMatPlug = jointFnDag.findPlug("parentInverseMatrix", false);
		MMatrix mJointWorld = jointDag.inclusiveMatrix();

		// Fetch parent inverse matrix value
		MObject parentInvObj;
		jointParentInvMatPlug.elementByLogicalIndex(0).getValue(parentInvObj);
		MFnMatrixData parentInvData(parentInvObj);
		MMatrix mJointParentInv = parentInvData.matrix();

		// Bake joint transforms to offsetParentMatrix
		MMatrix jointOffsetParentMatrix = mJointWorld * mJointParentInv;
		MFnMatrixData matrixData;
		MObject opmMatrixDataObject = matrixData.create(jointOffsetParentMatrix);
		jointOpmPlug.setValue(opmMatrixDataObject);

		// Zero the joint's local transforms
		MFnTransform jointFnTrans(jointDag);
		jointFnTrans.set(MTransformationMatrix::identity);

		// Set rest matrix on module
		MPlug inputRestPlug = inputRestMatrix.elementByLogicalIndex(i);
		inputRestPlug.setValue(opmMatrixDataObject);

		// Create control transform[cite: 1]
		MObject controlTransform = RigControlNode::createRigControl(oModule, dagMod, jointDag.partialPathName());

		// Hierarchy parenting
		if (previousControlTransform != MObject::kNullObj)
		{
			dagMod.reparentNode(controlTransform, previousControlTransform);
		}
		dagMod.doIt();
		previousControlTransform = controlTransform;

		// Matrix wiring
		MFnDependencyNode controlMFn(controlTransform);
		MPlug controlMatrixPlug = controlMFn.findPlug("matrix", false);
		MPlug controlOpmPlug = controlMFn.findPlug("offsetParentMatrix", false);

		dgMod.connect(controlMatrixPlug, controlMatrix.elementByLogicalIndex(i));
		dgMod.connect(outputControlOPM.elementByLogicalIndex(i), controlOpmPlug);
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

	// 2. Wire parent module socket connection
	if (!oParentModule.isNull())
	{
		localStat = RigModuleCommandHelpers::connectToParentModule(oParentModule, oModule, parentModuleSocketIndex, dgMod, mFirstControlWorld);
		if (!localStat)
		{
			MGlobal::displayError("Failed to connect module to parent.");
			if (status) *status = localStat;
			return MObject::kNullObj;
		}
	}

	// 3. Wire module to rig root
	if (!oRigRoot.isNull())
	{
		// TODO: we need to refactor this to pass in the rig root name
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dgMod, oModule);
	}

	if (status) *status = MS::kSuccess;
	return oModule;
}


MObject FkChainNode::createModule(
	const RigModuleData& moduleData,
	MObject rigRoot,
	MObject parentModule,
	MDGModifier& dgMod,
	MDagModifier& dagMod
)
{
	MStatus status;

	// 1. Resolve string joint names from RigModuleData into MDagPath instances
	MDagPathArray jointDags;
	for (const std::string& jointName : moduleData.joints)
	{
		MSelectionList selList;
		status = selList.add(jointName.c_str());
		if (status && selList.length() > 0)
		{
			MDagPath jointDag;
			status = selList.getDagPath(0, jointDag);
			if (status)
			{
				jointDags.append(jointDag);
			}
			else
			{
				MGlobal::displayError(MString("Failed to retrieve MDagPath for joint: ") + jointName.c_str());
			}
		}
		else
		{
			MGlobal::displayError(MString("Could not find joint in scene: ") + jointName.c_str());
		}
	}

	// 2. Safely cast parent socket index
	unsigned int socketIndex = 0;
	if (moduleData.parentSocketIndex >= 0)
	{
		socketIndex = static_cast<unsigned int>(moduleData.parentSocketIndex);
	}

	// 3. Delegate to the flattened arguments overload
	MStatus executionStatus;
	MObject oModule = FkChainNode::createModule(
		moduleData.name.c_str(),
		jointDags,
		parentModule,
		socketIndex,
		rigRoot,
		dgMod,
		dagMod,
		&executionStatus
	);

	if (!executionStatus)
	{
		MGlobal::displayError(MString("Failed to build FkChain module: ") + moduleData.name.c_str());
		return MObject::kNullObj;
	}

	return oModule;
}

// ------------------------------------------------------------------
// FkChainNode Commands
// ------------------------------------------------------------------

// FkChainNode Setup command ----------------------------------------
const MString FkChainNodeSetupCmd::commandString = "setupFkChainModule";

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
	MStatus status;
	MSyntax syntax = RigModuleCommandHelpers::createBaseModuleSyntax(status);
	if (!status)
	{
		MGlobal::displayError("Failed to create base module syntax for command.");
	}

	return syntax;
}

// Runs the command logic
// TODO: make the "brains" of this the createModule method.
MStatus FkChainNodeSetupCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(newSyntax(), args, &status);
	if (!status)
	{
		return status;
	}

	MDGModifier dgMod;
	MDagModifier dagMod;

	// Arg Gathering --------------------------------------------
	MDagPathArray jointDags = RigModuleCommandHelpers::getJointsFromArgs(argData, status);
	MObject oParentModule = RigModuleCommandHelpers::getParentModule(argData);
	unsigned int parentModuleSocketIndex = RigModuleCommandHelpers::getParentModuleSocketIndex(argData);
	MString moduleName = RigModuleCommandHelpers::getModuleNameFromArgs(argData);
	MObject oRigRoot = RigModuleCommandHelpers::getRigRootFromArgs(argData);

	// Command Action -------------------------------------------
	MObject oModule = FkChainNode::createModule(
		moduleName,
		jointDags,
		oParentModule,
		parentModuleSocketIndex,
		oRigRoot,
		dgMod,
		dagMod,
		&status
	);

	if (!status || oModule.isNull())
	{
		return status;
	}

	status = dgMod.doIt();
	status = dagMod.doIt();

	// Set command return value
	MFnDependencyNode moduleFn(oModule);
	MStringArray result;
	result.append(moduleFn.name());
	setResult(result);

	return status;
}
//MStatus FkChainNodeSetupCmd::doIt(const MArgList& args)
//{
//	// TODO: we want to separate the arg gathering part of doIt() from the action part of it
//	// This will allow us to call the "setup" command for a module from cpp without passing in command syntax.
//	// That will allow for more top-down, all in cpp command actions! :D
//	// Arg Gathering --------------------------------------------
//	MStatus status;
//	MArgDatabase argData(newSyntax(), args, &status);
//
//	MDGModifier dgMod;
//	MDagModifier dagMod;
//
//	// Gather the passed in joint names
//	MDagPathArray jointDags = RigModuleCommandHelpers::getJointsFromArgs(argData, status);
//	MObject oParentModule = RigModuleCommandHelpers::getParentModule(argData);
//
//	// NOTE: The socket index defaults to 0 for convenience.
//	unsigned int parentModuleSocketIndex = RigModuleCommandHelpers::getParentModuleSocketIndex(argData);
//
//	// TODO: run a shared RigModule method here which will do base-level shared functions.
//	MString moduleName = RigModuleCommandHelpers::getModuleNameFromArgs(argData);
//
//	// Create the FkChainModule node
//	MObject oModule = dgMod.createNode("fkChainModule");
//	dgMod.renameNode(oModule, moduleName);
//
//	// Command Action --------------------------------------------------------
//
//	// Get module plugs
//	MFnDependencyNode moduleFn(oModule);
//	MPlug inputRestMatrix = moduleFn.findPlug("inputRestMatrix", false);
//	MPlug controlMatrix = moduleFn.findPlug("controlMatrix", false);
//	MPlug outputControlOPM = moduleFn.findPlug("outputControlOffsetParentMatrix", false);
//	MPlug outputJointOPM = moduleFn.findPlug("outputJointOffsetParentMatrix", false);
//	
//	MObject previousControlTransform = MObject::kNullObj;
//	MMatrix mFirstControlWorld = MMatrix::identity;
//	unsigned int numJoints = jointDags.length();
//
//	for (unsigned int i = 0; i < numJoints; ++i)
//	{
//		// Bake the joint's transforms to its offset parent matrix (jointOPM = parentInverseMatrix * worldMatrix)
//		// Get the joint's dag path and confirm it exists
//		MDagPath jointDag = jointDags[i];
//
//		MFnDagNode jointFnDag(jointDag);
//		MPlug jointOpmPlug = jointFnDag.findPlug("offsetParentMatrix", false);
//		MPlug jointParentInvMatPlug = jointFnDag.findPlug("parentInverseMatrix", false);
//		MMatrix mJointWorld = jointDag.inclusiveMatrix();
//
//		// Fetch the parent inverse matrix value
//		MObject parentInvObj;
//		jointParentInvMatPlug.elementByLogicalIndex(0).getValue(parentInvObj);
//		MFnMatrixData parentInvData(parentInvObj);
//		MMatrix mJointParentInv = parentInvData.matrix();
//
//		// Set the joint's offset parent matrix to the parent inverse * the world
//		MMatrix jointOffsetParentMatrix = mJointWorld * mJointParentInv;
//		MFnMatrixData matrixData;
//		MObject opmMatrixDataObject = matrixData.create(jointOffsetParentMatrix);
//		jointOpmPlug.setValue(opmMatrixDataObject);
//
//		// Zero the joint's transforms (its transforms are now baked into its offset parent matrix)
//		MFnTransform jointFnTrans(jointDag);
//		jointFnTrans.set(MTransformationMatrix::identity);
//
//		// Set the input rest matrix to the joint's OPM value
//		MPlug inputRestPlug = inputRestMatrix.elementByLogicalIndex(i);
//		inputRestPlug.setValue(opmMatrixDataObject);
//
//		// Create the control transform
//		MObject controlTransform = RigControlNode::createRigControl(oModule, dagMod, jointDag.partialPathName());
//
//		// Parent the control transform to the previous parent if there is one
//		if (previousControlTransform != MObject::kNullObj)
//		{
//			dagMod.reparentNode(controlTransform, previousControlTransform);
//		}
//		dagMod.doIt();
//		previousControlTransform = controlTransform;
//
//		// Get the control's matrix plugs
//		MFnDependencyNode controlMFn(controlTransform);
//		MPlug controlMatrixPlug = controlMFn.findPlug("matrix", false);
//		MPlug controlOpmPlug = controlMFn.findPlug("offsetParentMatrix", false);
//
//		// Connect the control's matrix to the module's controlMatrix input at this index
//		dgMod.connect(controlMatrixPlug, controlMatrix.elementByLogicalIndex(i));
//		// Connect the output control opm to the control opm
//		dgMod.connect(outputControlOPM.elementByLogicalIndex(i), controlOpmPlug);
//		// Connect the output joint opm to the joint opm
//		dgMod.connect(outputJointOPM.elementByLogicalIndex(i), jointOpmPlug);
//
//		if (i == 0)
//		{
//			dgMod.doIt();
//			MSelectionList sel;
//			sel.add(controlTransform);
//			MDagPath firstControlDag;
//			sel.getDagPath(0, firstControlDag);
//			mFirstControlWorld = firstControlDag.inclusiveMatrix();
//		}
//	}
//
//	// Wire parent to the module and maintain offset
//	if (!oParentModule.isNull())
//	{
//		status = RigModuleCommandHelpers::connectToParentModule(oParentModule, oModule, parentModuleSocketIndex, dgMod, mFirstControlWorld);
//		if (!status)
//		{
//			MGlobal::displayError("Failed to connect module to parent.");
//			return status;
//		}
//	}
//	
//	// Wire this module to the rig root
//	RigModuleCommandHelpers::connectModuleToRigRoot(argData, dgMod, oModule);
//
//	status = dgMod.doIt();
//
//	// Set command return value (we may need to define MResultType)
//	MString finalModuleName = moduleFn.name();
//	MStringArray result;
//	result.append(finalModuleName);
//	setResult(result);
//
//	return status;
//}
