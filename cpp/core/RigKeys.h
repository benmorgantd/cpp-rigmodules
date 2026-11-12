#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// A rig's animation curves as plain data: the keys of every control channel, keyed by control markup and channel name
// like a pose, so they survive renames and rebuilds (D25, D34). It is the payload of getRigKeys and setRigKeys, a JSON
// string with no formatVersion, and the `controls` object of a ControlAnim file (ControlAnimFile.h), which has one.
// {
//     "Left": { "upperArm": {
//         "rotateX": [[1, 0.0], [2, 12.5]],
//         "rotateZ": {
//             "weighted": false, "preInfinity": "constant", "postInfinity": "constant",
//             "keys": [[1, 0.0, "auto", "auto"], [10, 90.0, [-12.5, 1.0], "flat"], [20, 45.0, "auto", "step"]]
//         }
//     } }
// }
// A channel is an array of keys, or an object with the curve's flags and its keys (D42). A key is [frame, value] or
// [frame, value, in, out]. A tangent is a type name (auto, clamped, linear, flat, step, spline) or [angleDegrees,
// weight] for a fixed tangent. A key of two numbers has two auto tangents, and on a channel that can't interpolate (an
// enum, bool or integer) every key is stepped whatever its tangents say. Frames are in the scene's UI time unit and
// values in its UI units, like cmds.keyframe.

#include "RigTemplateValidation.h"

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

enum class TangentKind
{
	Auto,
	Clamped,
	Linear,
	Flat,
	Step,
	Spline,
	Fixed
};

struct Tangent
{
	TangentKind kind = TangentKind::Auto;
	double angle = 0.0;    // degrees, Fixed only
	double weight = 1.0;   // Fixed only

	bool operator==(const Tangent& other) const;
};

enum class Infinity
{
	Constant,
	Linear,
	Cycle,
	CycleRelative,
	Oscillate
};

struct Key
{
	double frame = 0.0;
	double value = 0.0;
	Tangent in;
	Tangent out;
};

// One channel's curve: the flags Maya keeps per curve, and the keys in frame order.
struct ChannelCurve
{
	bool isWeighted = false;
	Infinity preInfinity = Infinity::Constant;
	Infinity postInfinity = Infinity::Constant;
	std::vector<Key> keys;
};

// side -> usage -> channel -> curve. Ordered maps keep the output stable.
using RigKeys = std::map<std::string, std::map<std::string, std::map<std::string, ChannelCurve>>>;

// Compact JSON of the getRigKeys payload: one entry per channel that has keys, as an array of keys when the curve has
// no non-default flag, and a key as two numbers when both its tangents are auto.
std::string serializeRigKeys(const RigKeys& keys);

// The same data as JSON for a file: every channel an object, every key four values. ordered_json keeps the flags before
// the keys.
nlohmann::ordered_json rigKeysToJson(const RigKeys& keys);

// Every problem goes into report: bad JSON, a side outside SideNames, an empty usage or channel name, a channel that is
// neither a key array nor a curve object, a key that is not [frame, value] or [frame, value, in, out], a tangent or
// infinity name that doesn't exist, an unknown curve key, and a frame used twice in one channel. outKeys holds what
// parsed, and the caller should apply nothing when report has errors.
void deserializeRigKeys(const std::string& text, RigKeys& outKeys, ValidationReport& report);

// deserializeRigKeys for JSON already parsed. where is the prefix of the messages.
void parseRigKeys(const nlohmann::json& keys, const std::string& where, RigKeys& outKeys, ValidationReport& report);

// The mirror image of a curve about the value axis: every value negated, and every fixed tangent's angle (the other
// tangent types recompute from the negated keys and come out mirrored). Frames, weights, flags and tangent types never
// change (D42).
ChannelCurve negateCurve(const ChannelCurve& curve);

// Moves every key of every channel by offset frames.
void offsetKeys(RigKeys& keys, double offset);
