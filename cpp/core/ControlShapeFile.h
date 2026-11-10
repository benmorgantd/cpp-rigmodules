#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// A rig's control shape file: one JSON file per rig holding each control's shape, keyed by its markup.
// {
//     "formatVersion": 2,
//     "controls": {
//         "Left": { "upperArm": { "shapeType": "Circle", "shapeTransform": [16 numbers, row-major], "filled": false, "wireframeAlpha": 1.0 } }
//     }
// }
// Color is not stored: it follows the side of the module a control is built on. Alpha is not part of that, so it is.
// Version 1 had no wireframeAlpha. The update step (D33) adds it as 1.0, the attribute's default.

#include "RigTemplateValidation.h"

#include <array>
#include <map>
#include <string>

struct UpdateTable;

const UpdateTable& controlShapeUpdateTable();

struct ControlShape
{
	std::string shapeType;
	std::array<double, 16> shapeTransform{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };   // Row-major, Maya's layout
	bool filled = false;
	double wireframeAlpha = 1.0;   // 0 to 1
};

// side -> usage -> shape. Ordered maps keep saved files stable, so they diff cleanly in version control.
using ControlShapeMap = std::map<std::string, std::map<std::string, ControlShape>>;

// Pretty-printed JSON for a shape file.
std::string serializeControlShapes(const ControlShapeMap& shapes);

// Every problem goes into report: bad JSON, a missing or unknown key, an unknown formatVersion,
// a side outside SideNames, an empty usage, a shapeType that is not a shape, a shapeTransform that is not 16 numbers
// or a wireframeAlpha outside 0 to 1. An older formatVersion is updated first.
// outShapes holds only the entries that parsed, and the caller should apply nothing when report has errors.
void deserializeControlShapes(const std::string& text, ControlShapeMap& outShapes, ValidationReport& report);
