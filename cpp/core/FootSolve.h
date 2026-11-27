#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// The ReverseFoot's math (D74, design/reverse-foot.md section 7), so it can be checked against closed-form cases.
//
// COUPLED TO TwoBoneIk (D74, D80): the foot hangs from a TwoBoneIk's sockets and feeds its ikTarget. A change to what the foot
// does with its channels changes where the limb's end joint reaches.
//
// Matrices are row-major with row vectors (VectorMath.h): a point moves as p' = p * M, and rows 0 to 2 of a rotation are where
// the X, Y and Z axes go. A motion is a rigid Mat4, rotation and translation, in world space at rest.
//
// The model. The marker pivots sit on the sole at rest, in chain order, root first: the module uses chainOrder (heel, toe tip,
// bankOut, bankIn, toe). Each pivot turns its subtree about its own rest position and the foot frame's axes, and a pivot
// is carried by the pivots before it in the chain (the literal hierarchy with the heel as root, author 2026-11-24). Because a
// pivot's own turn about its own position does not move it, that is the same as turning the ankle by the last pivot's rotation
// first and the first pivot's last: ankle' = Rot_0(Rot_1(... Rot_n(ankle))), every rotation about a REST pivot and a rest
// axis. The toe pivot, at the toe joint, is the ball of the foot: the ball channels and the roll below the break turn the ankle
// about it. Of its turns only ballSwivel, the outermost, carries the toe bone too (the whole foot swivels about the ball, toe tip
// included), so the toe stays planted for the pitch and the roll (see toeMotion). Past the
// break the toe tip carries everything, so the toe tip stays on the ground. The functions take any chain, so the tests can
// check other orders; the order is fixed by the module, not the template (author, 2026-11-24). There is no separate ball pivot
// (author, 2026-11-25): the axes come from the foot frame, not a marker, so a ball pivot apart from the toe joint only moved
// the roll's centre off the joint, and the toe slid to keep its bone length.
//
// The foot frame (positions only, a marker's orientation is never read): forward is the heel to the toe tip projected onto
// the ground, up is world +Y and lateral is up crossed with forward, so a character facing +Z has lateral +X, its left.
// Angles are degrees. Positive angles turn: pitch about lateral (heel up, toes down, for the toe and toe tip pivots; the heel
// pivot's angle lifts the toes), swivel about up (toes toward lateral), roll about forward (the +lateral side up). Bank is a
// roll the other way, so a positive bank tips the foot onto its +lateral edge.

#include "VectorMath.h"

#include <array>
#include <cstddef>
#include <span>

namespace FootSolve
{
	using VectorMath::Mat4;
	using VectorMath::Vec3;

	// What a pivot is for. Its marker's usage is the role's name, on the foot's side ([Left, heel]). Stored on the module by
	// value, so the values never move: 1 was the ball pivot (removed 2026-11-25, the toe pivot is the ball) and is never reused.
	enum class Role
	{
		Heel = 0,
		BankOut = 2,
		BankIn = 3,
		ToeTip = 4,
		Toe = 5,
	};

	inline constexpr std::size_t kRoleCount{ 5 };

	// The usage text of a role: "heel", "bankOut", "bankIn", "toeTip", "toe".
	const char* roleName(Role role);

	// The chain every foot builds, root first: heel, toeTip, bankOut, bankIn, toe. Every role is in it once.
	const std::array<Role, kRoleCount>& chainOrder();

	struct Pivot
	{
		Role role{ Role::Toe };
		Vec3 position;   // the marker's rest world position
	};

	// The animator's channels in degrees. The defaults are the rest (D34).
	struct Channels
	{
		double footRoll{ 0.0 };
		double breakAngle{ 30.0 };
		double heelRaise{ 0.0 };
		double heelSwivel{ 0.0 };
		double ballRaise{ 0.0 };
		double ballSwivel{ 0.0 };
		double ballRoll{ 0.0 };
		double bank{ 0.0 };
		double toeRaise{ 0.0 };
		double toeSwivel{ 0.0 };
		double toeTap{ 0.0 };
	};

	struct Frame
	{
		Vec3 forward{ 0.0, 0.0, 1.0 };
		Vec3 up{ 0.0, 1.0, 0.0 };
		Vec3 lateral{ 1.0, 0.0, 0.0 };
	};

	// The foot frame of a chain at rest: forward runs from the heel pivot to the toe tip pivot, projected onto the ground. A chain
	// with no heel starts at its first pivot, and one with no toe tip ends at its last, each end on its own. A foot with no length along the ground faces +Z.
	Frame footFrame(std::span<const Pivot> chain);

