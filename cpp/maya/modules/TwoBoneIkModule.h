#pragma once

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
#include <maya/MVector.h>

// Index shared by controlData, controlOPM, jointOPM, outputSocketMatrix, childPlugs and socketNames (D52 section 3.2).
// Slot numbers are never reused.
namespace TwoBoneIkSlot
{
	inline constexpr unsigned int Upper{ 0 };      // FK start control, drives joints[0]
	inline constexpr unsigned int Lower{ 1 };      // FK, drives joints[1]
	inline constexpr unsigned int End{ 2 };        // FK, drives joints[2]
	inline constexpr unsigned int Ik{ 3 };         // IK handle, at the end joint, no joint of its own
	inline constexpr unsigned int Pole{ 4 };       // pole, no joint
	inline constexpr unsigned int Settings{ 5 };   // settings (ikFkBlend, stretch, soft), no joint
	inline constexpr unsigned int Count{ 6 };
	inline constexpr unsigned int LimbCount{ 3 };  // the slots that are limb joints
}

// The order of the scalar channels the inverse reads (inverseChannels), which is the order of ModuleInverseInput::channels.
// Append only: the order is not a name, but the inverse indexes by it.
namespace TwoBoneIkInverseChannel
{
	inline constexpr unsigned int Soft{ 0 };
	inline constexpr unsigned int Stretch{ 1 };
	inline constexpr unsigned int Pin{ 2 };
	inline constexpr unsigned int Roll{ 3 };
	inline constexpr unsigned int Space{ 4 };
	inline constexpr unsigned int IkFkBlend{ 5 };
	inline constexpr unsigned int Count{ 6 };
}

struct TwoBoneIkSlotInput
{
	MObject oJoint;                // Null for the IK, pole and settings slots: they have no joint (D57)
	MMatrix mRestWorld;
	ControlMarkup controlMarkup;   // The joint's (side, usage), or the derived control's
	std::string baseName;          // What names a joint-less control's node (its usage). Empty for a slot with a joint
};

// What the plan step works out from the rest pose, stored on the node by createModule (D52 section 4.3).
struct TwoBoneIkRest
{
	double length1 = 0.0;           // root to mid, and mid to end
	double length2 = 0.0;
	double restPlaneAngle = 0.0;    // radians. See TwoBoneIkInput::restPlaneAngle.
	MVector restBendNormal;
	std::array<MMatrix, TwoBoneIkSlot::LimbCount> ikRestOffset;   // jointRest * frameAtRest^-1, per limb joint
};

// Field names are the JSON keys: "joints": [[side, usage] x 3] (root, mid, end). The IK handle, pole and settings controls are
// derived from them (D57). "controlName" (optional): the base of their usages, <base>IK, <base>Pole and <base>Settings.
// "poleOffset" and "settingsOffset" (optional, [x, y, z] in world space, left out or empty for the default): the pole's offset from
// the mid joint and the settings control's from the end joint. The other side of a mirrored module gets X negated.
struct TwoBoneIkArgs
{
	std::vector<JointSelector> joints;
	std::string controlName;
	std::vector<double> poleOffset;
	std::vector<double> settingsOffset;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TwoBoneIkArgs, joints, controlName, poleOffset, settingsOffset)

// A two-bone IK limb (arm, leg): an FK chain and an IK solve, blended joint by joint (D52). Child plugs: start, ik, pole,
// and settings (internal: it follows start's parent). Sockets: index_0, index_1, index_2 (the blended joint worlds), start,
// end, ik, pole, settings.
class TwoBoneIkNode : public RigModuleWithJointsNode<TwoBoneIkNode>
{
public:
	TwoBoneIkNode();
	virtual ~TwoBoneIkNode() override;

	static void* creator();
	static MStatus initialize();
	virtual MStatus compute(const MPlug& plug, MDataBlock& data) override;
	// The moduleArgs version table. Step 0 takes version 1 (ik, pole and settings joints) to 2 (derived controls, D57).
	static const UpdateTable& argUpdateTable();

public:
	static MTypeId id;

