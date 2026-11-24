#include "maya/modules/ReverseFootModule.h"
#include "core/Constants.h"
#include "core/FootSolve.h"
#include "core/NamingConvention.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/Side.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

#include <maya/MAngle.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MMatrix.h>
#include <maya/MPlug.h>

MTypeId ReverseFootModuleNode::id(0x002194);
std::array<MObject, 11> ReverseFootModuleNode::aChannels;
MObject ReverseFootModuleNode::aPivotRoles;
MObject ReverseFootModuleNode::aPivotPositions;
MObject ReverseFootModuleNode::aAnkleRestWorld;
MObject ReverseFootModuleNode::aAnkleSocketMatrix;
MObject ReverseFootModuleNode::aAnklePlugMatrix;
MObject ReverseFootModuleNode::aAnklePlugRestMatrix;
MObject ReverseFootModuleNode::aAnklePlugConnected;
MObject ReverseFootModuleNode::aIkFkDriver;

namespace
{
	// The ankle child plug has no element of childPlugs, so its plugIndex only says which plug the pass 2 callback is connecting.
	constexpr unsigned int kAnklePlug{ 1 };

	// The 11 channels, in the order of ReverseFootModuleNode::aChannels: name, rest in degrees, and whether a mirror negates it (D74
	// section 8). Names are the animator's, public API.
	struct ChannelSpec
	{
		const char* name;
		double restDegrees;
		bool negate;
	};
	constexpr std::array<ChannelSpec, 11> kChannels
	{
		ChannelSpec{ RigAttr::FootRoll, 0.0, false },
		ChannelSpec{ RigAttr::BreakAngle, 30.0, false },
		ChannelSpec{ RigAttr::HeelRaise, 0.0, false },
		ChannelSpec{ RigAttr::HeelSwivel, 0.0, true },
		ChannelSpec{ RigAttr::BallRaise, 0.0, false },
		ChannelSpec{ RigAttr::BallSwivel, 0.0, true },
		ChannelSpec{ RigAttr::BallRoll, 0.0, true },
		ChannelSpec{ RigAttr::FootBank, 0.0, true },
		ChannelSpec{ RigAttr::ToeRaise, 0.0, false },
		ChannelSpec{ RigAttr::ToeSwivel, 0.0, true },
		ChannelSpec{ RigAttr::ToeTap, 0.0, false },
	};

	// The channels in the order above, as FootSolve takes them (degrees).
	FootSolve::Channels channelsFromRadians(const double* values)
	{
		const auto degrees = [values](std::size_t k)
		{
			return MAngle(values[k], MAngle::kRadians).asDegrees();
		};
		FootSolve::Channels channels;
		channels.footRoll = degrees(0);
		channels.breakAngle = degrees(1);
		channels.heelRaise = degrees(2);
		channels.heelSwivel = degrees(3);
		channels.ballRaise = degrees(4);
		channels.ballSwivel = degrees(5);
		channels.ballRoll = degrees(6);
		channels.bank = degrees(7);
		channels.toeRaise = degrees(8);
		channels.toeSwivel = degrees(9);
		channels.toeTap = degrees(10);
		return channels;
	}

	MMatrix translationMatrix(const VectorMath::Vec3& position)
	{
		MMatrix mMatrix;
		mMatrix(3, 0) = position.x;
		mMatrix(3, 1) = position.y;
		mMatrix(3, 2) = position.z;
		return mMatrix;
	}
}

ReverseFootModuleNode::ReverseFootModuleNode()
{
}
ReverseFootModuleNode::~ReverseFootModuleNode()
{
}

void* ReverseFootModuleNode::creator()
{
	return new ReverseFootModuleNode();
}

