#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// A rig's control shape file: one JSON file per rig holding each control's shape, keyed by its markup.
// {
//     "formatVersion": 1,
//     "controls": {
//         "Left": { "upperArm": { "shapeType": "Circle", "shapeTransform": [16 numbers, row-major], "filled": false } }
//     }
// }
// Color is not stored: it follows the side of the module a control is built on.

#include "RigTemplateValidation.h"

#include <array>
#include <map>
#include <string>

namespace ControlShapeFileFormat
{
	inline constexpr int CurrentVersion{ 1 };
}

struct ControlShape
{
	std::string shapeType;
	std::array<double, 16> shapeTransform{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };   // Row-major, Maya's layout
	bool filled = false;
};

// side -> usage -> shape. Ordered maps keep saved files stable, so they diff cleanly in version control.
using ControlShapeMap = std::map<std::string, std::map<std::string, ControlShape>>;

// Pretty-printed JSON for a shape file.
std::string serializeControlShapes(const ControlShapeMap& shapes);

// Every problem goes into report: bad JSON, a missing or unknown key, an unknown formatVersion,
// a side outside SideNames, an empty usage, a shapeType that is not a shape, or a shapeTransform that is not 16 numbers.
// outShapes holds only the entries that parsed, and the caller should apply nothing when report has errors.
void deserializeControlShapes(const std::string& text, ControlShapeMap& outShapes, ValidationReport& report);
