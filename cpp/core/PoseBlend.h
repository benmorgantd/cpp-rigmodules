#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// Pose blending: moves a rig from a reference pose towards or past a library pose by a weight t >= 0, on pose
// data alone. All values are in the scene's UI units, like the pose file (PoseFile.h).
//
//   0 <= t <= 1  ref + t * (pose - ref)           0 is the reference, 1 is exactly the pose
//   t > 1        pose + (t - 1) * (pose - rest)   past 1 adds more of the pose's own offset from rest
//
// The two pieces meet at 1, so a slider never jumps. A stepped channel (bool, int, enum) can't take a value in
// between: it is the reference below t = 0.5 and the pose from 0.5 up. Every result is clamped to the channel's min
// and max. A negative t is the caller's error (loadRigPose rejects it).

#include "ControlChannels.h"
#include "PoseFile.h"

#include <map>
#include <string>
#include <vector>

// side -> usage -> the control's channels (their rest, range and type), like PoseMap.
using PoseChannelInfo = std::map<std::string, std::map<std::string, std::vector<ControlChannel>>>;

// True for a channel type that holds whole numbers only ("bool", "short", "long", "enum").
bool isSteppedChannelType(const std::string& type);

// One channel's blended value, before clamping. The formula is in the header comment.
double blendPoseValue(double ref, double pose, double rest, double t, bool stepped);

// Blends every channel of pose. ref supplies each channel's start value, and a channel ref lacks starts at its rest.
// channels supplies rest, range and type. A control or channel channels lacks is copied from pose unchanged, so the
// write that follows (RigPoseScene::setRigPose) reports it as it would for a plain load.
void blendPoses(const PoseMap& ref, const PoseMap& pose, const PoseChannelInfo& channels, double t, PoseMap& outBlended);
