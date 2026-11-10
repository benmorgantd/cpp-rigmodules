#include "FormatUpdate.h"
#include "RigTemplateValidation.h"

bool FormatUpdate::runUpdates(const UpdateTable& table, int version, nlohmann::json& data, const std::string& versionPath, ValidationReport& report)
{
	if (version < table.minVersion || version > table.currentVersion())
	{
		report.addError(versionPath, std::to_string(version) + " is not supported, this build reads versions "
			+ std::to_string(table.minVersion) + " to " + std::to_string(table.currentVersion()));
		return false;
	}

	for (int v = version; v < table.currentVersion(); ++v)
	{
		table.steps[static_cast<std::size_t>(v - table.minVersion)](data);
	}
	return true;
}