	// The channels, read from the owning controls (RigAttr). Blend, stretch, soft and pin are clamped to 0..1.
	static MObject aIkFkBlend;
	static MObject aStretch;
	static MObject aSoft;
	static MObject aPin;
	static MObject aRoll;        // an angle
	static MObject aPoleSpace;   // PoleSpaceLabels: 0 Parent, 1 Ik

	// Stored at build (section 3.4).
	static MObject aIkRestOffset;      // matrix array, one per limb joint
	static MObject aRestBendNormal;
	static MObject aRestPlaneAngle;
	static MObject aLimbLengths;

	// 1 while the controls they drive are visible: FK lower and end below blend 1, the IK handle and pole above blend 0.
	static MObject aFkVisible;
	static MObject aIkVisible;

	// Match outputs (D52 section 4.1), pulled only by setRigIkFkBlend, never during playback: the control locals that
	// reproduce the current pose on each side, by slot (fkMatchLocal 0 to 2, ikMatchLocal 3 and 4), and what the match
	// can't do, a JSON list of messages (JsonUtils). Not writable, not storable, connected to nothing.
	static MObject aFkMatchLocal;
	static MObject aIkMatchLocal;
	static MObject aMatchWarnings;

public:
	static RigModuleBuildResult createModule(
		const MString& moduleName,
		const Side& side,
		const ShapeType& shapeType,
		const std::array<TwoBoneIkSlotInput, TwoBoneIkSlot::Count>& slots,
		const TwoBoneIkRest& rest,
		MObject& oRigRoot,
		MDagModifier& dagMod
	);
	static bool planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan);
	// Control values from the limb joints (D52 section 6): the FK controls from the joint worlds, the IK handle and pole
	// from the observed positions through MathUtils::computeTwoBoneIkInverse, for the channels in inverseChannels().
	// The sockets are the forward evaluation of those controls. Not solved when a limb joint is missing.
	static ModuleInverseResult inverse(const ModuleInverseContext& context, const ModuleInverseInput& input);
	// The scalar channels the inverse reads, in TwoBoneIkInverseChannel order.
	static std::vector<ModuleInverseChannel> inverseChannels();
	// The pole's space deltas (ModuleSpaceDeltaFn): Parent is the pole child plug's parent motion, Ik is the IK handle's
	// motion since rest. Slot Pole only.
	static MStatus spaceDelta(const MObject& oModule, unsigned int slot, unsigned int label, MMatrix& outDelta);

private:
	struct TwoBoneIkSolveResult
	{
		MMatrix mControlOPM[TwoBoneIkSlot::Count];   // Control rest carried by parent motion (no local)
		MMatrix mJointWorld[TwoBoneIkSlot::Count];   // Slots 0 to 2 are the blended limb joints, the rest are the controls' worlds
	};

	// What one evaluation reads, as plain values: compute() fills it from plugs and the inverse from its input.
	struct TwoBoneIkEval
	{
		MMatrix mLocal[TwoBoneIkSlot::Count];
		MMatrix mRest[TwoBoneIkSlot::Count];
		MMatrix mUpperDelta;         // the child plugs' parent motion
		MMatrix mIkDelta;
		MMatrix mPoleParentDelta;    // used when the pole's space is Parent
		MMatrix mSettingsDelta;
		bool poleFollowsIk = false;
		double blend = 1.0;
		double stretch = 0.0;
		double soft = 0.0;
		double pin = 0.0;
		double rollRadians = 0.0;
		TwoBoneIkRest rest;          // not read at blend 0
	};

	// Reads the limb's plugs. The channels are read at every blend (the match outputs need them at 0); the IK rest data
	// only above blend 0, where evaluate() uses it.
	static MStatus readEval(MDataBlock& data, TwoBoneIkEval& outEval);
	// The forward evaluation, shared by compute() and the inverse so the sockets a restore returns are compute()'s.
	static void evaluate(const TwoBoneIkEval& eval, TwoBoneIkSolveResult& outResult);
	// The match outputs' branch of compute(): the inverse run on the current evaluation. Allocates.
	static MStatus computeMatch(const MPlug& plug, MDataBlock& data);
};
