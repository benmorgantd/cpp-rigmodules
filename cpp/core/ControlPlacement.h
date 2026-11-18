#pragma once

#include "VectorMath.h"

#include <optional>
#include <string>
#include <vector>

// Where a module's controls go when no control joint says so (D57, design/derived-controls.md), with no Maya types. Pure
// functions of rest matrices, run in the read phase: nothing here runs during evaluation.
//
// Matrices are row-major with row vectors (Maya's convention), as in VectorMath.

namespace ControlPlacement
{
	using VectorMath::Mat4;
	using VectorMath::Vec3;

	// How far (degrees) a bind joint's X axis may be from the line of the chain before the template is rejected.
	inline constexpr double MaxAxisAngleDegrees{ 60.0 };
	// A limb whose mid joint is off the root-to-end line by less than this share of the limb's length counts as straight.
	inline constexpr double StraightLimbTolerance{ 0.001 };
	// The settings control's default rise above the end joint, as a share of the limb's length.
	inline constexpr double DefaultSettingsRise{ 0.25 };

	enum class ChainProblem
	{
		None,
		TooFewJoints,            // fewer than two joints
		JointsOnTopOfEachOther,  // two neighbouring joints share a position: the chain has no direction there
		AxisAcrossChain,         // a joint's X axis is more than MaxAxisAngleDegrees off the chain's line
		AxisDirectionDisagrees   // a joint's X axis runs the other way along the chain from the first joint's
	};

	struct ChainCheck
	{
		ChainProblem problem{ ChainProblem::None };
		int jointIndex{ -1 };         // the joint the problem is at
		double angleDegrees{ 0.0 };   // AxisAcrossChain: the angle between the axis and the chain's line
		double sign{ 1.0 };           // +1 when the joints' X axes run toward the tip, -1 when they run back. Valid when problem is None
	};

	// Checks that every joint's X axis runs along the chain: within MaxAxisAngleDegrees of the line to the next joint (the last
	// joint uses the line from the one before it), and all of them the same way along it. A chain that runs back is fine, as in a
	// mirrored limb whose joints keep the other side's axes.
	ChainCheck checkChain(const std::vector<Mat4>& joints);

	// The error text for a problem, naming the joint by its entry in `jointNames` (one per joint, in order). Empty for None.
	std::string describeChainProblem(const ChainCheck& check, const std::vector<std::string>& jointNames);

	// `count` frames spread evenly by arc length along a smooth spline through the joints' positions (a spline that passes through
	// every joint, with tangents from each joint's neighbours). The first and last frames are the first and last joints. A frame's
	// rotation is the rotation of the two joints either side of it, interpolated by how far along that segment it is, so it keeps
	// the joints' axes and their direction (+X or -X down the chain). Returns an empty vector for fewer than two joints or a
	// count below two. Call checkChain first: a chain it rejects gives frames with no meaning.
	std::vector<Mat4> placeAlongChain(const std::vector<Mat4>& joints, int count);

	// A two-bone limb's pole and settings positions from its three joints.
	struct LimbPlacement
	{
		Vec3 pole;
		Vec3 settings;
	};

	// The pole is the mid joint plus `poleOffset` when it is given. Without it, the pole is pushed from the mid joint by the limb's
	// length along the bend (the part of root-to-mid that is perpendicular to the root-to-end line). A limb straighter than
	// StraightLimbTolerance has no bend, so the pole goes out along the root joint's -Y instead. The settings control is the end
	// joint plus `settingsOffset`, or DefaultSettingsRise of the limb's length straight up (+Y) when it is not given. Offsets are in
	// world space, so a caller negates X for a mirrored side.
	LimbPlacement placeLimb(const Mat4& root, const Mat4& mid, const Mat4& end, const std::optional<Vec3>& poleOffset, const std::optional<Vec3>& settingsOffset);
}
