#include "RigModule.h"
#include "RigJsonStructs.h"
#include "ModuleRegistry.h"
#include "Constants.h"
#include "JsonUtils.h"

#include <algorithm>
#include <unordered_map>
#include <string>
#include <vector>

#include <maya/MDataBlock.h>
#include <maya/MMatrix.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MArrayDataBuilder.h>
#include <maya/MGlobal.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MPlugArray.h>

// NOTE that RigModuleNodeBase can NOT define any static node attributes!

RigModuleNodeBase::RigModuleNodeBase() {}
RigModuleNodeBase::~RigModuleNodeBase() {}

MStatus RigModuleNodeBase::compute(const MPlug& plug, MDataBlock& data) 
{ 
	return MStatus::kSuccess; 
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

MMatrix RigModuleNodeBase::getInputMatrix(MDataBlock& data, const MObject& attr)
{
	MDataHandle hData = data.inputValue(attr);
	return hData.asMatrix();
}

MMatrix RigModuleNodeBase::getInputCompoundMatrix(
	MDataBlock& data,
	const MObject& aParentCompound,
	const MObject& aChildAttr,
	unsigned int logicalIndex)
{
	MStatus status;
	MArrayDataHandle hCompoundArray = data.inputArrayValue(aParentCompound, &status);

	if (status == MStatus::kSuccess && hCompoundArray.jumpToElement(logicalIndex) == MStatus::kSuccess)
	{
		MDataHandle hElement = hCompoundArray.inputValue();
		MDataHandle hChild = hElement.child(aChildAttr);
		return hChild.asMatrix();
	}

	// Return identity if the element is not found or uninitialized
	return MMatrix::identity;
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
MStatus RigModuleNodeBase::connectModuleToRigRoot(MObject& oRigRoot, MDagModifier& dagMod, const MObject& oModule, MObject& aRigRoot)
{
	MStatus status;
	MFnDependencyNode rigRootFn(oRigRoot);
	MPlug pRigModules = rigRootFn.findPlug(RigAttr::RigModules, false);

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

	status = dagMod.connect(pRigModules, pRigRoot);
	return status;
}

// First Pass Module Builder ------------------------------------------
std::unordered_map<std::string, RigModuleBuildResult> RigModuleNodeBase::buildModules(
	const std::vector<RigModuleData>& flatModuleList,
	MObject oRigRoot,
	MarkedJointSet& markedJoints,
	MDagModifier& dagMod)
{
	std::unordered_map<std::string, RigModuleBuildResult> rigModuleMap;

	// 5. Traverse and Build Modules from the flat list. Parenting between module controls will happen in the second pass.
	for (unsigned int i = 0; i < flatModuleList.size(); ++i)
	{
		RigModuleData moduleData = flatModuleList[i];

		// Not completely sure what this is doing with the pointers.
		const ModuleDescriptor* pDescriptor = ModuleRegistry::find(moduleData.moduleType);
		if (!pDescriptor || !pDescriptor->build)
		{
			MGlobal::displayError(MString("Unknown or non-buildable module type: ") + moduleData.moduleType.c_str());
			return std::unordered_map<std::string, RigModuleBuildResult>();
		}
		MGlobal::displayInfo(MString("Building module '") + moduleData.name.c_str() + "' of type " + moduleData.moduleType.c_str());

		BuildContext ctx{ moduleData, oRigRoot, dagMod, markedJoints };
		RigModuleBuildResult creationResult = pDescriptor->build(moduleData.moduleArgs, ctx);

		if (!creationResult.moduleNode.isNull() && creationResult.status == MStatus::kSuccess)
		{
			rigModuleMap[moduleData.name] = creationResult;
		}
		else
		{
			MGlobal::displayError("Error creating a rig module. Null returned.");
			return std::unordered_map<std::string, RigModuleBuildResult>();
		}
	}
	
	return rigModuleMap;
}


ControlSolveStep RigModuleNodeBase::solveControlStep(const MMatrix& mLocal, const MMatrix& mRestWorld, const MMatrix& mParentDelta)
{
	ControlSolveStep step;
	step.mOPM = mRestWorld * mParentDelta;   // no local in here, or the control double transforms
	step.mWorld = mLocal * step.mOPM;
	return step;
}

void RigModuleNodeBase::writeOutputMatrix(MArrayDataHandle& hOutputData, unsigned int logicalIndex, const MMatrix& mMatrix)
{
	if (hOutputData.jumpToElement(logicalIndex) == MStatus::kSuccess)
	{
		hOutputData.outputValue().setMMatrix(mMatrix);
	}
}

namespace
{
	// Name stored on childPlugs[index], empty if the element has none.
	MString getChildPlugNameAt(const MObject& oModule, unsigned int index)
	{
		MFnDependencyNode fnModule(oModule);
		MPlug pChildPlugs = fnModule.findPlug(RigAttr::ChildPlugs, false);
		MObject oChildPlugName = fnModule.attribute(RigAttr::ChildPlugName);

		// Physical walk, a logical lookup would create sparse elements.
		for (unsigned int e = 0; e < pChildPlugs.numElements(); ++e)
		{
			MPlug pElement = pChildPlugs.elementByPhysicalIndex(e);
			if (pElement.logicalIndex() == index)
			{
				return pElement.child(oChildPlugName).asString();
			}
		}
		return MString();
	}

	// The control wired to controlMessage[index], null if the part has none (e.g. the aim part).
	MObject findControlAtIndex(const MObject& oModule, unsigned int index)
	{
		MFnDependencyNode fnModule(oModule);
		MPlug pControlMessages = fnModule.findPlug(RigAttr::ControlMessage, false);

		for (unsigned int e = 0; e < pControlMessages.numElements(); ++e)
		{
			MPlug pElement = pControlMessages.elementByPhysicalIndex(e);
			if (pElement.logicalIndex() != index)
			{
				continue;
			}

			MPlugArray pDestinations;
			pElement.connectedTo(pDestinations, false, true);
			return pDestinations.length() > 0 ? pDestinations[0].node() : MObject::kNullObj;
		}
		return MObject::kNullObj;
	}
}

// Build-map lookups ----------------------------------------------------
bool RigModuleNodeBase::lookupParentSocket(
	const RigModuleMap& moduleMap, const std::string& identifier,
	const RigModuleBuildResult*& pOutModule, const RigModuleSocketInfo*& pOutSocket)
{
	std::string moduleName, socketName;
	if (!StringUtils::splitIdentifier(identifier, moduleName, socketName))
	{
		return false;
	}

	pOutModule = findInfo(moduleMap, moduleName);
	pOutSocket = pOutModule ? findInfo(pOutModule->parentSockets, socketName) : nullptr;
	return pOutSocket != nullptr;
}

bool RigModuleNodeBase::lookupChildPlug(
	const RigModuleMap& moduleMap, const std::string& identifier,
	const RigModuleBuildResult*& pOutModule, const RigModuleChildPlugInfo*& pOutChildPlug)
{
	std::string moduleName, plugName;
	if (!StringUtils::splitIdentifier(identifier, moduleName, plugName))
	{
		return false;
	}

	pOutModule = findInfo(moduleMap, moduleName);
	pOutChildPlug = pOutModule ? findInfo(pOutModule->childPlugs, plugName) : nullptr;
	return pOutChildPlug != nullptr;
}

// Plug lookups ---------------------------------------------------------
MStatus RigModuleNodeBase::getChildPlugMatrixPlugs(const MObject& oModule, unsigned int plugIndex, MPlug& pOutParentMatrix, MPlug& pOutParentRest)
{
	MStatus status;
	MFnDependencyNode fnModule(oModule);
	MPlug pChildPlugs = fnModule.findPlug(RigAttr::ChildPlugs, false, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MPlug pElement = pChildPlugs.elementByLogicalIndex(plugIndex, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MObject oChildPlugParentMatrix = fnModule.attribute(RigAttr::ChildPlugParentMatrix, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	pOutParentMatrix = pElement.child(oChildPlugParentMatrix, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MObject oChildPlugParentRest = fnModule.attribute(RigAttr::ChildPlugParentRestMatrix, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	pOutParentRest = pElement.child(oChildPlugParentRest, &status);
	return status;
}

MPlug RigModuleNodeBase::getSocketMatrixPlug(const MObject& oModule, unsigned int socketIndex, MStatus* pStatus)
{
	MStatus status;
	MFnDependencyNode fnModule(oModule);
	MPlug pSockets = fnModule.findPlug(RigAttr::OutputSocketMatrix, false, &status);
	MPlug pSocket;
	if (status)
	{
		pSocket = pSockets.elementByLogicalIndex(socketIndex, &status);
	}
	if (pStatus)
	{
		*pStatus = status;
	}
	return pSocket;
}

// Scene queries --------------------------------------------------------
// Every module derives from RigModuleNode, so moduleData identifies one regardless of Derived.
bool RigModuleNodeBase::isRigModule(const MObject& oNode)
{
	MStatus status;
	MFnDependencyNode fnNode(oNode, &status);
	return status && fnNode.hasAttribute(RigAttr::ModuleData);
}

MStatus RigModuleNodeBase::findModuleByName(const MString& name, MObject& oOutModule)
{
	MStatus status = DagUtils::getMObjectFromName(name, oOutModule);
	if (!status)
	{
		MGlobal::displayError(MString("Module not found: ") + name);
		return status;
	}
	if (!isRigModule(oOutModule))
	{
		MGlobal::displayError(MString("Node is not a rig module: ") + name);
		oOutModule = MObject::kNullObj;
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

// socketNames elements hold JSON string lists (["index_3","end"]), written by DagPlugAndSocketUtils::setParentSocketNames.
MStatus RigModuleNodeBase::findSocketIndex(const MObject& oModule, const MString& socketName, unsigned int& outIndex)
{
	MStatus status;
	MFnDependencyNode fnModule(oModule);
	MPlug pSocketNames = fnModule.findPlug(RigAttr::SocketNames, false, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	const std::string name = socketName.asChar();
	std::vector<std::string> names;
	std::string error;
	for (unsigned int e = 0; e < pSocketNames.numElements(); ++e)
	{
		MPlug pElement = pSocketNames.elementByPhysicalIndex(e);
		// Rigs built before D16 stored ';' separated text, which fails here. Rebuild them from the template.
		if (!JsonUtils::readJsonString(pElement.asString().asChar(), names, error))
		{
			const std::string index = std::to_string(pElement.logicalIndex());
			MGlobal::displayError(MString("Invalid socketNames[") + index.c_str() + "] on " + fnModule.name() + ": " + error.c_str());
			return MStatus::kFailure;
		}
		if (std::find(names.begin(), names.end(), name) != names.end())
		{
			outIndex = pElement.logicalIndex();
			return MStatus::kSuccess;
		}
	}
	return MStatus::kNotFound;
}

MStatus RigModuleNodeBase::findChildPlugIndex(const MObject& oModule, const MString& plugName, unsigned int& outIndex)
{
	MStatus status;
	MFnDependencyNode fnModule(oModule);
	MPlug pChildPlugs = fnModule.findPlug(RigAttr::ChildPlugs, false, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MObject oChildPlugName = fnModule.attribute(RigAttr::ChildPlugName);

	for (unsigned int e = 0; e < pChildPlugs.numElements(); ++e)
	{
		MPlug pElement = pChildPlugs.elementByPhysicalIndex(e);
		if (pElement.child(oChildPlugName).asString() == plugName)
		{
			outIndex = pElement.logicalIndex();
			return MStatus::kSuccess;
		}
	}
	return MStatus::kNotFound;
}

// Socket i is always control i's element in controlData, so the rest lives there.
MMatrix RigModuleNodeBase::getControlRestWorld(const MObject& oModule, unsigned int index)
{
	MFnDependencyNode fnModule(oModule);
	MPlug pControlData = fnModule.findPlug(RigAttr::ControlInputData, false);
	MPlug pElement = pControlData.elementByLogicalIndex(index);
	MObject oControlRestWorld = fnModule.attribute(RigAttr::ControlRestWorld);
	return DagUtils::getMatrixFromPlug(pElement.child(oControlRestWorld));
}

MStatus RigModuleNodeBase::resolveSocket(const MObject& oModule, const MString& socketName, RigModuleSocketInfo& outInfo)
{
	unsigned int index = 0;
	MStatus status = findSocketIndex(oModule, socketName, index);
	if (!status)
	{
		return status;
	}

	outInfo.outputIndex = index;
	outInfo.mRestWorld = getControlRestWorld(oModule, index);
	outInfo.oTransform = findControlAtIndex(oModule, index);
	return MStatus::kSuccess;
}

// Child plug index == control index on every current module. That assumption now lives only here.
MStatus RigModuleNodeBase::resolveChildPlug(const MObject& oModule, const MString& plugName, RigModuleChildPlugInfo& outInfo)
{
	unsigned int index = 0;
	MStatus status = findChildPlugIndex(oModule, plugName, index);
	if (!status)
	{
		return status;
	}

	outInfo.plugIndex = index;
	outInfo.oTransform = findControlAtIndex(oModule, index);
	return MStatus::kSuccess;
}

MStatus RigModuleNodeBase::resolveControlChildPlug(const MObject& oControl, MObject& oOutModule, unsigned int& outPlugIndex)
{
	MStatus status;
	if (oControl.isNull() || !oControl.hasFn(MFn::kTransform))
	{
		MGlobal::displayError("Control must be a rigControlNode transform.");
		return MStatus::kFailure;
	}

	MFnDependencyNode fnControl(oControl);
	if (fnControl.typeId() != RigControlNode::id)
	{
		MGlobal::displayError(MString("Not a rig control: ") + fnControl.name());
		return MStatus::kFailure;
	}

	MPlug pRigModule(oControl, RigControlNode::aRigModule);
	MPlug pModuleSource = pRigModule.source(&status);
	if (!status || pModuleSource.isNull() || !pModuleSource.isElement())
	{
		MGlobal::displayError(MString("Rig control does not belong to a module: ") + fnControl.name());
		return MStatus::kFailure;
	}

	oOutModule = pModuleSource.node();
	outPlugIndex = pModuleSource.logicalIndex();

	if (getChildPlugNameAt(oOutModule, outPlugIndex).length() == 0)
	{
		MGlobal::displayError(MString("Control is not a child plug of its module: ") + fnControl.name());
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}