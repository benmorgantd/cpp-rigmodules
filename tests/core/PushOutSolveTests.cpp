// core/PushOutSolve: the point against one capsule, the offset and mirror matrices and the draw map (D79,
// design/push-out.md).
//
// Every expected number is worked by hand. The capsule has radius 2 and half length 3 along X, its frame at the origin with
// the identity unless a test says otherwise. The push is the distance still needed to reach radius + margin from the nearest
// point of the segment, along the line from that point to the pushed point:
//   (1, 1, 0):  nearest (1, 0, 0), distance 1, push 1 along +Y
//   (4, 1, 0):  nearest (3, 0, 0), away (1, 1, 0), distance sqrt 2, push 2 - sqrt 2 along (1, 1, 0) / sqrt 2

#include "CoreTests.h"

#include "core/ControlShapes.h"
#include "core/PushOutSolve.h"

#include <algorithm>
#include <cmath>

namespace
{
	using PushOutSolve::Capsule;
	using PushOutSolve::Result;
	using PushOutSolve::Settings;
	using VectorMath::Mat4;
	using VectorMath::Vec3;

	constexpr double kTolerance{ 1.0e-9 };

	const Capsule kCapsule{ 2.0, 3.0 };

	void checkPush(const Result& result, double x, double y, double z)
	{
		CHECK_CLOSE(result.push.x, x, kTolerance);
		CHECK_CLOSE(result.push.y, y, kTolerance);
		CHECK_CLOSE(result.push.z, z, kTolerance);
	}
}

CORE_TEST(pushOutLeavesAClearPointAlone)
{
	const Result result = PushOutSolve::solve(kCapsule, Mat4(), { 0.0, 5.0, 0.0 }, Settings());
	CHECK(!result.isPushed);
	checkPush(result, 0.0, 0.0, 0.0);
	// Exactly on the surface is clear: nothing is left to push.
	CHECK(!PushOutSolve::solve(kCapsule, Mat4(), { 0.0, 2.0, 0.0 }, Settings()).isPushed);
}

CORE_TEST(pushOutMovesAPointOutAlongTheLineFromTheSegment)
{
	const Result side = PushOutSolve::solve(kCapsule, Mat4(), { 1.0, 1.0, 0.0 }, Settings());
	CHECK(side.isPushed);
	checkPush(side, 0.0, 1.0, 0.0);

	// Past the end it is the cap, a sphere about the segment's end.
	const Result cap = PushOutSolve::solve(kCapsule, Mat4(), { 4.0, 1.0, 0.0 }, Settings());
	const double depth = 2.0 - std::sqrt(2.0);
	checkPush(cap, depth * std::sqrt(0.5), depth * std::sqrt(0.5), 0.0);
}

CORE_TEST(pushOutOnTheSegmentGoesAlongLocalZ)
{
	// No line to follow: the push is the whole radius along the capsule's +Z, on every frame.
	checkPush(PushOutSolve::solve(kCapsule, Mat4(), { 1.0, 0.0, 0.0 }, Settings()), 0.0, 0.0, 2.0);

	// Turned a quarter about X (Z becomes -Y: row 2 is (0, -1, 0)), the same point goes along that +Z.
	const Mat4 turned = VectorMath::makeFrame({ 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 }, {});
	checkPush(PushOutSolve::solve(kCapsule, turned, { 1.0, 0.0, 0.0 }, Settings()), 0.0, -2.0, 0.0);
}

