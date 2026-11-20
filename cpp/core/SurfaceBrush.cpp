#include "SurfaceBrush.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace
{
	constexpr double kUnreached = std::numeric_limits<double>::infinity();
}

void SurfaceDistance::bind(const MeshAdjacency& adjacency, const std::vector<float>& positions)
{
	fAdjacency = &adjacency;
	fPositions = &positions;
	fBest.assign(adjacency.vertexCount(), kUnreached);
	fVertices.clear();
	fDistances.clear();
}

void SurfaceDistance::reach(const std::vector<SurfaceSeed>& seeds, double radius)
{
	fVertices.clear();
	fDistances.clear();
	if (!fAdjacency || !fPositions)
	{
		return;
	}

	using Entry = std::pair<double, unsigned int>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
	const auto reached = [this, &queue](unsigned int vertex, double distance)
	{
		if (distance >= fBest[vertex])
		{
			return;
		}
		if (fBest[vertex] == kUnreached)
		{
			fVertices.push_back(vertex);
		}
		fBest[vertex] = distance;
		queue.push({ distance, vertex });
	};
	for (const SurfaceSeed& seed : seeds)
	{
		if (seed.vertex < fBest.size() && seed.distance <= radius)
		{
			reached(seed.vertex, seed.distance);
		}
	}

	const float* position = fPositions->data();
	while (!queue.empty())
	{
		const Entry top = queue.top();
		queue.pop();
		if (top.first > fBest[top.second])
		{
			continue;
		}
		const float* from = position + 3 * static_cast<std::size_t>(top.second);
		for (unsigned int n = fAdjacency->offsets[top.second]; n < fAdjacency->offsets[top.second + 1]; ++n)
		{
			const unsigned int neighbor = fAdjacency->neighbors[n];
			const float* to = position + 3 * static_cast<std::size_t>(neighbor);
			const double dx = static_cast<double>(to[0]) - from[0];
			const double dy = static_cast<double>(to[1]) - from[1];
			const double dz = static_cast<double>(to[2]) - from[2];
			const double distance = top.first + std::sqrt(dx * dx + dy * dy + dz * dz);
			if (distance <= radius)
			{
				reached(neighbor, distance);
			}
		}
	}

	std::sort(fVertices.begin(), fVertices.end());
	fDistances.reserve(fVertices.size());
	for (const unsigned int vertex : fVertices)
	{
		fDistances.push_back(fBest[vertex]);
		fBest[vertex] = kUnreached;
	}
}

const std::vector<unsigned int>& SurfaceDistance::vertices() const
{
	return fVertices;
}

const std::vector<double>& SurfaceDistance::distances() const
{
	return fDistances;
}

const char* falloffName(FalloffMode mode)
{
	return mode == FalloffMode::Volume ? "volume" : "surface";
}

bool parseFalloff(const std::string& name, FalloffMode& mode)
{
	if (name == "surface")
	{
		mode = FalloffMode::Surface;
		return true;
	}
	if (name == "volume")
	{
		mode = FalloffMode::Volume;
		return true;
	}
	return false;
}

void volumeReach(const std::vector<float>& positions, const std::vector<Point3>& centers, double radius, std::vector<unsigned int>& vertices, std::vector<double>& distances)
{
	vertices.clear();
	distances.clear();
	const double radiusSquared = radius * radius;
	for (std::size_t v = 0; 3 * v + 2 < positions.size(); ++v)
	{
		double nearest = radiusSquared;
		bool isInside = false;
		for (const Point3& center : centers)
		{
			const double dx = static_cast<double>(positions[3 * v]) - center.x;
			const double dy = static_cast<double>(positions[3 * v + 1]) - center.y;
			const double dz = static_cast<double>(positions[3 * v + 2]) - center.z;
			const double squared = dx * dx + dy * dy + dz * dz;
			if (squared <= nearest)
			{
				nearest = squared;
				isInside = true;
			}
		}
		if (isInside)
		{
			vertices.push_back(static_cast<unsigned int>(v));
			distances.push_back(std::sqrt(nearest));
		}
	}
}

double brushFalloff(double t)
{
	if (t >= 1.0)
	{
		return 0.0;
	}
	if (t <= 0.0)
	{
		return 1.0;
	}
	const double s = 1.0 - t;
	return s * s * (3.0 - 2.0 * s);
}

void StrokeJournal::reset(std::size_t influenceCount)
{
	fInfluenceCount = influenceCount;
	fRowOf.clear();
	fVertices.clear();
	fRows.clear();
}

void StrokeJournal::record(const SmoothedWeights& edit)
{
	const std::size_t width = edit.influences.size();
	for (std::size_t r = 0; r < edit.vertices.size(); ++r)
	{
		const unsigned int vertex = edit.vertices[r];
		auto found = fRowOf.find(vertex);
		if (found == fRowOf.end())
		{
			found = fRowOf.emplace(vertex, fVertices.size()).first;
			fVertices.push_back(vertex);
			fRows.resize(fRows.size() + fInfluenceCount, std::numeric_limits<double>::quiet_NaN());
		}
		double* row = fRows.data() + found->second * fInfluenceCount;
		for (std::size_t k = 0; k < width; ++k)
		{
			double& slot = row[edit.influences[k]];
			if (std::isnan(slot))
			{
				slot = edit.oldRows[r * width + k];
			}
		}
	}
}

bool StrokeJournal::empty() const
{
	return fVertices.empty();
}

std::vector<unsigned int> StrokeJournal::sortedVertices() const
{
	std::vector<unsigned int> sorted = fVertices;
	std::sort(sorted.begin(), sorted.end());
	return sorted;
}

std::vector<double> StrokeJournal::oldRows(const std::vector<unsigned int>& sortedVertices, const std::vector<double>& finalRows) const
{
	std::vector<double> rows(finalRows);
	for (std::size_t r = 0; r < sortedVertices.size(); ++r)
	{
		const auto found = fRowOf.find(sortedVertices[r]);
		if (found == fRowOf.end())
		{
			continue;
		}
		const double* journaled = fRows.data() + found->second * fInfluenceCount;
		for (std::size_t k = 0; k < fInfluenceCount; ++k)
		{
			if (!std::isnan(journaled[k]))
			{
				rows[r * fInfluenceCount + k] = journaled[k];
			}
		}
	}
	return rows;
}
