// core/RibbonMath twistRamp: which share of the fk roll each ik control takes, from the template's [position, share] pairs.
//
// Every expected number is worked by hand. The shares are the pairs' straight line at each control's parameter, held at the first
// pair's share before it and the last pair's after it.

#include "CoreTests.h"

#include "core/RibbonMath.h"

#include <string>

namespace
{
	constexpr double kTolerance{ 1.0e-12 };

	RibbonMath::Setup setupWithKnots(std::initializer_list<double> knots)
	{
		RibbonMath::Setup setup;
		setup.controlCount = static_cast<int>(knots.size());
		int j = 0;
		for (const double u : knots)
		{
			setup.knotU[j++] = u;
		}
		return setup;
	}
}

CORE_TEST(a_setup_takes_all_of_the_roll_by_default)
{
	const RibbonMath::Setup setup;
	for (int j = 0; j < RibbonMath::MaxControls; ++j)
	{
		CHECK_CLOSE(setup.fkRollShare[j], 1.0, kTolerance);
	}
}

CORE_TEST(an_empty_ramp_leaves_every_share_at_one)
{
	RibbonMath::Setup setup = setupWithKnots({ 0.0, 0.5, 1.0 });
	RibbonMath::applyTwistRamp(setup, {});
	for (int j = 0; j < 3; ++j)
	{
		CHECK_CLOSE(setup.fkRollShare[j], 1.0, kTolerance);
	}
}

CORE_TEST(the_authors_example_ramp_gives_zero_035_and_one_at_the_ends_and_middle)
{
	RibbonMath::Setup setup = setupWithKnots({ 0.0, 0.5, 1.0 });
	RibbonMath::applyTwistRamp(setup, { { 0.0, 0.0 }, { 0.5, 0.35 }, { 1.0, 1.0 } });
	CHECK_CLOSE(setup.fkRollShare[0], 0.0, kTolerance);
	CHECK_CLOSE(setup.fkRollShare[1], 0.35, kTolerance);
	CHECK_CLOSE(setup.fkRollShare[2], 1.0, kTolerance);
}

CORE_TEST(a_share_between_two_pairs_is_on_the_line_between_them)
{
	// Knots at 0.25 and 0.75 are the middle of each half: 0.175 is half of 0 to 0.35, 0.675 is half of 0.35 to 1.
	RibbonMath::Setup setup = setupWithKnots({ 0.25, 0.75 });
	RibbonMath::applyTwistRamp(setup, { { 0.0, 0.0 }, { 0.5, 0.35 }, { 1.0, 1.0 } });
	CHECK_CLOSE(setup.fkRollShare[0], 0.175, kTolerance);
	CHECK_CLOSE(setup.fkRollShare[1], 0.675, kTolerance);
}

CORE_TEST(the_first_and_last_shares_are_held_outside_the_pairs)
{
	// Pairs at 0.25 (0.2) and 0.75 (0.8): 0 and 1 are held, 0.5 is the middle of the line.
	RibbonMath::Setup setup = setupWithKnots({ 0.0, 0.5, 1.0 });
	RibbonMath::applyTwistRamp(setup, { { 0.25, 0.2 }, { 0.75, 0.8 } });
	CHECK_CLOSE(setup.fkRollShare[0], 0.2, kTolerance);
	CHECK_CLOSE(setup.fkRollShare[1], 0.5, kTolerance);
	CHECK_CLOSE(setup.fkRollShare[2], 0.8, kTolerance);
}

CORE_TEST(one_pair_is_that_share_everywhere)
{
	RibbonMath::Setup setup = setupWithKnots({ 0.0, 0.5, 1.0 });
	RibbonMath::applyTwistRamp(setup, { { 0.5, 0.4 } });
	for (int j = 0; j < 3; ++j)
	{
		CHECK_CLOSE(setup.fkRollShare[j], 0.4, kTolerance);
	}
}

CORE_TEST(a_ramp_is_valid_when_it_is_ascending_within_zero_to_one)
{
	std::string reason;
	CHECK(RibbonMath::validateTwistRamp({ { 0.0, 0.0 }, { 0.5, 0.35 }, { 1.0, 1.0 } }, reason));
	CHECK(RibbonMath::validateTwistRamp({ { 0.5, 0.4 } }, reason));
}

CORE_TEST(a_ramp_with_no_pair_is_not_valid)
{
	std::string reason;
	CHECK(!RibbonMath::validateTwistRamp({}, reason));
	CHECK(reason.find("at least one") != std::string::npos);
}

CORE_TEST(positions_must_be_inside_zero_to_one)
{
	std::string reason;
	CHECK(!RibbonMath::validateTwistRamp({ { -0.1, 0.0 }, { 1.0, 1.0 } }, reason));
	CHECK(reason.find("pair 0") != std::string::npos);
	CHECK(!RibbonMath::validateTwistRamp({ { 0.0, 0.0 }, { 1.5, 1.0 } }, reason));
	CHECK(reason.find("pair 1") != std::string::npos);
}

CORE_TEST(positions_must_increase)
{
	std::string reason;
	CHECK(!RibbonMath::validateTwistRamp({ { 0.0, 0.0 }, { 0.5, 0.5 }, { 0.5, 1.0 } }, reason));
	CHECK(reason.find("pair 2") != std::string::npos);
	CHECK(!RibbonMath::validateTwistRamp({ { 0.5, 0.5 }, { 0.2, 1.0 } }, reason));
}

CORE_TEST(shares_must_be_inside_zero_to_one)
{
	std::string reason;
	CHECK(!RibbonMath::validateTwistRamp({ { 0.0, -0.2 }, { 1.0, 1.0 } }, reason));
	CHECK(reason.find("share") != std::string::npos);
	CHECK(!RibbonMath::validateTwistRamp({ { 0.0, 0.0 }, { 1.0, 1.2 } }, reason));
}

CORE_TEST(a_value_that_is_not_a_number_is_not_valid)
{
	std::string reason;
	const double nan = std::nan("");
	CHECK(!RibbonMath::validateTwistRamp({ { nan, 0.0 } }, reason));
	CHECK(!RibbonMath::validateTwistRamp({ { 0.0, nan } }, reason));
}
