#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// Skeleton (D41): the types and the reader's signature are here, and the reader reports "not built yet".
//
// A JointClip file is the transient bridge between Python and the plugin (D41, inverse-solve.md section 7). Python reads a
// USD file with pxr and writes this JSON, and a command reads it. It is never stored in an asset. It holds the source's
// joints, each joint's markup and rest, and a list of JointPoses. A pose from a single-frame USD is a JointClip with one
// JointPose.
// {
//     "formatVersion": 1,
//     "frameRate": 30.0,
//     "joints": [
//         { "path": "hips/spine", "markup": "<the joint's rigMarkup JSON>", "rest": { "translate": [0, 1, 0], "rotate": [0, 0, 0, 1], "scale": [1, 1, 1] } }
//     ],
//     "poses": [
//         [ { "translate": [0, 1, 0], "rotate": [0, 0, 0, 1], "scale": [1, 1, 1] } ]
//     ]
// }
// Every transform is local to the joint's parent (UsdSkel's convention), in the scene's units: the Python reader has
// already converted meters per unit, the up axis and the frame rate. A joint's parent is the path above it. A joint
// with no markup has "markup": "", stays in the file and is never mapped to the rig (it still composes the world
// matrices of the joints below it). A pose has one transform per joint, in joint order.

#include "RigTemplateValidation.h"

#include <array>
#include <string>
#include <vector>

struct UpdateTable;

const UpdateTable& jointClipUpdateTable();

struct JointTransform
{
	std::array<double, 3> translate{ 0.0, 0.0, 0.0 };
	std::array<double, 4> rotate{ 0.0, 0.0, 0.0, 1.0 };   // quaternion: x, y, z, w
	std::array<double, 3> scale{ 1.0, 1.0, 1.0 };
};

struct JointClipJoint
{
	std::string path;      // "/"-separated, which is also the hierarchy
	std::string markup;    // the joint's rigMarkup JSON verbatim (D15), or "" for an unmarked joint
	JointTransform rest;   // the source's rest, kept for the source check and for a later retarget
};

// One frame: every joint's local transform, in JointClip::joints order.
using JointPose = std::vector<JointTransform>;

struct JointClip
{
	double frameRate = 30.0;
	std::vector<JointClipJoint> joints;
	std::vector<JointPose> poses;
};

// Every problem goes into report. An older formatVersion is updated first. outClip holds a complete clip only when
// report has no errors, and the caller should use nothing otherwise. Reading only: Python writes the file.
void deserializeJointClip(const std::string& text, JointClip& outClip, ValidationReport& report);
