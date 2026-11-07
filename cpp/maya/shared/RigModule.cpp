#include "maya/shared/RigModule.h"
#include "core/Constants.h"
#include "core/JsonUtils.h"
#include "core/RigJsonStructs.h"
#include "core/TemplateMirror.h"
#include "maya/ModuleRegistry.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include <maya/MArrayDataBuilder.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MDGModifier.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MPlugArray.h>

// NOTE that RigModuleNodeBase can NOT define any static node attributes!

namespace
{
	// World units a mirror joint may sit from its reflected source when createRigFromTemplate checks positions (D28).
	constexpr double kMirrorPositionTolerance{ 0.001 };
}

RigModuleNodeBase::RigModuleNodeBase()
{
}
RigModuleNodeBase::~RigModuleNodeBase()
{
}

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

namespace
{
	// Guards against a module's plan and createModule naming child plugs or sockets differently.
	bool matchesInterface(const RigModuleBuildResult& result, const ModuleInterface& moduleInterface, std::string& outError)
	{
		bool plugsMatch = result.childPlugs.size() == moduleInterface.childPlugs.size();
		for (const ModulePlugInfo& plug : moduleInterface.childPlugs)
		{
			auto it = result.childPlugs.find(plug.name);
			plugsMatch = plugsMatch && it != result.childPlugs.end() && plug.hasControl == !it->second.oTransform.isNull();
		}

		bool socketsMatch = result.parentSockets.size() == moduleInterface.sockets.size();
		for (const std::string& socket : moduleInterface.sockets)
		{
			socketsMatch = socketsMatch && result.parentSockets.count(socket) > 0;
		}

		if (!plugsMatch)
		{
			outError = "built child plugs differ from the planned child plugs";
		}
		else if (!socketsMatch)
		{
			outError = "built sockets differ from the planned sockets";
		}
		return plugsMatch && socketsMatch;
	}

	std::string buildableModuleTypes()
	{
		std::string types;
		for (const ModuleDescriptor& descriptor : ModuleRegistry::all())
		{
			if (descriptor.plan)
			{
				types += types.empty() ? descriptor.moduleType : std::string(", ") + descriptor.moduleType;
			}
		}
		return types;
	}
}

// Read phase ---------------------------------------------------------
std::vector<ModulePlan> RigModuleNodeBase::planModules(
	const std::vector<RigModuleData>& modules,
	MarkedJointSet& markedJoints,
	bool checkMirrorPositions,
	ValidationReport& report)
{
	std::vector<ModulePlan> plans;

	for (std::size_t i = 0; i < modules.size(); ++i)
	{
		const RigModuleData& moduleData = modules[i];
		const std::string where = RigTemplateValidation::modulePath(i, moduleData);

		const ModuleDescriptor* pDescriptor = ModuleRegistry::find(moduleData.moduleType);
		if (!pDescriptor || !pDescriptor->plan)
		{
			report.addError(where + "." + TemplateKey::ModuleType,
				"'" + moduleData.moduleType + "' is not a module type templates can build (has: " + buildableModuleTypes() + ")");
			continue;
		}

		RigTemplateValidation::validateModuleFields(moduleData, where, pDescriptor->currentVersion, report);

		PlanContext ctx{ moduleData, where, markedJoints, report, checkMirrorPositions };
		ModulePlan plan;
		plan.name = moduleData.name;
		if (pDescriptor->plan(moduleData.moduleArgs, ctx, plan))
		{
			plans.push_back(std::move(plan));
		}
	}

	return plans;
}

bool RigModuleNodeBase::claimJoint(PlanContext& ctx, const JointSelector& selector, MObject& oOutJoint, MMatrix& mOutRestWorld)
{
	std::string error;
	if (!JointMarkupScene::claimJoint(ctx.markedJoints, selector, ctx.moduleData.name, oOutJoint, mOutRestWorld, error))
	{
		ctx.report.addError(ctx.where + "." + TemplateKey::ModuleArgs, error);
		return false;
	}

	if (!ctx.checkMirrorPositions || !ctx.moduleData.isMirror() || selector.side == SideNames::Center)
	{
		return true;
	}

	// The source module claims the source joint and reports it if it is missing, so a miss here needs no error.
	const JointSelector sourceSelector{ TemplateMirror::swapSide(selector.side), selector.usage };
	std::size_t sourceEntry = 0;
	if (!ctx.markedJoints.index.find(sourceSelector, sourceEntry))
	{
		return true;
	}

	// Mirror plane: world X = 0, so the source's reflected position is (-x, y, z).
	const MMatrix mSourceWorld = ctx.markedJoints.dagPaths[static_cast<unsigned int>(sourceEntry)].inclusiveMatrix();
	const double dx = mOutRestWorld[3][0] + mSourceWorld[3][0];
	const double dy = mOutRestWorld[3][1] - mSourceWorld[3][1];
	const double dz = mOutRestWorld[3][2] - mSourceWorld[3][2];
	const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
	if (distance > kMirrorPositionTolerance)
	{
		ctx.report.addError(ctx.where + "." + TemplateKey::ModuleArgs, "joint " + selector.side + ", " + selector.usage
			+ " is " + std::to_string(distance) + " from where joint " + sourceSelector.side + ", " + sourceSelector.usage
			+ " reflects to across X = 0 (checkMirrorPositions allows " + std::to_string(kMirrorPositionTolerance) + ")");
		return false;
	}
	return true;
}

// First Pass Module Builder ------------------------------------------
MStatus RigModuleNodeBase::buildModules(
	const std::vector<ModulePlan>& plans,
	MObject oRigRoot,
	MDagModifier& dagMod,
	RigModuleMap& outModuleMap)
{
	// Parenting between module controls happens in the second pass.
	for (const ModulePlan& plan : plans)
	{
		MGlobal::displayInfo(MString("Building module '") + plan.name.c_str() + "'");

		RigModuleBuildResult result = plan.build(oRigRoot, dagMod);
		if (!result || result.moduleNode.isNull())
		{
			MGlobal::displayError(MString("Failed building module '") + plan.name.c_str() + "'.");
			return MStatus::kFailure;
		}

		std::string error;
		if (!matchesInterface(result, plan.moduleInterface, error))
		{
			MGlobal::displayError(MString("Module '") + plan.name.c_str() + "': " + error.c_str() + ". Its plan step and createModule disagree.");
			return MStatus::kFailure;
		}

		outModuleMap[plan.name] = result;
	}

	return MStatus::kSuccess;
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