	struct Peel
	{
		double heel{ 0.0 };      // toes up about the heel, degrees from 0
		double ball{ 0.0 };      // heel up about the ball of the foot, the toe pivot
		double toeTip{ 0.0 };    // heel and ball up about the toe tip
	};

	// footRoll split over the three pivots: heel = max(-footRoll, 0), ball = clamp(footRoll, 0, breakAngle), toeTip =
	// max(footRoll - breakAngle, 0). They sum to footRoll when the break angle is 0 or more, and a break angle below 0 counts
	// as 0. The raise channels are added after this, so raising the ball never starts the toe tip.
	Peel splitPeel(double footRoll, double breakAngle);

	// The index in the chain of the pivot a signed bank turns about: for bank above 0 the bankOut or bankIn pivot with the
	// larger lateral position, for bank below 0 the other, so a left foot's positive bank uses bankOut and a right foot's uses
	// bankIn with no side branch. -1 when bank is 0 or the chain has no bank pivots.
	int bankPivotIndex(std::span<const Pivot> chain, const Frame& frame, double bank);

	// The motion of a point carried by the end of the first `count` pivots, for these channels: each pivot's turn, with the
	// last pivot's applied first and the first pivot's last (the hierarchy in the header). count is the chain's size for the
	// ankle, and a larger one counts as the whole chain. The Toe pivot turns by the ball channels here; toeTap is not in the
	// fold, it moves the toe bone alone (see toeMotion).
	Mat4 foldChain(std::span<const Pivot> chain, std::size_t count, const Frame& frame, const Channels& channels);

	// The ankle's motion with every pivot folded.
	Mat4 ankleMotion(std::span<const Pivot> chain, const Frame& frame, const Channels& channels);

	// The motion of a point carried by the toe pivot (the toe bone): toeTap about the toe pivot, then ballSwivel about it, then the
	// pivots before it. The toe pivot's pitch and roll (the roll below the break, ballRaise, ballRoll) are not in it: they turn the
	// ankle about the toe and leave the toe planted.
	// toeIndex is the Toe pivot's index in the chain (findPivot, which is -1 for none: check it first). An index past the
	// chain gives the identity.
	Mat4 toeMotion(std::span<const Pivot> chain, std::size_t toeIndex, const Frame& frame, const Channels& channels);

	// The index of the first pivot with this role, or -1.
	int findPivot(std::span<const Pivot> chain, Role role);

	// A rotation of `degrees` about `axis` through `point`, as a motion. Right-handed.
	Mat4 rotationAbout(const Vec3& point, const Vec3& axis, double degrees);

	// A motion applied to a point.
	Vec3 transformPoint(const Mat4& motion, const Vec3& point);

	// A rotation split in the foot frame, outermost first: a heading about up, a pitch about lateral, a roll about forward,
	// so the rotation is Rot_up(yaw) after Rot_lateral(pitch) after Rot_forward(roll). Degrees. Only the rotation of the motion is
	// read, and a uniform scale in it (a rig scaled through its Layout) is divided out. pitch is the foot's total footRoll and -roll its bank, whatever pivots they came from (the inverse, section 10).
	struct Euler
	{
		double yaw{ 0.0 };
		double pitch{ 0.0 };
		double roll{ 0.0 };
	};
	Euler decomposeRotation(const Mat4& motion, const Frame& frame);

	// The inverse's gauge (D74, design/reverse-foot.md section 10). ankleMotionSinceRest is the ankle's world motion since rest, the handle's
	// share still in it (only its rotation is read). footRoll and bank take the pitch and roll that the given channels (the sidecar's, else
	// the rig's) miss, on top of their given values, and every other channel is kept as given. So a sidecar's raises and ballRoll are not
	// counted twice, and given channels that already make the turn come back unchanged. The handle takes the heading, the position and
	// what the channels can't hold, such as a ball pitch turned before a bank in the fixed chain order.
	Channels solveAnkleChannels(std::span<const Pivot> chain, const Frame& frame, const Channels& given, const Mat4& ankleMotionSinceRest);

	// toeTap from the toe bone's world motion since rest with the handle's motion taken out: the pitch left once the ball swivel and the
	// pivots before the toe pivot are undone. toeIndex is the Toe pivot's index (findPivot). Only rotations are read.
	double solveToeTap(std::span<const Pivot> chain, std::size_t toeIndex, const Frame& frame, const Channels& channels, const Mat4& toeMotionSinceRest);
}
