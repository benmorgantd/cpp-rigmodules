#include "SelectionSets.h"
#include "Constants.h"
#include "JointMarkup.h"

#include <map>

#include <nlohmann/json.hpp>

namespace
{
	// side and module are null when the set is not about one.
	std::string makeMarkup(const std::string& kind, const std::string& side, const std::string& module)
	{
		nlohmann::json json;
		json[JointMarkupKey::MarkupVersion] = SelectionSets::MarkupVersion;
		json[SelectionSetKey::Kind] = kind;
		json[RigAttr::Side] = side.empty() ? nlohmann::json() : nlohmann::json(side);
		json[SelectionSetKey::Module] = module.empty() ? nlohmann::json() : nlohmann::json(module);
		return json.dump();
	}
}

std::vector<SelectionSets::Set> SelectionSets::planSets(const std::string& kind, const std::vector<Item>& items)
{
	std::vector<Set> sets(1);
	sets[0].name = kind + "Set";
	sets[0].markup = makeMarkup(kind, std::string(), std::string());

	// std::map keeps the module subsets in name order, so the plan is stable between builds.
	std::map<std::string, std::size_t> moduleSets;
	for (std::size_t i = 0; i < items.size(); ++i)
	{
		if (items[i].module.empty())
		{
			sets[0].items.push_back(i);
		}
		else
		{
			moduleSets[items[i].module];
		}
	}
	for (auto& entry : moduleSets)
	{
		entry.second = sets.size();
		Set moduleSet;
		moduleSet.name = entry.first + "_" + kind + "Set";
		moduleSet.markup = makeMarkup(kind, std::string(), entry.first);
		sets.push_back(moduleSet);
		sets[0].children.push_back(entry.second);
	}
	for (std::size_t i = 0; i < items.size(); ++i)
	{
		if (!items[i].module.empty())
		{
			sets[moduleSets[items[i].module]].items.push_back(i);
		}
	}

	for (const char* side : { SideNames::Left, SideNames::Right, SideNames::Center })
	{
		Set sideSet;
		sideSet.name = kind + side + "Set";
		sideSet.markup = makeMarkup(kind, side, std::string());
		for (std::size_t i = 0; i < items.size(); ++i)
		{
			if (items[i].side == side)
			{
				sideSet.items.push_back(i);
			}
		}
		sets.push_back(sideSet);
	}
	return sets;
}
