#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// The push-out solve (D79, design/push-out.md): one point against one capsule, in closed form, with no state. The capsule is
// a segment along its local X with a radius around it. Its frame is a world matrix whose axes give the direction (row 0 is
// the segment's axis, row 2 the fallback push) and whose scale is ignored, so a scaled joint does not resize it.

#include "VectorMath.h"

#include <string>

struct ShapePoint;   // ControlShapes.h: not included here, its ShapeType would clash with TemplateKey::ShapeType where both are in scope

namespace PushOutSolve
{
	struct Capsule
	{
		double radius = 1.0;       // above 0
		double halfLength = 0.0;   // of the straight part, 0 or more: 0 is a sphere
	};

	struct Settings
	{
		double margin = 0.0;       // added to the radius: the padding that absorbs the solve's small error
		double maxPush = 100.0;    // cap on the push length, above 0
		double weight = 1.0;       // 0 to 1: the share of the push that is applied
	};

	struct Result
	{
		VectorMath::Vec3 push;     // the world move, after maxPush and weight, zero when the point is clear
		bool isPushed = false;
	};

	// False, with the reason in outError, for a number the solve cannot use: a radius at or below 0, a negative half length or
	// margin, a maxPush at or below 0, a weight outside 0 to 1, or any of them not finite.
	bool validate(const Capsule& capsule, const Settings& settings, std::string& outError);

	// Moves the point out of the capsule along the line from the nearest point of its segment: by radius + margin - distance,
	// capped by maxPush and scaled by weight. On the segment there is no line, so the push is along the capsule's local +Z.
	// The same inputs give the same result on every frame.
	Result solve(const Capsule& capsule, const VectorMath::Mat4& capsuleWorld, const VectorMath::Vec3& pointWorld, const Settings& settings);

	// A matrix from a template offset: translate, and rotate in degrees applied X, then Y, then Z (Maya's XYZ order), row vectors.
	VectorMath::Mat4 offsetMatrix(const VectorMath::Vec3& translate, const VectorMath::Vec3& rotateDegrees);

	// The matrix reflected across the plane X = 0: its translation's X negated and its rotation turned so that a Y or Z turn
	// reverses. This is how a mirror's capsule is placed: reflect the source's world, then take it relative to the mirror's joint.
	VectorMath::Mat4 reflectAcrossX(const VectorMath::Mat4& world);

	// Where a library capsule mesh point (D26: a cylinder of radius 0.5 along X with its seams at |x| = 0.5 and half ellipsoid
	// caps beyond) lands for this capsule. Only the middle stretches: the caps keep the radius, so the drawing is the shape
	// the solve uses and not a scaled mesh.
	VectorMath::Vec3 drawPoint(const ShapePoint& meshPoint, double radius, double halfLength);
}
