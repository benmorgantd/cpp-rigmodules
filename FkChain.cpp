#include "FkChain.h"
#include "RigControlNode.h"
#include "RigJsonStructs.h"
#include "Utilities.h"
#include "RigModule.h"

#include "vector"
#include "utility"

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

	attributeAffects(aControlMatrix, aOutputJointOPM);
	attributeAffects(aInputRestMatrix, aOutputJointOPM);

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
		// By default, our control's offset parent matrix is just its baked starting offset.
		// TODO: once we do parenting this will have to be relative to the control's arbitrary parent.
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

		MMatrix jointOPM = controlLocal * jointOffset;

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

MObject FkChainNode::createModule(
	const MString& moduleName,
	const MDagPathArray& jointDags,
	MObject oRigRoot,
	MDGModifier& dgMod,
	MDagModifier& dagMod,
	MStatus* status)
{
	// Instantiate module node in DG
	MObject oModule = dgMod.createNode("fkChainModule");
	dgMod.renameNode(oModule, moduleName);
	dgMod.doIt();

	MFnDependencyNode moduleFn(oModule);
	MPlug pInputRestMatrix = moduleFn.findPlug("inputRestMatrix", false);
	MPlug pControlMatrix = moduleFn.findPlug("controlMatrix", false);
	MPlug pOutputControlOPM = moduleFn.findPlug("outputControlOPM", false);
	MPlug pOutputJointOPM = moduleFn.findPlug("outputJointOPM", false);

	MObject oPreviousControl;
	unsigned int numJoints = jointDags.length();

	std::vector<std::pair<MObject, MString>> parentSocketPairs;

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
			parentSocketPairs.push_back({ oControlTransform, "start" });
		}
		else if (i == numJoints - 1)
		{
			parentSocketPairs.push_back({ oControlTransform, "end" });
		}
		parentSocketPairs.push_back({ oControlTransform, MString("index_") + std::to_string(i).c_str() });

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
		// TODO: now that we are changing the parents of controls we might not be able to share rest matrices between joints and controls.
		DagUtils::setMatrixPlug(pInputRestMatrixIndex, mJointRest);

		MFnDagNode jointFnDag(jointDag);
		MPlug pJointOpm = jointFnDag.findPlug("offsetParentMatrix", false);
		dgMod.connect(pOutputJointOPM.elementByLogicalIndex(i), pJointOpm);

		dgMod.doIt();

		// Update persistent path for next iteration
		oPreviousControl = oControlTransform;
	}

	RigModuleNodeBase::connectControlParentSockets(dgMod, oModule, parentSocketPairs);
	dgMod.doIt();

	if (!oRigRoot.isNull())
	{
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dgMod, oModule, aRigRoot);
		dgMod.doIt();
	}

	if (status) *status = MS::kSuccess;
	return oModule;
}

MObject FkChainNode::createModule(
	const RigModuleData& moduleData,
	MObject rigRoot,
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

	// 3. Delegate to the flattened arguments overload
	MStatus executionStatus;
	// TODO: pass in shape type.
	MObject oModule = FkChainNode::createModule(
		moduleData.name.c_str(),
		jointDags,
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


// Only the start of the fkchain can be parented to another socket
MStringArray FkChainNode::getChildPlugNames(MObject& oModule)
{
	MStringArray childPlugNames;
	childPlugNames.append("start");

	for (unsigned int i = 0; i < childPlugNames.length(); ++i)
	{
		MGlobal::displayInfo("FkChain Child plug names: " + childPlugNames[i]);
	}

	return childPlugNames;
}

// All controls in the fkchain can be parent sockets for other modules.
MStringArray FkChainNode::getParentSocketNames(MObject& oModule)
{
	MStringArray parentSocketNames;
	parentSocketNames.append("start");
	parentSocketNames.append("end");

	if (oModule.isNull())
	{
		MGlobal::displayError("Could not extract child plug name info from null object.");
		return MStringArray();
	}

	MFnDependencyNode fnModule(oModule);
	MPlug pRigControls(oModule, aRigControls);
	unsigned int numControls = pRigControls.numConnectedElements();

	for (unsigned int i = 0; i < numControls; ++i)
	{
		parentSocketNames.append(MString() + i);  // the constructor does not support MString(i)
	}

	for (unsigned int i = 0; i < parentSocketNames.length(); ++i)
	{
		MGlobal::displayInfo("Fk Parent Sockets: " + parentSocketNames[i]);
	}

	return parentSocketNames;
}