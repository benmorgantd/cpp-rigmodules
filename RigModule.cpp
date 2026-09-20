#include "RigModule.h"
#include "RigControlNode.h"
#include "Side.h"

#include <maya/MFnTypedAttribute.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnData.h>
#include <maya/MDataBlock.h>
#include <maya/MMatrix.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MArrayDataBuilder.h>
#include <maya/MGlobal.h>
#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MObjectArray.h>
#include <maya/MSyntax.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>
#include <maya/MFnMatrixData.h>
#include <maya/MFnDependencyNode.h>

// Static attribute definitions
MObject RigModuleNodeBase::moduleData;
MObject RigModuleNodeBase::rigRoot;
MObject RigModuleNodeBase::parentModule;
MObject RigModuleNodeBase::childModules;
MObject RigModuleNodeBase::parentWorldMatrix;
MObject RigModuleNodeBase::parentModuleOffset;
MObject RigModuleNodeBase::outputSocketMatrix;
MObject RigModuleNodeBase::aRigControls;
MObject RigModuleNodeBase::aSide;

RigModuleNodeBase::RigModuleNodeBase() {}
RigModuleNodeBase::~RigModuleNodeBase() {}

MStatus RigModuleNodeBase::compute(const MPlug& plug, MDataBlock& data)
{
	return evaluateModuleSolver(plug, data);
}

