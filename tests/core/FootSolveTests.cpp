// core/FootSolve: the frame, the peel split, the bank pivot choice, the chain fold and the rotation split (
// design/reverse-foot.md sections 7, 8, 10 and 15).
//
// Every expected number is worked by hand from one foot, in a frame with forward +Z, up +Y and lateral +X (a character
// facing +Z, so lateral is its left). The pivots sit on the ground (y = 0): heel z = -10, the toe pivot (the ball of the
// foot) z = 10, bankOut (4, 0, 10), bankIn (-4, 0, 10) and toe tip z = 18. The ankle is at (0, 8, -6). A turn about a pivot
// is the plain 2D rotation of the point's offset from the pivot:
//   about lateral (+X): y' = dy cos - dz sin, z' = dy sin + dz cos
//   about up (+Y):      x' = dx cos + dz sin, z' = -dx sin + dz cos
//   about forward (+Z): x' = dx cos - dy sin, y' = dx sin + dy cos
// and a heel pivot turns by minus its angle (toes up), the toe (ball channels) and toe tip by plus theirs, a bank by minus its
// value. The chain is root first, in chainOrder (heel, toeTip, bankOut, bankIn, toe), so the toe pivot turns the ankle first
// and the heel last (the literal hierarchy; the order is fixed by the module since 2026-11-24, and the toe pivot took
// the ball pivot's channels on 2026-11-25).
// The numbers were worked with a separate calculation (the formulas above, no FootSolve code), to 6 places.

#include "CoreTests.h"

#include "core/FootSolve.h"
#include "core/RibbonMath.h"

#include <array>

namespace
{
	using FootSolve::Channels;
	using FootSolve::Frame;
	using FootSolve::Pivot;
	using FootSolve::Role;
	using VectorMath::Mat4;
	using VectorMath::Vec3;

	constexpr double kTolerance{ 1.0e-5 };

	// In chainOrder, the order every foot builds: heel, toeTip, bankOut, bankIn, toe. The last pivot turns a point first, so the
	// toe pivot turns the ankle before the banks and the toe tip, and the toe bone is not carried by its own pivot.
	std::vector<Pivot> leftFoot()
	{
		return {
			{ Role::Heel, { 0.0, 0.0, -10.0 } },
			{ Role::ToeTip, { 0.0, 0.0, 18.0 } },
			{ Role::BankOut, { 4.0, 0.0, 10.0 } },
			{ Role::BankIn, { -4.0, 0.0, 10.0 } },
			{ Role::Toe, { 0.0, 0.0, 10.0 } },
		};
	}

	// The same foot as a right one: out is on -X and in on +X.
	std::vector<Pivot> rightFoot()
	{
		std::vector<Pivot> chain = leftFoot();
		chain[2].position.x = -4.0;
		chain[3].position.x = 4.0;
		return chain;
	}

	// The foot turned a quarter about up, so it faces +X: (x, y, z) -> (z, y, -x).
	Vec3 faceX(const Vec3& v)
	{
		return { v.z, v.y, -v.x };
	}

	std::vector<Pivot> facingX(std::vector<Pivot> chain)
	{
		for (Pivot& pivot : chain)
		{
			pivot.position = faceX(pivot.position);
		}
		return chain;
	}

	const Vec3 kAnkle{ 0.0, 8.0, -6.0 };

	Vec3 ankleAt(const Channels& channels)
	{
		const std::vector<Pivot> chain = leftFoot();
		return FootSolve::transformPoint(FootSolve::ankleMotion(chain, FootSolve::footFrame(chain), channels), kAnkle);
	}

	void checkPoint(const Vec3& actual, double x, double y, double z)
	{
		CHECK_CLOSE(actual.x, x, kTolerance);
		CHECK_CLOSE(actual.y, y, kTolerance);
		CHECK_CLOSE(actual.z, z, kTolerance);
	}
}

CORE_TEST(footRoleNamesAreTheMarkerUsages)
{
	CHECK(std::string(FootSolve::roleName(Role::Heel)) == "heel");
	CHECK(std::string(FootSolve::roleName(Role::BankOut)) == "bankOut");
	CHECK(std::string(FootSolve::roleName(Role::BankIn)) == "bankIn");
	CHECK(std::string(FootSolve::roleName(Role::ToeTip)) == "toeTip");
	CHECK(std::string(FootSolve::roleName(Role::Toe)) == "toe");
}

CORE_TEST(footRoleValuesNeverMove)
{
	// The roles are stored on the module by value. 1 was the ball pivot, retired 2026-11-25, and is never reused.
	CHECK(static_cast<int>(Role::Heel) == 0);
	CHECK(static_cast<int>(Role::BankOut) == 2);
	CHECK(static_cast<int>(Role::BankIn) == 3);
	CHECK(static_cast<int>(Role::ToeTip) == 4);
	CHECK(static_cast<int>(Role::Toe) == 5);
}