MStatus ReverseFootModuleNode::initialize()
{
	MStatus status;
	MFnNumericAttribute nAttr;
	MFnUnitAttribute uAttr;
	MFnMatrixAttribute mAttr;
	MFnTypedAttribute tAttr;

	status = initializeSharedAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// The channels, connected from the foot control at build.
	for (std::size_t k = 0; k < kChannels.size(); ++k)
	{
		aChannels[k] = uAttr.create(kChannels[k].name, kChannels[k].name, MFnUnitAttribute::kAngle,
			MAngle(kChannels[k].restDegrees, MAngle::kDegrees).asRadians(), &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(uAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aChannels[k]));
	}

	// The pivots at rest: a role and a position per pivot, in chain order. Stored data the solve reads.
	aPivotRoles = nAttr.create(RigAttr::PivotRoles, RigAttr::PivotRoles, MFnNumericData::kInt, 0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setArray(true));
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aPivotRoles));
	aPivotPositions = mAttr.create(RigAttr::PivotPositions, RigAttr::PivotPositions, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setArray(true));
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aPivotPositions));
	aAnkleRestWorld = mAttr.create(RigAttr::AnkleRestWorld, RigAttr::AnkleRestWorld, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aAnkleRestWorld));

	// The ankle socket, on an output array of its own.
	aAnkleSocketMatrix = mAttr.create(RigAttr::AnkleSocketMatrix, RigAttr::AnkleSocketMatrix, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setArray(true));
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setUsesArrayDataBuilder(true));
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setWritable(false));
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(false));
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setWorldSpace(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aAnkleSocketMatrix));

	// The ankle child plug, on inputs of its own. Stage 1 never reads it.
	aAnklePlugMatrix = mAttr.create(RigAttr::AnklePlugMatrix, RigAttr::AnklePlugMatrix, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aAnklePlugMatrix));
	aAnklePlugRestMatrix = mAttr.create(RigAttr::AnklePlugRestMatrix, RigAttr::AnklePlugRestMatrix, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aAnklePlugRestMatrix));
	aAnklePlugConnected = nAttr.create(RigAttr::AnklePlugConnected, RigAttr::AnklePlugConnected, MFnNumericData::kBoolean, false, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aAnklePlugConnected));

	aIkFkDriver = tAttr.create(RigAttr::IkFkDriver, RigAttr::IkFkDriver, MFnData::kString, MObject::kNullObj, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(tAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aIkFkDriver));

	// AFFECTS RELATIONSHIPS
	declareControlAffects();
	// The ankle socket follows the foot control, the ik plug, the channels and the pivots, and never the ankle plug: that is what lets the
	// foot feed the Limb's ikTarget and take the Limb's end socket (blindSockets in makeInterface). compute() keeps to it.
	std::vector<MObject> oInputs(aChannels.begin(), aChannels.end());
	oInputs.insert(oInputs.end(), { aControlMatrix, aControlRestWorld, aChildPlugParentMatrix, aChildPlugParentRestMatrix,
		aPivotRoles, aPivotPositions, aAnkleRestWorld });
	for (const MObject& oInput : oInputs)
	{
		attributeAffects(oInput, aAnkleSocketMatrix);
	}
	return MStatus::kSuccess;
}

MStatus ReverseFootModuleNode::compute(const MPlug& plug, MDataBlock& data)
{
	const MObject oOutputAttr = plug.attribute();
	if (oOutputAttr == aControlOPM || oOutputAttr == aAnkleSocketMatrix)
	{
		return computeStage1(plug, data);
	}
	return MStatus::kUnknownParameter;
}

