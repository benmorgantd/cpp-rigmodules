#pragma once

#include "core/JointClipFile.h"
#include "core/PoseFile.h"

#include <vector>

#include <maya/MObject.h>
#include <maya/MStatus.h>

// The driver of the inverse solve (D41, inverse-solve.md section 5). It reads a rig's stored rests once, orders its
// modules parents first, and for each JointPose sweeps them in memory: the parent socket's motion becomes each child
// plug's parentDelta, the module's inverse turns its joints' world matrices into control values, and the sidecar fills
// what the joints can't give. It reads no posed scene state and writes nothing: the caller applies the poses through
// RigPoseScene or RigKeyScene. Displays its own errors and warnings. The module inverses it calls are in ModuleInverse.h.
class RigInverse
{
public:
	// The only mode so far: reproduce the source's world matrices. Retargeting to other proportions is a later mode.
	static constexpr const char* kMatchMode{ "match" };

	// One PoseMap per JointPose in the clip, in order, holding the control values for the rig's controls. sidecars has
	// one entry per JointPose (an empty PoseMap for none). A control's transform channels hold what its joints give, else
	// the sidecar's, else rest. Its other channels (custom ones, `space`) are there only when the sidecar has them,
	// because nothing derives them and a missing one keeps its value.
	static MStatus solve(const MObject& oRigRoot, const JointClip& clip, const std::vector<PoseMap>& sidecars, std::vector<PoseMap>& outPoses);
};
