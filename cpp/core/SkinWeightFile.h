#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// One mesh's skin weights, keyed by joint markup, with the mesh id and topology they belong to (D35).
// {
//     "formatVersion": 1,
//     "meshId": "...", "meshName": "body_geo",
//     "vertexCount": 8, "faceCount": 6, "topologyHash": "16 hex digits",
//     "skinMethod": "Linear",
//     "influences": [["Center", "spine_1"], ["Left", "upperArm"]],
//     "weights": [
//         [0, 0.75, 1, 0.25],
//         [1, 1]
//     ]
// }
// weights holds one line per vertex of influence index and weight pairs, in influence order. Zero weights are left out.
// Influences are sorted by side, then usage, so the skinCluster's own influence order never shows up in a diff.
// Weights are written exactly (the shortest text that reads back as the same double), so save and load never change
// a weight. blendWeights (one number per vertex) is written only for WeightBlended.
// meshName is for people reading the file. Loading never matches on it.

#include "JointMarkup.h"
#include "RigTemplateValidation.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace SkinWeightFileFormat
{
	inline constexpr int CurrentVersion{ 1 };
	// How far a vertex's weights may sum from 1 and still count as normalized. Save and load fail on any vertex
	// further off rather than normalizing it, because neither may change a weight.
	inline constexpr double NormalizeTolerance{ 1e-5 };
}

struct SkinWeights
{
	std::string meshId;
	std::string meshName;
	unsigned int vertexCount = 0;
	unsigned int faceCount = 0;
	std::uint64_t topologyHash = 0;
	std::string skinMethod;
	std::vector<JointSelector> influences;
	// One entry per vertex: (influence index, weight) pairs, with no zero weights.
	std::vector<std::vector<std::pair<unsigned int, double>>> weights;
	// One value per vertex for WeightBlended, otherwise empty.
	std::vector<double> blendWeights;
};

// 64-bit FNV-1a over the vertex count, the face count, each face's vertex count and each face's vertex indices
// in order, each fed as 4 little-endian bytes. Vertex order and connection order change it; point positions and
// UVs don't, so a sculpt keeps its weights. A fixed algorithm, unlike std::hash, so every build gets the same value.
std::uint64_t topologyHash(unsigned int vertexCount, const std::vector<int>& polygonCounts, const std::vector<int>& polygonConnects);

// "0123456789abcdef": 16 lowercase hex digits. JSON numbers can't hold 64 bits safely, so files store this text.
std::string formatTopologyHash(std::uint64_t hash);

// Sorts influences by (side, usage), renumbers every weight's influence index to match, and orders each vertex's
// pairs by index. Values are untouched.
void sortSkinInfluences(SkinWeights& weights);

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
