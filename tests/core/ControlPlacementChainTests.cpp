// core/ControlPlacement checkChain and describeChainProblem: the chain checks a Ribbon's bind joints and a limb's joints pass
// before their controls are placed.
//
// The chain is the ribbon_basic skeleton: tail1 to tail5 along +X at 0, 2, 4, 6 and 8, every joint with world axes. A joint is
// turned by giving its frame other axes. Every expected number is worked by hand.

#include "CoreTests.h"

#include "core/ControlPlacement.h"

#include <cmath>
#include <string>
#include <vector>

namespace
{
	using ControlPlacement::ChainProblem;
	using VectorMath::Mat4;
	using VectorMath::Vec3;

	constexpr double kPi{ 3.14159265358979323846 };
	constexpr double kTolerance{ 1.0e-9 };

	const std::vector<std::string> kNames{ "tail1", "tail2", "tail3", "tail4", "tail5" };

	Mat4 jointAt(double x)
	{
		return VectorMath::makeFrame({ 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 }, { x, 0.0, 0.0 });
	}

	std::vector<Mat4> straightChain()
	{
		return { jointAt(0.0), jointAt(2.0), jointAt(4.0), jointAt(6.0), jointAt(8.0) };
	}

	// A joint at (x, 0, 0) turned `degrees` about world Z.
	Mat4 turnedAboutZ(double x, double degrees)
	{
		const double c = std::cos(degrees * kPi / 180.0);
		const double s = std::sin(degrees * kPi / 180.0);
		return VectorMath::makeFrame({ c, s, 0.0 }, { -s, c, 0.0 }, { 0.0, 0.0, 1.0 }, { x, 0.0, 0.0 });
	}

	bool contains(const std::string& text, const std::string& part)
	{
		return text.find(part) != std::string::npos;
	}
}

CORE_TEST(a_straight_chain_passes_and_runs_toward_the_tip)
{
	const auto check = ControlPlacement::checkChain(straightChain());
	CHECK(check.problem == ChainProblem::None);
	CHECK_CLOSE(check.sign, 1.0, 0.0);
	CHECK(ControlPlacement::describeChainProblem(check, kNames).empty());
}

CORE_TEST(a_chain_whose_axes_all_run_back_passes_with_a_negative_sign)
{
	// Every joint turned 180 about Y: X runs -X down a chain that goes +X, as a mirrored limb keeps the other side's axes.
	std::vector<Mat4> joints;
	for (const double x : { 0.0, 2.0, 4.0, 6.0, 8.0 })
	{
		joints.push_back(VectorMath::makeFrame({ -1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, -1.0 }, { x, 0.0, 0.0 }));
	}
	const auto check = ControlPlacement::checkChain(joints);
	CHECK(check.problem == ChainProblem::None);
	CHECK_CLOSE(check.sign, -1.0, 0.0);
}

CORE_TEST(a_joint_turned_55_degrees_off_the_chain_passes)
{
	// cos 55 = 0.574 is above cos 60 = 0.5: inside the limit.
	auto joints = straightChain();
	joints[2] = turnedAboutZ(4.0, 55.0);
	CHECK(ControlPlacement::checkChain(joints).problem == ChainProblem::None);
}

CORE_TEST(a_joint_turned_65_degrees_off_the_chain_is_across_it)
{
	// tail3's X axis is (cos 65, sin 65, 0) and the line to tail4 is +X: 65 degrees, past the 60 degree limit.
	auto joints = straightChain();
	joints[2] = turnedAboutZ(4.0, 65.0);
	const auto check = ControlPlacement::checkChain(joints);
	CHECK(check.problem == ChainProblem::AxisAcrossChain);
	CHECK(check.jointIndex == 2);
	CHECK_CLOSE(check.angleDegrees, 65.0, kTolerance);
	CHECK_CLOSE(check.limitDegrees, 60.0, 0.0);
	const std::string message = ControlPlacement::describeChainProblem(check, kNames);
	CHECK(contains(message, "joint tail3 "));
	CHECK(contains(message, "65 degrees off the chain"));
	CHECK(contains(message, "within 60 degrees"));
}

CORE_TEST(a_joint_that_points_back_while_the_others_point_on_disagrees)
{
	// tail3 turned 180 about Y: its X axis is -X, along the chain's line but the other way from tail1's.
	auto joints = straightChain();
	joints[2] = VectorMath::makeFrame({ -1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, -1.0 }, { 4.0, 0.0, 0.0 });
	const auto check = ControlPlacement::checkChain(joints);
	CHECK(check.problem == ChainProblem::AxisDirectionDisagrees);
	CHECK(check.jointIndex == 2);
	const std::string message = ControlPlacement::describeChainProblem(check, kNames);
	CHECK(contains(message, "joint tail3 "));
	CHECK(contains(message, "from tail1"));
}

CORE_TEST(two_joints_on_each_other_have_no_direction)
{
	// tail2 moved onto tail1 at the origin: the first chord has no length, and the joint named is the second of the pair.
	auto joints = straightChain();
	joints[1] = jointAt(0.0);
	const auto check = ControlPlacement::checkChain(joints);
	CHECK(check.problem == ChainProblem::JointsOnTopOfEachOther);
	CHECK(check.jointIndex == 1);
	CHECK(contains(ControlPlacement::describeChainProblem(check, kNames), "joint tail2 sits on the joint before it"));
}

CORE_TEST(one_joint_is_too_few)
{
	const auto check = ControlPlacement::checkChain({ jointAt(0.0) });
	CHECK(check.problem == ChainProblem::TooFewJoints);
	CHECK(contains(ControlPlacement::describeChainProblem(check, kNames), "at least two joints"));
}
