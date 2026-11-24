// core/FootSolve: the frame, the peel split, the bank pivot choice, the chain fold and the rotation split (D74,
// design/reverse-foot.md sections 7, 8, 10 and 15).
//
// Every expected number is worked by hand from one foot, in a frame with forward +Z, up +Y and lateral +X (a character
// facing +Z, so lateral is its left). The pivots sit on the ground (y = 0): heel z = -10, ball z = 10, bankOut (4, 0, 10),
// bankIn (-4, 0, 10), toe tip z = 18, and the toe pivot (0, 1, 12). The ankle is at (0, 8, -6). A turn about a pivot is
// the plain 2D rotation of the point's offset from the pivot:
//   about lateral (+X): y' = dy cos - dz sin, z' = dy sin + dz cos
//   about up (+Y):      x' = dx cos + dz sin, z' = -dx sin + dz cos
//   about forward (+Z): x' = dx cos - dy sin, y' = dx sin + dy cos
// and a heel pivot turns by minus its angle (toes up), the ball and toe tip by plus theirs, a bank by minus its value.
// The chain is root first, so the toe tip turns the ankle first and the heel last (the literal hierarchy, D74).
// The numbers were worked with a separate calculation (the formulas above, no FootSolve code), to 6 places.

#include "CoreTests.h"

#include "core/FootSolve.h"
#include "core/RibbonMath.h"

namespace
{
	using FootSolve::Channels;
	using FootSolve::Frame;
	using FootSolve::Pivot;
	using FootSolve::Role;
	using VectorMath::Mat4;
	using VectorMath::Vec3;

	constexpr double kTolerance{ 1.0e-5 };

