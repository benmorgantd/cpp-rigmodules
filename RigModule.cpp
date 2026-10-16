#include "RigModule.h"
#include "RigJsonStructs.h"
#include "FkChain.h"
#include "AimModule.h"
#include "LayoutModule.h"

#include "unordered_map"

#include <maya/MDataBlock.h>
#include <maya/MMatrix.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MArrayDataBuilder.h>
#include <maya/MGlobal.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>
#include <maya/MFnMatrixData.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MStringArray.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnCompoundAttribute.h>
#include <maya/MObjectHandle.h>

// NOTE that RigModuleNodeBase can NOT define any static node attributes!

RigModuleNodeBase::RigModuleNodeBase() {}
RigModuleNodeBase::~RigModuleNodeBase() {}

MStatus RigModuleNodeBase::compute(const MPlug& plug, MDataBlock& data) 
{ 
	return MStatus::kSuccess; 
}

MStatus RigModuleNodeBase::initializeSharedAttributes(
	MObject& aModuleData, 
	MObject& aRigRoot, 
	MObject& aRigControls, 
	MObject& aOutputSocketMatrix, 
	MObject& aSide)
{
	MStatus status;

	// ---- Initialize Shared Attributes ----
	MFnTypedAttribute tAttr;
	MFnMatrixAttribute mAttr;
	MFnMessageAttribute msgAttr;
	MFnEnumAttribute eAttr;
	MFnNumericAttribute nAttr;
	MFnCompoundAttribute cAttr;

	// 1. Metadata Attributes
	aModuleData = tAttr.create("moduleData", "md", MFnData::kString, MObject::kNullObj, &status);
	tAttr.setStorable(true);
	addAttribute(aModuleData);

	// 2. Network Message Plugs
	aRigRoot = msgAttr.create("rigRoot", "rr", &status);
	addAttribute(aRigRoot);

	aRigControls = msgAttr.create("rigControls", "ctrls", &status);
	addAttribute(aRigControls);

	// 3. Output Socket Matrix Array
	aOutputSocketMatrix = mAttr.create("outputSocketMatrix", "soc", MFnMatrixAttribute::kDouble, &status);
	mAttr.setArray(true);
	mAttr.setUsesArrayDataBuilder(true);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	mAttr.setWorldSpace(true);
	addAttribute(aOutputSocketMatrix);

	// Side
	aSide = eAttr.create("side", "sd", 0);
	eAttr.addField("Center", 0);
	eAttr.addField("Left", 1);
	eAttr.addField("Right", 2);
	eAttr.setKeyable(false);
	eAttr.setStorable(true);
	addAttribute(aSide);

	if (status != MStatus::kSuccess)
	{
		MGlobal::displayError("Failure creating base attributes for module: " + status.errorString());
		return status;
	}

	return status;

	// ---------------------------------------------
}

// Matrix Calculation Helpers --------------------------------------------
MMatrix RigModuleNodeBase::computeOffsetParentMatrix(const MMatrix& mControlWorld, const MMatrix& mDagParentWorld)
{
	// M_OPM = M_ControlWorld * M_DAGParentWorld_Inverse
	return mControlWorld * mDagParentWorld.inverse();
}

int RigModuleNodeBase::resolveSocketIndex(const MString& key, unsigned int totalElements)
{
	if (key == "start" || key == "0" || key == "socketDefault")
	{
		return 0;
	}
	if (key == "end" || key == "-1")
	{
		return (totalElements > 0) ? static_cast<int>(totalElements) - 1 : 0;
	}

	int idx = key.asInt();
	if (idx < 0 && totalElements > 0)
	{
		idx = static_cast<int>(totalElements) + idx;
	}
	return (idx >= 0) ? idx : 0;
}

bool RigModuleNodeBase::parseSocketIdentifier(const MString& fullIdentifier, MString& outModuleName, MString& outSocketName)
{
	MStringArray tokens;
	if (fullIdentifier.split('.', tokens) == MS::kSuccess && tokens.length() == 2)
	{
		outModuleName = tokens[0];
		outSocketName = tokens[1];
		return true;
	}

	// Single token represents a standalone node or root control
	outModuleName = "";
	outSocketName = fullIdentifier;
	return false;
}

// DataBlock Handles Access --------------------------------------------

MMatrix RigModuleNodeBase::getInputMatrix(MDataBlock& data, const MObject& attr, unsigned int idx)
{
	MStatus status;
	MArrayDataHandle hArray = data.inputArrayValue(attr, &status);
	if (status == MStatus::kSuccess && hArray.jumpToElement(idx) == MS::kSuccess)
	{
		return hArray.inputValue().asMatrix();
	}
	MGlobal::displayError("Error retrieving array matrix element at index.");
	return MMatrix::identity;
}

