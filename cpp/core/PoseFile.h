#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// A pose file: the channel values of a rig's controls, keyed by control markup and channel name, never by node name, so
// a pose survives renames and rebuilds (D25, D34).
// {
//     "formatVersion": 1,
//     "controls": {
//         "Left": { "upperArm": { "translateX": 0.0, "rotateY": 12.5, "ikFkBlend": 1.0 } }
//     }
// }
// Values are in the scene's UI units, like cmds.getAttr. An enum stores its index (labels are append-only) and a bool
// stores 0 or 1.

#include "RigTemplateValidation.h"

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <utility>
#include <vector>

struct UpdateTable;

const UpdateTable& poseUpdateTable();

// Channel name and value, in the control's channel box order.
using PoseChannels = std::vector<std::pair<std::string, double>>;

// side -> usage -> channels. Ordered maps keep saved files stable, so they diff cleanly in version control.
using PoseMap = std::map<std::string, std::map<std::string, PoseChannels>>;

// Pretty-printed JSON for a pose file.
std::string serializePose(const PoseMap& pose);

// Every problem goes into report: bad JSON, a missing or unknown key, an unknown formatVersion, a side outside
// SideNames, an empty usage, an empty channel name or a value that is not a number. An older formatVersion is updated
// first. outPose holds the entries that parsed, and the caller should apply nothing when report has errors.
void deserializePose(const std::string& text, PoseMap& outPose, ValidationReport& report);