CORE_TEST(footChainOrderIsFixedAndHoldsEveryRoleOnce)
{
	const std::array<Role, FootSolve::kRoleCount> expected{ Role::Heel, Role::ToeTip, Role::BankOut, Role::BankIn, Role::Toe };
	CHECK(FootSolve::chainOrder() == expected);
	const std::vector<Pivot> chain = leftFoot();
	for (std::size_t i = 0; i < chain.size(); ++i)
	{
		CHECK(chain[i].role == FootSolve::chainOrder()[i]);
	}
}

CORE_TEST(footFrameFacesAlongHeelToToeTip)
{
	const Frame frame = FootSolve::footFrame(leftFoot());
	checkPoint(frame.forward, 0.0, 0.0, 1.0);
	checkPoint(frame.up, 0.0, 1.0, 0.0);
	checkPoint(frame.lateral, 1.0, 0.0, 0.0);
}

CORE_TEST(footFrameIgnoresHeightAndFollowsTheHeading)
{
	// Heel at the origin, tip 10 along +X and 3 up: forward is +X (the height is projected out), lateral up x forward = -Z.
	const std::vector<Pivot> chain{ { Role::Heel, { 0.0, 0.0, 0.0 } }, { Role::ToeTip, { 10.0, 3.0, 0.0 } } };
	const Frame frame = FootSolve::footFrame(chain);
	checkPoint(frame.forward, 1.0, 0.0, 0.0);
	checkPoint(frame.lateral, 0.0, 0.0, -1.0);
}

CORE_TEST(footFrameFallsBackToTheFirstAndLastPivot)
{
	const std::vector<Pivot> chain{ { Role::BankOut, { 0.0, 0.0, 0.0 } }, { Role::Toe, { 0.0, 0.0, -5.0 } } };
	checkPoint(FootSolve::footFrame(chain).forward, 0.0, 0.0, -1.0);
}

CORE_TEST(footFrameWithNoLengthFacesPlusZ)
{
	const std::vector<Pivot> straightUp{ { Role::Heel, { 1.0, 0.0, 1.0 } }, { Role::ToeTip, { 1.0, 5.0, 1.0 } } };
	checkPoint(FootSolve::footFrame(straightUp).forward, 0.0, 0.0, 1.0);
	const std::vector<Pivot> none;
	checkPoint(FootSolve::footFrame(none).forward, 0.0, 0.0, 1.0);
}

CORE_TEST(footPeelSplit)
{
	FootSolve::Peel peel = FootSolve::splitPeel(-20.0, 30.0);
	CHECK_CLOSE(peel.heel, 20.0, 0.0);
	CHECK_CLOSE(peel.ball, 0.0, 0.0);
	CHECK_CLOSE(peel.toeTip, 0.0, 0.0);

	peel = FootSolve::splitPeel(0.0, 30.0);
	CHECK_CLOSE(peel.heel + peel.ball + peel.toeTip, 0.0, 0.0);

	peel = FootSolve::splitPeel(20.0, 30.0);
	CHECK_CLOSE(peel.heel, 0.0, 0.0);
	CHECK_CLOSE(peel.ball, 20.0, 0.0);
	CHECK_CLOSE(peel.toeTip, 0.0, 0.0);

	// Exactly at the break the ball has all of it and the toe tip has not started.
	peel = FootSolve::splitPeel(30.0, 30.0);
	CHECK_CLOSE(peel.ball, 30.0, 0.0);
	CHECK_CLOSE(peel.toeTip, 0.0, 0.0);

	peel = FootSolve::splitPeel(50.0, 30.0);
	CHECK_CLOSE(peel.ball, 30.0, 0.0);
	CHECK_CLOSE(peel.toeTip, 20.0, 0.0);
}

CORE_TEST(footPeelBreakAngleEdges)
{
	// A break angle of 0 hands everything to the toe tip. Below 0 counts as 0.
	FootSolve::Peel peel = FootSolve::splitPeel(10.0, 0.0);
	CHECK_CLOSE(peel.ball, 0.0, 0.0);
	CHECK_CLOSE(peel.toeTip, 10.0, 0.0);
	peel = FootSolve::splitPeel(10.0, -5.0);
	CHECK_CLOSE(peel.ball, 0.0, 0.0);
	CHECK_CLOSE(peel.toeTip, 10.0, 0.0);
	// A break past the roll keeps it all in the ball.
	peel = FootSolve::splitPeel(10.0, 90.0);
	CHECK_CLOSE(peel.ball, 10.0, 0.0);
	CHECK_CLOSE(peel.toeTip, 0.0, 0.0);
}

