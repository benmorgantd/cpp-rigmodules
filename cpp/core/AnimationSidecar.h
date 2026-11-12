#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// Skeleton (D41): the type and the reader's signature are here, and the reader reports "not built yet".
//
// A clip's sidecar: the control values a JointClip can't carry (custom channels, `space`, Layout controls, and the
// transforms of controls whose joints are missing from the source), one entry for every frame of the clip. A pose's
// sidecar is a plain pose file (PoseFile.h), so this adds only the frame layer. Every frame holds the pose file's
// `controls` map, parsed by the same code.
// {
//     "formatVersion": 1,
//     "frameRate": 30.0,
//     "frames": {
//         "1": { "Left": { "arm": { "ikFkBlend": 0.0, "space": 0 } } },
//         "2": { "Left": { "arm": { "ikFkBlend": 0.0, "space": 0 } } }
//     }
// }
// Every channel is baked every frame and the file holds no tangent data (author, 2026-11-11).

#include "PoseFile.h"

#include <map>
#include <string>

struct UpdateTable;

const UpdateTable& animationSidecarUpdateTable();

struct AnimationSidecar
{
	double frameRate = 30.0;
	std::map<int, PoseMap> frames;   // frame number -> that frame's pose
};

// Every problem goes into report, as for deserializePose. Reading only: Python writes the file.
void deserializeAnimationSidecar(const std::string& text, AnimationSidecar& outSidecar, ValidationReport& report);
