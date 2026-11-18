#pragma once

#include "core/RibbonMath.h"
#include "maya/nodes/RigControlNode.h"
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

// The order of the channels on the first control, which is the order of RibbonModuleNode::aChannels, of
// ribbonChannelSpecs() and of ModuleInverseInput::channels. Append only: the order is not a name, but the node indexes by it.
namespace RibbonChannel
{
	inline constexpr unsigned int StartTwist{ 0 };
	inline constexpr unsigned int EndTwist{ 1 };
	inline constexpr unsigned int SineAmplitude{ 2 };
	inline constexpr unsigned int SineFrequency{ 3 };
	inline constexpr unsigned int SinePhase{ 4 };
	inline constexpr unsigned int SineOrient{ 5 };
	inline constexpr unsigned int SineStartFalloff{ 6 };
	inline constexpr unsigned int SineEndFalloff{ 7 };
	inline constexpr unsigned int StartAimBlend{ 8 };
	inline constexpr unsigned int EndAimBlend{ 9 };
	inline constexpr unsigned int BendRoundness{ 10 };
	inline constexpr unsigned int VolumePreserve{ 11 };
	inline constexpr unsigned int ScaleStart{ 12 };
	inline constexpr unsigned int ScaleMid{ 13 };
	inline constexpr unsigned int ScaleEnd{ 14 };
	inline constexpr unsigned int Count{ 15 };
}

// One ribbon control or bind joint, resolved by the plan step.
struct RibbonSlotInput
{
	MObject oJoint;
	MMatrix mRestWorld;
	ControlMarkup controlMarkup;   // The joint's (side, usage)
};

// "controls": the control joints, root to tip, [[side, usage], ...] (2 to 8). "joints": the bind joints, root to tip (2 to 64).
// The controls' joints are markers, not skinned, and the bind joints are what skins and exports.
// "startAimBlend" and "endAimBlend" (optional, 0 to 1, default 0): the default, and so the rest (D34), of the channels of the same
// names, for a ribbon whose ends should aim down the chain on every build (a shoulder or thigh, design/ribbon-twist-follow.md).
struct RibbonArgs
{
	std::vector<JointSelector> controls;
	std::vector<JointSelector> joints;
	double startAimBlend{ 0.0 };
	double endAimBlend{ 0.0 };
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RibbonArgs, controls, joints, startAimBlend, endAimBlend)

// A virtual ribbon (D56, design/ribbon.md): a few ik controls shape a curve and every bind joint rides it through a
// hidden tweaker control. Slots (the logical index controlData, controlOPM, jointOPM, outputSocketMatrix and childPlugs share):
// 0 to J-1 are the ik controls, J to J+n-1 are the tweakers. Child plugs: start (slot 0) and end (slot J-1). Sockets: index_k
// for ik control k, with start and end on the first and last. The channels live on the first control.
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
	static MObject aKnotU;           // double array, one per ik control
	static MObject aBindU;           // double array, one per bind joint
	static MObject aRestLength;
	static MObject aChainSign;       // +1 or -1: whether the controls' X axes run down the chain (RibbonMath::Setup::chainSign)
	static MObject aRestUp;
	static MObject aRestTangent;
	static MObject aIkRestOffset;    // matrix array, one per ik control (the interior ones are used)
	static MObject aBindRestOffset;  // matrix array, one per bind joint

public:
	static RigModuleBuildResult createModule(
		const MString& moduleName,
		const Side& side,
		const ShapeType& shapeType,
		const std::vector<RibbonSlotInput>& controls,
		const std::vector<RibbonSlotInput>& joints,
		const RibbonMath::Setup& setup,
		const std::array<double, RibbonChannel::Count>& channelDefaults,
		MObject& oRigRoot,
		MDagModifier& dagMod
	);
	static bool planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan);
	// Control values from the joints (D56 section 7): the ik controls from their marker joints and the tweakers from the bind
	// joints, the residual. A control whose joint is missing keeps its fallback value and the tweakers absorb the difference,
	// so every bind joint that is present comes back exactly.
	static ModuleInverseResult inverse(const ModuleInverseContext& context, const ModuleInverseInput& input);
	// The channels the inverse reads, in RibbonChannel order.
	static std::vector<ModuleInverseChannel> inverseChannels();

private:
	static constexpr unsigned int MaxSlots{ RibbonMath::MaxControls + RibbonMath::MaxBindJoints };

	// What one evaluation produces, by slot, in world space.
	struct RibbonSolve
	{
		MMatrix mControlOPM[MaxSlots];   // rest carried by the parent's motion (the ik controls on the curve), no local
		MMatrix mWorld[MaxSlots];        // local * controlOPM
	};

	// The forward evaluation, shared by compute() and the inverse so a restore returns compute()'s sockets. mRest and
	// mLocal are by slot; the start's parent motion carries the whole ribbon and the end's is its own.
	static void evaluate(
		const RibbonMath::Setup& setup,
		const RibbonMath::Params& params,
		const MMatrix* mRest,
		const MMatrix* mLocal,
		const MMatrix& mStartDelta,
		const MMatrix& mEndDelta,
		RibbonSolve& outSolve);
};
