// core/RibbonMath and core/ControlPlacement for the anchored Ribbon (D80, design/limb-split.md): a curve through three anchor frames, its
// two ends counted as controls with no control of their own.
//
// Every expected number is worked by hand. The limb is an upper bone 4 long along +X from the origin and a lower bone 4 long along +Y
// from (4, 0, 0), so the mid anchor's parameter is 4 / 8 = 0.5.

#include "CoreTests.h"

#include "core/ControlPlacement.h"
#include "core/RibbonMath.h"

#include <vector>

namespace
{
	constexpr double kTolerance{ 1.0e-9 };

	using VectorMath::makeFrame;

	// A frame whose X points along `x` (in the XY plane) and whose Z is world Z, at `o`.
	RibbonMath::Mat4 planarFrame(double xx, double xy, double ox, double oy)
	{
		return makeFrame({ xx, xy, 0.0 }, { -xy, xx, 0.0 }, { 0.0, 0.0, 1.0 }, { ox, oy, 0.0 });
	}

	void checkSameMatrix(const RibbonMath::Mat4& a, const RibbonMath::Mat4& b)
	{
		for (int r = 0; r < 4; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				CHECK_CLOSE(a.m[r][c], b.m[r][c], 1.0e-6);
			}
		}
	}
}

CORE_TEST(three_limb_controls_are_the_ends_and_the_mid)
{
	// An anchored ribbon with one interior control: the curve's two ends and the mid anchor's parameter, 4 / 8.
	const std::vector<double> u = ControlPlacement::limbControlParameters(4.0, 4.0, 3);
	CHECK(u.size() == 3);
	CHECK_CLOSE(u[0], 0.0, kTolerance);
	CHECK_CLOSE(u[1], 0.5, kTolerance);
	CHECK_CLOSE(u[2], 1.0, kTolerance);
}

CORE_TEST(five_limb_controls_put_one_between_each_end_and_the_mid)
{
	// Two extras on equal bones: one on each, halfway along it (0.25 and 0.75), with the mid at 0.5.
	const std::vector<double> u = ControlPlacement::limbControlParameters(4.0, 4.0, 5);
	CHECK(u.size() == 5);
	const double expected[5] = { 0.0, 0.25, 0.5, 0.75, 1.0 };
	for (int j = 0; j < 5; ++j)
	{
		CHECK_CLOSE(u[j], expected[j], kTolerance);
	}
}

CORE_TEST(an_anchored_curve_with_one_interior_control_gives_every_bind_joint_its_rest)
{
	// Bind joints at the start, halfway down each bone and at the end, along their bones, and one at the mid along the bisector (45
	// degrees). At rest (the anchors where they were built, no local on any control) each comes back exactly where it was.
	const RibbonMath::Mat4 anchors[3] = { planarFrame(1.0, 0.0, 0.0, 0.0), planarFrame(0.0, 1.0, 4.0, 0.0), planarFrame(0.0, 1.0, 4.0, 4.0) };
	const double half = 0.70710678118654752;
	const RibbonMath::Mat4 binds[5] = { planarFrame(1.0, 0.0, 0.0, 0.0), planarFrame(1.0, 0.0, 2.0, 0.0), planarFrame(half, half, 4.0, 0.0),
		planarFrame(0.0, 1.0, 4.0, 2.0), planarFrame(0.0, 1.0, 4.0, 4.0) };
	const std::vector<double> controlU = ControlPlacement::limbControlParameters(4.0, 4.0, 3);

	RibbonMath::Setup setup;
	RibbonMath::Mat4 controlRest[RibbonMath::MaxControls];
	const char* pReason = "";
	CHECK(RibbonMath::buildAnchoredSetup(anchors, 3, controlU.data(), 3, binds, 5, setup, controlRest, pReason));
	CHECK(setup.controlCount == 3);
	CHECK(setup.fkAnchors);
	// The interior control is placed on the mid anchor.
	CHECK_CLOSE(controlRest[1].m[3][0], 4.0, 1.0e-9);
	CHECK_CLOSE(controlRest[1].m[3][1], 0.0, 1.0e-9);

	RibbonMath::Input input;
	for (int k = 0; k < 3; ++k)
	{
		input.fkWorld[k] = anchors[k];
	}
	input.startOPM = controlRest[0];
	input.endOPM = controlRest[2];
	RibbonMath::Output output;
	RibbonMath::evaluate(setup, RibbonMath::Params{}, input, output);
	for (int i = 0; i < 5; ++i)
	{
		checkSameMatrix(output.bindOPM[i], binds[i]);
	}
	// The curve's two ends sit on the start and end anchors.
	CHECK_CLOSE(output.controlWorld[0].m[3][0], 0.0, 1.0e-6);
	CHECK_CLOSE(output.controlWorld[2].m[3][0], 4.0, 1.0e-6);
	CHECK_CLOSE(output.controlWorld[2].m[3][1], 4.0, 1.0e-6);
}