CORE_TEST(footBankPivotFollowsTheSideNotTheName)
{
	const Frame frame{};
	const std::vector<Pivot> left = leftFoot();
	CHECK(FootSolve::bankPivotIndex(left, frame, 10.0) == 2);    // bankOut, on +X
	CHECK(FootSolve::bankPivotIndex(left, frame, -10.0) == 3);   // bankIn
	// A right foot's positive bank is its bankIn, which is the pivot on +X, with no side branch.
	const std::vector<Pivot> right = rightFoot();
	CHECK(FootSolve::bankPivotIndex(right, frame, 10.0) == 3);
	CHECK(FootSolve::bankPivotIndex(right, frame, -10.0) == 2);
	CHECK(FootSolve::bankPivotIndex(left, frame, 0.0) == -1);

	const std::vector<Pivot> noBank{ { Role::Heel, { 0.0, 0.0, -10.0 } }, { Role::Toe, { 0.0, 0.0, 10.0 } } };
	CHECK(FootSolve::bankPivotIndex(noBank, frame, 10.0) == -1);
}

CORE_TEST(footAtRestDoesNotMove)
{
	const Channels rest;
	checkPoint(ankleAt(rest), 0.0, 8.0, -6.0);
	const std::vector<Pivot> chain = leftFoot();
	const Mat4 motion = FootSolve::ankleMotion(chain, FootSolve::footFrame(chain), rest);
	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			CHECK_CLOSE(motion.m[r][c], r == c ? 1.0 : 0.0, 1.0e-12);
		}
	}
}

CORE_TEST(footHeelRegime)
{
	// footRoll -20: the foot turns -20 about the heel (toes up), so the ankle rises.
	Channels channels;
	channels.footRoll = -20.0;
	checkPoint(ankleAt(channels), 0.0, 8.885622, -8.977391);
}

CORE_TEST(footBallRegime)
{
	Channels channels;
	channels.footRoll = 20.0;
	checkPoint(ankleAt(channels), 0.0, 12.989863, -2.298921);
}

CORE_TEST(footRollExactlyAtTheBreak)
{
	Channels channels;
	channels.footRoll = 30.0;
	checkPoint(ankleAt(channels), 0.0, 14.928203, 0.143594);
}

CORE_TEST(footToeTipRegime)
{
	// footRoll 50 with a break of 30: the toe pivot turns the ankle 30 first, then the toe tip turns it 20. Toe 30: rel (8, -16) about
	// (0, 10) gives (y 14.928203, z 0.143594). Toe tip 20 about (0, 18): rel (14.928203, -17.856406) gives y = 14.928203 cos20 +
	// 17.856406 sin20 = 20.135173, z = 18 + 14.928203 sin20 - 17.856406 cos20 = 6.326213. The toe tip stays on the ground.
	Channels channels;
	channels.footRoll = 50.0;
	checkPoint(ankleAt(channels), 0.0, 20.135173, 6.326213);
}

CORE_TEST(footRollJustPastTheBreakStartsTheToeTip)
{
	// Toe 30, then toe tip 5 about (0, 18): y = 14.928203 cos5 + 17.856406 sin5, z = 18 + 14.928203 sin5 - 17.856406 cos5.
	Channels channels;
	channels.footRoll = 35.0;
	checkPoint(ankleAt(channels), 0.0, 16.427685, 1.512621);
}

CORE_TEST(footBallRaiseNeverStartsTheToeTip)
{
	// footRoll 25 and ballRaise 10 is 35 about the toe pivot and nothing about the toe tip: the raise is added after the split.
	Channels channels;
	channels.footRoll = 25.0;
	channels.ballRaise = 10.0;
	checkPoint(ankleAt(channels), 0.0, 15.730439, 1.482179);

	// With nothing but the raise the ankle is pushed up the way the roll does it.
	Channels raiseOnly;
	raiseOnly.ballRaise = 20.0;
	checkPoint(ankleAt(raiseOnly), 0.0, 12.989863, -2.298921);
}

CORE_TEST(footHeelAndToeRaises)
{
	Channels heel;
	heel.heelRaise = 15.0;
	checkPoint(ankleAt(heel), 0.0, 8.762683, -8.206849);

	Channels toe;
	toe.toeRaise = 10.0;
	checkPoint(ankleAt(toe), 0.0, 12.046018, -4.246201);
}

