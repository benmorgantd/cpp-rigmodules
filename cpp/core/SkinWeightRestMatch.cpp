#include "SkinWeightRestMatch.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace
{
	// A row that moves by less than this is not written, so flat regions cost nothing and undo stays small.
	constexpr double kChangedEpsilon = 1e-9;
	// Below this a row has no weight to scale.
	constexpr double kMinimumSum = 1e-12;
	// Edges shorter than this have no direction.
	constexpr double kMinimumLength = 1e-12;
	// Below this the candidate positions of a vertex coincide, so no weights can move it.
	constexpr double kMinimumSpread = 1e-24;
	// How strongly a solve prefers the weights it started from, as a share of the spread of the candidate positions. It
	// only picks one answer when several fit equally (more than three influences), so it is small.
	constexpr double kRegularization = 1e-3;
	constexpr unsigned int kSolveIterations = 40;
	constexpr unsigned int kRotationIterations = 12;

	struct Vec3
	{
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
	};

	Vec3 pointAt(const double* positions, unsigned int vertex)
	{
		return { positions[3 * static_cast<std::size_t>(vertex)], positions[3 * static_cast<std::size_t>(vertex) + 1], positions[3 * static_cast<std::size_t>(vertex) + 2] };
	}

	Vec3 transformPoint(const double* m, const Vec3& p)
	{
		return { p.x * m[0] + p.y * m[3] + p.z * m[6] + m[9], p.x * m[1] + p.y * m[4] + p.z * m[7] + m[10], p.x * m[2] + p.y * m[5] + p.z * m[8] + m[11] };
	}

	double dot(const Vec3& a, const Vec3& b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	// The rotation nearest a 3 x 3 matrix, row-major, by Newton's iteration for the polar decomposition. The identity for
	// a matrix that has no inverse.
	void nearestRotation(const double* a, double (&q)[9])
	{
		std::copy(a, a + 9, q);
		for (unsigned int pass = 0; pass < kRotationIterations; ++pass)
		{
			// The cofactors over the determinant are the inverse transpose.
			const double cofactors[9] =
			{
				q[4] * q[8] - q[5] * q[7], q[5] * q[6] - q[3] * q[8], q[3] * q[7] - q[4] * q[6],
				q[2] * q[7] - q[1] * q[8], q[0] * q[8] - q[2] * q[6], q[1] * q[6] - q[0] * q[7],
				q[1] * q[5] - q[2] * q[4], q[2] * q[3] - q[0] * q[5], q[0] * q[4] - q[1] * q[3]
			};
			const double determinant = q[0] * cofactors[0] + q[1] * cofactors[1] + q[2] * cofactors[2];
			if (std::abs(determinant) < kMinimumSum)
			{
				const double identity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
				std::copy(identity, identity + 9, q);
				return;
			}
			for (std::size_t i = 0; i < 9; ++i)
			{
				q[i] = 0.5 * (q[i] + cofactors[i] / determinant);
			}
		}
	}

	// Moves a row to the nearest row that is not below 0 and adds up to total (Euclidean projection onto the simplex).
	void projectToSimplex(double* row, std::size_t count, double total, std::vector<double>& scratch)
	{
		scratch.assign(row, row + count);
		std::sort(scratch.begin(), scratch.end(), std::greater<double>());
		double cumulative = 0.0;
		double shift = 0.0;
		for (std::size_t k = 0; k < count; ++k)
		{
			cumulative += scratch[k];
			const double candidate = (cumulative - total) / static_cast<double>(k + 1);
			if (scratch[k] - candidate <= 0.0)
			{
				break;
			}
			shift = candidate;
		}
		for (std::size_t k = 0; k < count; ++k)
		{
			row[k] = std::max(row[k] - shift, 0.0);
		}
	}
}

void restMatchPosition(const RestMatchSkin& skin, const double* weights, std::size_t influenceCount, unsigned int vertex, double (&out)[3])
{
	const Vec3 rest = pointAt(skin.restPositions->data(), vertex);
	out[0] = 0.0;
	out[1] = 0.0;
	out[2] = 0.0;
	for (std::size_t i = 0; i < influenceCount; ++i)
	{
		if (weights[i] == 0.0)
		{
			continue;
		}
		const Vec3 moved = transformPoint(skin.matrices->data() + i * kRestMatrixSize, rest);
		out[0] += weights[i] * moved.x;
		out[1] += weights[i] * moved.y;
		out[2] += weights[i] * moved.z;
	}
}

SmoothedWeights matchRestWeights(std::size_t vertexCount, const std::vector<unsigned int>& vertices, std::size_t influenceCount, const RestMatchSettings& settings, const RestMatchSkin& skin, const NeighborFetch& fetchNeighbors, const WeightFetch& fetch)
{
	SmoothedWeights result;
	if (influenceCount == 0 || vertices.empty() || !(settings.strength > 0.0 && settings.strength <= 1.0) || !(settings.smoothness >= 0.0 && settings.smoothness <= 1.0) || settings.vertexScale.size() != vertices.size())
	{
		return result;
	}
	if (!skin.restPositions || !skin.matrices || skin.restPositions->size() < 3 * vertexCount || skin.matrices->size() < kRestMatrixSize * influenceCount)
	{
		return result;
	}
	for (std::size_t t = 0; t < vertices.size(); ++t)
	{
		if (vertices[t] >= vertexCount || (t > 0 && vertices[t] <= vertices[t - 1]) || !(settings.vertexScale[t] > 0.0 && settings.vertexScale[t] <= 1.0))
		{
			return result;
		}
	}

	std::vector<unsigned int> rowStart;
	std::vector<unsigned int> neighborIds;
	fetchNeighbors(vertices, rowStart, neighborIds);
	if (rowStart.size() != vertices.size() + 1)
	{
		return result;
	}

	// The working set: what is solved and the neighbors it reads.
	std::vector<unsigned int> working = vertices;
	working.insert(working.end(), neighborIds.begin(), neighborIds.end());
	std::sort(working.begin(), working.end());
	working.erase(std::unique(working.begin(), working.end()), working.end());
	const auto localOf = [&working](unsigned int vertex) -> std::size_t
	{
		return static_cast<std::size_t>(std::lower_bound(working.begin(), working.end(), vertex) - working.begin());
	};

	std::vector<double> table;
	fetch(working, table);
	if (table.size() != working.size() * influenceCount)
	{
		return result;
	}

	const double* rest = skin.restPositions->data();
	const double* matrices = skin.matrices->data();
	const auto isLocked = [&settings](std::size_t influence)
	{
		return influence < settings.locked.size() && settings.locked[influence];
	};

	// Where the current weights put every vertex of the working set.
	std::vector<double> posed(working.size() * 3);
	for (std::size_t row = 0; row < working.size(); ++row)
	{
		double position[3];
		restMatchPosition(skin, table.data() + row * influenceCount, influenceCount, working[row], position);
		std::copy(position, position + 3, posed.begin() + row * 3);
	}

	// Only unlocked influences with some weight in the working set take part, as in smoothing.
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

	std::vector<std::size_t> lockedColumns;
	for (std::size_t i = 0; i < influenceCount; ++i)
	{
		if (isLocked(i))
		{
			lockedColumns.push_back(i);
		}
	}

	const std::size_t targetCount = vertices.size();
	std::vector<double> before(targetCount * width);
	std::vector<double> after(targetCount * width);
	std::vector<double> delta(targetCount * width, 0.0);   // what the solve would change, before smoothing and strength
	std::vector<bool> isSolved(targetCount, false);
	std::vector<Vec3> candidates(width);
	std::vector<double> solved(width);
	std::vector<double> projectionScratch;
	std::vector<std::size_t> keepScratch;
	for (std::size_t t = 0; t < targetCount; ++t)
	{
		const unsigned int vertex = vertices[t];
		const std::size_t self = localOf(vertex);
		const double* weights = table.data() + self * influenceCount;
		double* start = before.data() + t * width;
		double* out = after.data() + t * width;
		double total = 0.0;
		for (std::size_t k = 0; k < width; ++k)
		{
			start[k] = weights[active[k]];
			total += start[k];
		}
		std::copy(start, start + width, out);
		const std::size_t neighborCount = rowStart[t + 1] - rowStart[t];
		if (total <= kMinimumSum || neighborCount == 0)
		{
			continue;
		}

		// Where the rest shape wants the vertex.
		const Vec3 restHere = pointAt(rest, vertex);
		const Vec3 current = pointAt(posed.data(), static_cast<unsigned int>(self));
		Vec3 target;
		if (settings.target == RestTarget::Edge)
		{
			std::size_t used = 0;
			for (unsigned int n = rowStart[t]; n < rowStart[t + 1]; ++n)
			{
				const Vec3 neighbor = pointAt(posed.data(), static_cast<unsigned int>(localOf(neighborIds[n])));
				const Vec3 restNeighbor = pointAt(rest, neighborIds[n]);
				const Vec3 away = { current.x - neighbor.x, current.y - neighbor.y, current.z - neighbor.z };
				const double length = std::sqrt(dot(away, away));
				if (length < kMinimumLength)
				{
					continue;
				}
				const Vec3 restEdge = { restHere.x - restNeighbor.x, restHere.y - restNeighbor.y, restHere.z - restNeighbor.z };
				const double scale = std::sqrt(dot(restEdge, restEdge)) / length;
				target.x += neighbor.x + away.x * scale;
				target.y += neighbor.y + away.y * scale;
				target.z += neighbor.z + away.z * scale;
				++used;
			}
			if (used == 0)
			{
				continue;
			}
			target = { target.x / static_cast<double>(used), target.y / static_cast<double>(used), target.z / static_cast<double>(used) };
		}
		else
		{
			Vec3 averagePosed;
			Vec3 averageRest;
			for (unsigned int n = rowStart[t]; n < rowStart[t + 1]; ++n)
			{
				const Vec3 neighbor = pointAt(posed.data(), static_cast<unsigned int>(localOf(neighborIds[n])));
				const Vec3 restNeighbor = pointAt(rest, neighborIds[n]);
				averagePosed = { averagePosed.x + neighbor.x, averagePosed.y + neighbor.y, averagePosed.z + neighbor.z };
				averageRest = { averageRest.x + restNeighbor.x, averageRest.y + restNeighbor.y, averageRest.z + restNeighbor.z };
			}
			const double inverseCount = 1.0 / static_cast<double>(neighborCount);
			const Vec3 delta = { restHere.x - averageRest.x * inverseCount, restHere.y - averageRest.y * inverseCount, restHere.z - averageRest.z * inverseCount };

			// The skin's own rotation here, every influence included, turns the rest delta into the pose.
			double blend[9] = {};
			for (std::size_t i = 0; i < influenceCount; ++i)
			{
				if (weights[i] != 0.0)
				{
					for (std::size_t e = 0; e < 9; ++e)
					{
						blend[e] += weights[i] * matrices[i * kRestMatrixSize + e];
					}
				}
			}
			double q[9];
			nearestRotation(blend, q);
			target = { averagePosed.x * inverseCount + delta.x * q[0] + delta.y * q[3] + delta.z * q[6],
					   averagePosed.y * inverseCount + delta.x * q[1] + delta.y * q[4] + delta.z * q[7],
					   averagePosed.z * inverseCount + delta.x * q[2] + delta.y * q[5] + delta.z * q[8] };
		}

		// The weights of the unlocked influences, each at least 0 and adding up to total, whose blend of candidate
		// positions is nearest the target. The candidates are centered, which the fixed total allows, so the step below
		// depends on how far apart they are and not on where the mesh is.
		Vec3 center;
		for (std::size_t k = 0; k < width; ++k)
		{
			candidates[k] = transformPoint(matrices + active[k] * kRestMatrixSize, restHere);
			center = { center.x + candidates[k].x, center.y + candidates[k].y, center.z + candidates[k].z };
		}
		center = { center.x / static_cast<double>(width), center.y / static_cast<double>(width), center.z / static_cast<double>(width) };
		Vec3 fixedPart = current;
		double spread = 0.0;
		for (std::size_t k = 0; k < width; ++k)
		{
			fixedPart = { fixedPart.x - start[k] * candidates[k].x, fixedPart.y - start[k] * candidates[k].y, fixedPart.z - start[k] * candidates[k].z };
			candidates[k] = { candidates[k].x - center.x, candidates[k].y - center.y, candidates[k].z - center.z };
			spread += dot(candidates[k], candidates[k]);
		}
		if (spread < kMinimumSpread)
		{
			continue;
		}
		const Vec3 wanted = { target.x - fixedPart.x - total * center.x, target.y - fixedPart.y - total * center.y, target.z - fixedPart.z - total * center.z };
		const double pull = kRegularization * spread / static_cast<double>(width);
		const double step = 1.0 / (2.0 * (spread + pull));
		std::copy(start, start + width, solved.begin());
		for (unsigned int pass = 0; pass < kSolveIterations; ++pass)
		{
			Vec3 residual = { -wanted.x, -wanted.y, -wanted.z };
			for (std::size_t k = 0; k < width; ++k)
			{
				residual = { residual.x + solved[k] * candidates[k].x, residual.y + solved[k] * candidates[k].y, residual.z + solved[k] * candidates[k].z };
			}
			for (std::size_t k = 0; k < width; ++k)
			{
				solved[k] -= step * 2.0 * (dot(candidates[k], residual) + pull * (solved[k] - start[k]));
			}
			projectToSimplex(solved.data(), width, total, projectionScratch);
		}

		double* change = delta.data() + t * width;
		for (std::size_t k = 0; k < width; ++k)
		{
			change[k] = solved[k] - start[k];
		}
		isSolved[t] = true;
	}

	// Smoothing the update: each solved vertex's change is blended with the average change of its neighbors, a vertex
	// that was not reached or not solved counting as no change. The solves are noisy because every vertex chases a target
	// that depends on neighbors that are moving too, and the noise is the crunch. Blending the changes, not the weights,
	// keeps a pose that already fits (no change anywhere) exactly where it is. A blend can leave the simplex, so the
	// result is projected back onto it.
	std::vector<double> blended(width);
	for (std::size_t t = 0; t < targetCount; ++t)
	{
		if (!isSolved[t])
		{
			continue;
		}
		const double* start = before.data() + t * width;
		const double* change = delta.data() + t * width;
		double* out = after.data() + t * width;
		const double* weights = table.data() + localOf(vertices[t]) * influenceCount;
		double total = 0.0;
		for (std::size_t k = 0; k < width; ++k)
		{
			total += start[k];
			blended[k] = start[k] + change[k];
		}
		if (settings.smoothness > 0.0)
		{
			const double inverseCount = 1.0 / static_cast<double>(rowStart[t + 1] - rowStart[t]);
			for (std::size_t k = 0; k < width; ++k)
			{
				double neighborChange = 0.0;
				for (unsigned int n = rowStart[t]; n < rowStart[t + 1]; ++n)
				{
					const auto found = std::lower_bound(vertices.begin(), vertices.end(), neighborIds[n]);
					if (found != vertices.end() && *found == neighborIds[n])
					{
						neighborChange += delta[static_cast<std::size_t>(found - vertices.begin()) * width + k];
					}
				}
				blended[k] = start[k] + (1.0 - settings.smoothness) * change[k] + settings.smoothness * neighborChange * inverseCount;
			}
			projectToSimplex(blended.data(), width, total, projectionScratch);
		}

		const double strength = settings.strength * settings.vertexScale[t];
		for (std::size_t k = 0; k < width; ++k)
		{
			out[k] = start[k] + strength * (blended[k] - start[k]);
		}
		if (settings.maxInfluences > 0)
		{
			// Locked weights count towards the limit, and the unlocked ones keep at least one.
			std::size_t lockedNonZero = 0;
			for (const std::size_t i : lockedColumns)
			{
				lockedNonZero += weights[i] != 0.0 ? 1 : 0;
			}
			const std::size_t limit = settings.maxInfluences > lockedNonZero ? settings.maxInfluences - lockedNonZero : 1;
			keepLargestWeights(out, width, limit, total, keepScratch);
		}
	}

	result.influences = active;
	for (std::size_t t = 0; t < targetCount; ++t)
	{
		const double* row = after.data() + t * width;
		const double* old = before.data() + t * width;
		bool changed = false;
		for (std::size_t k = 0; k < width && !changed; ++k)
		{
			changed = std::abs(row[k] - old[k]) > kChangedEpsilon;
		}
		if (changed)
		{
			result.vertices.push_back(vertices[t]);
			result.oldRows.insert(result.oldRows.end(), old, old + width);
			result.newRows.insert(result.newRows.end(), row, row + width);
		}
	}
	return result;
}
