#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// One mesh's skin weights, keyed by joint markup, with the mesh id and topology they belong to (D35).
// {
//     "formatVersion": 3,
//     "meshId": "...", "meshName": "body_geo",
//     "vertexCount": 8, "faceCount": 6, "topologyHash": "16 hex digits", "skeletonHash": "16 hex digits",
//     "skinMethod": "Linear",
//     "influences": [["Center", "spine_1"], ["Left", "upperArm"]],
//     "skeleton": [
//         [["Center", "pelvis"], null],
//         [["Center", "spine_1"], ["Center", "pelvis"]]
//     ],
//     "weights": [
//         [0, 0.75, 1, 0.25],
//         [1, 1]
//     ]
// }
// weights holds one line per vertex of influence index and weight pairs, in influence order. Zero weights are left out.
// Influences are sorted by side, then usage, so the skinCluster's own influence order never shows up in a diff.
// Weights are written exactly (the shortest text that reads back as the same double), so save and load never change
// a weight. blendWeights (one number per vertex) is written only for WeightBlended.
// skeleton (version 3, D70) is the structure of the skeleton the weights were made on: every marked joint's selector
// and its parent's, null for a root, with no rests and no names. It is what lets a joint that is gone give its weight to
// its parent (core SkinWeightFold). Sorted like the influences. A file saved before version 3 has none, and says [].
// meshName is for people reading the file. Loading never matches on it.

#include "JointMarkup.h"
#include "RigTemplateValidation.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct UpdateTable;

// The weight file's version steps (D33). Version 2 added skeletonHash (D54), version 3 added skeleton (D70).
const UpdateTable& skinWeightUpdateTable();

namespace SkinWeightFileFormat
{
	// How far a vertex's weights may sum from 1 and still count as normalized. Save and load fail on any vertex
	// further off rather than normalizing it, because neither may change a weight.
	inline constexpr double NormalizeTolerance{ 1e-5 };
}

// One joint of the saved skeleton and its parent, by markup (D70).
struct SkeletonLink
{
	JointSelector joint;
	bool hasParent = false;
	JointSelector parent;       // set when hasParent
};

struct SkinWeights
{
	std::string meshId;
	std::string meshName;
	unsigned int vertexCount = 0;
	unsigned int faceCount = 0;
	std::uint64_t topologyHash = 0;
	// The skeleton the weights were made on (D54): the rig root's skeletonHash text, "" when the influences belong to
	// no rig or the file is version 1. A status (D70): a load whose hash isn't the rig's still loads.
	std::string skeletonHash;
	std::string skinMethod;
	std::vector<JointSelector> influences;
	// The skeleton's structure when the weights were saved, sorted by (side, usage). Empty for a file saved before
	// version 3 and for a rigid bind, which has no use for it.
	std::vector<SkeletonLink> skeleton;
	// One entry per vertex: (influence index, weight) pairs, with no zero weights.
	std::vector<std::vector<std::pair<unsigned int, double>>> weights;
	// One value per vertex for WeightBlended, otherwise empty.
	std::vector<double> blendWeights;
};

// Fnv1a64 (Hash64.h) over the vertex count, the face count, each face's vertex count and each face's vertex indices
// in order, each fed with addUInt32. Vertex order and connection order change it; point positions and UVs don't, so a
// sculpt keeps its weights. Files store it as Hash64::format text.
std::uint64_t topologyHash(unsigned int vertexCount, const std::vector<int>& polygonCounts, const std::vector<int>& polygonConnects);

// Sorts influences by (side, usage), renumbers every weight's influence index to match, and orders each vertex's
// pairs by index. Values are untouched.
void sortSkinInfluences(SkinWeights& weights);

// Fills weights with one influence that takes every vertex at weight 1, Linear: a rigid bind (D54, the look swap's
// rigid meshes). The header fields (meshId, meshName, counts, topologyHash, skeletonHash) are the caller's and are
// left alone; vertexCount must be set.
void setRigidSkinWeights(SkinWeights& weights, const JointSelector& influence);

// The vertices whose weights don't sum to 1 within NormalizeTolerance, a vertex with no weights included.
std::vector<unsigned int> findUnnormalizedVertices(const SkinWeights& weights);

// "3 vertices are not normalized: 0 (sum 0.5), 4 (sum 1.2), 7 (sum 0)", listing at most the first 10.
std::string describeUnnormalizedVertices(const SkinWeights& weights, const std::vector<unsigned int>& vertices);

// Pretty-printed JSON with one line per vertex, so a weight edit is a small diff.
std::string serializeSkinWeights(const SkinWeights& weights);

// Every problem goes into report: bad JSON, a missing or unknown key, an unknown formatVersion, an unknown skinMethod,
// an influence that isn't a valid [side, usage] or appears twice, a weights list whose length isn't vertexCount,
// an influence index out of range or listed twice on one vertex, a negative weight, a vertex whose weights aren't
// normalized, or blendWeights missing for WeightBlended or present otherwise.
// The caller applies nothing when report has errors.
void deserializeSkinWeights(const std::string& text, SkinWeights& outWeights, ValidationReport& report);
