#pragma once

#include "core/ControlMarkup.h"
#include "core/RigJsonStructs.h"
#include "core/RigTemplateValidation.h"
#include "maya/shared/RigModule.h"

#include <array>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MVector.h>

// The two-bone IK/FK solve (D52), as plain values in and out, with no node and no plug. TwoBoneIkNode reads its plugs into a
// TwoBoneIkEval and calls TwoBoneIkSolve::evaluate, the inverse calls it on the values it built, and the Limb module (D62,
// design/limb.md) calls it for its limb joints. The plan step's derived controls and the rest data are here too, for the same
// reason. A change here changes every one of them: read design/limb.md first.

// The analytic solve of the bend (D52 section 4.2) and its inverse (section 6), on positions. Allocation-free.
// Inputs of TwoBoneIkSolve::computeTwoBoneIk. Positions are world space, lengths are the rest bone lengths.
struct TwoBoneIkInput
{
	MVector root;
	MVector handle;
	MVector pole;
	double length1 = 1.0;
	double length2 = 1.0;
	double stretch = 0.0;          // 0 to 1
	double soft = 0.0;             // 0 to 1, a fraction of the limb's reach
	double pin = 0.0;              // 0 to 1
	double rollRadians = 0.0;
	double restPlaneAngle = 0.0;   // radians: the rest mid's direction from the pole's, about the root-to-handle axis
	MVector restBendNormal;        // the bend direction when the pole is on the root-to-handle line
	MVector restAxis;              // the root-to-handle direction when the handle sits on the root
};

struct TwoBoneIkResult
{
	MVector mid;
	MVector end;
	MVector normal;   // unit bend direction, perpendicular to the root-to-handle axis (the solved plane)
};

// Inputs of TwoBoneIkSolve::computeTwoBoneIkInverse: the joints' world positions and the settings the forward solve will use.
struct TwoBoneIkInverseInput
{
	MVector root;
	MVector mid;
	MVector end;
	double length1 = 1.0;
	double length2 = 1.0;
	double stretch = 0.0;
	double soft = 0.0;
	double pin = 0.0;
	double rollRadians = 0.0;
	double restPlaneAngle = 0.0;
	MVector restAxis;         // rest root to rest handle
	MVector restPoleOffset;   // rest root to rest pole
};

struct TwoBoneIkInverseResult
{
	MVector handle;
	MVector pole;
};

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
	// The ikTarget child plug is not a slot: it has no control, joint or socket and no element of childPlugs, so its RigModuleChildPlugInfo
	// carries this in place of an index (the build callback tells it apart by it).
	inline constexpr unsigned int IkTargetPlug{ 0xFFFFFFFFu };
}

// The order of the scalar channels the two-bone inverse reads (TwoBoneIkNode::inverseChannels), which is the order of
// ModuleInverseInput::channels. A Limb's list starts with these (D62). Append only: the order is not a name, but the inverse indexes by it.
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

// Field names are the JSON keys: "joints": [[side, usage] x 3] (root, mid, end). The IK handle, pole and settings controls are
// derived from them (D57). "controlName" (optional): the base of their usages, <base>IK, <base>Pole and <base>Settings.
// "poleOffset" and "settingsOffset" (optional, [x, y, z] in world space, left out or empty for the default): the pole's offset from
// the mid joint and the settings control's from the end joint. The other side of a mirrored module gets X negated.
// "poleParentDefault" (optional, PoleParentDefault: "parent", the default, or "ik"): the space the pole is built in, and its rest.
struct TwoBoneIkArgs
{
	std::vector<JointSelector> joints;
	std::string controlName;
	std::vector<double> poleOffset;
	std::vector<double> settingsOffset;
	double ikFkBlend{ 1.0 };
	bool ikOrientToWorld{ false };
	bool ikAlignToPole{ false };
	std::string poleParentDefault{ PoleParentDefault::Parent };
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TwoBoneIkArgs, joints, controlName, poleOffset, settingsOffset, ikFkBlend, ikOrientToWorld, ikAlignToPole, poleParentDefault)

// What the plan step works out from the rest pose, stored on the node by createModule (D52 section 4.3).
struct TwoBoneIkRest
{
	double length1 = 0.0;           // root to mid, and mid to end
	double length2 = 0.0;
	double restPlaneAngle = 0.0;    // radians. See TwoBoneIkInput::restPlaneAngle.
	MVector restBendNormal;
	std::array<MMatrix, TwoBoneIkSlot::LimbCount> ikRestOffset;   // jointRest * frameAtRest^-1, per limb joint
	short poleSpace = PoleSpaceLabels::ParentIndex;              // the pole's build value and rest, from poleParentDefault
};

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
	// The ikTarget (D62 section 8, a TwoBoneIk's or a Limb's): the end joint reaches the IK handle's rest carried by this motion instead of the handle's own
	// world. The target's motion already holds whatever moved the handle (a foot hangs from the ik socket), so the handle's local and its
	// parent's motion are not applied again. The pole and the ik socket still follow the handle, and never read the target.
	bool hasTarget = false;
	MMatrix mTargetDelta;
	bool poleFollowsIk = false;
	double blend = 1.0;
	double stretch = 0.0;
	double soft = 0.0;
	double pin = 0.0;
	double rollRadians = 0.0;
	TwoBoneIkRest rest;          // not read at blend 0
};

