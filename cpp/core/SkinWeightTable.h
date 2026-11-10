#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// Operations on a skin weight table: one row per vertex, one column per influence, row-major, the layout
// MFnSkinCluster::getWeights and setWeights use for every vertex at once. The home for the paintable skin tools
// (normalize, mirror, smooth): each works on the whole table in memory, so the scene side reads and writes it in
// one call each.

#include <cstddef>
#include <vector>

// On every row with more than maxInfluences non-zero weights, keeps the largest maxInfluences, sets the rest to 0
// and divides the kept weights by their sum. Equal weights keep the lower influence index, so the result is
// repeatable. Rows with maxInfluences or fewer non-zero weights are left exactly as they are.
// Returns the rows it changed, in order. maxInfluences must be at least 1.
std::vector<unsigned int> pruneToMaxInfluences(std::vector<double>& table, std::size_t influenceCount, std::size_t maxInfluences);