MMatrix getMatrixFromPlug(const MPlug& pMatrixPlug)
{
	MMatrix matrixValue;

	MObject dataObj;
	MStatus status = pMatrixPlug.getValue(dataObj);

	if (status && !dataObj.isNull() && dataObj.hasFn(MFn::kMatrixData))
	{
		MFnMatrixData fnMatrixData(dataObj);
		matrixValue = fnMatrixData.matrix();
	}

	return matrixValue;
}

MMatrix RigModuleNodeBase::getInputMatrix(MDataBlock& data, const MObject& attr)
{
	MDataHandle hData = data.inputValue(attr);
	return hData.asMatrix();
}

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

void RigModuleNodeBase::setOutputMatrix(MDataBlock& data, const MObject& attr, const MMatrix& mat)
{
	MDataHandle hOutput = data.outputValue(attr);
	hOutput.setMMatrix(mat);
}

// Networking functionality --------------------------------------------
MStatus RigModuleNodeBase::connectModuleToRigRoot(MObject& oRigRoot, MDGModifier& dgMod, const MObject& oModule, MObject& aRigRoot)
{
	MStatus status;
	MFnDependencyNode rigRootFn(oRigRoot);
	MPlug pRigModules = rigRootFn.findPlug("rm", false);

	if (pRigModules.isNull())
	{
		MGlobal::displayError("Failed to find rig modules plug on given rig root object.");
		return MStatus::kFailure;
	}

	MFnDependencyNode moduleFn(oModule);
	MPlug pRigRoot(oModule, aRigRoot);

	if (pRigRoot.isNull())
	{
		MGlobal::displayError("Failed to find rigRoot plug on the module object.");
		return MStatus::kFailure;
	}

	status = dgMod.connect(pRigModules, pRigRoot);
	return status;
}


// First Pass Module Builder ------------------------------------------
std::unordered_map<std::string, MObject> RigModuleNodeBase::buildModules(
	const std::vector<RigModuleData>& flatModuleList,
	MObject oRigRoot,
	MDGModifier& dgMod,
	MDagModifier& dagMod)
{
	//MObjectArray oRigModules;
	std::unordered_map<std::string, MObject> rigModuleMap;

	// 5. Traverse and Build Module Hierarchy Recursively
	for (unsigned int i = 0; i < flatModuleList.size(); ++i)
	{
		// Ensure our mods are up to date before we build the next module.
		// This means that when we query attributes from previous modules we will have them declared and set.

		RigModuleData moduleData = flatModuleList[i];
		MObject oModule;

		// 1. Branching creation logic based on moduleType
		if (moduleData.moduleType == "FkChain")
		{
			// Call the static creation method for FkChain
			oModule = FkChainNode::createModule(
				moduleData,
				oRigRoot,
				dgMod,
				dagMod
			);
		}
		else if (moduleData.moduleType == "AimModule")
		{
			oModule = AimModuleNode::createModule(
				moduleData,
				oRigRoot,
				dgMod,
				dagMod
			);
		}
		// Future module types (e.g., IkChain, SplineRibbon) branch here...
		else
		{
			MGlobal::displayError(MString("Unknown module type: ") + moduleData.moduleType.c_str());
			return std::unordered_map<std::string, MObject>();
		}
		if (!oModule.isNull())
		{
			rigModuleMap[moduleData.name] = oModule;
		}
		else
		{
			MGlobal::displayError("Error creating a rig module. Null returned.");
			return std::unordered_map<std::string, MObject>();
		}
	}
	
	return rigModuleMap;
}

// 2nd pass function for parenting the controls to their respective dag and matrix parents from any other module.
void RigModuleNodeBase::parentModuleControls(std::unordered_map<std::string, MObject> rigModuleMap, const std::vector<RigModuleData>& flatModuleList, MDGModifier& dgMod, MDagModifier& dagMod)
{

	/*
	Go through child plugs and find the mobject related to the parent socket. Make the parent relationship.
	*/

	// Ensure rig module object array and module list from json are the same size.
	if (rigModuleMap.size() != static_cast<unsigned int>(flatModuleList.size()))
	{
		MGlobal::displayError("Unable to parent module controls because object array and module list from json are not equal lengths.");
		return;
	}

	// Go through each module and do its dag parenting
	for (unsigned int i = 0; i < flatModuleList.size(); ++i)
	{
		RigModuleData moduleJsonData = flatModuleList[i];
		// NOTE: we are assuming all modules are named uniquely, which we must enforce. 
		std::string moduleName = moduleJsonData.name;
		MObject oModule = rigModuleMap[moduleName];

		// If the module didn't define any dag parents, we should parent all of its child plug transforms to the Layout module.
		if (moduleJsonData.parentStructure.childParentMap.size() == 0)
		{
			MGlobal::displayInfo(MString("Rig module ") + moduleName.c_str() + " did not define any dag parents. Defaulting to parenting all child plugs to the Layout module.");

			// Parent all child plug transforms to the layout module's "end" parent socket.
			MObject oLayoutEndSocket;
			MObject oLayoutModule = rigModuleMap["layout"];
			MStringArray layoutModuleSockets = LayoutModuleNode::getParentSocketNames(oLayoutModule);
			// Get the corresponding transform for the "end" socket
			// To do this, we need to create message connections from the module to control to connect the socket names while creating the module.
			MFnDependencyNode fnLayoutModule(oLayoutModule);
			MPlug pParentSockets = fnLayoutModule.findPlug("parentSockets.end", false);  // default to layout end

			if (!pParentSockets.isNull())
			{
				MPlugArray parentSocketConnections;
				pParentSockets.connectedTo(parentSocketConnections, true, false);

				if (parentSocketConnections.length())
				{
					oLayoutEndSocket = parentSocketConnections[0].node();
				}
			}

			// TODO: iterate through all the child plugs in the given module and parent them to the layout end socket.
			// TODO: we have to do the same thing we did with the Layout Module with the other modules. Define their sockets, connect them on creation.

		}
	}
}

