#include "maya/modules/FkHierarchyModule.h"
#include "core/Constants.h"
#include "core/HierarchyPlan.h"
#include "core/RigJsonStructs.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/JointMarkupScene.h"
#include "maya/shared/ModuleInverse.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/Side.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include <maya/MDagModifier.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnTransform.h>
#include <maya/MMatrix.h>
#include <maya/MMatrixArray.h>
#include <maya/MPlug.h>

// ------------------------------------------------------------------
// FkHierarchyModuleNode Implementation
// ------------------------------------------------------------------

MTypeId FkHierarchyModuleNode::id(0x002193);

FkHierarchyModuleNode::FkHierarchyModuleNode()
{
}

FkHierarchyModuleNode::~FkHierarchyModuleNode()
{
}

void* FkHierarchyModuleNode::creator()
{
	return new FkHierarchyModuleNode();
}

MStatus FkHierarchyModuleNode::initialize()
{
	MStatus status;
	status = initializeSharedAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MFnNumericAttribute nAttr;
	aControlParentIndex = nAttr.create(RigAttr::ControlParentIndex, RigAttr::ControlParentIndex, MFnNumericData::kInt, -1, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setArray(true));
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(aControlParentIndex));

	// AFFECTS RELATIONSHIPS
	// Every control/child-plug input affects control OPM, joint OPM and socket matrix outputs, and so does the parent index.
	declareStandardAffects();
	for (const MObject& oOutput : { aControlOPM, aJointOPM, aOutputSocketMatrix })
	{
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::attributeAffects(aControlParentIndex, oOutput));
	}

	return MStatus::kSuccess;
}

MStatus FkHierarchyModuleNode::compute(const MPlug& plug, MDataBlock& data)
{
	const MObject oOutputAttr = plug.attribute();
	if (oOutputAttr == aControlOPM || oOutputAttr == aJointOPM || oOutputAttr == aOutputSocketMatrix)
	{
		return computeHierarchyOutputs(plug, data);
	}

	return MStatus::kUnknownParameter;
}