CORE_TEST(footBankBothSigns)
{
	Channels channels;
	channels.bank = 10.0;
	checkPoint(ankleAt(channels), 1.449954, 8.573055, -6.0);
	channels.bank = -10.0;
	checkPoint(ankleAt(channels), -1.449954, 8.573055, -6.0);
}

CORE_TEST(footBankOnARightFootIsTheMirrorOfTheLeft)
{
	// The pivots are mirrored, the ankle too: the same channel value banks toward +X on both, using bankIn on the right foot.
	const std::vector<Pivot> chain = rightFoot();
	Channels channels;
	channels.bank = 10.0;
	const Vec3 ankle = FootSolve::transformPoint(FootSolve::ankleMotion(chain, FootSolve::footFrame(chain), channels), kAnkle);
	// The bankIn pivot is at (4, 0, 10): turning -10 about forward from the ankle's offset (-4, 8).
	checkPoint(ankle, 1.449954, 8.573055, -6.0);
}

CORE_TEST(footSwivels)
{
	Channels heel;
	heel.heelSwivel = 30.0;
	checkPoint(ankleAt(heel), 2.0, 8.0, -6.535898);
	Channels ball;
	ball.ballSwivel = 30.0;
	checkPoint(ankleAt(ball), -8.0, 8.0, -3.856406);
	Channels tip;
	tip.toeSwivel = 30.0;
	checkPoint(ankleAt(tip), -12.0, 8.0, -2.78461);
}

CORE_TEST(footBallRoll)
{
	Channels channels;
	channels.ballRoll = 20.0;
	checkPoint(ankleAt(channels), 2.736161, 7.517541, -6.0);
}

CORE_TEST(footPitchAndBankTogether)
{
	// Toe 20 and bank 10 on the bankOut pivot: the toe pivot turns the ankle first, to (0, 12.989863, -2.298921), then the bank turns
	// it -10 about forward at (4, 0): rel (-4, 12.989863) gives x = 4 - 4 cos10 + 12.989863 sin10, y = 4 sin10 + 12.989863 cos10.
	Channels channels;
	channels.footRoll = 20.0;
	channels.bank = 10.0;
	checkPoint(ankleAt(channels), 2.316435, 13.487111, -2.298921);
}

CORE_TEST(footRotationRowsArePlainTurns)
{
	// A roll of 20 is a 20 degree turn about lateral: Y goes to (0, cos, sin) and Z to (0, -sin, cos), X stays.
	Channels channels;
	channels.footRoll = 20.0;
	const std::vector<Pivot> chain = leftFoot();
	const Mat4 motion = FootSolve::ankleMotion(chain, FootSolve::footFrame(chain), channels);
	CHECK_CLOSE(motion.m[0][0], 1.0, 1.0e-12);
	CHECK_CLOSE(motion.m[1][1], 0.9396926, 1.0e-6);
	CHECK_CLOSE(motion.m[1][2], 0.3420201, 1.0e-6);
	CHECK_CLOSE(motion.m[2][1], -0.3420201, 1.0e-6);
	CHECK_CLOSE(motion.m[2][2], 0.9396926, 1.0e-6);
}

CORE_TEST(footTheSameChannelsOnAFootFacingX)
{
	// Turning the whole foot a quarter changes nothing but the heading: the frame follows the markers, so the result is
	// the facing-+Z result turned the same quarter.
	const std::vector<Pivot> chain = facingX(leftFoot());
	const Frame frame = FootSolve::footFrame(chain);
	checkPoint(frame.forward, 1.0, 0.0, 0.0);
	checkPoint(frame.lateral, 0.0, 0.0, -1.0);

	Channels channels;
	channels.footRoll = 20.0;
	channels.bank = 10.0;
	const Vec3 turned = FootSolve::transformPoint(FootSolve::ankleMotion(chain, frame, channels), faceX(kAnkle));
	const Vec3 expected = faceX({ 2.316435, 13.487111, -2.298921 });
	checkPoint(turned, expected.x, expected.y, expected.z);

	Channels toeTip;
	toeTip.footRoll = 50.0;
	const Vec3 tipTurned = FootSolve::transformPoint(FootSolve::ankleMotion(chain, frame, toeTip), faceX(kAnkle));
	const Vec3 tipExpected = faceX({ 0.0, 20.135173, 6.326213 });
	checkPoint(tipTurned, tipExpected.x, tipExpected.y, tipExpected.z);
}

