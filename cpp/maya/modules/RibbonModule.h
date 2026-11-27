#pragma once

#include "core/RibbonMath.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RibbonSolve.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/Side.h"

#include <array>
#include <string>
#include <vector>

#include <maya/MArrayDataHandle.h>
#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MString.h>
#include <maya/MTypeId.h>

// One ribbon control or bind joint, resolved by the plan step.
struct RibbonSlotInput
{
	MObject oJoint;                // Null for an ik control: it has no joint, its rest is derived from the bind joints (D57)
	MMatrix mRestWorld;
	ControlMarkup controlMarkup;
	std::string baseName;          // What names an ik control's node (its usage, as it has no joint). Empty for a slot with a joint
};

// "joints": the bind joints, root to tip (2 to 64). "controlCount" (optional, 2 to 8, default 3): how many ik controls, spread
// evenly along the chain (D57). "controlName" (optional): the base of the control usages, <base>IK_<nn> and <base>Tweak_<nn>,
// by default the first bind joint's usage without its trailing _NN. There are no control joints: the controls' rests come from
// the bind joints (core/ControlPlacement).
// "fkStyle" (optional, default false, D59): adds an fk chain of "fkControlCount" controls (2 to 8, default controlCount) spread the
// same way, <base>FK_<nn>. The base curve passes through the chain and every ik control floats on it. The chain follows the start
// plug, so a fkStyle ribbon has no end child plug. "fkControlCount" without fkStyle is an error.
// "startAimBlend" and "endAimBlend" (optional, 0 to 1, default 0): the default, and so the rest (D34), of the channels of the same
// names, for a ribbon whose ends should aim down the chain on every build (a shoulder or thigh, design/ribbon-twist-follow.md).
// "twistRamp" (optional, needs fkStyle or anchorJoints, D75): [position, share] pairs, positions strictly increasing in 0 to 1 along the
// ribbon and shares 0 to 1. Each ik control takes that share of the fk chain's roll at its place, linear between the pairs. Left out, every
// share is 1.
//
// The anchored form (D80, design/limb-split.md section 3.2), chosen by "anchorJoints": [[side, usage] x 3], the start, mid and end
// anchors, read and not claimed. The curve passes through the three frames its start, mid and end child plugs carry (a TwoBoneIk's
// index_0 to index_2), with a corner at the mid. "joints" are then 5 to 64, the first at the start anchor, the last at the end anchor and
// exactly one at the mid. "controlCount" (1 to 6, default 3) counts the interior ik controls, the first at the mid anchor: the curve's
// ends are the anchors, so they have none, and the bind joints' curve is drawn through the controls, so the mid always has one. "startAnchorRotation" ("full" or "swing", default "full"), "endAnchorRotation" ("full" or
// "twist", default "full") and "bendSoftness" (0 to 1, default 1, the rest of the channel of that name) are only for this form: a
// limb sets "swing", "twist" and 0. fkStyle, fkControlCount and the aim blends are errors in it. Its channels go on the control behind
// its settings plug.
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
	std::vector<JointSelector> anchorJoints;
	std::string startAnchorRotation{ "full" };
	std::string endAnchorRotation{ "full" };
	double bendSoftness{ 1.0 };
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RibbonArgs, joints, controlCount, controlName, startAimBlend, endAimBlend, fkStyle, fkControlCount, twistRamp,
	anchorJoints, startAnchorRotation, endAnchorRotation, bendSoftness)

// The anchored form's resolved anchors (D80), from the plan step.
struct RibbonAnchors
{
	std::array<MMatrix, RibbonSolve::AnchorCount> mRest;   // the anchor joints' rest worlds: start, mid, end
	std::array<std::string, RibbonSolve::AnchorCount> jointNames;   // as "[side, usage]", for the build's messages
	short startRotation{ AnchorRotation::Full };
	short endRotation{ AnchorRotation::Full };
	double tolerance{ 0.0 };   // how far a plug's socket may sit from its anchor joint at rest: a share of the anchors' length
};

