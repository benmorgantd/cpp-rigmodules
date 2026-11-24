#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// A marker set and its hash (D73, design/markers.md). A marker is a transform at a fixed offset from one joint: no skin,
// not a joint, never driven by an animator. The file sits beside skeleton.json in kind, not inside it.
// {
//     "formatVersion": 1,
//     "markersHash": "16 hex digits",
//     "markers": [
//         {"markup": ["Left", "heel"], "joint": ["Left", "ankle"], "export": false, "name": "heel_00_L_MRK", "offsetLocal": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -9.5, -1.2, 1]}
//     ]
// }
// One line per marker, sorted by side then usage, so an edit to one marker is a one-line diff. offsetLocal is the
// marker's matrix relative to its joint, row by row as Maya's MMatrix with the translation in the last row, written
// exactly. Marker markup is its own namespace: ["Left", "heel"] the marker and ["Left", "heel"] a joint can coexist.

#include "JointMarkup.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct UpdateTable;
struct ValidationReport;

// The marker file's version steps (D33). None yet: the format is version 1.
const UpdateTable& markerUpdateTable();

namespace MarkerFileFormat
{
	// Offset values are rounded to this step before hashing, the same step as a skeleton's rest (SkeletonFileFormat::RestGrid),
	// so float noise from a save, or a marker moved back to where it was, gives back the same hash.
	inline constexpr double OffsetGrid{ 1e-4 };
}

struct Marker
{
	JointSelector markup;         // side and usage, unique in the file
	JointSelector joint;          // the joint it follows
	bool isExported = false;      // JSON key "export"
	std::string name;             // without a namespace
	std::array<double, 16> offsetLocal{ 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0 };
};

struct MarkerSet
{
	std::vector<Marker> markers;
};

// Sorts markers by (side, usage), the order the hash and the file use.
void sortMarkers(MarkerSet& set);

// Fnv1a64 (Hash64.h) over the marker count, then each marker in (side, usage) order: side, usage, the joint's side and
// usage, export, name, and the 16 offsetLocal values each rounded to OffsetGrid steps as a 64-bit integer.
// Deterministic whatever the order passed in. Changing what this feeds changes every stamped hash, so it is public API.
std::uint64_t markersHash(const MarkerSet& set);

// The file text above, with the markers sorted and the hash filled in.
std::string serializeMarkers(const MarkerSet& set);

// Every problem goes into report: bad JSON, a missing or unknown key, an unknown formatVersion, a marker whose markup,
// joint, export, name or offsetLocal is invalid, and two markers with one (side, usage). outSet's markers come back
// sorted. The caller creates nothing when report has errors.
// The stored markersHash is not trusted: the hash is always recomputed. A stored value that differs (a hand edit, a
// merge) is a warning naming both, and the next save writes the right one.
void deserializeMarkers(const std::string& text, MarkerSet& outSet, ValidationReport& report);

// The index of the marker with this (side, usage), or -1.
int findMarker(const MarkerSet& set, const JointSelector& selector);

// The markup text on a marker locator in an editing scene (RigAttr::MarkerMarkup): the marker's markup, joint and
// export flag as a JSON object. name and offsetLocal are not in it: the node's name and place give them.
std::string serializeMarkerMarkup(const Marker& marker);
// False, with the reason in outError, for bad JSON or a missing, mistyped or invalid key. outMarker's other fields
// are left as they were.
bool parseMarkerMarkup(const std::string& text, Marker& outMarker, std::string& outError);

// A marker placed on a build's joints: its world matrix at rest, the joint's rest world times offsetLocal.
struct ResolvedMarker
{
	Marker marker;
	std::array<double, 16> world{ 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0 };
};

// The text getRigMarkers returns and the rig root stores: a JSON list in file order of {markup, joint, export, name,
// world}. Empty list text, "[]", for no markers. Written whole, so it reads back through nlohmann::json::parse.
std::string serializeResolvedMarkers(const std::vector<ResolvedMarker>& markers);
