#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// Which controls a rig traversal returns. An empty list does not filter on that field.
// Values in one list are alternatives, and every non-empty list must match.

#include "ControlMarkup.h"

#include <string>
#include <vector>

struct RigControlFilter
{
	std::vector<std::string> moduleTypes;   // Template moduleType names, e.g. "FkChain"
	std::vector<std::string> sides;
	std::vector<std::string> usages;

	bool matches(const std::string& moduleType, const ControlMarkup& markup) const;
};

// Fails on a side outside SideNames or a moduleType outside knownModuleTypes, so a typo is an error
// instead of an empty result.
bool validateRigControlFilter(const RigControlFilter& filter, const std::vector<std::string>& knownModuleTypes, std::string& outError);
