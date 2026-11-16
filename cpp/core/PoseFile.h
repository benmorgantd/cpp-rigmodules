#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// A pose file: the channel values of a rig's controls, keyed by control markup and channel name, never by node name, so
// a pose survives renames and rebuilds (D25, D34).
// {
//     "formatVersion": 3,
//     "meta": { "tags": ["hands"], "created": "2026-11-14T10:22:00Z" },
//     "controls": {
//         "Left": { "upperArm": { "translateX": 0.0, "rotateY": 12.5, "ikFkBlend": 1.0 } }
//     }
// }
// Values are in the scene's UI units, like cmds.getAttr. An enum stores its index (labels are append-only) and a bool
// stores 0 or 1. `meta` is the library metadata (LibraryMeta.h, D51), an empty object when there is none. Version 1
// files have no meta and load with an empty one. Version 2 named rigName `variant` and skeleton `skeletonId` (D54).

#include "LibraryMeta.h"
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
std::string serializePose(const PoseMap& pose, const LibraryMeta& meta = LibraryMeta());

// Every problem goes into report: bad JSON, a missing or unknown key, an unknown formatVersion, a side outside
// SideNames, an empty usage, an empty channel name or a value that is not a number. An older formatVersion is updated
// first. outPose holds the entries that parsed, and the caller should apply nothing when report has errors. pMeta, when
// given, receives the file's meta. A bad meta is an error too.
void deserializePose(const std::string& text, PoseMap& outPose, ValidationReport& report, LibraryMeta* pMeta = nullptr);