MStatus FkHierarchyModuleNode::computeHierarchyOutputs(const MPlug& plug, MDataBlock& data)
{
	MStatus status;

	MArrayDataHandle hControlInputData = data.inputArrayValue(aControlInputData, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hParentIndex = data.inputArrayValue(aControlParentIndex, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hChildPlugs = data.inputArrayValue(aChildPlugs, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	SolvedOutputs outputs = openSolvedOutputs(data, aJointOPM, status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// The root hangs from the module's parent socket. Identity if unparented.
	const MMatrix mRootDelta = readChildPlugParentDelta(hChildPlugs, 0);

	const unsigned int count = hControlInputData.elementCount();
	if (fParent.size() != count)
	{
		fParent.resize(count);
		fHasChild.resize(count);
		fMotion.resize(count);
	}

	// A parent that is not an earlier slot (a hand-edited rig) is read as a root, so the solve never reads a motion it has not made yet.
	std::fill(fHasChild.begin(), fHasChild.end(), 0);
	for (unsigned int i = 0; i < count; ++i)
	{
		int parent = -1;
		if (hParentIndex.jumpToElement(i))
		{
			parent = hParentIndex.inputValue().asInt();
		}
		fParent[i] = (parent >= 0 && parent < static_cast<int>(i)) ? parent : -1;
		if (fParent[i] >= 0)
		{
			fHasChild[static_cast<std::size_t>(fParent[i])] = 1;
		}
	}

	for (unsigned int i = 0; i < count; ++i)
	{
		MMatrix mControlLocal = MMatrix::identity;
		MMatrix mControlRestWorld = MMatrix::identity;
		status = readControlInput(hControlInputData, i, mControlLocal, mControlRestWorld);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		const MMatrix& mParentDelta = fParent[i] < 0 ? mRootDelta : fMotion[static_cast<std::size_t>(fParent[i])];
		const ControlSolveStep step = solveControlStep(mControlLocal, mControlRestWorld, mParentDelta);
		writeSolvedSlot(outputs, i, step.mOPM, step.mWorld);

		// A leaf has no child to hand its motion to, so its matrix inverse is skipped.
		if (fHasChild[i])
		{
			fMotion[i] = MathUtils::computeDeltaFromRest(mControlRestWorld, step.mWorld);
		}
	}

	commitSolvedOutputs(data, plug, aJointOPM, outputs);
	return MStatus::kSuccess;
}

ModuleInverseResult FkHierarchyModuleNode::inverse(const ModuleInverseContext& context, const ModuleInverseInput& input)
{
	return ModuleInverse::solveChain(context, input, true);
}

// Joints run down +X and bend about Z, the same as FkChain, so the rotate order is XYZ.
RigModuleBuildResult FkHierarchyModuleNode::createModule(
	const MString& moduleName,
	const Side& side,
	const ShapeType& shapeType,
	const MObjectArray& oJoints,
	const MMatrixArray& mJointMatrices,
	const std::vector<ControlMarkup>& controlMarkups,
	const std::vector<int>& parentIndex,
	const std::vector<std::string>& socketNames,
	MObject& oRigRoot,
	MDagModifier& dagMod)
{
	RigModuleBuildResult result;

	MObject oModule = dagMod.MDGModifier::createNode(FkHierarchyModuleNode::id);
	dagMod.renameNode(oModule, moduleName);

	MPlug pSide(oModule, FkHierarchyModuleNode::aSide);
	dagMod.newPlugValueShort(pSide, getShortFromSide(side));
	result.moduleNode = oModule;

	const unsigned int numJoints = oJoints.length();
	std::vector<MObject> oControls(numJoints);

	for (unsigned int i = 0; i < numJoints; ++i)
	{
		MStatus jointStatus = FkHierarchyModuleNode::wireJoint(oModule, oJoints[i], i, mJointMatrices[i], dagMod);
		if (!jointStatus)
		{
			result.status = jointStatus;
			return result;
		}

		MFnTransform fnJoint(oJoints[i]);
		oControls[i] = RigControlNode::createRigControl(dagMod, fnJoint.name(), shapeType, side, controlMarkups[i], MEulerRotation::kXYZ);

		RigModuleSocketInfo socketInfo;
		socketInfo.oTransform = oControls[i];
		socketInfo.mRestWorld = mJointMatrices[i];
		socketInfo.outputIndex = i;

		std::vector<std::string> namesHere{ socketNames[i] };
		if (i == 0)
		{
			namesHere.push_back(PlugAndSocketNames::Start);
		}
		for (const std::string& name : namesHere)
		{
			result.parentSockets[name] = socketInfo;
		}
		DagPlugAndSocketUtils::setParentSocketNames(oModule, i, namesHere, dagMod);

		if (parentIndex[i] < 0)
		{
			// Only the root's control can be parented to another module.
			result.childPlugs[PlugAndSocketNames::Start] = RigModuleChildPlugInfo{ oControls[i], 0 };
			DagPlugAndSocketUtils::setChildPlugName(oModule, 0, PlugAndSocketNames::Start, dagMod);
		}
		else
		{
			// Parent the control in the fk hierarchy.
			dagMod.reparentNode(oControls[i], oControls[static_cast<std::size_t>(parentIndex[i])]);
		}

		MStatus wireStatus = wireControl(oModule, oControls[i], i, mJointMatrices[i], dagMod);
		if (!wireStatus)
		{
			result.status = wireStatus;
			return result;
		}

		dagMod.newPlugValueInt(MPlug(oModule, aControlParentIndex).elementByLogicalIndex(i), parentIndex[i]);
	}

	if (!oRigRoot.isNull())
	{
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dagMod, oModule, aRigRoot);
	}

	// Tie this module's reparent logic to the result callback called during creation.
	result.dagParentCallback = FkHierarchyModuleNode::reparentChildPlugToSocket;
	result.status = MS::kSuccess;
	return result;
}

bool FkHierarchyModuleNode::planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan)
{
	FkHierarchyArgs hierarchyArgs;
	if (!readModuleArgs(args, { FkHierarchyArgs::RootKey }, ctx, hierarchyArgs, { FkHierarchyArgs::TerminatorsKey }))
	{
		return false;
	}
	const std::string argsPath = ctx.where + "." + TemplateKey::ModuleArgs;

	// Find the root without claiming it: every joint the walk keeps, the root included, is claimed below. A selector
	// that matches nothing is claimed once, for the same error any module gives.
	std::size_t rootEntry = 0;
	if (!ctx.markedJoints.index.find(hierarchyArgs.root, rootEntry))
	{
		MObject oIgnored;
		MMatrix mIgnored;
		claimJoint(ctx, hierarchyArgs.root, oIgnored, mIgnored);
		return false;
	}

	std::vector<HierarchyJoint> walked;
	MStatus status = JointMarkupScene::collectJointSubtree(ctx.markedJoints.dagPaths[static_cast<unsigned int>(rootEntry)], walked);
	if (!status)
	{
		ctx.report.addError(argsPath, std::string("could not walk the joints below the root: ") + status.errorString().asChar());
		return false;
	}

	const HierarchyPlan hierarchy = HierarchyPlanner::plan(walked, hierarchyArgs.terminators, ctx.moduleData.isMirror());
	for (const std::string& warning : hierarchy.warnings)
	{
		ctx.report.addWarning(argsPath + "." + FkHierarchyArgs::TerminatorsKey, warning);
	}
	for (const std::string& error : hierarchy.errors)
	{
		ctx.report.addError(argsPath, error);
	}
	if (!hierarchy.errors.empty())
	{
		return false;
	}

	// Claim every joint even after a failure, so the report lists every bad joint at once.
	MObjectArray oJoints;
	MMatrixArray mWorldMatrices;
	std::vector<ControlMarkup> controlMarkups;   // Each control takes its joint's (side, usage)
	std::vector<std::string> socketNames;
	bool allClaimed = true;
	bool claimFailed = false;
	for (const std::size_t walkedIndex : hierarchy.joints)
	{
		const JointSelector& selector = walked[walkedIndex].selector;

		std::string socketName;
		std::string error;
		if (!HierarchyPlanner::socketName(selector, socketName, error))
		{
			ctx.report.addError(argsPath, error);
			allClaimed = false;
			continue;
		}

		MObject oJoint;
		MMatrix mWorld;
		if (!claimJoint(ctx, selector, oJoint, mWorld))
		{
			allClaimed = false;
			claimFailed = true;
			continue;
		}
		oJoints.append(oJoint);
		mWorldMatrices.append(mWorld);
		controlMarkups.push_back(makeControlMarkup(selector.side, selector.usage));
		socketNames.push_back(socketName);
	}
	if (!allClaimed)
	{
		if (claimFailed)
		{
			ctx.report.addError(argsPath, "FkHierarchy takes every joint below its root. End the walk above a joint another module owns with terminators, or give that branch its own FkHierarchy");
		}
		return false;
	}

	ModuleInterface moduleInterface;
	moduleInterface.childPlugs.push_back(ModulePlugInfo{ PlugAndSocketNames::Start, true });
	for (std::size_t i = 0; i < socketNames.size(); ++i)
	{
		moduleInterface.sockets.push_back(socketNames[i]);
		if (i == 0)
		{
			moduleInterface.sockets.push_back(PlugAndSocketNames::Start);
		}
	}
	moduleInterface.controls = controlMarkups;
	outPlan.moduleInterface = moduleInterface;

	const MString moduleName = ctx.moduleData.name.c_str();
	const Side side = getSideFromString(ctx.moduleData.side);
	const ShapeType shapeType = getShapeTypeFromString(ctx.moduleData.shapeType);
	const std::vector<int> parentIndex = hierarchy.parentIndex;
	outPlan.build = [moduleName, side, shapeType, oJoints, mWorldMatrices, controlMarkups, parentIndex, socketNames](MObject& oRigRoot, MDagModifier& dagMod)
	{
		return FkHierarchyModuleNode::createModule(moduleName, side, shapeType, oJoints, mWorldMatrices, controlMarkups, parentIndex, socketNames, oRigRoot, dagMod);
	};
	return true;
}
