#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// The raw shape data, as tools/generate_control_shapes.py writes it into ControlShapeLibrary.cpp.
// Only ControlShapes.cpp reads it. Everything else uses getShapeMesh.

#include "ControlShapes.h"

#include <array>
#include <vector>

struct ShapeMeshSource
{
	std::vector<ShapePoint> points;
	std::vector<std::vector<unsigned int>> faces;      // Vertex loops. Planar, may be concave, no collinear points.
	std::vector<std::array<unsigned int, 2>> lines;     // Edges with no area, drawn in the wire only
};

// An empty source for a value outside the enum.
const ShapeMeshSource& getShapeMeshSource(ShapeType shapeType);
