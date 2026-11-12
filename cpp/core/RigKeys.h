#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// A rig's animation curves as plain data: the keys of every control channel, keyed by control markup and channel name
// like a pose, so they survive renames and rebuilds (D25, D34). It is the payload of getRigKeys and setRigKeys, a JSON
// string, not a file the system owns, so it carries no formatVersion.
// {
//     "Left": { "upperArm": { "rotateZ": [[1, 0.0], [2, 12.5]] } }
// }
// A key is [frame, value]. Frames are in the scene's UI time unit and values in its UI units, like cmds.keyframe.

#include "RigTemplateValidation.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

// (frame, value) pairs of one channel.
using ChannelKeys = std::vector<std::pair<double, double>>;

// side -> usage -> channel -> keys. Ordered maps keep the output stable.
using RigKeys = std::map<std::string, std::map<std::string, std::map<std::string, ChannelKeys>>>;

// Compact JSON, one entry per channel that has keys.
std::string serializeRigKeys(const RigKeys& keys);

// Every problem goes into report: bad JSON, a side outside SideNames, an empty usage or channel name, keys that are not
// an array of [frame, value] number pairs, and a frame used twice in one channel. outKeys holds what parsed, and the
// caller should apply nothing when report has errors.
void deserializeRigKeys(const std::string& text, RigKeys& outKeys, ValidationReport& report);