// COUPLED TO Limb (D62, design/limb.md): LimbNode runs this module's solve, channels, control derivation and inverse. A change here
// changes Limb. Read design/limb.md and run test_limb_matches_parts before changing any of them.
//
// A virtual ribbon (D56, design/ribbon.md): a few ik controls shape a curve and every bind joint rides it through a
// hidden tweaker control. Slots (the logical index controlData, controlOPM, jointOPM, outputSocketMatrix and childPlugs share):
// 0 to J-1 are the ik controls, J to J+n-1 are the tweakers. Child plugs: start (slot 0) and end (slot J-1). Sockets: index_k
// for ik control k, with start and end on the first and last. The channels live on the first control. With fkStyle (D59) the fk
// controls are the last slots, after the tweakers, with no socket, and there is no end child plug.
//
// Anchored (D80): 0 to k-1 are the interior ik controls, k to k+n-1 the tweakers, and the child plugs start, mid, end and settings are
// the four elements after them, with no control. ikControlCount is k + 2, the curve's controls with its two ends, which have no slot.
// Sockets: index_<j> for interior ik control j, and bind_<i>. The channels are hosted on the settings plug's control.
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
	static MObject aFkRollShare;     // double array, one per ik control, default 1: the template's twistRamp at each (D75)
	// The anchored form (D80): the anchors' rests (none on a plain ribbon, AnchorCount on an anchored one) and their rotations.
	static MObject aAnchorRest;
	static MObject aStartAnchorRotation;
	static MObject aEndAnchorRotation;

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
	// The anchored form (D80). controls are the interior ik controls.
	static RigModuleBuildResult createAnchoredModule(
		const MString& moduleName,
		const Side& side,
		const ShapeType& shapeType,
		const std::vector<RibbonSlotInput>& controls,
		const std::vector<RibbonSlotInput>& joints,
		const RibbonMath::Setup& setup,
		const RibbonAnchors& anchors,
		const std::array<double, RibbonChannel::Count>& channelDefaults,
		MObject& oRigRoot,
		MDagModifier& dagMod
	);
	static bool planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan);
	// moduleArgs version 2 (D57) dropped "controls" for "controlCount".
	static const UpdateTable& argUpdateTable();
	// Control values from the joints (D56 section 7, D57): the end controls from the first and last bind joints, whose frames
	// they share, and the tweakers from the bind joints, the residual. An interior ik control has no joint, so it keeps its
	// fallback value and the tweakers absorb the difference, so every bind joint that is present comes back exactly.
	static ModuleInverseResult inverse(const ModuleInverseContext& context, const ModuleInverseInput& input);
	// The channels the inverse reads, in RibbonChannel order. An anchored ribbon's are on its host control, which RigInverse finds from the
	// channel's connection.
	static std::vector<ModuleInverseChannel> inverseChannels();

private:
	static constexpr unsigned int MaxSlots{ RibbonSolve::MaxSlots };
	static MStatus computeAnchored(const MPlug& plug, MDataBlock& data, MArrayDataHandle& hAnchorRest);
	// Limb's inverse steps 2 and 3, moved (design/limb-split.md section 5): the anchors from the plugs' solved parents, the setup rebuilt
	// from the stored rests, the tweakers from the bind joints.
	static ModuleInverseResult anchoredInverse(const ModuleInverseContext& context, const ModuleInverseInput& input);
	static bool planAnchored(const RibbonArgs& ribbonArgs, const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan);
	// Pass 2 for the settings plug: adds the anchored channels to the host control and connects them to the node (design/limb-split.md
	// section 3.5). A name the control already has is an error, which is a second ribbon on one settings plug or a name the host's module uses.
	static MStatus hostChannels(const MObject& oModule, const MObject& oHost, const std::array<double, RibbonChannel::Count>& channelDefaults, MDagModifier& dagMod);
	// The stored rest, by this node's attributes, for the shared reads and writes.
	static RibbonSolve::RestAttrs restAttrs();
};
