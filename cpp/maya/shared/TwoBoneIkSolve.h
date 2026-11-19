#pragma once

#include <array>

#include <maya/MMatrix.h>
#include <maya/MVector.h>

// The two-bone IK/FK solve (D52), as plain values in and out, with no node and no plug. TwoBoneIkNode reads its plugs into a
// TwoBoneIkEval and calls TwoBoneIkSolve::evaluate, the inverse calls it on the values it built, and the Limb module (D62,
// design/limb.md) calls it for its limb joints. A change here changes every one of them: read design/limb.md first.

struct TwoBoneIkResult;   // maya/shared/Utilities.h

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

// What the plan step works out from the rest pose, stored on the node by createModule (D52 section 4.3).
struct TwoBoneIkRest
{
	double length1 = 0.0;           // root to mid, and mid to end
	double length2 = 0.0;
	double restPlaneAngle = 0.0;    // radians. See TwoBoneIkInput::restPlaneAngle.
	MVector restBendNormal;
	std::array<MMatrix, TwoBoneIkSlot::LimbCount> ikRestOffset;   // jointRest * frameAtRest^-1, per limb joint
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
	// The component of v perpendicular to the unit vector vAxis.
	MVector perpendicularPart(const MVector& v, const MVector& vAxis);

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
}
