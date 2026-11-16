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

// The file is the skeleton's only stored form (D54, author 2026-11-16): there is no scene file beside it, so loading
// a skeleton is deserializeSkeleton and creating its joints.

#include "JointMarkup.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct UpdateTable;
struct ValidationReport;

// The skeleton file's version steps (D33). None yet: the format is version 1.
const UpdateTable& skeletonUpdateTable();

namespace SkeletonFileFormat
{
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

// Every problem goes into report: bad JSON, a missing or unknown key, an unknown formatVersion, a joint whose markup,
// name, rotateOrder or restLocal is invalid, two joints with one (side, usage), a parent that names no joint in the
// file, and parents that form a loop. outSkeleton's joints come back sorted. The caller creates nothing when report
// has errors.
// The stored skeletonHash is not trusted: the hash is always recomputed from the joints. A stored value that differs
// (a hand edit, a merge) is a warning naming both, and the next save writes the right one.
void deserializeSkeleton(const std::string& text, Skeleton& outSkeleton, ValidationReport& report);

// The joints' indices ordered so every parent comes before its children, for creating them. skeleton must have
// passed deserializeSkeleton's checks (every parent present, no loop).
std::vector<std::size_t> skeletonParentsFirst(const Skeleton& skeleton);

// The index of the joint with this (side, usage), or -1.
int findSkeletonJoint(const Skeleton& skeleton, const JointSelector& selector);