MStringArray RigModuleNodeBase::getChildPlugNames(MObject& oModule)
{
	MGlobal::displayError("Child plug names are not defined for this module.");
	return MStringArray();
}

MStringArray RigModuleNodeBase::getParentSocketNames(MObject& oModule)
{
	MGlobal::displayError("Parent socket names are not defined for this module.");
	return MStringArray();
}

// We do this in one pass because you can't modify an already existing attribute in c++
MStatus RigModuleNodeBase::connectControlParentSockets(
	MDGModifier& dgMod,
	MObject& oModuleNode,
	const std::vector<std::pair<MObject, MString>>& parentSocketPairs)
{
	if (parentSocketPairs.empty())
	{
		return MStatus::kSuccess;
	}

	MStatus status;
	MFnCompoundAttribute cAttr;
	MFnMessageAttribute mAttr;
	MFnDependencyNode fnModule(oModuleNode);

	// 1. Create the top-level compound attribute MObject
	MObject aParentSocketsComp = cAttr.create("parentSockets", "parentSockets", &status);

	// 2. Build all child message attributes and append to compound attribute BEFORE adding to node
	for (unsigned int i=0; i < parentSocketPairs.size(); ++i)
	{
		const MString& socketName = parentSocketPairs[i].second;
		if (socketName.length() == 0)
		{
			MGlobal::displayError("Empty socket name provided in parentSocketPairs.");
			return MStatus::kFailure;
		}

		MObject aSocketChild = mAttr.create(socketName, socketName, &status);

		status = cAttr.addChild(aSocketChild);
	}

	// 3. Register populated compound attribute dynamically on the module instance
	status = dgMod.addAttribute(oModuleNode, aParentSocketsComp);

	// 4. Flush modifier so compound attribute and child plugs exist in DG
	status = dgMod.doIt();
	std::map<const char*, unsigned int> controlIndexTracker;

	// 5. Connect each module socket plug to its corresponding control's parentControlSockets plug
	for (unsigned int i = 0; i < parentSocketPairs.size(); ++i)
	{
		const MObject& oControlNode = parentSocketPairs[i].first;
		const MString& socketName = parentSocketPairs[i].second;

		MString socketPlugPath = MString("parentSockets.") + socketName;
		MPlug pModuleSocket = fnModule.findPlug(socketPlugPath, false, &status);

		MFnDependencyNode fnControl(oControlNode);
		MPlug pControlParentSockets = fnControl.findPlug("parentControlSockets", false, &status);
		MDagPath dControl;
		MDagPath::getAPathTo(oControlNode, dControl);
		// TODO: using getAPathTo is risky, we may want to find a way for creating a control to return a dag path instead.
		const char* controlName = dControl.fullPathName().asChar();


		if (status == MStatus::kSuccess && !pModuleSocket.isNull() && !pControlParentSockets.isNull())
		{
			// If it hasn't yet been added to our counter, query its initial count
			if (controlIndexTracker.find(controlName) == controlIndexTracker.end())
			{
				controlIndexTracker[controlName] = pControlParentSockets.numConnectedElements();
			}

			// Increment the index tracker for this handle.
			unsigned int nextIndex = controlIndexTracker[controlName]++;
			status = dgMod.connect(pModuleSocket, pControlParentSockets.elementByLogicalIndex(nextIndex));
		}
		else
		{
			MGlobal::displayError(MString("Failed to connect parent socket '") + socketName + "' on control.");
			return MStatus::kFailure;
		}
	}

	// 6. Flush connections to finalize DG state
	status = dgMod.doIt();

	return MStatus::kSuccess;
}
