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
#include <maya/MArrayDataBuilder.h>

// ------------------------------------------------------------------
// FkChainNode Implementation
// ------------------------------------------------------------------

MTypeId FkChainNode::id(0x00218B);

MObject FkChainNode::aInputRestMatrix;
MObject FkChainNode::aControlMatrix;
MObject FkChainNode::aOutputControlOPM;
MObject FkChainNode::aOutputJointOPM;

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

	status = RigModuleNodeBase::initializeBaseAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MFnMatrixAttribute mAttr;

	// 1. INPUT ARRAYS
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

	// 2. OUTPUT ARRAYS
	aOutputControlOPM = mAttr.create("outputControlOPM", "copm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setUsesArrayDataBuilder(true);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	addAttribute(aOutputControlOPM);

	aOutputJointOPM = mAttr.create("outputJointOPM", "jopm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setUsesArrayDataBuilder(true);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	addAttribute(aOutputJointOPM);

	// 3. AFFECTS RELATIONSHIPS
	attributeAffects(aParentWorldMatrix, aOutputControlOPM);
	attributeAffects(aInputRestMatrix, aOutputControlOPM);

	attributeAffects(aControlMatrix, aOutputJointOPM);
	attributeAffects(aInputRestMatrix, aOutputJointOPM);
	attributeAffects(aParentWorldMatrix, aOutputJointOPM);

	attributeAffects(aControlMatrix, aOutputSocketMatrix);
	attributeAffects(aParentWorldMatrix, aOutputSocketMatrix);
	attributeAffects(aInputRestMatrix, aOutputSocketMatrix);

	return MStatus::kSuccess;
}

MStatus FkChainNode::evaluateModuleSolver(const MPlug& plug, MDataBlock& data)
{
	unsigned int index = plug.isElement() ? plug.logicalIndex() : 0;

	// -------------------------------------------------------------------
	// 1. Output Control Offset Parent Matrix Array
	// -------------------------------------------------------------------
	if (plug == aOutputControlOPM || plug.array() == aOutputControlOPM)
	{
		MMatrix inputRestLocal = getInputMatrix(data, aInputRestMatrix, index);
		MMatrix controlOPM = inputRestLocal;

		if (index == 0)
		{
			MMatrix parentWorld = getInputMatrix(data, aParentWorldMatrix);
			MMatrix parentOffset = getInputMatrix(data, aParentModuleOffset);
			controlOPM = inputRestLocal * parentOffset * parentWorld;
		}

		setOutputMatrix(data, aOutputControlOPM, index, controlOPM);
		data.setClean(plug);
		return MStatus::kSuccess;
	}


	// -------------------------------------------------------------------
	// 2. Output Joint Offset Parent Matrix Array
	// -------------------------------------------------------------------
	if (plug == aOutputJointOPM || plug.array() == aOutputJointOPM)
	{

		MMatrix controlLocal = getInputMatrix(data, aControlMatrix, index);
		MMatrix jointOffset = getInputMatrix(data, aInputRestMatrix, index);

		MMatrix jointOPM = controlLocal * jointOffset;

		if (index == 0)
		{
			// include the module parent matrix in the chain
			MMatrix parentWorld = getInputMatrix(data, aParentWorldMatrix);
			MMatrix parentOffset = getInputMatrix(data, aParentModuleOffset);
			jointOPM = jointOPM * parentOffset * parentWorld;
		}

		setOutputMatrix(data, aOutputJointOPM, index, jointOPM);
		data.setClean(plug);
		return MStatus::kSuccess;
	}
	

	// -------------------------------------------------------------------
	// 3. Output Socket Matrix Array
	// -------------------------------------------------------------------
	if (plug == aOutputSocketMatrix || plug.array() == aOutputSocketMatrix)
	{
		MMatrix mParentWorld = getInputMatrix(data, aParentWorldMatrix);
		MMatrix mParentOffset = getInputMatrix(data, aParentModuleOffset);

		// Establish the base world space entering the module chain
		// Local Step * Parent World (Row-Major)
		MMatrix mCurrentWorld = mParentOffset * mParentWorld;

		MArrayDataHandle hControlArray = data.inputArrayValue(aControlMatrix);
		MArrayDataHandle hRestArray = data.inputArrayValue(aInputRestMatrix);
		MArrayDataHandle hOutSocketArray = data.outputArrayValue(aOutputSocketMatrix);

		unsigned int elementCount = hControlArray.elementCount();

		// Evaluate sequentially down the chain
		for (unsigned int i = 0; i < elementCount; ++i)
		{
			hControlArray.jumpToElement(i);
			hRestArray.jumpToElement(i);

			MMatrix mControlLocal = hControlArray.inputValue().asMatrix();
			MMatrix mRestLocal = hRestArray.inputValue().asMatrix();

			// 1. Combine local control and rest matrices for this step
			MMatrix mLocalStep = mControlLocal * mRestLocal;

			// 2. Transform local step into current world space
			MMatrix mSocketWorld = mLocalStep * mCurrentWorld;

			// Jump directly to the output element slot without rebuild
			if (hOutSocketArray.jumpToElement(hControlArray.elementIndex()) == MStatus::kSuccess)
			{
				// 3. Write directly to existing datablock handle
				hOutSocketArray.outputValue().setMMatrix(mSocketWorld);
			}
			else
			{
				// We need to use MArrayDataBuilder if we couldn't yet jump to the element index.
				MArrayDataBuilder builder = hOutSocketArray.builder();
				MDataHandle hNewElem = builder.addElement(hControlArray.elementIndex());
				hNewElem.setMMatrix(mSocketWorld);
				hOutSocketArray.set(builder);
			}

			// 4. Update accumulated world matrix for the next child downstream
			mCurrentWorld = mSocketWorld;
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
	MPlug inputRestMatrix(oModule, FkChainNode::aInputRestMatrix);
	MPlug controlMatrix(oModule, FkChainNode::aControlMatrix);
	// TODO: use this style for other findPlug calls.
	MPlug outputControlOPM = moduleFn.findPlug("outputControlOPM", false);
	MPlug outputJointOPM = moduleFn.findPlug("outputJointOPM", false);

	MObject previousControlTransform = MObject::kNullObj;
	MMatrix mFirstControlWorld = MMatrix::identity;
	unsigned int numJoints = jointDags.length();

	// For this module, set the numSockets value to the number of joints we have.
	MPlug pNumSockets(oModule, FkChainNode::aNumSockets);
	dgMod.newPlugValueShort(pNumSockets, numJoints);

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

		// Create control transform
		MObject controlTransform = RigControlNode::createRigControl(oModule, dagMod, jointDag.partialPathName());

		// Hierarchy parenting
		if (previousControlTransform != MObject::kNullObj)
		{
			dagMod.reparentNode(controlTransform, previousControlTransform);
		}
		dagMod.doIt();
		previousControlTransform = controlTransform;

		// Wire the control's matrix to the module's input control matrix plug
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

	// Execute both the dg and dag mod at the end of each module's creation so that future modules can depend on their attrs existing.
	dgMod.doIt();
	dagMod.doIt();

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
	// TODO: I don't really understand what this is from gemini, but this is the pattern for getting our moduleArgs substruct.
	const auto* fkArgs = std::get_if<FkModuleArgs>(&moduleData.moduleArgs);
	if (!fkArgs)
	{
		MGlobal::displayError(MString("Invalid moduleArgs variant type for FkChainModule: ") + moduleData.name.c_str());
		return MObject::kNullObj;
	}

	for (const std::string& jointName : fkArgs->joints)
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
	else
	{
		// Fall back to the last possible socket index on the parent
		short numSockets = 0;

		if (!parentModule.isNull())
		{
			MFnDependencyNode fnParent(parentModule);
			MPlug pNumSockets = fnParent.findPlug("numSockets", false);
			if (!pNumSockets.isNull())
			{
				numSockets = pNumSockets.asShort();
			}

			if (numSockets > 0)
			{
				socketIndex = static_cast<unsigned int>(numSockets - 1);
			}
			else
			{
				// TODO: we continue to hit this error when it's not defined in the json data.
				MGlobal::displayError(MString("Module did not define its numSockets: ") + MString(fnParent.name()));
				socketIndex = 0;
			}
		}
		else
		{
			MGlobal::displayError("Given parent module was null.");
			socketIndex = 0;
		}
	}

	// 3. Delegate to the flattened arguments overload
	MStatus executionStatus;
	// TODO: pass in shape type.
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
const MString FkChainNodeSetupCmd::aCommandString = "setupFkChainModule";

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
