#include "SingleJointFK.h"

#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnCompoundAttribute.h>
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

MTypeId SingleJointFKNode::id(0x0021A);

MObject SingleJointFKNode::inputRestMatrix;
MObject SingleJointFKNode::control;
MObject SingleJointFKNode::controlWorldMatrix;
MObject SingleJointFKNode::jointParentWorldMatrix;
MObject SingleJointFKNode::outputControlOPM;
MObject SingleJointFKNode::outputJointOPM;

const MString SingleJointFKNode::commandString = "singleJointFkModule";

SingleJointFKNode::SingleJointFKNode() {}
SingleJointFKNode::~SingleJointFKNode() {}

void* SingleJointFKNode::creator()
{
	return new SingleJointFKNode();
}

MStatus SingleJointFKNode::initialize()
{
	MStatus status;

	status = RigModuleNodeBase::initializeBaseAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MFnMatrixAttribute mAttr;
	MFnCompoundAttribute cAttr;

	inputRestMatrix = mAttr.create("inputRestMatrix", "irm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setStorable(true);
	mAttr.setKeyable(true);
	addAttribute(inputRestMatrix);

	controlWorldMatrix = mAttr.create("controlWorldMatrix", "cwm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setStorable(true);
	mAttr.setKeyable(true);

	jointParentWorldMatrix = mAttr.create("jointParentWorldMatrix", "jpwm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setStorable(true);
	mAttr.setKeyable(true);

	outputControlOPM = mAttr.create("outputControlOffsetParentMatrix", "copm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setWritable(false);
	mAttr.setStorable(false);

	outputJointOPM = mAttr.create("outputJointOffsetParentMatrix", "jopm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setWritable(false);
	mAttr.setStorable(false);

	control = cAttr.create("control", "ctl", &status);
	cAttr.addChild(controlWorldMatrix);
	cAttr.addChild(jointParentWorldMatrix);
	cAttr.addChild(outputControlOPM);
	cAttr.addChild(outputJointOPM);
	addAttribute(control);

	attributeAffects(parentWorldMatrix, outputControlOPM);
	attributeAffects(inputRestMatrix, outputControlOPM);

	attributeAffects(controlWorldMatrix, outputJointOPM);
	attributeAffects(jointParentWorldMatrix, outputJointOPM);

	attributeAffects(controlWorldMatrix, outputSocketMatrix);

	return MStatus::kSuccess;
}

MStatus SingleJointFKNode::evaluateModuleSolver(const MPlug& plug, MDataBlock& data)
{
	if (plug == outputControlOPM)
	{
		MMatrix inputRestLocal = data.inputValue(inputRestMatrix).asMatrix();
		MMatrix parentWorld = data.inputValue(parentWorldMatrix).asMatrix();

		MMatrix controlOPM = inputRestLocal * parentWorld;

		MDataHandle hControlOPM = data.outputValue(outputControlOPM);
		hControlOPM.setMMatrix(controlOPM);

		data.setClean(plug);
		return MStatus::kSuccess;
	}

	if (plug == outputJointOPM)
	{
		MMatrix controlWorld = data.inputValue(controlWorldMatrix).asMatrix();
		MMatrix jointParentWorld = data.inputValue(jointParentWorldMatrix).asMatrix();

		MMatrix jointOPM = controlWorld * jointParentWorld.inverse();

		MDataHandle hJointOPM = data.outputValue(outputJointOPM);
		hJointOPM.setMMatrix(jointOPM);

		data.setClean(plug);
		return MStatus::kSuccess;
	}

	if (plug == outputSocketMatrix)
	{
		MMatrix controlWorld = data.inputValue(controlWorldMatrix).asMatrix();

		MArrayDataHandle hSocketArray = data.outputArrayValue(outputSocketMatrix);
		MArrayDataBuilder builderSocket(&data, outputSocketMatrix, 1);
		MDataHandle hSocket0 = builderSocket.addElement(0);
		hSocket0.setMMatrix(controlWorld);
		hSocketArray.set(builderSocket);
		hSocketArray.setAllClean();

		data.setClean(plug);
		return MStatus::kSuccess;
	}

	return MStatus::kUnknownParameter;
}

// ------------------------------------------------------------------
// SingleJointFKCmd Implementation
// ------------------------------------------------------------------

const MString SingleJointFKCmd::commandString = "createSingleJointFkModule";

SingleJointFKCmd::SingleJointFKCmd() {}
SingleJointFKCmd::~SingleJointFKCmd() {}

void* SingleJointFKCmd::creator()
{
	return new SingleJointFKCmd();
}

MSyntax SingleJointFKCmd::newSyntax()
{
	MSyntax syntax;
	syntax.addFlag("-j", "-joint", MSyntax::kString);
	syntax.addFlag("-c", "-control", MSyntax::kString);
	syntax.addFlag("-n", "-name", MSyntax::kString);
	syntax.addFlag("-p", "-parentModule", MSyntax::kString);
	syntax.addFlag("-si", "-socketIndex", MSyntax::kUnsigned);
	syntax.addFlag("-rr", "-rigRoot", MSyntax::kString);
	return syntax;
}

MStatus SingleJointFKCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(newSyntax(), args, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MString jointName, controlName, moduleName = "fkModule", parentModuleName, rigRootName;
	unsigned int socketIndex = 0;

	if (argData.isFlagSet("-joint")) argData.getFlagArgument("-joint", 0, jointName);
	if (argData.isFlagSet("-control")) argData.getFlagArgument("-control", 0, controlName);
	if (argData.isFlagSet("-name")) argData.getFlagArgument("-name", 0, moduleName);
	if (argData.isFlagSet("-parentModule")) argData.getFlagArgument("-parentModule", 0, parentModuleName);
	if (argData.isFlagSet("-socketIndex")) argData.getFlagArgument("-socketIndex", 0, socketIndex);
	if (argData.isFlagSet("-rigRoot")) argData.getFlagArgument("-rigRoot", 0, rigRootName);

	// 1. Get Target Joint DagPath & Matrices
	MSelectionList sel;
	sel.add(jointName);
	MDagPath jointPath;
	sel.getDagPath(0, jointPath);

	MFnTransform fnJoint(jointPath);
	MMatrix jointWorldRest = jointPath.inclusiveMatrix();

	// 2. Determine Parent Driver Matrix & Calculate Rest Local Matrix
	MMatrix parentWorldRest = MMatrix::identity;

	if (parentModuleName.length() > 0)
	{
		MSelectionList pSel;
		pSel.add(parentModuleName);
		MObject parentModuleObj;
		pSel.getDependNode(0, parentModuleObj);
		MFnDependencyNode fnParentModule(parentModuleObj);

		MPlug socketPlug = fnParentModule.findPlug("outputSocketMatrix", false).elementByLogicalIndex(socketIndex);
		MObject matrixObj;
		socketPlug.getValue(matrixObj);

		// Extract matrix value using MFnMatrixData
		MFnMatrixData fnMatrix(matrixObj);
		parentWorldRest = fnMatrix.matrix();
	}

	// Calculate Local Rest Matrix: M_LocalRest = M_JointRest * M_ParentRest^-1
	MMatrix localRestMatrix = jointWorldRest * parentWorldRest.inverse();

	// 3. Create Custom Module Node
	MObject moduleObj = fDgModifier.createNode(SingleJointFKNode::id, &status);
	fDgModifier.renameNode(moduleObj, moduleName + "_node");
	fDgModifier.doIt();

	MFnDependencyNode fnModule(moduleObj);

	// Set Local Rest Matrix
	MPlug plugRest = fnModule.findPlug("inputRestMatrix", false);
	MFnMatrixData fnMatrix;
	MObject matrixObj = fnMatrix.create(localRestMatrix);
	plugRest.setValue(matrixObj);

	// 4. Create or Resolve Flat Control Transform
	MObject controlObj;
	if (controlName.length() > 0)
	{
		MSelectionList cSel;
		cSel.add(controlName);
		MDagPath ctrlPath;
		cSel.getDagPath(0, ctrlPath);
		controlObj = ctrlPath.node();
	}
	else
	{
		controlObj = fDagModifier.createNode("transform");
		fDagModifier.renameNode(controlObj, moduleName + "_CTL");
		fDagModifier.doIt();
	}

	MFnDependencyNode fnControl(controlObj);

	// 5. Connect Solvers and Wire Network
	// Control World -> Module
	fDgModifier.connect(fnControl.findPlug("worldMatrix", false).elementByLogicalIndex(0),
		fnModule.findPlug("control", false).child(0)); // controlWorldMatrix

	// Module Output Control OPM -> Control OPM
	fDgModifier.connect(fnModule.findPlug("control", false).child(2), // outputControlOffsetParentMatrix
		fnControl.findPlug("offsetParentMatrix", false));

	// Module Output Joint OPM -> Driven Joint OPM
	fDgModifier.connect(fnModule.findPlug("control", false).child(3), // outputJointOffsetParentMatrix
		fnJoint.findPlug("offsetParentMatrix", false));

	// Skeleton Parent World Matrix -> Module (if joint has DAG parent)
	if (jointPath.length() > 1)
	{
		MDagPath parentPath = jointPath;
		parentPath.pop();
		MFnDependencyNode fnJointParent(parentPath.node());
		fDgModifier.connect(fnJointParent.findPlug("worldMatrix", false).elementByLogicalIndex(0),
			fnModule.findPlug("control", false).child(1)); // jointParentWorldMatrix
	}

	// Connect Parent Module Socket -> Module Parent World Matrix
	if (parentModuleName.length() > 0)
	{
		MSelectionList pSel;
		pSel.add(parentModuleName);
		MObject parentModuleObj;
		pSel.getDependNode(0, parentModuleObj);
		MFnDependencyNode fnParentModule(parentModuleObj);

		fDgModifier.connect(fnParentModule.findPlug("outputSocketMatrix", false).elementByLogicalIndex(socketIndex),
			fnModule.findPlug("parentWorldMatrix", false));

		fDgModifier.connect(fnParentModule.findPlug("childModules", false).elementByLogicalIndex(0),
			fnModule.findPlug("parentModule", false));
	}

	// Connect Rig Root -> Module
	if (rigRootName.length() > 0)
	{
		MSelectionList rSel;
		rSel.add(rigRootName);
		MObject rigRootObj;
		rSel.getDependNode(0, rigRootObj);
		MFnDependencyNode fnRigRoot(rigRootObj);

		fDgModifier.connect(fnRigRoot.findPlug("rigModules", false),
			fnModule.findPlug("rigRoot", false));
	}

	// Zero local transform channels on driven joint
	plugRest = fnJoint.findPlug("translate", false);
	for (unsigned int i = 0; i < 3; ++i) plugRest.child(i).setDouble(0.0);
	plugRest = fnJoint.findPlug("rotate", false);
	for (unsigned int i = 0; i < 3; ++i) plugRest.child(i).setDouble(0.0);

	return redoIt();
}

MStatus SingleJointFKCmd::redoIt()
{
	MStatus status = fDagModifier.doIt();
	if (!status) return status;
	return fDgModifier.doIt();
}

MStatus SingleJointFKCmd::undoIt()
{
	MStatus status = fDgModifier.undoIt();
	if (!status) return status;
	return fDagModifier.undoIt();
}