CORE_TEST(pushOutMarginMaxPushAndWeight)
{
	// Margin grows the radius: (1, 2.2, 0) is 2.2 from the segment, 0.3 short of 2.5.
	Settings margin;
	margin.margin = 0.5;
	checkPush(PushOutSolve::solve(kCapsule, Mat4(), { 1.0, 2.2, 0.0 }, margin), 0.0, 0.3, 0.0);

	// (1, 0.5, 0) is 1.5 deep. maxPush caps it, and weight scales what is left.
	Settings capped;
	capped.maxPush = 0.5;
	checkPush(PushOutSolve::solve(kCapsule, Mat4(), { 1.0, 0.5, 0.0 }, capped), 0.0, 0.5, 0.0);
	Settings half;
	half.weight = 0.5;
	checkPush(PushOutSolve::solve(kCapsule, Mat4(), { 1.0, 0.5, 0.0 }, half), 0.0, 0.75, 0.0);
	Settings none;
	none.weight = 0.0;
	checkPush(PushOutSolve::solve(kCapsule, Mat4(), { 1.0, 0.5, 0.0 }, none), 0.0, 0.0, 0.0);
}

CORE_TEST(pushOutFollowsTheCapsuleFrameAndIgnoresItsScale)
{
	// A quarter turn about Z puts the segment along +Y, and the frame is at (10, 0, 0). The point (11, 1, 0) is 1 from the
	// segment at (10, 1, 0), so it goes 1 along +X.
	const Mat4 frame = VectorMath::makeFrame({ 0.0, 1.0, 0.0 }, { -1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 10.0, 0.0, 0.0 });
	checkPush(PushOutSolve::solve(kCapsule, frame, { 11.0, 1.0, 0.0 }, Settings()), 1.0, 0.0, 0.0);

	// The same frame scaled by 3 gives the same push: a scaled joint does not resize the capsule.
	const Mat4 scaled = VectorMath::makeFrame({ 0.0, 3.0, 0.0 }, { -3.0, 0.0, 0.0 }, { 0.0, 0.0, 3.0 }, { 10.0, 0.0, 0.0 });
	checkPush(PushOutSolve::solve(kCapsule, scaled, { 11.0, 1.0, 0.0 }, Settings()), 1.0, 0.0, 0.0);
}

CORE_TEST(pushOutValidateRefusesNumbersTheSolveCannotUse)
{
	std::string error;
	CHECK(PushOutSolve::validate(kCapsule, Settings(), error));
	CHECK(PushOutSolve::validate({ 1.0, 0.0 }, Settings(), error));   // a sphere

	CHECK(!PushOutSolve::validate({ 0.0, 1.0 }, Settings(), error));
	CHECK(error.rfind("radius", 0) == 0);
	CHECK(!PushOutSolve::validate({ 1.0, -1.0 }, Settings(), error));
	CHECK(error.rfind("halfLength", 0) == 0);
	CHECK(!PushOutSolve::validate(kCapsule, { -0.1, 1.0, 1.0 }, error));
	CHECK(error.rfind("margin", 0) == 0);
	CHECK(!PushOutSolve::validate(kCapsule, { 0.0, 0.0, 1.0 }, error));
	CHECK(error.rfind("maxPush", 0) == 0);
	CHECK(!PushOutSolve::validate(kCapsule, { 0.0, 1.0, 1.5 }, error));
	CHECK(error.rfind("weight", 0) == 0);
	CHECK(!PushOutSolve::validate({ std::nan(""), 1.0 }, Settings(), error));
}

CORE_TEST(pushOutOffsetMatrixTurnsXThenYThenZ)
{
	// 90 about Z turns X to +Y and Y to -X.
	const Mat4 z = PushOutSolve::offsetMatrix({ 1.0, 2.0, 3.0 }, { 0.0, 0.0, 90.0 });
	CHECK_CLOSE(z.m[0][0], 0.0, kTolerance);
	CHECK_CLOSE(z.m[0][1], 1.0, kTolerance);
	CHECK_CLOSE(z.m[1][0], -1.0, kTolerance);
	CHECK_CLOSE(z.m[1][1], 0.0, kTolerance);
	CHECK_CLOSE(z.m[3][0], 1.0, kTolerance);
	CHECK_CLOSE(z.m[3][1], 2.0, kTolerance);
	CHECK_CLOSE(z.m[3][2], 3.0, kTolerance);

	// 90 about X then 90 about Y: X is untouched by the first turn and goes to -Z by the second, Y goes to +Z and then +X.
	const Mat4 xy = PushOutSolve::offsetMatrix({}, { 90.0, 90.0, 0.0 });
	CHECK_CLOSE(xy.m[0][0], 0.0, kTolerance);
	CHECK_CLOSE(xy.m[0][2], -1.0, kTolerance);
	CHECK_CLOSE(xy.m[1][0], 1.0, kTolerance);
	CHECK_CLOSE(xy.m[1][2], 0.0, kTolerance);
}

