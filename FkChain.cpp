#include "FkChain.h"
#include "RigControlNode.h"
#include "RigJsonStructs.h"
#include "Utilities.h"
#include "RigModule.h"
#include "Side.h"

#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

#include <maya/MFnDependencyNode.h>
#include <maya/MMatrix.h>
#include <maya/MPlug.h>
#include <maya/MGlobal.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>
#include <maya/MMatrixArray.h>
#include <maya/MFnTransform.h>

// ------------------------------------------------------------------
// FkChainNode Implementation
// ------------------------------------------------------------------

MTypeId FkChainNode::id(0x00218B);

FkChainNode::FkChainNode() {}
FkChainNode::~FkChainNode() {}

void* FkChainNode::creator()
{
	return new FkChainNode();
}

MStatus FkChainNode::initialize()
{
	MStatus status;
	status = initializeSharedAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// AFFECTS RELATIONSHIPS
	// Every control/child-plug input affects control OPM, joint OPM and socket matrix outputs.
	declareStandardAffects();

	return MStatus::kSuccess;
}

MStatus FkChainNode::compute(const MPlug& plug, MDataBlock& data)
{
	const MObject oOutputAttr = plug.attribute();

	// Control OPM is the only output that excludes the control's local matrix.
	// Joint OPM and socket matrix both want the control's world matrix.
	if (oOutputAttr == aControlOPM)
	{
		return computeChainOutput(plug, data, false);
	}

	if (oOutputAttr == aJointOPM || oOutputAttr == aOutputSocketMatrix)
	{
		return computeChainOutput(plug, data, true);
	}

	return MStatus::kUnknownParameter;
}

// TODO: set rotate order for +x down and +z bend
RigModuleBuildResult FkChainNode::createModule(
	const MString& moduleName,
	const Side& side,
	const ShapeType& shapeType,
	const MObjectArray& oJoints,
	const MMatrixArray& mJointWorldRests,
	MObject& oRigRoot,
	MDagModifier& dagMod)
{
	RigModuleBuildResult result;

	// Instantiate module node in DG
	MObject oFkChainModule = dagMod.MDGModifier::createNode(FkChainNode::id);
	dagMod.renameNode(oFkChainModule, moduleName);

	MPlug pSide(oFkChainModule, FkChainNode::aSide);
	dagMod.newPlugValueShort(pSide, getShortFromSide(side));
	result.moduleNode = oFkChainModule;

	MFnDependencyNode moduleFn(oFkChainModule);
	MPlug pControlInputData(oFkChainModule, FkChainNode::aControlInputData);
	MPlug pOutputJointOPM(oFkChainModule, FkChainNode::aJointOPM);
	MPlug pControlMessage(oFkChainModule, FkChainNode::aControlMessage);

	MObject oPreviousControl;
	unsigned int numJoints = oJoints.length();

	for (unsigned int i = 0; i < numJoints; ++i)
	{
		// Turn off inherits transform for all joints so their OPM can be in world space
		MObject oJoint = oJoints[i];
		MMatrix mJointRestWorld = mJointWorldRests[i];

		// Make sure the joint retains its original transform and bake them again
		// The module drives the joint's world transform through its OPM, so nothing may be inherited.
		DagUtils::setInheritsTransforms(oJoint, false, dagMod);
		DagUtils::setSegmentScaleCompensate(oJoint, false, dagMod);
		MStatus bakeStatus = DagUtils::bakeJointTransformsToOPM(oJoint, mJointRestWorld, dagMod);

		if (!bakeStatus)
		{
			result.status = bakeStatus;
			return result;
		}

		// Create control and flush modifier
		MFnTransform fnJoint(oJoint);
		MObject oControlTransform = RigControlNode::createRigControl(oFkChainModule, dagMod, fnJoint.name(), shapeType, side);

		RigModuleSocketInfo socketInfo;
		socketInfo.oTransform = oControlTransform;
		socketInfo.mRestWorld = mJointRestWorld;
		socketInfo.outputIndex = i;

		const std::vector<std::string> socketNames = socketNamesAt(i, numJoints);
		for (const std::string& socketName : socketNames)
		{
			result.parentSockets[socketName] = socketInfo;
		}
		DagPlugAndSocketUtils::setParentSocketNames(oFkChainModule, i, socketNames, dagMod);

		if (i == 0)
		{
			// Only the first control in an FK chain can be parented to another module.
			result.childPlugs[PlugAndSocketNames::Start] = RigModuleChildPlugInfo{ oControlTransform, 0 };
			DagPlugAndSocketUtils::setChildPlugName(oFkChainModule, 0, PlugAndSocketNames::Start, dagMod);
		}
		else
		{
			// Parent the control in the fk hierarchy.
			dagMod.reparentNode(oControlTransform, oPreviousControl);
		}

		MStatus wireStatus = wireControl(oFkChainModule, oControlTransform, i, mJointRestWorld, dagMod);
		if (!wireStatus)
		{
			result.status = wireStatus;
			return result;
		}

		MPlug pJointOpm = fnJoint.findPlug("offsetParentMatrix", false);
		dagMod.connect(pOutputJointOPM.elementByLogicalIndex(i), pJointOpm);
		
		// Update persistent path for next iteration
		oPreviousControl = oControlTransform;
	}

	if (!oRigRoot.isNull())
	{
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dagMod, oFkChainModule, aRigRoot);
	}

	// Tie this module's reparent logic to the result callback called during creation.
	result.dagParentCallback = FkChainNode::reparentChildPlugToSocket;
	result.status = MS::kSuccess;
	return result;
}

