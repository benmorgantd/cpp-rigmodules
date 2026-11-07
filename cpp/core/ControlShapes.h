#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// The shapes a rigControlNode can draw. Every shape is a mesh that fits the -1..1 cube, drawn as a wire and,
// when filled, as triangles. Orientation (D26): +X is the normal of flat shapes that ring a bone and the long
// axis of round 3D shapes. Pointer shapes point down +X and lie in the XZ plane.

#include "Constants.h"

#include <cstddef>
#include <string>
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

// A shape ready to draw: index pairs for the wire (face edges, each once, plus lines) and index triples for the fill.
struct ShapeMesh
{
	std::vector<ShapePoint> points;
	std::vector<unsigned int> wireIndices;
	std::vector<unsigned int> triangleIndices;
};

// Built from the library on first use and kept for the session. A value outside the enum gives an empty mesh.
const ShapeMesh& getShapeMesh(ShapeType shapeType);

// Builds every shape and checks it: indices in range, faces of at least 3 points, each face triangulated
// into (points - 2) triangles. The plugin refuses to load on failure, so a bad library edit can't ship.
bool validateShapeLibrary(std::string& outError);
