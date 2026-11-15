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
	inline constexpr unsigned int Ik{ 3 };         // IK handle, drives the ik marker joint
	inline constexpr unsigned int Pole{ 4 };       // pole, drives the pole marker joint
	inline constexpr unsigned int Settings{ 5 };   // settings (ikFkBlend, stretch, soft), drives the settings marker joint
	inline constexpr unsigned int Count{ 6 };
	inline constexpr unsigned int LimbCount{ 3 };  // the slots that are limb joints
}

struct TwoBoneIkSlotInput
{
	MObject oJoint;
	MMatrix mRestWorld;
	ControlMarkup controlMarkup;   // The joint's (side, usage)
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

// Field names are the JSON keys: "joints": [[side, usage] x 3], "ik": [...], "pole": [...], "settings": [...]
struct TwoBoneIkArgs
{
	std::vector<JointSelector> joints;
	JointSelector ik;
	JointSelector pole;
	JointSelector settings;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TwoBoneIkArgs, joints, ik, pole, settings)

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
	// Not built yet (D52 phase 5): reports solved = false with the reason, and returns each slot's control step so a restore's
	// sweep still has a socket for every slot.
	static ModuleInverseResult inverse(const ModuleInverseContext& context, const ModuleInverseInput& input);

private:
	struct TwoBoneIkSolveResult
	{
		MMatrix mControlOPM[TwoBoneIkSlot::Count];   // Control rest carried by parent motion (no local)
		MMatrix mJointWorld[TwoBoneIkSlot::Count];   // Slots 0 to 2 are the blended limb joints, the rest are the controls' worlds
	};

	static MStatus solve(MDataBlock& data, TwoBoneIkSolveResult& outResult);
};