std::vector<std::string> FkChainNode::socketNamesAt(unsigned int index, unsigned int count)
{
	std::vector<std::string> names{ PlugAndSocketNames::Index + std::to_string(index) };
	if (index == 0)
	{
		names.push_back(PlugAndSocketNames::Start);
	}
	if (index == count - 1)
	{
		names.push_back(PlugAndSocketNames::End);
	}
	return names;
}

ModuleInterface FkChainNode::makeInterface(unsigned int count)
{
	ModuleInterface moduleInterface;
	moduleInterface.childPlugs.push_back(ModulePlugInfo{ PlugAndSocketNames::Start, true });
	for (unsigned int i = 0; i < count; ++i)
	{
		const std::vector<std::string> names = socketNamesAt(i, count);
		moduleInterface.sockets.insert(moduleInterface.sockets.end(), names.begin(), names.end());
	}
	return moduleInterface;
}

bool FkChainNode::planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan)
{
	FkModuleArgs fkArgs;
	if (!readModuleArgs(args, { FkModuleArgs::JointsKey }, ctx, fkArgs))
	{
		return false;
	}

	if (fkArgs.joints.empty())
	{
		ctx.report.addError(ctx.where + "." + TemplateKey::ModuleArgs + "." + FkModuleArgs::JointsKey, "needs at least one joint");
		return false;
	}

	// Claim every joint even after a failure, so the report lists every bad selector at once.
	MObjectArray oJoints;
	MMatrixArray mWorldMatrices;
	bool allClaimed = true;
	for (const JointSelector& selector : fkArgs.joints)
	{
		MObject oJoint;
		MMatrix mWorld;
		if (!claimJoint(ctx, selector, oJoint, mWorld))
		{
			allClaimed = false;
			continue;
		}
		oJoints.append(oJoint);
		mWorldMatrices.append(mWorld);
	}
	if (!allClaimed)
	{
		return false;
	}

	outPlan.moduleInterface = makeInterface(oJoints.length());

	const MString moduleName = ctx.moduleData.name.c_str();
	const Side side = getSideFromString(ctx.moduleData.side);
	const ShapeType shapeType = getShapeTypeFromString(ctx.moduleData.shapeType);
	outPlan.build = [moduleName, side, shapeType, oJoints, mWorldMatrices](MObject& oRigRoot, MDagModifier& dagMod)
	{
		return FkChainNode::createModule(moduleName, side, shapeType, oJoints, mWorldMatrices, oRigRoot, dagMod);
	};
	return true;
}