namespace TwoBoneIkSolve
{
	// Closed-form two-bone solve with stretch, soft reach, pin and roll (D52 section 4.2). Stateless: the same inputs
	// give the same pose, so a pole that crosses the root-to-handle line flips the limb. Allocation-free.
	TwoBoneIkResult computeTwoBoneIk(const TwoBoneIkInput& input);

	// The handle and pole that computeTwoBoneIk turns into these joint positions, for the settings given (D52 section 6).
	// Exact for a pose the limb can make, pin included. Otherwise the nearest it can: the forward solve of the result
	// differs from the joints, which is how a caller finds a pose IK can't reproduce.
	TwoBoneIkInverseResult computeTwoBoneIkInverse(const TwoBoneIkInverseInput& input);

	// The component of v perpendicular to the unit vector vAxis.
	MVector perpendicularPart(const MVector& v, const MVector& vAxis);

	// Whether the FK controls (fk) or the IK controls (not fk) of a module that follows an ik/fk blend are visible at the
	// blend: FK hides at 1 and IK at 0. TwoBoneIk, Limb and a ReverseFoot on either share it.
	inline bool isBlendControlVisible(bool fk, double blend)
	{
		return fk ? blend < 1.0 : blend > 0.0;
	}

	// The slots whose sockets follow their own controls and never the IK solve: the handle, the pole and the settings control.
	inline bool isHandleSocket(unsigned int slot)
	{
		return slot == TwoBoneIkSlot::Ik || slot == TwoBoneIkSlot::Pole || slot == TwoBoneIkSlot::Settings;
	}

	MMatrix makeFrame(const MVector& vX, const MVector& vY, const MVector& vZ, const MVector& vOrigin);

	// A matrix's rotation as a frame at vOrigin: +X along its X row, +Z perpendicular to that and its Y row. Scale and
	// shear are dropped.
	MMatrix orthonormalFrame(const MMatrix& mMatrix, const MVector& vOrigin);

	// The three limb joints' frames from a solve: root and mid by position and the bend normal, the end by the solved
	// end and the IK handle's rotation (scale dropped), so turning the handle turns the hand. The plan step builds the
	// rest offsets with this, evaluate() builds the live joints, and the inverse reads the handle back through the same
	// frame, so they can't drift (D41).
	void computeLimbFrames(const TwoBoneIkResult& solve, const MVector& vRoot, const MMatrix& mIkWorld, MMatrix outFrames[TwoBoneIkSlot::LimbCount]);

	// The forward evaluation, shared by compute(), the inverse and Limb so the sockets a restore returns are compute()'s.
	// The FK chain is always solved (the controls' offsets stay current), the IK above blend 0.
	void evaluate(const TwoBoneIkEval& eval, TwoBoneIkSolveResult& outResult);

	// Everything the rest pose fixes: bone lengths, the plane angle that makes IK at rest reproduce the rest bend, and the offsets that
	// give each IK joint its own rest orientation (bind pose safe). The rests are valid (planSlots checked the geometry). The inverse
	// calls this again from the controls' stored rests.
	TwoBoneIkRest computeRest(const std::array<MMatrix, TwoBoneIkSlot::Count>& restWorlds);

	// The child plug each slot owns, or nullptr for the slots with none (the FK lower and end controls).
	const char* childPlugNameAt(unsigned int slot);
	// Every name of the socket on a slot. index_0 to index_2 (and start and end, which are the same sockets) carry the blended joint worlds.
	std::vector<std::string> socketNamesAt(unsigned int slot);
	// The child plugs and sockets of the limb's six slots: the pole and settings plugs take no constraint, and the settings plug
	// follows start's parent (D52 decision 14). Then the optional ikTarget plug, whose blindSockets are the handle's, the pole's and the
	// settings control's (D80 phase 2). Limb has the same ones.
	ModuleInterface makeInterface();

	// The plan step's shared half (read phase, no scene changes): checks the limb's arguments (three joints, the blend's range, the
	// pole and settings offsets, ikAlignToPole), claims the joints and derives the IK handle, pole and settings controls (D57), their
	// names and markup, and the rest. Reports into ctx.report and returns false on a problem. outSlots holds, per slot, the joint (or
	// none), its rest world, its markup and its base name.
	bool planSlots(const TwoBoneIkArgs& ikArgs, PlanContext& ctx, std::array<TwoBoneIkSlotInput, TwoBoneIkSlot::Count>& outSlots, TwoBoneIkRest& outRest);
}