// Stage 1: where the foot control is, and where the Limb's end joint should reach (the ankle socket). One solve writes both, so Maya
// computes it once. Reads the ik plug and the foot control, never the ankle plug.
MStatus ReverseFootModuleNode::computeStage1(const MPlug& plug, MDataBlock& data)
{
	MStatus status;

	MArrayDataHandle hRoles = data.inputArrayValue(aPivotRoles, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hPositions = data.inputArrayValue(aPivotPositions, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	std::array<FootSolve::Pivot, ReverseFootSlot::PivotCap> pivots;
	std::size_t pivotCount = 0;
	for (unsigned int i = 0; i < hRoles.elementCount() && pivotCount < pivots.size(); ++i)
	{
		if (hRoles.jumpToElement(i) != MStatus::kSuccess || hPositions.jumpToElement(i) != MStatus::kSuccess)
		{
			break;
		}
		const MMatrix mPosition = hPositions.inputValue().asMatrix();
		pivots[pivotCount].role = static_cast<FootSolve::Role>(hRoles.inputValue().asInt());
		pivots[pivotCount].position = { mPosition(3, 0), mPosition(3, 1), mPosition(3, 2) };
		++pivotCount;
	}

	MArrayDataHandle hControlInputData = data.inputArrayValue(aControlInputData, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MMatrix mControlLocal = MMatrix::identity;
	MMatrix mControlRest = MMatrix::identity;
	if (pivotCount == 0 || readControlInput(hControlInputData, ReverseFootSlot::Foot, mControlLocal, mControlRest) != MStatus::kSuccess)
	{
		// Not built yet: the build sets the pivots and the control after it creates the node.
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	double values[kChannels.size()];
	for (std::size_t k = 0; k < kChannels.size(); ++k)
	{
		values[k] = data.inputValue(aChannels[k]).asDouble();
	}
	const std::span<const FootSolve::Pivot> chain(pivots.data(), pivotCount);
	const FootSolve::Frame frame = FootSolve::footFrame(chain);
	const MMatrix mFoot = MathUtils::toMMatrix(FootSolve::ankleMotion(chain, frame, channelsFromRadians(values)));

	// The foot hangs from the handle: its pivots turn the ankle first, and the handle's motion carries the result.
	MArrayDataHandle hChildPlugs = data.inputArrayValue(aChildPlugs, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	const MMatrix mHandleDelta = readChildPlugParentDelta(hChildPlugs, ReverseFootSlot::IkPlug);

	const ControlSolveStep step = solveControlStep(mControlLocal, mControlRest, mHandleDelta);
	const MMatrix mAnkleWorld = data.inputValue(aAnkleRestWorld).asMatrix() * mFoot * mHandleDelta;

	MArrayDataHandle hControlOPM = data.outputArrayValue(aControlOPM, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hAnkle = data.outputArrayValue(aAnkleSocketMatrix, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	writeOutputMatrix(hControlOPM, ReverseFootSlot::Foot, step.mOPM);
	writeOutputMatrix(hAnkle, ReverseFootSlot::AnkleSocket, mAnkleWorld);

	const MObject oNode = plug.node();
	commitOutput(data, oNode, aControlOPM, hControlOPM);
	commitOutput(data, oNode, aAnkleSocketMatrix, hAnkle);
	data.setClean(plug);
	return MStatus::kSuccess;
}

ModuleInverseResult ReverseFootModuleNode::inverse(const ModuleInverseContext& context, const ModuleInverseInput&)
{
	// The one control is derived and locked, so the joints give it nothing: it keeps the sidecar's channels or the rest, which the driver
	// writes from fallbackLocal and the sidecar. The ankle socket feeds a Limb's ikTarget, which the sweep does not follow.
	ModuleInverseResult result;
	result.controlLocal.resize(context.controlRestWorld.size());
	result.socketWorld.assign(context.controlRestWorld.size(), MMatrix::identity);
	result.solved = true;
	return result;
}

ModuleInterface ReverseFootModuleNode::makeInterface()
{
	ModuleInterface moduleInterface;
	// Neither plug has a control, so a template constraint on either is an error (author, 2026-11-24). The ankle plug takes the Limb's end
	// socket, which follows ikTarget, so it must not be seen by the ankle socket that feeds it.
	moduleInterface.childPlugs.push_back(ModulePlugInfo{ PlugAndSocketNames::Ik, false, false });
	moduleInterface.childPlugs.push_back(ModulePlugInfo{ PlugAndSocketNames::Ankle, false, false, "", "", false, { PlugAndSocketNames::Ankle } });
	moduleInterface.sockets.push_back(PlugAndSocketNames::Ankle);
	return moduleInterface;
}

RigModuleBuildResult ReverseFootModuleNode::createModule(
	const MString& moduleName,
	const Side& side,
	const ShapeType& shapeType,
	const Plan& plan,
	MObject& oRigRoot,
	MDagModifier& dagMod)
{
	RigModuleBuildResult result;
	MStatus status;

	// 1. Module node and its stored rest data.
	MObject oModule = dagMod.MDGModifier::createNode(ReverseFootModuleNode::id);
	dagMod.renameNode(oModule, moduleName);
	dagMod.newPlugValueShort(MPlug(oModule, aSide), getShortFromSide(side));
	result.moduleNode = oModule;

	for (std::size_t i = 0; i < plan.chain.size(); ++i)
	{
		const unsigned int index = static_cast<unsigned int>(i);
		dagMod.newPlugValueInt(MPlug(oModule, aPivotRoles).elementByLogicalIndex(index), static_cast<int>(plan.chain[i].role));
		MPlug pPosition = MPlug(oModule, aPivotPositions).elementByLogicalIndex(index);
		DagUtils::setMatrixPlug(pPosition, translationMatrix(plan.chain[i].position), dagMod);
	}
	MPlug pAnkleRest(oModule, aAnkleRestWorld);
	DagUtils::setMatrixPlug(pAnkleRest, plan.mAnkleRestWorld, dagMod);
	dagMod.newPlugValueString(MPlug(oModule, aIkFkDriver), plan.ikFkDriver.c_str());

	// 2. The foot control: derived from the ball marker, with no joint (D57), world aligned.
	const MObject oControl = RigControlNode::createRigControl(dagMod, MString(plan.controlUsage.c_str()), shapeType, side, plan.controlMarkup, MEulerRotation::kXYZ);
	status = wireControl(oModule, oControl, ReverseFootSlot::Foot, plan.mFootControlRest, dagMod);
	CHECK_MSTATUS_AND_RETURN(status, result);

	// 3. The ankle socket (no DAG transform: nothing hangs from it by DAG) and the two plugs, neither with a control.
	RigModuleSocketInfo ankleSocket;
	ankleSocket.mRestWorld = plan.mAnkleRestWorld;
	ankleSocket.outputIndex = ReverseFootSlot::AnkleSocket;
	result.parentSockets[PlugAndSocketNames::Ankle] = ankleSocket;
	DagPlugAndSocketUtils::setParentSocketNames(oModule, ReverseFootSlot::AnkleSocket, { PlugAndSocketNames::Ankle }, dagMod);
	result.childPlugs[PlugAndSocketNames::Ik] = RigModuleChildPlugInfo{ MObject::kNullObj, ReverseFootSlot::IkPlug };
	DagPlugAndSocketUtils::setChildPlugName(oModule, ReverseFootSlot::IkPlug, PlugAndSocketNames::Ik, dagMod);
	result.childPlugs[PlugAndSocketNames::Ankle] = RigModuleChildPlugInfo{ MObject::kNullObj, kAnklePlug };

	// 4. The channels, all on the foot control. The control must exist before an attribute can be added to it, and the attribute before it
	// can be wired, so the build runs the modifier between the steps, as TwoBoneIk does.
	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN(status, result);
	for (const ChannelSpec& spec : kChannels)
	{
		status = RigPoseScene::addAngleChannel(oControl, spec.name, spec.negate, dagMod, spec.restDegrees);
		CHECK_MSTATUS_AND_RETURN(status, result);
	}
	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN(status, result);
	for (std::size_t k = 0; k < kChannels.size(); ++k)
	{
		const MPlug pControlAttr = MFnDependencyNode(oControl).findPlug(kChannels[k].name, false, &status);
		CHECK_MSTATUS_AND_RETURN(status, result);
		status = dagMod.connect(pControlAttr, MPlug(oModule, aChannels[k]));
		CHECK_MSTATUS_AND_RETURN(status, result);
	}
	// The control carries channels only: its transform is locked and hidden.
	status = DagUtils::queueLockAndHideTransform(oControl, dagMod);
	CHECK_MSTATUS_AND_RETURN(status, result);

	// The claimed toe joints are not driven yet. Freeze each at its rest with nothing inherited, which is what wiring it does first.
	for (std::size_t i = 0; i < 2; ++i)
	{
		MObject oToeJoint = plan.oToeJoints[i];
		DagUtils::setInheritsTransform(oToeJoint, false, dagMod);
		DagUtils::setSegmentScaleCompensate(oToeJoint, false, dagMod);
		status = DagUtils::bakeJointTransformsToOPM(oToeJoint, plan.mToeJointRests[i], dagMod);
		CHECK_MSTATUS_AND_RETURN(status, result);
	}

	// 5. Network
	if (!oRigRoot.isNull())
	{
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dagMod, oModule, aRigRoot);
	}

	result.dagParentCallback = [](const RigModuleDagReparentArgs& args)
	{
		// The ankle plug is a matrix connection only: it has no control to reparent, and nothing reads it in stage 1.
		if (args.childPlug.plugIndex == kAnklePlug)
		{
			MStatus plugStatus;
			const MPlug pSocket = ReverseFootModuleNode::getSocketMatrixPlug(args.oParentTargetModuleNode, args.parentSocket.outputIndex, &plugStatus);
			CHECK_MSTATUS_AND_RETURN_IT(plugStatus);
			MPlug pRest(args.oChildModuleNode, ReverseFootModuleNode::aAnklePlugRestMatrix);
			DagUtils::setMatrixPlug(pRest, args.parentSocket.mRestWorld, args.dagMod);
			plugStatus = args.dagMod.connect(pSocket, MPlug(args.oChildModuleNode, ReverseFootModuleNode::aAnklePlugMatrix));
			CHECK_MSTATUS_AND_RETURN_IT(plugStatus);
			return args.dagMod.newPlugValueBool(MPlug(args.oChildModuleNode, ReverseFootModuleNode::aAnklePlugConnected), true);
		}
		return ReverseFootModuleNode::reparentChildPlugToSocket(args);
	};
	result.status = MStatus::kSuccess;
	return result;
}

bool ReverseFootModuleNode::planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan)
{
	ReverseFootArgs footArgs;
	if (!readModuleArgs(args, { "joints", "reverseHierarchy", "ikFkDriver" }, ctx, footArgs, { "controlName" }))
	{
		return false;
	}

	const std::string argsPath = ctx.where + "." + TemplateKey::ModuleArgs;
	if (footArgs.joints.size() != 3)
	{
		ctx.report.addError(argsPath + ".joints", "needs three joints, the ankle, the toe and the toe tip, got " + std::to_string(footArgs.joints.size()));
		return false;
	}
	bool valid = true;

	// The roles come from the markers' usages. Every problem is reported, so one attempt lists them all.
	std::vector<FootSolve::Role> roles;
	std::vector<JointSelector> roleSelectors;
	for (const JointSelector& selector : footArgs.reverseHierarchy)
	{
		FootSolve::Role role;
		if (!FootSolve::findRole(selector.usage, role))
		{
			ctx.report.addError(argsPath + ".reverseHierarchy", formatJointSelector(selector) + " is not a pivot role. The allowed usages are " + FootSolve::allowedRoles());
			valid = false;
			continue;
		}
		roles.push_back(role);
		roleSelectors.push_back(selector);
	}
	for (const std::string& problem : FootSolve::checkRoles(roles))
	{
		ctx.report.addError(argsPath + ".reverseHierarchy", problem);
		valid = false;
	}
	if (std::find(roles.begin(), roles.end(), FootSolve::Role::Ball) == roles.end())
	{
		ctx.report.addError(argsPath + ".reverseHierarchy", "needs a ball pivot: the foot control is placed at it and the foot rolls over it");
		valid = false;
	}

	// The pivots' rest positions, from the markers. Every missing marker or joint is reported.
	Plan plan;
	for (std::size_t i = 0; i < roles.size(); ++i)
	{
		ResolvedMarker marker;
		if (!resolveMarker(ctx, roleSelectors[i], marker))
		{
			valid = false;
			continue;
		}
		plan.chain.push_back(FootSolve::Pivot{ roles[i], { marker.world[12], marker.world[13], marker.world[14] } });
	}

	// The foot drives the toe and the toe tip. The ankle is a reference only: the Limb drives it, and a joint two modules drive is an error.
	if (!findJointRest(ctx, footArgs.joints[0], plan.mAnkleRestWorld))
	{
		valid = false;
	}
	for (std::size_t i = 0; i < 2; ++i)
	{
		valid = claimJoint(ctx, footArgs.joints[i + 1], plan.oToeJoints[i], plan.mToeJointRests[i]) && valid;
	}

	if (footArgs.ikFkDriver.empty())
	{
		ctx.report.addError(argsPath + ".ikFkDriver", "must name the Limb or TwoBoneIk module whose ikFkBlend the foot follows");
		valid = false;
	}
	NamingConvention::Name controlName;
	if (!NamingConvention::fromUsage(footArgs.controlName, ctx.moduleData.side, NamingConvention::Kind::Control, controlName))
	{
		ctx.report.addError(argsPath + ".controlName", "'" + footArgs.controlName + "' must be camelCase, with an optional _NN: it names the foot control");
		valid = false;
	}
	if (!valid)
	{
		return false;
	}

	// A mirrored foot follows the other side's limb (D28). The selectors' sides were swapped when the template expanded.
	plan.ikFkDriver = footArgs.ikFkDriver;
	if (ctx.moduleData.isMirror())
	{
		const std::string mirrored = NamingConvention::swapSide(footArgs.ikFkDriver);
		plan.ikFkDriver = mirrored.empty() ? footArgs.ikFkDriver : mirrored;
	}

	const int ballIndex = FootSolve::findPivot(plan.chain, FootSolve::Role::Ball);
	plan.mFootControlRest = translationMatrix(plan.chain[static_cast<std::size_t>(ballIndex)].position);
	plan.controlUsage = footArgs.controlName;
	plan.controlMarkup = makeControlMarkup(ctx.moduleData.side, footArgs.controlName);

	outPlan.moduleInterface = makeInterface();
	outPlan.moduleInterface.controls.push_back(plan.controlMarkup);

	const MString moduleName = ctx.moduleData.name.c_str();
	const Side side = getSideFromString(ctx.moduleData.side);
	const ShapeType shapeType = getShapeTypeFromString(ctx.moduleData.shapeType);
	outPlan.build = [moduleName, side, shapeType, plan](MObject& oRigRoot, MDagModifier& dagMod)
	{
		return ReverseFootModuleNode::createModule(moduleName, side, shapeType, plan, oRigRoot, dagMod);
	};
	return true;
}
