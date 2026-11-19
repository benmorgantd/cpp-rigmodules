#include "SkinWeightSmooth.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace
{
	// A row that moves by less than this is not written, so flat regions cost nothing and undo stays small.
	constexpr double kChangedEpsilon = 1e-9;
	// Below this a row has no weight to scale.
	constexpr double kMinimumSum = 1e-12;

	// Calls onEdge(a, b) once per polygon edge, so an edge shared by two faces is seen twice. Degenerate edges are skipped.
	template <typename OnEdge>
	void forEachFaceEdge(const std::vector<int>& faceCounts, const std::vector<int>& faceConnects, OnEdge onEdge)
	{
		std::size_t corner = 0;
		for (const int count : faceCounts)
		{
			for (int k = 0; k < count; ++k)
			{
				const unsigned int a = static_cast<unsigned int>(faceConnects[corner + k]);
				const unsigned int b = static_cast<unsigned int>(faceConnects[corner + (k + 1) % count]);
				if (a != b)
				{
					onEdge(a, b);
				}
			}
			corner += static_cast<std::size_t>(count);
		}
	}

	// Keeps the limit largest weights of the row (ties keep the lower column), zeroes the rest and scales what is left
	// to total. Same tie rule as pruneToMaxInfluences.
	void keepLargest(double* row, std::size_t columns, std::size_t limit, double total, std::vector<std::size_t>& scratch)
	{
		scratch.clear();
		for (std::size_t k = 0; k < columns; ++k)
		{
			if (row[k] != 0.0)
			{
				scratch.push_back(k);
			}
		}
		if (scratch.size() <= limit)
		{
			return;
		}
		const auto isKeptBefore = [row](std::size_t a, std::size_t b)
		{
			return row[a] != row[b] ? row[a] > row[b] : a < b;
		};
		std::nth_element(scratch.begin(), scratch.begin() + limit - 1, scratch.end(), isKeptBefore);
		double keptSum = 0.0;
		for (std::size_t k = 0; k < limit; ++k)
		{
			keptSum += row[scratch[k]];
		}
		for (std::size_t k = limit; k < scratch.size(); ++k)
		{
			row[scratch[k]] = 0.0;
		}
		if (keptSum > kMinimumSum)
		{
			const double scale = total / keptSum;
			for (std::size_t k = 0; k < limit; ++k)
			{
				row[scratch[k]] *= scale;
			}
		}
	}
}

std::size_t MeshAdjacency::vertexCount() const
{
	return offsets.empty() ? 0 : offsets.size() - 1;
}

MeshAdjacency buildMeshAdjacency(const std::vector<int>& faceCounts, const std::vector<int>& faceConnects, std::size_t vertexCount)
{
	// Rows first with repeats, one entry per edge side, then each row is sorted and compacted.
	std::vector<unsigned int> start(vertexCount + 1, 0);
	forEachFaceEdge(faceCounts, faceConnects, [&start](unsigned int a, unsigned int b)
	{
		++start[a + 1];
		++start[b + 1];
	});
	std::partial_sum(start.begin(), start.end(), start.begin());

	std::vector<unsigned int> raw(start.back());
	std::vector<unsigned int> cursor(start.begin(), start.end() - 1);
	forEachFaceEdge(faceCounts, faceConnects, [&raw, &cursor](unsigned int a, unsigned int b)
	{
		raw[cursor[a]++] = b;
		raw[cursor[b]++] = a;
	});

	MeshAdjacency adjacency;
	adjacency.offsets.assign(vertexCount + 1, 0);
	adjacency.neighbors.reserve(raw.size());
	for (std::size_t v = 0; v < vertexCount; ++v)
	{
		const auto first = raw.begin() + start[v];
		auto last = raw.begin() + start[v + 1];
		std::sort(first, last);
		last = std::unique(first, last);
		adjacency.neighbors.insert(adjacency.neighbors.end(), first, last);
		adjacency.offsets[v + 1] = static_cast<unsigned int>(adjacency.neighbors.size());
	}
	return adjacency;
}

