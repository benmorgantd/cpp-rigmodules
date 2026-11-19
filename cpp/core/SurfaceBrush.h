#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// What a skin weight brush needs that is not about one operation (D65):
//  - SurfaceDistance: how far along the mesh surface each vertex is from the stroke's center, so the brush radius is a
//    length over the surface and not a ball in space. A brush near a thin gap, or on the other side of a thin wall,
//    does not reach across it, and two shells never share a dab;
//  - brushFalloff: how strong the brush is at a fraction of the radius;
//  - StrokeJournal: the weights from before a stroke, kept as the stroke's dabs overlap, so one undo restores them.

#include "SkinWeightSmooth.h"

#include <cstddef>
#include <unordered_map>
#include <vector>

// A vertex the stroke starts from and how far the center is from it (for a hit inside a face: the face's corners).
struct SurfaceSeed
{
	unsigned int vertex = 0;
	double distance = 0.0;
};

// Shortest paths along the mesh edges (Dijkstra), from any number of seeds at once, out to a radius. Bound once per
// mesh per stroke; a reach costs what the vertices inside the radius cost, not what the mesh costs.
class SurfaceDistance
{
public:
	// adjacency and positions (x, y, z per vertex) must outlive this object and keep their size. Positions may change
	// in place, which a deforming mesh does.
	void bind(const MeshAdjacency& adjacency, const std::vector<float>& positions);

	// The vertices within radius of the seeds along the surface, in vertex order, and their distances.
	void reach(const std::vector<SurfaceSeed>& seeds, double radius);

	const std::vector<unsigned int>& vertices() const;
	const std::vector<double>& distances() const;

private:
	const MeshAdjacency* fAdjacency = nullptr;
	const std::vector<float>* fPositions = nullptr;
	std::vector<double> fBest;     // per vertex, infinity until reached. Reset after each reach, so it costs nothing per call
	std::vector<unsigned int> fVertices;
	std::vector<double> fDistances;
};

// 1 at the center, 0 at the radius and beyond, smooth between (smoothstep). t is distance divided by radius.
double brushFalloff(double t);

// The first weight of every (vertex, influence) a stroke changed, so the stroke can be undone whole. A dab reports only
// the influences it changed, so an influence that no dab touched on a vertex has no entry and counts as unchanged.
class StrokeJournal
{
public:
	void reset(std::size_t influenceCount);
	// First write wins: a value is kept from the first dab that changed it, never from a later one.
	void record(const SmoothedWeights& edit);
	bool empty() const;
	std::vector<unsigned int> sortedVertices() const;

	// The weights of sortedVertices before the stroke, every influence, row-major. finalRows is the same table after
	// the stroke and supplies what the stroke never changed.
	std::vector<double> oldRows(const std::vector<unsigned int>& sortedVertices, const std::vector<double>& finalRows) const;

private:
	std::size_t fInfluenceCount = 0;
	std::unordered_map<unsigned int, std::size_t> fRowOf;
	std::vector<unsigned int> fVertices;
	std::vector<double> fRows;     // NaN until an influence changed on that vertex
};