MStatus RigModuleNodeBase::initializeBaseAttributes()
{
	MFnTypedAttribute tAttr;
	MFnMatrixAttribute mAttr;
	MFnMessageAttribute msgAttr;
	MFnEnumAttribute eAttr;
	MStatus status;

	// 1. Metadata Attributes
	moduleData = tAttr.create("moduleData", "md", MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(moduleData);

	// 2. Network Message Plugs
	rigRoot = msgAttr.create("rigRoot", "rr", &status);
	addAttribute(rigRoot);

	parentModule = msgAttr.create("parentModule", "pm", &status);
	addAttribute(parentModule);

	childModules = msgAttr.create("childModules", "cmods", &status);
	msgAttr.setArray(true);
	addAttribute(childModules);

	aRigControls = msgAttr.create("rigControls", "ctrls", &status);
	addAttribute(aRigControls);

	// 3. Base Driving Input Matrix (Scalar)
	parentWorldMatrix = mAttr.create("parentWorldMatrix", "pwm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setStorable(true);
	mAttr.setKeyable(true);
	mAttr.setWorldSpace(true);
	addAttribute(parentWorldMatrix);

	parentModuleOffset = mAttr.create("parentModuleOffset", "pmo", MFnMatrixAttribute::kDouble, &status);
	mAttr.setStorable(true);
	mAttr.setKeyable(true);
	mAttr.setWorldSpace(true);
	mAttr.setHidden(true);
	addAttribute(parentModuleOffset);

	// 4. Output Socket Matrix Array (The ONLY matrix array attribute across the framework)
	outputSocketMatrix = mAttr.create("outputSocketMatrix", "soc", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setUsesArrayDataBuilder(true);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	mAttr.setWorldSpace(true);
	addAttribute(outputSocketMatrix);

	// Side
	// TODO: "callback" of sorts when aSide gets changed to also change the color of the controls.
	aSide = eAttr.create("side", "sd", 0);
	eAttr.addField("Center", 0);
	eAttr.addField("Left", 1);
	eAttr.addField("Right", 2);
	eAttr.setKeyable(false);
	eAttr.setStorable(true);
	addAttribute(aSide);

	return MStatus::kSuccess;
}

// Shared functions --------------------------------------------

MMatrix RigModuleNodeBase::getInputMatrix(MDataBlock& data, const MObject& attr, unsigned int idx)
{
	MArrayDataHandle hArray = data.inputArrayValue(attr);
	if (hArray.jumpToElement(idx) == MS::kSuccess)
	{
		// Do NOT re-aquire hArray here because we've just moved it to the desired plug.
		return hArray.inputValue().asMatrix();
	}
	else
	{
		MGlobal::displayError("error in getting array element");
	}
	return MMatrix::identity;
}

MMatrix RigModuleNodeBase::getInputMatrix(MDataBlock& data, const MObject& attr)
{
	MDataHandle hData = data.inputValue(attr);
	return hData.asMatrix();
}

// TODO: this could return MStatus not void
void RigModuleNodeBase::setOutputMatrix(MDataBlock& data, const MObject& attr, unsigned int idx, const MMatrix& mat)
{
	MArrayDataHandle hArray = data.outputArrayValue(attr);
	if (hArray.jumpToElement(idx) == MS::kSuccess)
	{
		hArray.outputValue().setMMatrix(mat);
	}
	else
	{
		MArrayDataBuilder builder(&data, attr, 1);
		MDataHandle hElement = builder.addElement(idx);
		hElement.setMMatrix(mat);
		hArray.set(builder);
	}
}

// Networking functionality

// Return the MObjects for the child nodes of this module
MObjectArray RigModuleNodeBase::getChildModules(MObject& moduleNode)
{
	// TODO: we should add protection here (check status) because this could fail if we passed in the wrong MObject
	MPlug pChildModules(moduleNode, RigModuleNodeBase::childModules);
	MObjectArray childModuleNodes;

	MPlugArray pConnectedChildren;
	pChildModules.connectedTo(pConnectedChildren, false, true);

	for (unsigned int i = 0; i < pConnectedChildren.length(); ++i)
	{
		// TODO: we could add protections here
		childModuleNodes.append(pConnectedChildren[i].node());
	}
	return childModuleNodes;
}

MObject RigModuleNodeBase::getParentModule(MObject& moduleNode)
{
	MObject parentModule;
	MPlug pParentModule(moduleNode, RigModuleNodeBase::parentModule);
	MPlugArray pConnectedParents;
	pParentModule.connectedTo(pConnectedParents, true, false);  // Get inputs. There can only be one

	if (pConnectedParents.length() > 0)
	{
		parentModule = pConnectedParents[0].node();
	}

	return parentModule;
}

// Creates a rig control and wires it to the module
MObject RigModuleNodeBase::createRigControl(MObject& moduleNode, MDagModifier& dagMod, const MString& jointName)
{
	// Get module side
	MPlug pSide(moduleNode, RigModuleNodeBase::aSide);
	unsigned int sideInt = pSide.asInt();
	Side sideEnum = getSideFromInt(sideInt);
	const char* sideSuffix = getSideSuffix(sideEnum);
	MColor sideColor = getColorFromSide(sideEnum);

	// Create and name the control transform
	MObject controlTransform = dagMod.createNode("rigControlNode");


	MString ctrlName = jointName;
	ctrlName.substitute("_jnt", "");  // TODO: global var for jnt suffix
	ctrlName += "_ctrl";  // TODO: global naming method, global var for ctrl suffix
	ctrlName += sideSuffix;
	dagMod.renameNode(controlTransform, ctrlName);

	// Connect the control to the module node
	MPlug pRigModule(controlTransform, RigControlNode::aRigModule);
	MPlug pRigControls(moduleNode, RigModuleNodeBase::aRigControls);
	if (!pRigModule.isNull() && !pRigControls.isNull())
	{
		// Connect the attributes
		dagMod.connect(pRigControls, pRigModule);
	}
	else
	{
		MGlobal::displayError("Unable to connect controls to module because plugs were null.");
	}
	
	// TODO: set rig control side color based on module side attr.
	MPlug controlColor(controlTransform, RigControlNode::aWireColor);
	dagMod.newPlugValueFloat(controlColor.child(0), sideColor.r);
	dagMod.newPlugValueFloat(controlColor.child(1), sideColor.g);
	dagMod.newPlugValueFloat(controlColor.child(2), sideColor.b);

	return controlTransform;
}

// RigModuleCommandHelpers ------------------------------------------------
const char* RigModuleCommandHelpers::kNameFlagShort = "-n";
const char* RigModuleCommandHelpers::kNameFlagLong = "-name";
const char* RigModuleCommandHelpers::kJointsFlagShort = "-j";
const char* RigModuleCommandHelpers::kJointsFlagLong = "-joints";
const char* RigModuleCommandHelpers::kParentModuleShort = "-pm";
const char* RigModuleCommandHelpers::kParentModuleLong = "-parentModule";
const char* RigModuleCommandHelpers::kParentSocketIndexShort = "-psi";
const char* RigModuleCommandHelpers::kParentSocketIndexLong = "-parentSocketIndex";
const char* RigModuleCommandHelpers::kRigRootLong = "-rigRoot";
const char* RigModuleCommandHelpers::kRigRootShort = "-rr";


MString RigModuleCommandHelpers::getModuleNameFromArgs(const MArgDatabase& argData)
{
	// Gather the passed in module name
	MString moduleName = "fkChainModule_01";
	if (argData.isFlagSet(kNameFlagShort))
	{
		argData.getFlagArgument(kNameFlagShort, 0, moduleName);
	}

	return moduleName;
}

MDagPathArray RigModuleCommandHelpers::getJointsFromArgs(const MArgDatabase& argData, MStatus& status)
{
	MDagPathArray jointDags;
	MObjectArray jointObjs;
	unsigned int numJoints = 0;

	if (argData.isFlagSet(kJointsFlagShort))
	{
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
					MString jointName = flagArgs.asString(j, &status);
					MDagPath jointDag;
					MSelectionList jointSel;
					jointSel.add(jointName);
					status = jointSel.getDagPath(0, jointDag);

					if (status == MStatus::kSuccess && !jointDag.node().isNull())
					{
						jointDags.append(jointDag);
					}
					else
					{
						MGlobal::displayError("Failure getting joint depend node: " + jointName);
						status = MStatus::kFailure;
					}
					
				}
			}
		}
	}

	if (jointDags.length() == 0 || numJoints == 0)
	{
		MGlobal::displayError("At least one joint must be given with the -j flag.");
		return MDagPathArray();
	}
	return jointDags;
}