NeighborFetch neighborsFromAdjacency(const MeshAdjacency& adjacency)
{
	return [&adjacency](const std::vector<unsigned int>& targets, std::vector<unsigned int>& offsets, std::vector<unsigned int>& neighbors)
	{
		offsets.assign(1, 0);
		neighbors.clear();
		for (const unsigned int v : targets)
		{
			neighbors.insert(neighbors.end(), adjacency.neighbors.begin() + adjacency.offsets[v], adjacency.neighbors.begin() + adjacency.offsets[v + 1]);
			offsets.push_back(static_cast<unsigned int>(neighbors.size()));
		}
	};
}

SmoothedWeights smoothSkinWeights(std::size_t vertexCount, const std::vector<unsigned int>* vertices, std::size_t influenceCount, const SkinSmoothSettings& settings, const NeighborFetch& fetchNeighbors, const WeightFetch& fetch)
{
	SmoothedWeights result;
	const std::size_t vertexTotal = vertexCount;
	if (influenceCount == 0 || vertexTotal == 0 || settings.iterations < 1 || !(settings.strength > 0.0 && settings.strength <= 1.0))
	{
		return result;
	}

	std::vector<unsigned int> targets;
	if (vertices)
	{
		targets = *vertices;
		std::sort(targets.begin(), targets.end());
		targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
		while (!targets.empty() && targets.back() >= vertexTotal)
		{
			targets.pop_back();
		}
	}
	else
	{
		targets.resize(vertexTotal);
		std::iota(targets.begin(), targets.end(), 0u);
	}
	if (targets.empty())
	{
		return result;
	}
	if (!settings.vertexScale.empty())
	{
		// Parallel to the caller's own list, so that list must already have been sorted, unique and in range.
		if (!vertices || targets != *vertices || settings.vertexScale.size() != targets.size())
		{
			return result;
		}
		for (const double scale : settings.vertexScale)
		{
			if (!(scale > 0.0 && scale <= 1.0))
			{
				return result;
			}
		}
	}

	std::vector<unsigned int> rowStart;
	std::vector<unsigned int> neighborIds;
	fetchNeighbors(targets, rowStart, neighborIds);
	if (rowStart.size() != targets.size() + 1)
	{
		return result;
	}

	// The working set: what is smoothed and the neighbors it reads. The rest of the mesh is never fetched.
	std::vector<unsigned int> working = targets;
	if (vertices)
	{
		working.insert(working.end(), neighborIds.begin(), neighborIds.end());
		std::sort(working.begin(), working.end());
		working.erase(std::unique(working.begin(), working.end()), working.end());
	}
	const bool isWholeMesh = working.size() == vertexTotal;
	const auto localOf = [&working, isWholeMesh](unsigned int vertex) -> std::size_t
	{
		return isWholeMesh ? vertex : static_cast<std::size_t>(std::lower_bound(working.begin(), working.end(), vertex) - working.begin());
	};

	std::vector<double> table;
	fetch(working, table);
	if (table.size() != working.size() * influenceCount)
	{
		return result;
	}

	const auto isLocked = [&settings](std::size_t influence)
	{
		return influence < settings.locked.size() && settings.locked[influence];
	};

	// Only unlocked influences with some weight in the working set take part: a patch of skin has a handful out of
	// the cluster's dozens. A locked one never changes, so it is not carried.
	std::vector<unsigned int> active;
	{
		std::vector<bool> hasWeight(influenceCount, false);
		for (std::size_t row = 0; row < working.size(); ++row)
		{
			const double* weights = table.data() + row * influenceCount;
			for (std::size_t i = 0; i < influenceCount; ++i)
			{
				if (weights[i] != 0.0)
				{
					hasWeight[i] = true;
				}
			}
		}
		for (std::size_t i = 0; i < influenceCount; ++i)
		{
			if (hasWeight[i] && !isLocked(i))
			{
				active.push_back(static_cast<unsigned int>(i));
			}
		}
	}
	const std::size_t width = active.size();
	if (width == 0)
	{
		return result;
	}

	// The active columns of the working set, and per target its own row, its neighbors' rows and the total its
	// unlocked weights must keep (the locked share never moves, so the rest keeps what was left over).
	std::vector<double> current(working.size() * width);
	for (std::size_t row = 0; row < working.size(); ++row)
	{
		for (std::size_t k = 0; k < width; ++k)
		{
			current[row * width + k] = table[row * influenceCount + active[k]];
		}
	}
	const std::size_t targetCount = targets.size();
	std::vector<std::size_t> selfRow(targetCount);
	std::vector<std::size_t> neighborStart(targetCount + 1, 0);
	std::vector<std::size_t> neighborRow;
	std::vector<double> total(targetCount, 0.0);
	std::vector<double> before(targetCount * width);
	for (std::size_t t = 0; t < targetCount; ++t)
	{
		selfRow[t] = localOf(targets[t]);
		for (unsigned int n = rowStart[t]; n < rowStart[t + 1]; ++n)
		{
			neighborRow.push_back(localOf(neighborIds[n]));
		}
		neighborStart[t + 1] = neighborRow.size();
		for (std::size_t k = 0; k < width; ++k)
		{
			const double w = current[selfRow[t] * width + k];
			before[t * width + k] = w;
			total[t] += w;
		}
	}

	// Each pass is computed for every target from the rows as the last pass left them, then written back.
	std::vector<double> next(targetCount * width);
	for (unsigned int pass = 0; pass < settings.iterations; ++pass)
	{
		for (std::size_t t = 0; t < targetCount; ++t)
		{
			const double* self = current.data() + selfRow[t] * width;
			double* out = next.data() + t * width;
			std::copy(self, self + width, out);
			for (std::size_t n = neighborStart[t]; n < neighborStart[t + 1]; ++n)
			{
				const double* neighbor = current.data() + neighborRow[n] * width;
				for (std::size_t k = 0; k < width; ++k)
				{
					out[k] += neighbor[k];
				}
			}
			const double inverseCount = 1.0 / static_cast<double>(neighborStart[t + 1] - neighborStart[t] + 1);
			const double strength = settings.vertexScale.empty() ? settings.strength : settings.strength * settings.vertexScale[t];
			double sum = 0.0;
			for (std::size_t k = 0; k < width; ++k)
			{
				out[k] = self[k] + strength * (out[k] * inverseCount - self[k]);
				sum += out[k];
			}
			if (sum > kMinimumSum)
			{
				const double scale = total[t] / sum;
				for (std::size_t k = 0; k < width; ++k)
				{
					out[k] *= scale;
				}
			}
		}
		for (std::size_t t = 0; t < targetCount; ++t)
		{
			std::copy(next.begin() + t * width, next.begin() + (t + 1) * width, current.begin() + selfRow[t] * width);
		}
	}

	if (settings.maxInfluences > 0)
	{
		std::vector<std::size_t> lockedColumns;
		for (std::size_t i = 0; i < influenceCount; ++i)
		{
			if (isLocked(i))
			{
				lockedColumns.push_back(i);
			}
		}
		std::vector<std::size_t> scratch;
		for (std::size_t t = 0; t < targetCount; ++t)
		{
			// Locked weights count towards the limit, and the unlocked ones keep at least one.
			std::size_t lockedNonZero = 0;
			for (const std::size_t i : lockedColumns)
			{
				lockedNonZero += table[selfRow[t] * influenceCount + i] != 0.0 ? 1 : 0;
			}
			const std::size_t limit = settings.maxInfluences > lockedNonZero ? settings.maxInfluences - lockedNonZero : 1;
			keepLargest(current.data() + selfRow[t] * width, width, limit, total[t], scratch);
		}
	}

	result.influences = active;
	for (std::size_t t = 0; t < targetCount; ++t)
	{
		const double* row = current.data() + selfRow[t] * width;
		const double* old = before.data() + t * width;
		bool changed = false;
		for (std::size_t k = 0; k < width && !changed; ++k)
		{
			changed = std::abs(row[k] - old[k]) > kChangedEpsilon;
		}
		if (changed)
		{
			result.vertices.push_back(targets[t]);
			result.oldRows.insert(result.oldRows.end(), old, old + width);
			result.newRows.insert(result.newRows.end(), row, row + width);
		}
	}
	return result;
}
