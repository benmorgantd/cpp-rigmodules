#include "LayoutModule.h"
#include "RigControlNode.h"
#include "Side.h"

#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnNumericData.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>
#include <maya/MSyntax.h>
#include <maya/MGlobal.h>
#include <maya/MStringArray.h>
#include <maya/MArgDatabase.h>
#include <maya/MFnMatrixData.h>
#include <maya/MArrayDataBuilder.h>


// ------------------------------------------------------------------
// LayoutModuleNode Implementation
// ------------------------------------------------------------------

MTypeId LayoutModuleNode::id(0x0021C);

MObject LayoutModuleNode::controlMatrix;
MObject LayoutModuleNode::outputSocketMatrix;
MObject LayoutModuleNode::aRigControls;
MObject LayoutModuleNode::aSide;
MObject LayoutModuleNode::childModules;
MObject LayoutModuleNode::rigRoot;
MObject LayoutModuleNode::aNumSockets;

LayoutModuleNode::LayoutModuleNode() {}
LayoutModuleNode::~LayoutModuleNode() {}

const MString LayoutModuleNode::commandString = "layoutModule";

void* LayoutModuleNode::creator()
{
	return new LayoutModuleNode();
}

MStatus LayoutModuleNode::initialize()
{
	MStatus status;

	// TODO: this module does not really need all of these attributes. 
	MFnMatrixAttribute mAttr;
	MFnMessageAttribute msgAttr;
	MFnEnumAttribute eAttr;
	MFnNumericAttribute nAttr;

	// 1. INPUT ARRAYS
	controlMatrix = mAttr.create("controlMatrix", "cm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setStorable(true);
	mAttr.setKeyable(true);
	addAttribute(controlMatrix);

	rigRoot = msgAttr.create("rigRoot", "rr", &status);
	addAttribute(rigRoot);

	childModules = msgAttr.create("childModules", "cmods", &status);
	msgAttr.setArray(true);
	addAttribute(childModules);

	aRigControls = msgAttr.create("rigControls", "ctrls", &status);
	addAttribute(aRigControls);

	aSide = eAttr.create("side", "sd", 0);
	eAttr.addField("Center", 0);
	eAttr.addField("Left", 1);
	eAttr.addField("Right", 2);
	eAttr.setKeyable(false);
	eAttr.setStorable(true);
	addAttribute(aSide);

	outputSocketMatrix = mAttr.create("outputSocketMatrix", "soc", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setUsesArrayDataBuilder(true);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	mAttr.setWorldSpace(true);
	addAttribute(outputSocketMatrix);

	// numSockets attr
	aNumSockets = nAttr.create("numSockets", "ns", MFnNumericData::kShort, 0, &status);
	nAttr.setStorable(true);
	addAttribute(aNumSockets);

	// 3. AFFECTS RELATIONSHIPS
	attributeAffects(controlMatrix, outputSocketMatrix);

	return MStatus::kSuccess;
}
// In the case of this node, there is only one output to compute, the outputSocketMatrix.
// TODO: the solver for this module is not evaluating correctly
MStatus LayoutModuleNode::compute(const MPlug& plug, MDataBlock& data)
{
	// TODO: child modules should attach to the last index of the layout module. Make "-1" the default.
	// TODO: there are world-space calculation issues here.
	if (plug == outputSocketMatrix || plug.array() == outputSocketMatrix)
	{
		// TODO: we are not getting into this if statement
		MArrayDataHandle hControlArray = data.inputArrayValue(controlMatrix);
		MArrayDataHandle hOutSocketArray = data.outputArrayValue(outputSocketMatrix);

		// Start with identity (or parent world matrix if layout has an offset)
		MMatrix mCurrentWorld = MMatrix::identity;
		unsigned int elementCount = hControlArray.elementCount();

		// Sequential downstream accumulation loop
		for (unsigned int i = 0; i < elementCount; ++i)
		{
			hControlArray.jumpToElement(i);
			MMatrix mControlLocal = hControlArray.inputValue().asMatrix();
			// Maya Row-Major Order: Local Step * Current Accumulated World Matrix
			MMatrix mSocketWorld = mControlLocal * mCurrentWorld;

			// Jump directly to the output element slot without rebuilding array handles
			if (hOutSocketArray.jumpToElement(hControlArray.elementIndex()) == MStatus::kSuccess)
			{
				// Write directly to the existing output datablock handle
				hOutSocketArray.outputValue().setMMatrix(mSocketWorld);
			}
			else
			{
				// We need to use MArrayDataBuilder.
				MArrayDataBuilder builder = hOutSocketArray.builder();
				MDataHandle hNewElem = builder.addElement(hControlArray.elementIndex());
				hNewElem.setMMatrix(mSocketWorld);
				hOutSocketArray.set(builder);
			}
			mCurrentWorld = mSocketWorld;
		}

		// Clean the entire output array block at once
		hOutSocketArray.setAllClean();
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	return MStatus::kUnknownParameter;
}

MObject LayoutModuleNode::createModule(MObject& oRigRoot, MDGModifier& dgMod, MDagModifier& dagMod, const unsigned short numControls)
{
	MGlobal::displayInfo("Creating Layout module.");
	// First create the module node
	MObject oLayoutModule = dgMod.createNode(LayoutModuleNode::commandString);
	dgMod.renameNode(oLayoutModule, "layoutModule"); // TODO: namespaces
	const char* sideSuffix = getSideSuffix(Side::Center);

	MObject oPreviousControlNode = MObject::kNullObj;

	// For this module, set the numSockets value to the number of joints we have.
	MFnDependencyNode fnLayoutModule(oLayoutModule);
	MPlug pNumSockets = fnLayoutModule.findPlug("numSockets", false);
	if (!pNumSockets.isNull())
	{
		dgMod.newPlugValueShort(pNumSockets, numControls);
	}

	for (unsigned int i = 0; i < numControls; ++i)
	{
		MObject oControl = RigControlNode::createRigControl(oLayoutModule, dagMod, MString("layout_") + i);
		MFnDependencyNode fnControl(oControl);
		MPlug pShapeType(oControl, RigControlNode::aShapeType);
		dgMod.newPlugValueShort(pShapeType, ShapeType::Square);

		// Set decreasing radius using shape transform matrix
		MPlug pShapeTransform(oControl, RigControlNode::aShapeTransform);
		MTransformationMatrix mShapeTransform;

		double scaleValue = 1.0 - (0.1 * i);
		const double matrixScale[3] = { scaleValue, scaleValue, scaleValue };
		mShapeTransform.setScale(matrixScale, MSpace::kObject);

		// While we have the shape transform, also set the normal and up vectors. 
		// This is a 90 degree rotation about the Z axis.
		mShapeTransform.setRotationQuaternion(0.0, 0.0, 0.7011, 0.7011);

		MFnMatrixData fnShapeTransform;
		MObject oShapeTransform = fnShapeTransform.create(mShapeTransform.asMatrix());
		dgMod.newPlugValue(pShapeTransform, oShapeTransform); 

		// Wire the control's matrix to the module's control matrix input
		MPlug pControlOutputMatrix(oControl, RigControlNode::matrix);
		MPlug pModuleInputMatrix(oLayoutModule, LayoutModuleNode::controlMatrix);
		dgMod.connect(pControlOutputMatrix, pModuleInputMatrix.elementByLogicalIndex(i));

		if (oPreviousControlNode != MObject::kNullObj)
		{
			dagMod.reparentNode(oControl, oPreviousControlNode);
		}
		dagMod.doIt();
		oPreviousControlNode = oControl;
	}

	RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dgMod, oLayoutModule);

	// Always keep dgMod and dagMod up to date after creating a module
	//dgMod.doIt();
	//dagMod.doIt();

	return oLayoutModule;
}

// ------------------------------------------------------------------
// LayoutModule Commands
// ------------------------------------------------------------------

// LayoutModule Setup command ----------------------------------------
const MString LayoutModuleSetupCmd::commandString = "setupLayoutModule";

LayoutModuleSetupCmd::LayoutModuleSetupCmd() {}
LayoutModuleSetupCmd::~LayoutModuleSetupCmd() {}


void* LayoutModuleSetupCmd::creator()
{
	return new LayoutModuleSetupCmd();
}

//TODO: attribute naming conventions for matrices, function sets, etc.
// Creates the syntax for the command
MSyntax LayoutModuleSetupCmd::newSyntax()
{
	MStatus status;
	MSyntax syntax;
	status = syntax.addArg(MSyntax::kString);

	if (!status)
	{
		MGlobal::displayError("Failed to create base module syntax for command.");
	}

	return syntax;
}

// Runs the command logic
MStatus LayoutModuleSetupCmd::doIt(const MArgList& args)
{
	// TODO: we want to separate the arg gathering part of doIt() from the action part of it
	// This will allow us to call the "setup" command for a module from cpp without passing in command syntax.
	// That will allow for more top-down, all in cpp command actions! :D
	// Arg Gathering --------------------------------------------
	MStatus status;
	MArgDatabase argData(newSyntax(), args, &status);

	MDGModifier dgMod;
	MDagModifier dagMod;

	MObject oRigRoot = RigModuleCommandHelpers::getRigRootFromArgs(argData);

	if (oRigRoot.isNull())
	{
		MGlobal::displayError("Failed to find rig root.");
		return MStatus::kFailure;
	}

	MObject oModule = LayoutModuleNode::createModule(oRigRoot, dgMod, dagMod, 3);

	status = dgMod.doIt();
	status = dagMod.doIt();

	// Set command return value (we may need to define MResultType)
	MStringArray result;
	MFnDependencyNode fnModule(oModule);
	result.append(fnModule.name());
	setResult(result);

	return status;
}