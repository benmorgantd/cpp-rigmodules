#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// A ControlAnim file (D42): sparse keys with tangents on the channels of a rig's controls, keyed by control markup and
// channel name like a pose file, so an animation survives renames and rebuilds and loads on any rig built from the
// same template (D25, D34). The `controls` object is the keys payload of RigKeys.h, written with every channel an
// object and every key four values.
// {
//     "formatVersion": 1,
//     "frameRate": 30.0,
//     "startFrame": 1,
//     "endFrame": 60,
//     "controls": {
//         "Left": { "upperArm": { "rotateZ": {
//             "weighted": false, "preInfinity": "constant", "postInfinity": "constant",
//             "keys": [[1, 0.0, "auto", "auto"], [10, 90.0, [-12.5, 1.0], "flat"], [20, 45.0, "auto", "step"]]
//         } } }
//     }
// }
// Frames are in the scene's UI time unit and values in its UI units, like cmds.keyframe. startFrame and endFrame are
// the range the animation was saved over, and every key lies inside it. A pose that must be an animation (an engine
// wants two frames) is a ControlAnim over two frames with the same values on both.

#include "RigKeys.h"
#include "RigTemplateValidation.h"

#include <string>

struct UpdateTable;

const UpdateTable& controlAnimUpdateTable();

struct ControlAnim
{
	double frameRate = 30.0;
	double startFrame = 1.0;
	double endFrame = 1.0;
	RigKeys controls;
};

// Pretty-printed JSON for a ControlAnim file.
std::string serializeControlAnim(const ControlAnim& anim);

// Every problem goes into report: bad JSON, a missing or unknown key, an unknown formatVersion, a frameRate that is not
// a number above 0, a range that is not two numbers with endFrame at least startFrame, a key outside the range, and
// everything deserializeRigKeys reports. An older formatVersion is updated first. outAnim holds what parsed, and the
// caller should apply nothing when report has errors.
void deserializeControlAnim(const std::string& text, ControlAnim& outAnim, ValidationReport& report);
