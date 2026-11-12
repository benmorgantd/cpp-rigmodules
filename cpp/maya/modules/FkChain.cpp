#include "maya/modules/FkChain.h"
#include "core/RigJsonStructs.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/Side.h"
#include "maya/shared/Utilities.h"

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include <maya/MDagModifier.h>
#include <maya/MDGModifier.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnTransform.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MMatrixArray.h>
#include <maya/MPlug.h>

// ------------------------------------------------------------------
// FkChainNode Implementation
// ------------------------------------------------------------------

MTypeId FkChainNode::id(0x00218B);

FkChainNode::FkChainNode()
{
}
FkChainNode::~FkChainNode()
{
}

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

	// One solve writes control OPM, joint OPM and socket matrix together.
	if (oOutputAttr == aControlOPM || oOutputAttr == aJointOPM || oOutputAttr == aOutputSocketMatrix)
	{
		return computeChainOutputs(plug, data, aJointOPM);
	}

	return MStatus::kUnknownParameter;
}

ModuleInverseResult FkChainNode::inverse(const ModuleInverseContext& context, const ModuleInverseInput& input)
{
	return ModuleInverse::solveChain(context, input, true);
}

// Joints run down +X and bend about Z, so the middle axis (the one that gimbal locks at 90 degrees) should be
// Y, the side swing that rarely goes far. In XYZ, Z is also the parent-space axis, so the bend curve stays
// clean, and X, the twist, turns about the bone itself. ZXY would put the twist in the middle (D40).
RigModuleBuildResult FkChainNode::createModule(
	const MString& moduleName,
	const Side& side,
	const ShapeType& shapeType,
	const MObjectArray& oJoints,
	const MMatrixArray& mJointWorldRests,
	const std::vector<ControlMarkup>& controlMarkups,
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
	MPlug pControlMessage(oFkChainModule, FkChainNode::aControlMessage);

	MObject oPreviousControl;
	unsigned int numJoints = oJoints.length();

	for (unsigned int i = 0; i < numJoints; ++i)
	{
		MObject oJoint = oJoints[i];
		MMatrix mJointRestWorld = mJointWorldRests[i];

		MStatus jointStatus = FkChainNode::wireJoint(oFkChainModule, oJoint, i, mJointRestWorld, dagMod);
		if (!jointStatus)
		{
			result.status = jointStatus;
			return result;
		}

		// Create control and flush modifier
		MFnTransform fnJoint(oJoint);
		MObject oControlTransform = RigControlNode::createRigControl(dagMod, fnJoint.name(), shapeType, side, controlMarkups[i], MEulerRotation::kXYZ);

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
	std::vector<ControlMarkup> controlMarkups;   // Each control takes its joint's (side, usage)
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
		controlMarkups.push_back(makeControlMarkup(selector.side, selector.usage));
	}
	if (!allClaimed)
	{
		return false;
	}

	outPlan.moduleInterface = makeInterface(oJoints.length());
	outPlan.moduleInterface.controls = controlMarkups;

	const MString moduleName = ctx.moduleData.name.c_str();
	const Side side = getSideFromString(ctx.moduleData.side);
	const ShapeType shapeType = getShapeTypeFromString(ctx.moduleData.shapeType);
	outPlan.build = [moduleName, side, shapeType, oJoints, mWorldMatrices, controlMarkups](MObject& oRigRoot, MDagModifier& dagMod)
	{
		return FkChainNode::createModule(moduleName, side, shapeType, oJoints, mWorldMatrices, controlMarkups, oRigRoot, dagMod);
	};
	return true;
}