CORE_TEST(pushOutReflectAcrossXMirrorsTheFrame)
{
	// A quarter turn about Z at (1, 2, 3), reflected across X = 0, is the opposite quarter turn at (-1, 2, 3).
	const Mat4 source = PushOutSolve::offsetMatrix({ 1.0, 2.0, 3.0 }, { 0.0, 0.0, 90.0 });
	const Mat4 mirror = PushOutSolve::reflectAcrossX(source);
	CHECK_CLOSE(mirror.m[0][1], -1.0, kTolerance);
	CHECK_CLOSE(mirror.m[1][0], 1.0, kTolerance);
	CHECK_CLOSE(mirror.m[2][2], 1.0, kTolerance);
	CHECK_CLOSE(mirror.m[3][0], -1.0, kTolerance);
	CHECK_CLOSE(mirror.m[3][1], 2.0, kTolerance);
	CHECK_CLOSE(mirror.m[3][2], 3.0, kTolerance);

	// Reflecting twice gives the frame back.
	const Mat4 back = PushOutSolve::reflectAcrossX(mirror);
	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			CHECK_CLOSE(back.m[r][c], source.m[r][c], kTolerance);
		}
	}
}

CORE_TEST(pushOutDrawPointKeepsRoundCaps)
{
	// Radius 2, half length 3. Mid-segment points stretch to the segment, a cap tip lands one radius past its end.
	const Vec3 middle = PushOutSolve::drawPoint({ 0.25, 0.5, 0.0 }, 2.0, 3.0);
	CHECK_CLOSE(middle.x, 1.5, kTolerance);
	CHECK_CLOSE(middle.y, 1.0, kTolerance);
	const Vec3 seam = PushOutSolve::drawPoint({ 0.5, 0.0, 1.0 }, 2.0, 3.0);
	CHECK_CLOSE(seam.x, 3.0, kTolerance);
	CHECK_CLOSE(seam.z, 2.0, kTolerance);
	const Vec3 tip = PushOutSolve::drawPoint({ -1.0, 0.0, 0.0 }, 2.0, 3.0);
	CHECK_CLOSE(tip.x, -5.0, kTolerance);
}

CORE_TEST(pushOutCapsuleDrawnFromTheLibraryMeshFitsTheSolve)
{
	// Every point of the real library capsule, mapped, lies within the radius of the segment, and the mesh reaches the caps'
	// tips: so the drawn shape is the shape the solve uses.
	const double radius = 1.5;
	const double halfLength = 4.0;
	const ShapeMesh& mesh = getShapeMesh(ShapeType::Capsule);
	CHECK(!mesh.points.empty());
	double reach = 0.0;
	double widest = 0.0;
	for (const ShapePoint& point : mesh.points)
	{
		const Vec3 mapped = PushOutSolve::drawPoint(point, radius, halfLength);
		const double onSegment = std::clamp(mapped.x, -halfLength, halfLength);
		const double away = std::sqrt((mapped.x - onSegment) * (mapped.x - onSegment) + mapped.y * mapped.y + mapped.z * mapped.z);
		// The library's points are written to 6 digits, so allow that much.
		CHECK(away <= radius + 1e-5);
		reach = std::max(reach, std::fabs(mapped.x));
		widest = std::max(widest, away);
	}
	CHECK_CLOSE(reach, halfLength + radius, 1e-5);
	CHECK_CLOSE(widest, radius, 1e-5);
}
