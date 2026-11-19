#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// Smoothing skin weights over a mesh's neighbors (D64). Built for the command now and a brush later, so a dab costs
// what the vertices under it cost, not what the mesh costs:
//  - the neighbors of only the vertices being smoothed are asked for, through a callback, so nothing is built for the
//    rest of the mesh (a whole-mesh smooth can hand over a bulk MeshAdjacency instead);
//  - only the working set is read: the vertices to smooth and their neighbors, through a fetch callback;
//  - only the influences that carry weight there are carried through the passes and written back;
//  - each pass reads the previous pass only (Jacobi), so every vertex updates at the same time and the order of the
//    vertices changes nothing.
// A vertex becomes the average of itself and its neighbors. A plain neighbor average swaps a checkerboard pattern on a
// quad mesh every pass and never damps it.

#include <cstddef>
#include <functional>
#include <vector>

// Which vertices share an edge, as compressed rows: the neighbors of vertex v, sorted and without repeats, are
// neighbors[offsets[v]] up to neighbors[offsets[v + 1]]. The bulk form, for smoothing a whole mesh.
struct MeshAdjacency
{
	std::vector<unsigned int> offsets;
	std::vector<unsigned int> neighbors;

	std::size_t vertexCount() const;
};

// The neighbor map of a mesh from its polygons: faceCounts holds each face's vertex count and faceConnects every
// face's vertex ids one face after another (what MFnMesh::getVertices returns).
MeshAdjacency buildMeshAdjacency(const std::vector<int>& faceCounts, const std::vector<int>& faceConnects, std::size_t vertexCount);

// Fills the neighbors of the sorted, unique target vertices: offsets has targets.size() + 1 entries and row i, from
// neighbors[offsets[i]] up to neighbors[offsets[i + 1]], holds the neighbors of targets[i], sorted and without repeats.
using NeighborFetch = std::function<void(const std::vector<unsigned int>& targets, std::vector<unsigned int>& offsets, std::vector<unsigned int>& neighbors)>;

// A NeighborFetch that reads rows out of a bulk adjacency, which must outlive it.
NeighborFetch neighborsFromAdjacency(const MeshAdjacency& adjacency);

struct SkinSmoothSettings
{
	unsigned int iterations = 3;   // at least 1
	double strength = 1.0;         // how far each pass goes towards the average: above 0, at most 1
	std::size_t maxInfluences = 0; // above 0: a smoothed vertex keeps only this many non-zero weights, locked ones included
	std::vector<bool> locked;      // per influence, empty for none. A locked influence keeps its weight on every vertex
	// Per vertex, empty for none: a factor in (0, 1] on strength, which is what lets a brush fall off. When set, the
	// vertices passed to smoothSkinWeights must be sorted and unique and this parallel to them, or nothing is smoothed.
	std::vector<double> vertexScale;
};

// The vertices whose weights changed. Each row of oldRows and newRows holds the weights of influences[0..] for the
// vertex at the same position in vertices. An influence not listed did not change.
struct SmoothedWeights
{
	std::vector<unsigned int> vertices;
	std::vector<unsigned int> influences;
	std::vector<double> oldRows;
	std::vector<double> newRows;
};

// Reads the weights of the sorted vertex ids it is given, row-major with influenceCount columns, into table.
using WeightFetch = std::function<void(const std::vector<unsigned int>& vertices, std::vector<double>& table)>;

// Smooths the weights of vertices (null is every vertex) and returns the rows that changed, in vertex order. A vertex
// with a locked influence keeps that weight, and its other weights keep their total. A vertex with no neighbor, a row
// that moves by less than 1e-9 and a mesh with no weight in the working set are left out. iterations must be at least
// 1 and strength above 0 and at most 1.
SmoothedWeights smoothSkinWeights(std::size_t vertexCount, const std::vector<unsigned int>* vertices, std::size_t influenceCount, const SkinSmoothSettings& settings, const NeighborFetch& fetchNeighbors, const WeightFetch& fetch);
