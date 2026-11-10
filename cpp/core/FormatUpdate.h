#pragma once

// Maya-free core: no Maya headers here.
// Format versions and their update steps (D33). Every saved format has one integer version, which goes up only
// when saved data changes shape or meaning. Old data is brought forward by running the format's steps in order,
// so the code after the update reads only the current shape.

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

struct ValidationReport;

// Rewrites saved JSON from one version to the next, in place. A step is tolerant: data it doesn't recognise is
// left alone for the validation that follows to report.
using UpdateStep = void (*)(nlohmann::json& data);

// A format's steps. steps[i] updates version (minVersion + i) to the next, so the current version is
// minVersion + steps.size(). Raising minVersion and deleting the steps below it drops support for old data.
struct UpdateTable
{
	int minVersion = 1;
	std::vector<UpdateStep> steps;

	int currentVersion() const
	{
		return minVersion + static_cast<int>(steps.size());
	}
};

namespace FormatUpdate
{
	// Runs every step from `version` to the table's current version. False, with an error at versionPath, for a
	// version this build doesn't read: below minVersion or above the current version.
	bool runUpdates(const UpdateTable& table, int version, nlohmann::json& data, const std::string& versionPath, ValidationReport& report);
}
