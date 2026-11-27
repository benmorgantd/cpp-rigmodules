#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// Painting skin weights so a posed mesh goes back towards its rest shape. A brush dab does not smooth the weights
// over their neighbors: it asks, for each vertex, where the rest shape says the vertex should sit given where its
// neighbors are in the current pose, and moves the weights to the blend of the influences that gets closest.
//
// The skin is linear blend skinning, so the posed position of a vertex is linear in its weights: with c_k the place the
// vertex would be if it were bound to influence k alone, P = sum(w_k * c_k). That makes the weights of one vertex a
// small least-squares problem over the simplex (weights not below 0, a fixed total), which is solved here directly. A
// vertex reads the pose of its neighbors as the last pass left it (Jacobi), so the order of the vertices changes
// nothing, and a dab is one pass: a stroke supplies the repetition.
//
// Two ways to say where a vertex should sit:
//  - Delta: the average of its neighbors' posed positions plus its rest delta (its rest position minus the average of
//    its neighbors' rest positions), turned by the skin's own rotation there. This is what a delta mush keeps;
//  - Edge: the average, over its edges, of the point at the rest edge length from that neighbor along the current
//    direction of the edge.

#include "SkinWeightSmooth.h"

#include <cstddef>
#include <vector>

enum class RestTarget
{
	Delta,
	Edge
};

// How many numbers describe one influence: a 3 x 3 linear part, row by row, then the translation. A point p becomes
// p.x * row0 + p.y * row1 + p.z * row2 + translation (the row-vector convention Maya's matrices use).
constexpr std::size_t kRestMatrixSize = 12;

// What the pose is made of. Both must outlive the call.
struct RestMatchSkin
{
	const std::vector<double>* restPositions = nullptr; // x y z per vertex of the whole mesh, in the space the matrices act on
	const std::vector<double>* matrices = nullptr;      // kRestMatrixSize per influence: the matrix that takes a rest point to the pose
};

struct RestMatchSettings
{
	RestTarget target = RestTarget::Delta;
	double strength = 1.0;         // above 0, at most 1
	double smoothness = 0.0;       // 0 to 1: how much each vertex's change is blended with its neighbors' changes. 0 is none
	std::size_t maxInfluences = 0; // above 0: a vertex keeps only this many non-zero weights, locked ones included
	std::vector<bool> locked;      // per influence, empty for none. A locked influence keeps its weight on every vertex
	std::vector<double> vertexScale; // parallel to the vertices, each in (0, 1]: a factor on strength, which is what lets a brush fall off
};

// The posed position of a vertex from one row of its weights (influenceCount of them).
void restMatchPosition(const RestMatchSkin& skin, const double* weights, std::size_t influenceCount, unsigned int vertex, double (&out)[3]);

// Works out new weights for the sorted, unique vertices and returns the rows that changed, in vertex order, in the
// shape smoothing returns. The weights of the vertices and their neighbors are fetched, and only the influences that
// carry weight there and are not locked take part. A vertex with no neighbor, no unlocked weight or no change is left
// out. Empty for settings that are not valid: strength outside (0, 1], smoothness outside [0, 1], or vertexScale not parallel to the vertices.
SmoothedWeights matchRestWeights(std::size_t vertexCount, const std::vector<unsigned int>& vertices, std::size_t influenceCount, const RestMatchSettings& settings, const RestMatchSkin& skin, const NeighborFetch& fetchNeighbors, const WeightFetch& fetch);
