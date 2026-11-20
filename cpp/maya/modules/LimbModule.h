#pragma once

#include "core/RibbonMath.h"
#include "maya/modules/RibbonModule.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RibbonSolve.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/Side.h"
#include "maya/shared/TwoBoneIkSolve.h"

#include <array>
#include <string>
#include <vector>

#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MString.h>
#include <maya/MTypeId.h>

// The slots of a Limb: the two-bone limb's six (TwoBoneIkSlot), then the ribbon's ik controls, then the tweakers. The logical index
// controlData, controlOPM, jointOPM and outputSocketMatrix share. Slot numbers are never reused.
namespace LimbSlot
{
	inline constexpr unsigned int FirstRibbon{ TwoBoneIkSlot::Count };
}

// The order of the ribbon channels on the settings control, which is the order of LimbNode::aChannels and of limbChannelSpecs():
// the Ribbon's, less its aim blends and its end handle scale, which a limb's joints replace, plus the elbow's softness. Append only.
namespace LimbChannel
{
	inline constexpr unsigned int StartTwist{ 0 };
	inline constexpr unsigned int EndTwist{ 1 };
	inline constexpr unsigned int SineAmplitude{ 2 };
	inline constexpr unsigned int SineFrequency{ 3 };
	inline constexpr unsigned int SinePhase{ 4 };
	inline constexpr unsigned int SineOrient{ 5 };
	inline constexpr unsigned int SineStartFalloff{ 6 };
	inline constexpr unsigned int SineEndFalloff{ 7 };
	inline constexpr unsigned int VolumePreserve{ 8 };
	inline constexpr unsigned int ScaleStart{ 9 };
	inline constexpr unsigned int ScaleMid{ 10 };
	inline constexpr unsigned int ScaleEnd{ 11 };
	inline constexpr unsigned int BendSoftness{ 12 };
	inline constexpr unsigned int Count{ 13 };
}

// "joints": the limb's three joints, [[side, usage] x 3] (root, mid, end), none of them skin joints, as TwoBoneIk's. "bindJoints":
// the skin joints, 5 to 64, root to tip, all leaves: the first sits at the root joint, the last at the end joint, and exactly one at
// the mid joint. "controlCount" (optional, 3 to 8, default 5): the ribbon's ik controls, the first at the root, one at the mid joint,
// one at the end, the rest spread between. The other keys are TwoBoneIk's (controlName, poleOffset, settingsOffset, ikFkBlend,
// ikOrientToWorld, ikAlignToPole). There is no fk ribbon. design/limb.md section 4.1.
struct LimbArgs
{
	std::vector<JointSelector> joints;
	std::vector<JointSelector> bindJoints;
	int controlCount{ 5 };
	std::string controlName;
	std::vector<double> poleOffset;
	std::vector<double> settingsOffset;
	double ikFkBlend{ 1.0 };
	bool ikOrientToWorld{ false };
	bool ikAlignToPole{ false };
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LimbArgs, joints, bindJoints, controlCount, controlName, poleOffset, settingsOffset, ikFkBlend, ikOrientToWorld, ikAlignToPole)

// COUPLED TO TwoBoneIk AND Ribbon (D62, design/limb.md): this node runs their solves (TwoBoneIkSolve, RibbonSolve and core/RibbonMath),
// the TwoBoneIk half's attributes, plug reads and build (TwoBoneIkPart) and the Ribbon's channels and stored rest. A change to any of
// them changes Limb: read design/limb.md and run test_limb_matches_parts before making it.
//
// A limb with one continuous ribbon (arm, leg): the two-bone IK/FK solve blends the start, mid and end joints, and a single curve
// passes through them with the ribbon's ik controls on it and a tweaker for every bind joint. The ribbon's ends are the start joint's
// swing and the end joint's twist on the mid joint's swing (the Swing and Twist constraints' frames, D31), so the shoulder's and
// wrist's twist eases along the bones. Child plugs and sockets are TwoBoneIk's: child plugs start, ik, pole and settings (internal),
// sockets index_0, index_1, index_2, start, end, ik, pole and settings. Slots: LimbSlot. The channels are on the settings control.
class LimbNode : public RigModuleWithJointsNode<LimbNode>
{
public:
	LimbNode();
	virtual ~LimbNode() override;

	static void* creator();
	static MStatus initialize();
	virtual MStatus compute(const MPlug& plug, MDataBlock& data) override;

public:
	static MTypeId id;

	// The two-bone half's inputs (TwoBoneIkPart): read from the owning controls, blend, stretch, soft and pin clamped to 0..1.
	static MObject aIkFkBlend;
	static MObject aStretch;
	static MObject aSoft;
	static MObject aPin;
	static MObject aRoll;
	static MObject aPoleSpace;
	static MObject aIkRestOffset;
	static MObject aRestBendNormal;
	static MObject aRestPlaneAngle;
	static MObject aLimbLengths;
	static MObject aFkVisible;
	static MObject aIkVisible;

	// The match outputs (TwoBoneIkPart), pulled only by setRigIkFkBlend: the two-bone limb's control locals on each side and the warnings.
	static MObject aFkMatchLocal;
	static MObject aIkMatchLocal;
	static MObject aMatchWarnings;

	// The ribbon's channels, read from the settings control (LimbChannel order). Angles are in radians.
	static std::array<MObject, LimbChannel::Count> aChannels;

	// The ribbon's rest, stored at build (RibbonSolve::RestAttrs). The ik controls' offsets are apart from the two-bone limb's.
	static MObject aIkControlCount;
	static MObject aKnotU;
	static MObject aBindU;
	static MObject aRestLength;
	static MObject aChainSign;
	static MObject aRestUp;
	static MObject aRestTangent;
	static MObject aRibbonIkRestOffset;
	static MObject aBindRestOffset;
	static MObject aFkRollRest;
	static MObject aFkRollRestIn;

public:
	static RigModuleBuildResult createModule(
		const MString& moduleName,
		const Side& side,
		const ShapeType& shapeType,
		const std::array<TwoBoneIkSlotInput, TwoBoneIkSlot::Count>& slots,
		const TwoBoneIkRest& rest,
		const std::vector<RibbonSlotInput>& ribbonControls,
		const std::vector<RibbonSlotInput>& bindJoints,
		const RibbonMath::Setup& setup,
		double ikFkBlend,
		MObject& oRigRoot,
		MDagModifier& dagMod);
	static bool planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan);
	// The two-bone inverse on the first six slots, then the tweakers from the bind joints against the ribbon the blended joints give
	// (design/limb.md section 7). The ribbon's ik controls have no joint and keep the sidecar's value or the rest. The sockets are the
	// forward joints (slots 0 to 5).
	static ModuleInverseResult inverse(const ModuleInverseContext& context, const ModuleInverseInput& input);
	// The two-bone channels, then the ribbon's in LimbChannel order (all read from the settings control).
	static std::vector<ModuleInverseChannel> inverseChannels();

private:
	static RibbonSolve::RestAttrs restAttrs();
};