MStatus RigModuleCommandHelpers::connectModuleToRigRoot(const MArgDatabase& argData, MDGModifier& dgMod, const MObject& moduleNode)
{
	MStatus status;
	MString rigRootName;
	MObject rigRoot;

	if (argData.isFlagSet(kRigRootShort))
	{
		argData.getFlagArgument(kRigRootShort, 0, rigRootName);

		MSelectionList rigRootSelList;
		status = rigRootSelList.add(rigRootName);
		rigRootSelList.getDependNode(0, rigRoot);

		if (!status || rigRoot.isNull())
		{
			MGlobal::displayError("Rig Root name " + rigRootName + " does not exist.");
			return MStatus::kFailure;
		}
	}

	MFnDependencyNode rigRootFn(rigRoot);
	MPlug pRigModules = rigRootFn.findPlug("rm", false);  // TODO: also make this a reference

	if (pRigModules.isNull())
	{
		MGlobal::displayError("Failed to find rig modules plug on given rig root object.");
		return MStatus::kFailure;
	}

	MFnDependencyNode moduleFn(moduleNode);
	MPlug pRigRoot = moduleFn.findPlug("rigRoot", false);

	if (pRigRoot.isNull())
	{
		MGlobal::displayError("Failed to find rigRoot plug on the module object.");
		return MStatus::kFailure;
	}

	status = dgMod.connect(pRigModules, pRigRoot);
	return status;
}

MSyntax RigModuleCommandHelpers::createBaseModuleSyntax(MStatus& status)
{
	MSyntax syntax;
	status = syntax.addFlag(kNameFlagShort, kNameFlagLong, MSyntax::kString);
	status = syntax.addFlag(kJointsFlagShort, kJointsFlagLong, MSyntax::kString);
	status = syntax.makeFlagMultiUse(kJointsFlagShort);
	status = syntax.addFlag(kParentModuleShort, kParentModuleLong, MSyntax::kString);
	status = syntax.addFlag(kParentSocketIndexShort, kParentSocketIndexLong, MSyntax::kLong);
	status = syntax.addFlag(kRigRootShort, kRigRootLong, MSyntax::kString);
	return syntax;
}

unsigned int RigModuleCommandHelpers::getParentModuleSocketIndex(const MArgDatabase& argData)
{
	unsigned int parentModuleSocketIndex = 0;
	if (argData.isFlagSet(kParentSocketIndexShort))
	{
		argData.getFlagArgument(kParentSocketIndexShort, 0, parentModuleSocketIndex);
	}
	return parentModuleSocketIndex;
}

MObject RigModuleCommandHelpers::getParentModule(const MArgDatabase& argData)
{
	MString parentModuleName;
	MObject oParentModule;
	MStatus status;

	if (argData.isFlagSet(kParentModuleShort))
	{
		argData.getFlagArgument(kParentModuleShort, 0, parentModuleName);

		MSelectionList modSelList;
		status = modSelList.add(parentModuleName);

		if (status != MStatus::kSuccess)
		{
			MGlobal::displayError("Module name " + parentModuleName + " does not exist.");
			return MObject::kNullObj;
		}
	}

	return oParentModule;
}
MStatus RigModuleCommandHelpers::connectToParentModule(const MObject& oParentModule, const MObject oModule, unsigned int parentModuleSocketIndex, MDGModifier& dgMod, MMatrix mFirstControlWorld)
{
	if (oParentModule.isNull())
	{
		MGlobal::displayError("Given parent module object is null.");
		return MStatus::kFailure;
	}

	// Wire the parent module to the new module
	MFnDependencyNode fnParentModule(oParentModule);
	MPlug childModulesPlug = fnParentModule.findPlug("childModules", false);
	unsigned int numElements = childModulesPlug.numElements();

	MFnDependencyNode fnModule(oModule);

	MPlug parentModulePlug = fnModule.findPlug("parentModule", false);
	dgMod.connect(childModulesPlug.elementByLogicalIndex(numElements), parentModulePlug);

	// Wire the parent's socket matrix index to the child
	MPlug parentSocketMatrix = fnParentModule.findPlug("outputSocketMatrix", false).elementByLogicalIndex(parentModuleSocketIndex);
	MPlug childParentMatrix = fnModule.findPlug("parentWorldMatrix", false);
	dgMod.connect(parentSocketMatrix, childParentMatrix);

	// Compute offset --------------------------
	MObject parentSocketObj;
	parentSocketMatrix.getValue(parentSocketObj);
	MFnMatrixData parentSocketData(parentSocketObj);
	MMatrix mParentSocketWorld = parentSocketData.matrix();
	MMatrix mParentModuleOffset = mFirstControlWorld * mParentSocketWorld.inverse();

	// Set the parentModuleOffset plug value
	MPlug parentModuleOffset = fnModule.findPlug("parentModuleOffset", false);
	MFnMatrixData parentModuleOffsetData;
	MObject parentModuleOffsetObj = parentModuleOffsetData.create(mParentModuleOffset);
	parentModuleOffset.setValue(parentModuleOffsetObj);

	return MStatus::kSuccess;
}
