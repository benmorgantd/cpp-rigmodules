#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// A skeleton's description and its hash (D54, design/rigs-and-looks.md section 5). Two skeletons are the same when
// their hashes match: the same marked joints, names, parents, rotate orders and rest transforms.
// {
//     "formatVersion": 1,
//     "skeletonHash": "16 hex digits",
//     "joints": [
//         {"markup": ["Center", "root"], "export": true, "skin": true, "name": "root_jnt", "parent": null, "rotateOrder": "xyz", "restLocal": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 10, 0, 1]},
//         {"markup": ["Center", "spine_1"], "export": true, "skin": true, "name": "spine_1_jnt", "parent": ["Center", "root"], ...}
//     ]
// }
// One line per joint, sorted by side then usage, so an edit to one joint is a one-line diff. restLocal is the rest
// matrix relative to the parent's (the world matrix for a joint with no parent), row by row as Maya's MMatrix,
// written exactly. parent is the nearest ancestor among the skeleton's joints, so groups between joints fold into
// restLocal.

#include "JointMarkup.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace SkeletonFileFormat
{
	inline constexpr int CurrentVersion{ 1 };
	// Rest matrix values are rounded to this step before hashing (author, 2026-11-16), so float noise from a save,
	// or a joint moved back to where it was, gives back the same hash.
	inline constexpr double RestGrid{ 1e-4 };
}

struct SkeletonJoint
{
	JointMarkup markup;
	std::string name;             // without a namespace
	bool hasParent = false;
	JointSelector parent;         // set when hasParent
	int rotateOrder = 0;          // index into RotateOrders::All
	std::array<double, 16> restLocal{ 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0 };
};

struct Skeleton
{
	std::vector<SkeletonJoint> joints;
};

// Sorts joints by (side, usage), the order the hash and the file use.
void sortSkeletonJoints(Skeleton& skeleton);

// Fnv1a64 (Hash64.h) over the joint count, then each joint in (side, usage) order: side, usage, export, skin, name,
// whether it has a parent, the parent's side and usage, rotate order, and the 16 restLocal values each rounded to
// RestGrid steps as a 64-bit integer. Deterministic: the same skeleton always gives the same hash, whatever the joint
// order passed in. Changing what this feeds changes every stamped hash, so it is public API.
std::uint64_t skeletonHash(const Skeleton& skeleton);

// The file text above, with the joints sorted and the hash filled in.
std::string serializeSkeleton(const Skeleton& skeleton);