CORE_TEST(footToeTapTurnsOnlyTheToe)
{
	const std::vector<Pivot> chain = leftFoot();
	const Frame frame = FootSolve::footFrame(chain);
	Channels channels;
	channels.toeTap = 25.0;
	// The tap does not reach the ankle.
	checkPoint(FootSolve::transformPoint(FootSolve::ankleMotion(chain, frame, channels), kAnkle), 0.0, 8.0, -6.0);
	// A point on the toe bone, 4 beyond the toe pivot, tips down about it: y = -4 sin25, z = 10 + 4 cos25.
	const std::size_t toe = static_cast<std::size_t>(FootSolve::findPivot(chain, Role::Toe));
	checkPoint(FootSolve::transformPoint(FootSolve::toeMotion(chain, toe, frame, channels), { 0.0, 0.0, 14.0 }), 0.0, -1.690473, 13.625231);
}

CORE_TEST(footBallPitchAndRollTurnTheAnkleAboutThePlantedToe)
{
	// The regression of 2026-11-25: the roll below the break, ballRaise and ballRoll turn the ankle about the toe pivot and leave the toe
	// bone where it is, toe tip included, and the ankle keeps its rest distance to the toe. With a ball pivot apart from the toe the
	// distance changed, and the node's length-keeping pull slid the toe and dropped the toe tip.
	const std::vector<Pivot> chain = leftFoot();
	const Frame frame = FootSolve::footFrame(chain);
	const std::size_t toe = static_cast<std::size_t>(FootSolve::findPivot(chain, Role::Toe));
	const Vec3 toeRest = chain[toe].position;
	const double restDistance = VectorMath::length(VectorMath::sub(kAnkle, toeRest));
	Channels channels;
	channels.footRoll = 20.0;
	channels.ballRaise = 5.0;
	channels.ballRoll = 15.0;
	const Mat4 toeBone = FootSolve::toeMotion(chain, toe, frame, channels);
	checkPoint(FootSolve::transformPoint(toeBone, toeRest), toeRest.x, toeRest.y, toeRest.z);
	checkPoint(FootSolve::transformPoint(toeBone, { 0.0, 0.0, 14.0 }), 0.0, 0.0, 14.0);
	const Vec3 ankle = FootSolve::transformPoint(FootSolve::ankleMotion(chain, frame, channels), kAnkle);
	CHECK_CLOSE(VectorMath::length(VectorMath::sub(ankle, toeRest)), restDistance, kTolerance);
}

CORE_TEST(footBallSwivelTurnsTheWholeFootAboutTheBall)
{
	// ballSwivel turns the whole foot about the toe pivot, toe tip included (the author's "squash a bug", 2026-11-25): +30 about up at
	// (0, 0, 10). The ankle, offset (0, -16), goes to x = -16 sin30 = -8, z = 10 - 16 cos30 = -3.856406 (footSwivels). A point on the toe
	// bone 4 ahead of the pivot, offset (0, 4), goes to x = 4 sin30 = 2, z = 10 + 4 cos30 = 13.464102: it swings the other way, so the
	// foot turns as one piece.
	const std::vector<Pivot> chain = leftFoot();
	const Frame frame = FootSolve::footFrame(chain);
	const std::size_t toe = static_cast<std::size_t>(FootSolve::findPivot(chain, Role::Toe));
	Channels channels;
	channels.ballSwivel = 30.0;
	checkPoint(FootSolve::transformPoint(FootSolve::ankleMotion(chain, frame, channels), kAnkle), -8.0, 8.0, -3.856406);
	checkPoint(FootSolve::transformPoint(FootSolve::toeMotion(chain, toe, frame, channels), { 0.0, 0.0, 14.0 }), 2.0, 0.0, 13.464102);
	// With a tap the toe bone tips about the pivot first, then swivels: (0, -1.690473, 13.625231) from the tap, offset (0, 3.625231)
	// turned 30 gives x = 3.625231 sin30 = 1.812616, z = 10 + 3.625231 cos30 = 13.139542.
	channels.toeTap = 25.0;
	checkPoint(FootSolve::transformPoint(FootSolve::toeMotion(chain, toe, frame, channels), { 0.0, 0.0, 14.0 }), 1.812616, -1.690473, 13.139542);
}

CORE_TEST(footToeIsCarriedByThePivotsBeforeIt)
{
	const std::vector<Pivot> chain = leftFoot();
	const Frame frame = FootSolve::footFrame(chain);
	const std::size_t toe = static_cast<std::size_t>(FootSolve::findPivot(chain, Role::Toe));
	Channels channels;
	// Below the break the toe pivot turns the ankle and the toe stays planted: the toe bone is not carried by its own pivot.
	channels.footRoll = 20.0;
	checkPoint(FootSolve::transformPoint(FootSolve::foldChain(chain, toe, frame, channels), chain[toe].position), 0.0, 0.0, 10.0);
	// Past the break the toe tip carries it: 20 about (0, 18), rel (0, -8), gives y = 8 sin20, z = 18 - 8 cos20.
	channels.footRoll = 50.0;
	checkPoint(FootSolve::transformPoint(FootSolve::foldChain(chain, toe, frame, channels), chain[toe].position), 0.0, 2.736161, 10.482459);
	// A tapped toe bone point is tapped first and carried second: (0, 0, 14) tapped 25 about the toe pivot to (0, -1.690473,
	// 13.625231), then 20 about the toe tip: rel (-1.690473, -4.374769).
	channels.toeTap = 25.0;
	checkPoint(FootSolve::transformPoint(FootSolve::toeMotion(chain, toe, frame, channels), { 0.0, 0.0, 14.0 }), 0.0, -0.092266, 13.310886);
}

