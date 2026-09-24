#include "LayoutModule.h"
#include "RigControlNode.h"
#include "Side.h"

#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>
#include <maya/MSyntax.h>
#include <maya/MGlobal.h>
#include <maya/MStringArray.h>
#include <maya/MArgDatabase.h>


// ------------------------------------------------------------------
// FkChainNode Implementation
// ------------------------------------------------------------------

MTypeId LayoutModuleNode::id(0x0021C);

MObject LayoutModuleNode::controlMatrix;
MObject LayoutModuleNode::outputControlOPM;

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

	MFnMatrixAttribute mAttr;
	MFnMessageAttribute msgAttr;
	MFnEnumAttribute eAttr;

	// Network Message Plugs
	rigRoot = msgAttr.create("rigRoot", "rr", &status);
	addAttribute(rigRoot);

	childModules = msgAttr.create("childModules", "cmods", &status);
	msgAttr.setArray(true);
	addAttribute(childModules);

	aRigControls = msgAttr.create("rigControls", "ctrls", &status);
	addAttribute(aRigControls);

	// Output Socket Matrix Array 
	outputSocketMatrix = mAttr.create("outputSocketMatrix", "soc", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setUsesArrayDataBuilder(true);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	mAttr.setWorldSpace(true);
	addAttribute(outputSocketMatrix);

	// Side
	aSide = eAttr.create("side", "sd", 0);
	eAttr.addField("Center", 0);
	eAttr.addField("Left", 1);
	eAttr.addField("Right", 2);
	eAttr.setKeyable(false);
	eAttr.setStorable(true);
	addAttribute(aSide);

	// 1. INPUT ARRAYS
	controlMatrix = mAttr.create("controlMatrix", "cm", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setStorable(true);
	mAttr.setKeyable(true);
	addAttribute(controlMatrix);

	// 2. AFFECTS RELATIONSHIPS
	attributeAffects(controlMatrix, outputSocketMatrix);

	return MStatus::kSuccess;
}

MStatus LayoutModuleNode::evaluateModuleSolver(const MPlug& plug, MDataBlock& data)
{
	unsigned int index = plug.isElement() ? plug.logicalIndex() : 0;

	// -------------------------------------------------------------------
	// 1. Output Socket Matrix Array
	// -------------------------------------------------------------------
	// This is the only output we have to set, as the module has no parents or joints.
	// All the module does is create an array of output sockets.
	if (plug == outputSocketMatrix || plug.array() == outputSocketMatrix)
	{
		MArrayDataHandle hControlArray = data.inputArrayValue(controlMatrix);
		MArrayDataHandle hOutSocketArray = data.outputArrayValue(outputSocketMatrix);
		MMatrix mSocketWorld = MMatrix::identity;

		unsigned int elementCount = hControlArray.elementCount();

		// Evaluate sequentially down the chain
		for (unsigned int i = 0; i < elementCount; ++i)
		{
			hControlArray.jumpToElement(i);

			// Jump directly to the output element slot without rebuild
			if (hOutSocketArray.jumpToElement(hControlArray.elementIndex()) == MStatus::kSuccess)
			{
				MMatrix mControlLocal = hControlArray.inputValue().asMatrix();

				// Accumulate step matrix downstream
				mSocketWorld = mControlLocal * mSocketWorld;

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

MObject LayoutModuleNode::createModule(MObject& oRigRoot, MDGModifier& dgMod, MDagModifier& dagMod, const unsigned int numControls)
{
	// First create the module node
	MObject oLayoutModule = dgMod.createNode(LayoutModuleNode::commandString);
	dgMod.renameNode(oLayoutModule, "layoutModule"); // TODO: namespaces
	const char* sideSuffix = getSideSuffix(Side::Center);

	MObject oPreviousControlNode = MObject::kNullObj;

	for (unsigned int i = 0; i < numControls; ++i)
	{
		MObject oControl = RigControlNode::createRigControl(oLayoutModule, dagMod, MString("layout_") + i);
		MFnDependencyNode fnControl(oControl);
		MPlug pShapeType(oControl, RigControlNode::aShapeType);
		dgMod.newPlugValueShort(pShapeType, ShapeType::Box);

		// Set decreasing radius
		MPlug pWidth(oControl, RigControlNode::aWidth);
		dgMod.newPlugValueDouble(pWidth, 1.0 - (i * 0.1));
		MPlug pHeight(oControl, RigControlNode::aHeight);
		dgMod.newPlugValueDouble(pHeight, 0.0);

		// Set normal up
		//MPlug pNormal(oControl, RigControlNode::aNormalVector);
		//MPlug pNormalX = pNormal.child(0);
		//MPlug pNormalY = pNormal.child(1);
		//MPlug pNormalZ = pNormal.child(2);
		//dgMod.newPlugValueDouble(pNormalX, 0.0);
		//dgMod.newPlugValueDouble(pNormalY, 1.0);
		//dgMod.newPlugValueDouble(pNormalZ, 0.0);

		if (oPreviousControlNode != MObject::kNullObj)
		{
			dagMod.reparentNode(oControl, oPreviousControlNode);
		}
		dagMod.doIt();
		oPreviousControlNode = oControl;
	}

	RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dgMod, oLayoutModule);

	return oLayoutModule;
}

// ------------------------------------------------------------------
// LayoutModule Commands
// ------------------------------------------------------------------

// LayoutModule Setup command ----------------------------------------
const MString LayoutModuleSetupCmd::commandString = "setupLayoutModule";
const char* LayoutModuleSetupCmd::kRigRootLong = "-rigRoot";
const char* LayoutModuleSetupCmd::kRigRootShort = "-rr";

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
	status = syntax.addFlag(kRigRootShort, kRigRootLong, MSyntax::kString);

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