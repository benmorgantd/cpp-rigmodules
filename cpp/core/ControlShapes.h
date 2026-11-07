#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// The shapes a rigControlNode can draw. Every shape uses one convention: +X is the normal of a flat
// shape and the long axis of Capsule and Cylinder, so a control on an +X-down-the-bone joint wraps the bone.

#include "Constants.h"

#include <cstddef>
#include <string_view>
#include <vector>

// Stored on the node by index, so new shapes go on the end and existing values never move.
// RigControlNodeAttr::ShapeTypes::All holds the names in the same order.
enum ShapeType : int
{
	Box = 0,
	Sphere = 1,
	Circle = 2,
	Capsule = 3,
	Triangle = 4,
	Cylinder = 5,
	Square = 6,
	HalfCircleArrow = 7,
	ArchedDoubleArrow = 8,
	PointedCircle = 9,
	Diamond = 10,
	CircleArrow = 11,
	Dome = 12,
	SingleArrow = 13,
	FourArrow = 14,
	CircleStick = 15,
	Pivot = 16,
	Count = 17
};

static_assert(RigControlNodeAttr::ShapeTypes::All.size() == ShapeType::Count, "ShapeType and RigControlNodeAttr::ShapeTypes::All must list the same shapes");

// Strict lookup. False when the name is not a shape, so a typo in a file is reported instead of becoming a Box.
bool findShapeType(std::string_view name, ShapeType& outShapeType);
// For names that already passed validation. Falls back to Box.
ShapeType getShapeTypeFromString(std::string_view name);
const char* getShapeTypeString(ShapeType shapeType);

struct ShapePoint
{
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
};

// The polyline for a curve shape, in shape space with unit extent. Drawn as one line strip.
// nullptr for the primitives (Box, Sphere, Circle, Capsule, Cylinder, Square), which the draw manager draws itself.
const std::vector<ShapePoint>* getCurveShapePoints(ShapeType shapeType);