CORE_TEST(footFindPivot)
{
	const std::vector<Pivot> chain = leftFoot();
	CHECK(FootSolve::findPivot(chain, Role::Heel) == 0);
	CHECK(FootSolve::findPivot(chain, Role::Toe) == 4);
	const std::vector<Pivot> toeOnly{ { Role::Toe, {} } };
	CHECK(FootSolve::findPivot(toeOnly, Role::Heel) == -1);
}

CORE_TEST(footDecomposesPitchAndRoll)
{
	// Heel -20 with a bank of 10: the bank (after the heel in the chain) turns the ankle first, so the rotation is a pitch of -20
	// about lateral after a roll of -10 about forward, the order decomposeRotation reads.
	const std::vector<Pivot> chain = leftFoot();
	const Frame frame = FootSolve::footFrame(chain);
	Channels channels;
	channels.footRoll = -20.0;
	channels.bank = 10.0;
	FootSolve::Euler euler = FootSolve::decomposeRotation(FootSolve::ankleMotion(chain, frame, channels), frame);
	CHECK_CLOSE(euler.pitch, -20.0, 1.0e-9);
	CHECK_CLOSE(euler.roll, -10.0, 1.0e-9);
	CHECK_CLOSE(euler.yaw, 0.0, 1.0e-9);

	channels.bank = -10.0;
	euler = FootSolve::decomposeRotation(FootSolve::ankleMotion(chain, frame, channels), frame);
	CHECK_CLOSE(euler.pitch, -20.0, 1.0e-9);
	CHECK_CLOSE(euler.roll, 10.0, 1.0e-9);
}

CORE_TEST(footDecomposesAHeading)
{
	// A heading of 30 about up, outermost, leaves the pitch and the roll as they were.
	const std::vector<Pivot> chain = leftFoot();
	const Frame frame = FootSolve::footFrame(chain);
	Channels channels;
	channels.footRoll = -20.0;
	channels.bank = 10.0;
	const Mat4 heading = FootSolve::rotationAbout({ 0.0, 0.0, 0.0 }, frame.up, 30.0);
	const Mat4 motion = RibbonMath::multiply(FootSolve::ankleMotion(chain, frame, channels), heading);
	const FootSolve::Euler euler = FootSolve::decomposeRotation(motion, frame);
	CHECK_CLOSE(euler.yaw, 30.0, 1.0e-9);
	CHECK_CLOSE(euler.pitch, -20.0, 1.0e-9);
	CHECK_CLOSE(euler.roll, -10.0, 1.0e-9);
}

CORE_TEST(footDecomposesOnAFootFacingX)
{
	// The angles are read in the foot's own axes, so a foot facing +X gives the same ones.
	const std::vector<Pivot> chain = facingX(leftFoot());
	const Frame frame = FootSolve::footFrame(chain);
	Channels channels;
	channels.footRoll = -20.0;
	channels.bank = 10.0;
	const FootSolve::Euler euler = FootSolve::decomposeRotation(FootSolve::ankleMotion(chain, frame, channels), frame);
	CHECK_CLOSE(euler.pitch, -20.0, 1.0e-9);
	CHECK_CLOSE(euler.roll, -10.0, 1.0e-9);
	CHECK_CLOSE(euler.yaw, 0.0, 1.0e-9);
}

