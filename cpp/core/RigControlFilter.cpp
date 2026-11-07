#include "RigControlFilter.h"
#include "Constants.h"
#include "JointMarkup.h"

#include <algorithm>

namespace
{
	bool matchesAny(const std::vector<std::string>& values, const std::string& value)
	{
		return values.empty() || std::find(values.begin(), values.end(), value) != values.end();
	}

	std::string joinNames(const std::vector<std::string>& names)
	{
		std::string joined;
		for (const std::string& name : names)
		{
			joined += (joined.empty() ? "" : ", ") + name;
		}
		return joined;
	}
}

bool RigControlFilter::matches(const std::string& moduleType, const ControlMarkup& markup) const
{
	return matchesAny(moduleTypes, moduleType) && matchesAny(sides, markup.side) && matchesAny(usages, markup.usage);
}

bool validateRigControlFilter(const RigControlFilter& filter, const std::vector<std::string>& knownModuleTypes, std::string& outError)
{
	for (const std::string& side : filter.sides)
	{
		if (!isValidSideName(side))
		{
			outError = "side '" + side + "' is not one of " + SideNames::Center + ", " + SideNames::Left + ", " + SideNames::Right;
			return false;
		}
	}

	for (const std::string& moduleType : filter.moduleTypes)
	{
		if (std::find(knownModuleTypes.begin(), knownModuleTypes.end(), moduleType) == knownModuleTypes.end())
		{
			outError = "moduleType '" + moduleType + "' is not one of " + joinNames(knownModuleTypes);
			return false;
		}
	}

	return true;
}
