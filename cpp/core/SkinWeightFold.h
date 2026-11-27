#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// Folding a skin weight table onto the joints a rig has now. A joint a weight file names that is gone gives its
// weight to its parent, and that parent's parent, up the skeleton the file stored, to the first joint that is a skin
// joint in the current rig. So a removed joint, or a renamed or unmarked one, costs the mesh its detail there and not
// its binding.

#include "SkeletonFile.h"
#include "SkinWeightFile.h"

#include <vector>

// A joint the rig has now, and whether it is a skin joint.
struct CurrentJoint
{
	JointSelector joint;
	bool isSkinned = false;
};

struct FoldedInfluence
{
	JointSelector from;   // an influence of the file that no joint in the rig has
	JointSelector to;     // the skin joint its weight went to
};

struct FoldResult
{
	std::vector<FoldedInfluence> folded;
	// The missing influences with no skin joint above them in the file's skeleton, or a file with no skeleton at all.
	// When this isn't empty, weights was left as it was.
	std::vector<JointSelector> unfoldable;
};

// Replaces every influence of weights that is not among joints with the first skin joint above it. The walk goes
// parent by parent through weights.skeleton and passes a parent that is gone, or is not a skin joint, to the next.
// Never gives weight to a joint the mesh wasn't bound to for any reason but this walk: a target is added as an
// influence when the file didn't have it. Weights that land on one joint are added, so a vertex's sum is unchanged.
// influences come back sorted (sortSkinInfluences). Nothing changes when no influence is missing.
FoldResult foldSkinWeights(SkinWeights& weights, const std::vector<CurrentJoint>& joints);

// The skeleton's joints as a weight file stores them (version 3), sorted by (side, usage).
std::vector<SkeletonLink> skeletonLinks(const Skeleton& skeleton);

// The skeleton's joints as foldSkinWeights wants the rig's.
std::vector<CurrentJoint> currentJoints(const Skeleton& skeleton);