CORE_TEST(footDecompositionRebuildsTheRotationWhenTheChainSandwichesTheRoll)
{
	// The toe pivot turns the ankle before the bank (it is last in the chain), so a ball roll with a bank is a pitch then a roll, and
	// past the break the bank sits between the ball's pitch and the toe tip's. Either way the pitch and roll read in roll-first
	// order are not footRoll and bank (the inverse's gauge: the handle takes the rest). The decomposition still gives angles whose
	// rotation is the motion's.
	const std::vector<Pivot> chain = leftFoot();
	const Frame frame = FootSolve::footFrame(chain);
	for (const double footRoll : { 20.0, 50.0 })
	{
		Channels channels;
		channels.footRoll = footRoll;
		channels.bank = 10.0;
		channels.heelSwivel = 12.0;
		const Mat4 motion = FootSolve::ankleMotion(chain, frame, channels);
		const FootSolve::Euler euler = FootSolve::decomposeRotation(motion, frame);

		const Vec3 origin{ 0.0, 0.0, 0.0 };
		Mat4 rebuilt = FootSolve::rotationAbout(origin, frame.forward, euler.roll);
		rebuilt = RibbonMath::multiply(rebuilt, FootSolve::rotationAbout(origin, frame.lateral, euler.pitch));
		rebuilt = RibbonMath::multiply(rebuilt, FootSolve::rotationAbout(origin, frame.up, euler.yaw));
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 3; ++c)
			{
				CHECK_CLOSE(rebuilt.m[r][c], motion.m[r][c], 1.0e-9);
			}
		}
	}
}

CORE_TEST(footToeTapIsThePitchOfTheToesRelativeRotation)
{
	// A toe turned 25 about lateral, relative to the ankle, reads back as a toeTap of 25 and nothing else.
	const Frame frame{};
	const Mat4 relative = FootSolve::rotationAbout({ 3.0, 4.0, 5.0 }, frame.lateral, 25.0);
	const FootSolve::Euler euler = FootSolve::decomposeRotation(relative, frame);
	CHECK_CLOSE(euler.pitch, 25.0, 1.0e-9);
	CHECK_CLOSE(euler.roll, 0.0, 1.0e-9);
	CHECK_CLOSE(euler.yaw, 0.0, 1.0e-9);
}

CORE_TEST(footGaugePutsPitchInFootRollAndRollInBank)
{
	// No sidecar: a heel roll of -20 with a bank of 10, carried by a heading of 30 (the handle's), comes back as those two channels,
	// whatever the heading. The other channels stay as given.
	const std::vector<Pivot> chain = leftFoot();
	const Frame frame = FootSolve::footFrame(chain);
	Channels authored;
	authored.footRoll = -20.0;
	authored.bank = 10.0;
	const Mat4 heading = FootSolve::rotationAbout({ 2.0, 0.0, 1.0 }, frame.up, 30.0);
	const Mat4 motion = RibbonMath::multiply(FootSolve::ankleMotion(chain, frame, authored), heading);
	const Channels solved = FootSolve::solveAnkleChannels(chain, frame, Channels{}, motion);
	CHECK_CLOSE(solved.footRoll, -20.0, 1.0e-9);
	CHECK_CLOSE(solved.bank, 10.0, 1.0e-9);
	CHECK_CLOSE(solved.ballRaise, 0.0, 1.0e-12);
	CHECK_CLOSE(solved.breakAngle, 30.0, 1.0e-12);
}

CORE_TEST(footGaugeIgnoresAUniformScale)
{
	// A rig scaled through its Layout scales the ankle's motion: the channels read off it are the unscaled ones.
	const std::vector<Pivot> chain = leftFoot();
	const Frame frame = FootSolve::footFrame(chain);
	Channels authored;
	authored.footRoll = -20.0;
	authored.bank = 10.0;
	Mat4 scaled = FootSolve::ankleMotion(chain, frame, authored);
	for (int r = 0; r < 3; ++r)
	{
		for (int c = 0; c < 3; ++c)
		{
			scaled.m[r][c] *= 1.95;
		}
	}
	const Channels solved = FootSolve::solveAnkleChannels(chain, frame, Channels{}, scaled);
	CHECK_CLOSE(solved.footRoll, -20.0, 1.0e-9);
	CHECK_CLOSE(solved.bank, 10.0, 1.0e-9);
}

CORE_TEST(footGaugeSubtractsTheSidecarsRaisesAndBallRoll)
{
	// The author's gauge (2026-11-25): a pose authored with footRoll 15, ballRaise 10, heelRaise -5 and ballRoll 4, restored with the
	// sidecar's raises and ballRoll, gives back footRoll 15 and bank 0, not the whole pitch and roll.
	const std::vector<Pivot> chain = leftFoot();
	const Frame frame = FootSolve::footFrame(chain);
	Channels authored;
	authored.footRoll = 15.0;
	authored.ballRaise = 10.0;
	authored.heelRaise = -5.0;
	authored.ballRoll = 4.0;
	Channels sidecar = authored;
	sidecar.footRoll = 0.0;
	const Channels solved = FootSolve::solveAnkleChannels(chain, frame, sidecar, FootSolve::ankleMotion(chain, frame, authored));
	CHECK_CLOSE(solved.footRoll, 15.0, 1.0e-6);
	CHECK_CLOSE(solved.bank, 0.0, 1.0e-6);
	CHECK_CLOSE(solved.ballRaise, 10.0, 1.0e-12);
	CHECK_CLOSE(solved.ballRoll, 4.0, 1.0e-12);

	// Without the sidecar the same pose puts every pitch in footRoll. A heel raise lifts the toes, the heel regime's way (a negative
	// footRoll), so heelRaise -5 is +5 of pitch: 15 + 10 + 5.
	const Channels bare = FootSolve::solveAnkleChannels(chain, frame, Channels{}, FootSolve::ankleMotion(chain, frame, authored));
	CHECK_CLOSE(bare.footRoll, 30.0, 1.0e-6);
	CHECK_CLOSE(bare.ballRaise, 0.0, 1.0e-12);
}

