#include "SkinWeightTable.h"

#include <algorithm>

std::vector<unsigned int> pruneToMaxInfluences(std::vector<double>& table, std::size_t influenceCount, std::size_t maxInfluences)
{
	std::vector<unsigned int> changed;
	if (influenceCount == 0 || maxInfluences == 0)
	{
		return changed;
	}

	// Reused for every row, so a dense mesh allocates once.
	std::vector<std::size_t> nonZero;
	nonZero.reserve(influenceCount);
	const std::size_t rowCount = table.size() / influenceCount;
	for (std::size_t row = 0; row < rowCount; ++row)
	{
		double* const weights = table.data() + row * influenceCount;
		nonZero.clear();
		for (std::size_t i = 0; i < influenceCount; ++i)
		{
			if (weights[i] != 0.0)
			{
				nonZero.push_back(i);
			}
		}
		if (nonZero.size() <= maxInfluences)
		{
			continue;
		}

		// Largest first, then lower index, so ties break the same way every run.
		const auto isKeptBefore = [weights](std::size_t a, std::size_t b)
		{
			return weights[a] != weights[b] ? weights[a] > weights[b] : a < b;
		};
		std::nth_element(nonZero.begin(), nonZero.begin() + maxInfluences - 1, nonZero.end(), isKeptBefore);

		double keptSum = 0.0;
		for (std::size_t k = 0; k < maxInfluences; ++k)
		{
			keptSum += weights[nonZero[k]];
		}
		for (std::size_t k = maxInfluences; k < nonZero.size(); ++k)
		{
			weights[nonZero[k]] = 0.0;
		}
		if (keptSum > 0.0)
		{
			for (std::size_t k = 0; k < maxInfluences; ++k)
			{
				weights[nonZero[k]] /= keptSum;
			}
		}
		changed.push_back(static_cast<unsigned int>(row));
	}
	return changed;
}
