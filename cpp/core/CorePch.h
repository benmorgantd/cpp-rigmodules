#pragma once

// Precompiled header for core/ (and the start of the one for maya/). RigModules.vcxproj force-includes it into every
// .cpp, so a file does not need to include it. It holds only what core/ may include (the standard library and
// nlohmann json), which keeps core/ Maya-free: do not add a Maya header here. Add a header only if most files use it,
// because a change here recompiles everything.

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