CORE_TEST(footGaugeKeepsGivenChannelsThatAlreadyMakeTheTurn)
{
	// A sidecar that holds every channel of a pose, swivels and a ball roll with a bank included (which don't decompose into footRoll
	// and bank on their own), gives them back unchanged under any heading of the handle.
	const std::vector<Pivot> chain = leftFoot();
	const Frame frame = FootSolve::footFrame(chain);
	Channels authored;
	authored.footRoll = 22.0;
	authored.bank = -7.0;
	authored.ballRoll = 5.0;
	authored.heelSwivel = 9.0;
	authored.toeSwivel = -4.0;
	authored.ballRaise = 6.0;
	const Mat4 heading = FootSolve::rotationAbout({ 1.0, 0.0, -3.0 }, frame.up, -25.0);
	const Mat4 motion = RibbonMath::multiply(FootSolve::ankleMotion(chain, frame, authored), heading);
	const Channels solved = FootSolve::solveAnkleChannels(chain, frame, authored, motion);
	CHECK_CLOSE(solved.footRoll, 22.0, 1.0e-9);
	CHECK_CLOSE(solved.bank, -7.0, 1.0e-9);
}

CORE_TEST(footToeTapIsSolvedUnderTheCarryingPivots)
{
	// The toe bone is carried by the ball swivel and the heel, toe tip and bank pivots before the toe pivot: a toe tap of 25 under a roll
	// of 40, a bank of -8 and a ball swivel of 20 reads back as 25.
	const std::vector<Pivot> chain = leftFoot();
	const Frame frame = FootSolve::footFrame(chain);
	Channels channels;
	channels.footRoll = 40.0;
	channels.bank = -8.0;
	channels.ballSwivel = 20.0;
	channels.toeTap = 25.0;
	const int toeIndex = FootSolve::findPivot(chain, Role::Toe);
	CHECK(toeIndex >= 0);
	const Mat4 toe = FootSolve::toeMotion(chain, static_cast<std::size_t>(toeIndex), frame, channels);
	CHECK_CLOSE(FootSolve::solveToeTap(chain, static_cast<std::size_t>(toeIndex), frame, channels, toe), 25.0, 1.0e-9);
}

CORE_TEST(footToeRoleAndOversizeCountsLeaveTheAnkleAlone)
{
	const std::vector<Pivot> chain = leftFoot();
	const Frame frame = FootSolve::footFrame(chain);
	Channels channels;
	channels.footRoll = 20.0;
	channels.toeTap = 25.0;
	// Folding past the chain is the whole chain, and the Toe pivot's own tap does not reach the ankle.
	const Vec3 whole = FootSolve::transformPoint(FootSolve::foldChain(chain, 99, frame, channels), kAnkle);
	checkPoint(whole, 0.0, 12.989863, -2.298921);
	// An index past the chain is the identity rather than a read past its end.
	checkPoint(FootSolve::transformPoint(FootSolve::toeMotion(chain, 99, frame, channels), kAnkle), 0.0, 8.0, -6.0);
}

CORE_TEST(footBankPivotsAloneStillTurn)
{
	// A chain with the bank pivots and no heel or toe: the bank turns about bankOut and nothing else moves. With no heel the
	// frame starts at the first pivot, so the toe tip is placed straight ahead of it to keep the foot facing +Z.
	const std::vector<Pivot> chain{ { Role::BankOut, { 4.0, 0.0, 10.0 } }, { Role::BankIn, { -4.0, 0.0, 10.0 } }, { Role::ToeTip, { 4.0, 0.0, 18.0 } } };
	const Frame frame = FootSolve::footFrame(chain);
	Channels channels;
	channels.bank = 10.0;
	checkPoint(FootSolve::transformPoint(FootSolve::ankleMotion(chain, frame, channels), kAnkle), 1.449954, 8.573055, -6.0);
}
