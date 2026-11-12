#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
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

// Every problem goes into report, as for deserializePose: bad JSON, a missing or unknown key, an unknown formatVersion,
// a frameRate that is not a number above 0, a frame that is not a whole number, and anything deserializePoseControls
// rejects. outSidecar is set only when report has no errors. Reading only: Python writes the file.
void deserializeAnimationSidecar(const std::string& text, AnimationSidecar& outSidecar, ValidationReport& report);
