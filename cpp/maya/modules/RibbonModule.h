#pragma once

#include "core/RibbonMath.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RibbonSolve.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/Side.h"

#include <array>
#include <string>
#include <vector>

#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MString.h>
#include <maya/MTypeId.h>

// One ribbon control or bind joint, resolved by the plan step.
struct RibbonSlotInput
{
	MObject oJoint;                // Null for an ik control: it has no joint, its rest is derived from the bind joints
	MMatrix mRestWorld;
	ControlMarkup controlMarkup;
	std::string baseName;          // What names an ik control's node (its usage, as it has no joint). Empty for a slot with a joint
};

// "joints": the bind joints, root to tip (2 to 64). "controlCount" (optional, 2 to 8, default 3): how many ik controls, spread
// evenly along the chain. "controlName" (optional): the base of the control usages, <base>IK_<nn> and <base>Tweak_<nn>,
// by default the first bind joint's usage without its trailing _NN. There are no control joints: the controls' rests come from
// the bind joints (core/ControlPlacement).
// "fkStyle" (optional, default false): adds an fk chain of "fkControlCount" controls (2 to 8, default controlCount) spread the
// same way, <base>FK_<nn>. The base curve passes through the chain and every ik control floats on it. The chain follows the start
// plug, so a fkStyle ribbon has no end child plug. "fkControlCount" without fkStyle is an error.
// "startAimBlend" and "endAimBlend" (optional, 0 to 1, default 0): the default, and so the rest, of the channels of the same
// names, for a ribbon whose ends should aim down the chain on every build (a shoulder or thigh, design/ribbon-twist-follow.md).
// "twistRamp" (optional, needs fkStyle): [position, share] pairs, positions strictly increasing in 0 to 1 along the ribbon and shares
// 0 to 1. Each ik control takes that share of the fk chain's roll at its place, linear between the pairs. Left out, every share is 1.
struct RibbonArgs
{
	std::vector<JointSelector> joints;
	int controlCount{ 3 };
	std::string controlName;
	double startAimBlend{ 0.0 };
	double endAimBlend{ 0.0 };
	bool fkStyle{ false };
	int fkControlCount{ -1 };
	RibbonMath::TwistRamp twistRamp;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RibbonArgs, joints, controlCount, controlName, startAimBlend, endAimBlend, fkStyle, fkControlCount, twistRamp)

// COUPLED TO Limb (design/limb.md): LimbNode runs this module's solve, channels, control derivation and inverse. A change here
// changes Limb. Read design/limb.md and run test_limb_matches_parts before changing any of them.
//
// A virtual ribbon (design/ribbon.md): a few ik controls shape a curve and every bind joint rides it through a
// hidden tweaker control. Slots (the logical index controlData, controlOPM, jointOPM, outputSocketMatrix and childPlugs share):
// 0 to J-1 are the ik controls, J to J+n-1 are the tweakers. Child plugs: start (slot 0) and end (slot J-1). Sockets: index_k
// for ik control k, with start and end on the first and last. The channels live on the first control. With fkStyle the fk
// controls are the last slots, after the tweakers, with no socket, and there is no end child plug.
class RibbonModuleNode : public RigModuleWithJointsNode<RibbonModuleNode>
{
public:
	RibbonModuleNode();
	virtual ~RibbonModuleNode() override;

	static void* creator();
	static MStatus initialize();
	virtual MStatus compute(const MPlug& plug, MDataBlock& data) override;

public:
	static MTypeId id;

	// The channels, read from the first control (RibbonChannel order). Angles are in radians.
	static std::array<MObject, RibbonChannel::Count> aChannels;

	// Stored at build (RibbonMath::Setup).
	static MObject aIkControlCount;
	static MObject aFkControlCount;
	static MObject aKnotU;           // double array, one per ik control
	static MObject aBindU;           // double array, one per bind joint
	static MObject aRestLength;
	static MObject aChainSign;       // +1 or -1: whether the controls' X axes run down the chain (RibbonMath::Setup::chainSign)
	static MObject aRestUp;
	static MObject aRestTangent;
	static MObject aIkRestOffset;    // matrix array, one per ik control (the interior ones are used)
	static MObject aBindRestOffset;  // matrix array, one per bind joint
	static MObject aFkRollRest;      // double array, one per fk control
	static MObject aFkRollRestIn;    // double array, one per fk control
	static MObject aFkRollShare;     // double array, one per ik control, default 1: the template's twistRamp at each

public:
	static RigModuleBuildResult createModule(
		const MString& moduleName,
		const Side& side,
		const ShapeType& shapeType,
		const std::vector<RibbonSlotInput>& controls,
		const std::vector<RibbonSlotInput>& joints,
		const std::vector<RibbonSlotInput>& fkControls,
		const RibbonMath::Setup& setup,
		const std::array<double, RibbonChannel::Count>& channelDefaults,
		MObject& oRigRoot,
		MDagModifier& dagMod
	);
	static bool planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan);
	// moduleArgs version 2 dropped "controls" for "controlCount".
	static const UpdateTable& argUpdateTable();
	// Control values from the joints (design/ribbon.md section 7): the end controls from the first and last bind joints, whose frames
	// they share, and the tweakers from the bind joints, the residual. An interior ik control has no joint, so it keeps its
	// fallback value and the tweakers absorb the difference, so every bind joint that is present comes back exactly.
	static ModuleInverseResult inverse(const ModuleInverseContext& context, const ModuleInverseInput& input);
	// The channels the inverse reads, in RibbonChannel order.
	static std::vector<ModuleInverseChannel> inverseChannels();

private:
	static constexpr unsigned int MaxSlots{ RibbonSolve::MaxSlots };
	// The stored rest, by this node's attributes, for the shared reads and writes.
	static RibbonSolve::RestAttrs restAttrs();
};