	std::vector<Pivot> leftFoot()
	{
		return {
			{ Role::Heel, { 0.0, 0.0, -10.0 } },
			{ Role::Ball, { 0.0, 0.0, 10.0 } },
			{ Role::BankOut, { 4.0, 0.0, 10.0 } },
			{ Role::BankIn, { -4.0, 0.0, 10.0 } },
			{ Role::ToeTip, { 0.0, 0.0, 18.0 } },
			{ Role::Toe, { 0.0, 1.0, 12.0 } },
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

CORE_TEST(footRolesAreTheDocumentedSix)
{
	Role role{ Role::Heel };
	CHECK(FootSolve::findRole("heel", role) && role == Role::Heel);
	CHECK(FootSolve::findRole("ball", role) && role == Role::Ball);
	CHECK(FootSolve::findRole("bankOut", role) && role == Role::BankOut);
	CHECK(FootSolve::findRole("bankIn", role) && role == Role::BankIn);
	CHECK(FootSolve::findRole("toeTip", role) && role == Role::ToeTip);
	CHECK(FootSolve::findRole("toe", role) && role == Role::Toe);
	CHECK(!FootSolve::findRole("ankle", role));
	CHECK(!FootSolve::findRole("Heel", role));
	CHECK(FootSolve::allowedRoles() == "heel, ball, bankOut, bankIn, toeTip, toe");
	CHECK(std::string(FootSolve::roleName(Role::BankOut)) == "bankOut");
}

CORE_TEST(footRoleChecks)
{
	CHECK(FootSolve::checkRoles({ Role::Heel, Role::Ball, Role::ToeTip, Role::Toe }).empty());
	CHECK(FootSolve::checkRoles({ Role::Heel, Role::Ball, Role::BankOut, Role::BankIn, Role::ToeTip }).empty());
	CHECK(FootSolve::checkRoles({}).empty());

	const std::vector<std::string> twice = FootSolve::checkRoles({ Role::Heel, Role::Ball, Role::Heel });
	CHECK(twice.size() == 1);
	CHECK(twice[0] == "the role heel is listed twice");

	const std::vector<std::string> noIn = FootSolve::checkRoles({ Role::Ball, Role::BankOut });
	CHECK(noIn.size() == 1);
	CHECK(noIn[0].find("bankOut needs a bankIn") != std::string::npos);

	const std::vector<std::string> noOut = FootSolve::checkRoles({ Role::Ball, Role::BankIn });
	CHECK(noOut.size() == 1);
	CHECK(noOut[0].find("bankIn needs a bankOut") != std::string::npos);

	// Three problems in one list are three messages.
	CHECK(FootSolve::checkRoles({ Role::Ball, Role::Ball, Role::BankOut, Role::Heel, Role::Heel }).size() == 3);
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
	const std::vector<Pivot> chain{ { Role::Ball, { 0.0, 0.0, 0.0 } }, { Role::Toe, { 0.0, 0.0, -5.0 } } };
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

	const std::vector<Pivot> noBank{ { Role::Heel, { 0.0, 0.0, -10.0 } }, { Role::Ball, { 0.0, 0.0, 10.0 } } };
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
	// footRoll 50 with a break of 30: 30 about the ball and 20 about the toe tip, the toe tip turning the ankle first.
	Channels channels;
	channels.footRoll = 50.0;
	checkPoint(ankleAt(channels), 0.0, 19.527368, 7.629656);
}

CORE_TEST(footRollJustPastTheBreakStartsTheToeTip)
{
	Channels channels;
	channels.footRoll = 35.0;
	checkPoint(ankleAt(channels), 0.0, 16.319051, 1.857166);
}

CORE_TEST(footBallRaiseNeverStartsTheToeTip)
{
	// footRoll 25 and ballRaise 10 is 35 about the ball and nothing about the toe tip: the raise is added after the split.
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
	// Ball 20 and bank 10 on the bankOut pivot: the bank turns the ankle first, the ball second.
	Channels channels;
	channels.footRoll = 20.0;
	channels.bank = 10.0;
	checkPoint(ankleAt(channels), 1.449954, 13.528359, -2.102925);
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
	const Vec3 expected = faceX({ 1.449954, 13.528359, -2.102925 });
	checkPoint(turned, expected.x, expected.y, expected.z);

	Channels toeTip;
	toeTip.footRoll = 50.0;
	const Vec3 tipTurned = FootSolve::transformPoint(FootSolve::ankleMotion(chain, frame, toeTip), faceX(kAnkle));
	const Vec3 tipExpected = faceX({ 0.0, 19.527368, 7.629656 });
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
	// A point on the toe bone, 4 beyond the toe pivot, tips down about it.
	const std::size_t toe = static_cast<std::size_t>(FootSolve::findPivot(chain, Role::Toe));
	checkPoint(FootSolve::transformPoint(FootSolve::toeMotion(chain, toe, frame, channels), { 0.0, 1.0, 16.0 }), 0.0, -0.690473, 15.625231);
}

CORE_TEST(footToeIsCarriedByThePivotsBeforeIt)
{
	const std::vector<Pivot> chain = leftFoot();
	const Frame frame = FootSolve::footFrame(chain);
	const std::size_t toe = static_cast<std::size_t>(FootSolve::findPivot(chain, Role::Toe));
	Channels channels;
	channels.footRoll = 20.0;
	// The toe pivot rides the ball's turn.
	checkPoint(FootSolve::transformPoint(FootSolve::foldChain(chain, toe, frame, channels), chain[toe].position), 0.0, 0.255652, 12.221405);
	// A tapped toe bone point is tapped first and carried second.
	channels.toeTap = 25.0;
	checkPoint(FootSolve::transformPoint(FootSolve::toeMotion(chain, toe, frame, channels), { 0.0, 1.0, 16.0 }), 0.0, -2.572775, 15.049833);
}

CORE_TEST(footFindPivot)
{
	const std::vector<Pivot> chain = leftFoot();
	CHECK(FootSolve::findPivot(chain, Role::Heel) == 0);
	CHECK(FootSolve::findPivot(chain, Role::Toe) == 5);
	const std::vector<Pivot> ballOnly{ { Role::Ball, {} } };
	CHECK(FootSolve::findPivot(ballOnly, Role::Heel) == -1);
}

CORE_TEST(footDecomposesPitchAndRoll)
{
	// Ball 20 with a bank of 10: the rotation is a pitch of 20 about lateral after a roll of -10 about forward.
	const std::vector<Pivot> chain = leftFoot();
	const Frame frame = FootSolve::footFrame(chain);
	Channels channels;
	channels.footRoll = 20.0;
	channels.bank = 10.0;
	FootSolve::Euler euler = FootSolve::decomposeRotation(FootSolve::ankleMotion(chain, frame, channels), frame);
	CHECK_CLOSE(euler.pitch, 20.0, 1.0e-9);
	CHECK_CLOSE(euler.roll, -10.0, 1.0e-9);
	CHECK_CLOSE(euler.yaw, 0.0, 1.0e-9);

	// The heel regime's pitch is negative: footRoll -20 is a pitch of -20.
	channels.footRoll = -20.0;
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
	channels.footRoll = 20.0;
	channels.bank = 10.0;
	const Mat4 heading = FootSolve::rotationAbout({ 0.0, 0.0, 0.0 }, frame.up, 30.0);
	const Mat4 motion = RibbonMath::multiply(FootSolve::ankleMotion(chain, frame, channels), heading);
	const FootSolve::Euler euler = FootSolve::decomposeRotation(motion, frame);
	CHECK_CLOSE(euler.yaw, 30.0, 1.0e-9);
	CHECK_CLOSE(euler.pitch, 20.0, 1.0e-9);
	CHECK_CLOSE(euler.roll, -10.0, 1.0e-9);
}

CORE_TEST(footDecomposesOnAFootFacingX)
{
	// The angles are read in the foot's own axes, so a foot facing +X gives the same ones.
	const std::vector<Pivot> chain = facingX(leftFoot());
	const Frame frame = FootSolve::footFrame(chain);
	Channels channels;
	channels.footRoll = 20.0;
	channels.bank = 10.0;
	const FootSolve::Euler euler = FootSolve::decomposeRotation(FootSolve::ankleMotion(chain, frame, channels), frame);
	CHECK_CLOSE(euler.pitch, 20.0, 1.0e-9);
	CHECK_CLOSE(euler.roll, -10.0, 1.0e-9);
	CHECK_CLOSE(euler.yaw, 0.0, 1.0e-9);
}

CORE_TEST(footDecompositionRebuildsTheRotationWhenTheChainSandwichesTheRoll)
{
	// Past the break the bank turns between the ball's pitch and the toe tip's, so pitch and roll are not footRoll and
	// bank any more. The decomposition still gives angles whose rotation is the motion's.
	const std::vector<Pivot> chain = leftFoot();
	const Frame frame = FootSolve::footFrame(chain);
	Channels channels;
	channels.footRoll = 50.0;
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
	// A chain with the bank pivots and no heel or ball: the bank turns about bankOut and nothing else moves. With no heel the
	// frame starts at the first pivot, so the toe tip is placed straight ahead of it to keep the foot facing +Z.
	const std::vector<Pivot> chain{ { Role::BankOut, { 4.0, 0.0, 10.0 } }, { Role::BankIn, { -4.0, 0.0, 10.0 } }, { Role::ToeTip, { 4.0, 0.0, 18.0 } } };
	const Frame frame = FootSolve::footFrame(chain);
	Channels channels;
	channels.bank = 10.0;
	checkPoint(FootSolve::transformPoint(FootSolve::ankleMotion(chain, frame, channels), kAnkle), 1.449954, 8.573055, -6.0);
}
