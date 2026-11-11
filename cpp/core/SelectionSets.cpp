#include "SelectionSets.h"
#include "Constants.h"
#include "JointMarkup.h"

#include <cctype>
#include <map>

#include <nlohmann/json.hpp>

namespace
{
	// side and module are null when the set is not about one.
	std::string makeMarkup(const std::string& kind, const char* role, const std::string& side, const std::string& module)
	{
		nlohmann::json json;
		json[JointMarkupKey::MarkupVersion] = SelectionSets::MarkupVersion;
		json[SelectionSetKey::Kind] = kind;
		json[SelectionSetKey::Role] = role;
		json[RigAttr::Side] = side.empty() ? nlohmann::json() : nlohmann::json(side);
		json[SelectionSetKey::Module] = module.empty() ? nlohmann::json() : nlohmann::json(module);
		return json.dump();
	}

	std::string lowerCase(std::string text)
	{
		for (char& c : text)
		{
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return text;
	}
}

std::vector<SelectionSets::Set> SelectionSets::planSets(const std::string& kind, const std::vector<Item>& items)
{
	std::vector<Set> sets(1);
	sets[0].name = kind + "_set";
	sets[0].markup = makeMarkup(kind, SelectionSetRole::All, std::string(), std::string());

	for (const char* side : { SideNames::Left, SideNames::Right, SideNames::Center })
	{
		Set sideSet;
		sideSet.name = kind + "_" + lowerCase(side) + "_set";
		sideSet.markup = makeMarkup(kind, SelectionSetRole::Side, side, std::string());
		for (std::size_t i = 0; i < items.size(); ++i)
		{
			if (items[i].side == side)
			{
				sideSet.items.push_back(i);
			}
		}
		sets[0].children.push_back(sets.size());
		sets.push_back(sideSet);
	}

	const std::size_t modulesIndex = sets.size();
	Set modulesSet;
	modulesSet.name = kind + "_modules_set";
	modulesSet.markup = makeMarkup(kind, SelectionSetRole::Modules, std::string(), std::string());
	sets.push_back(modulesSet);
	sets[0].children.push_back(modulesIndex);

	// std::map keeps the module sets in name order, so the plan is stable between builds.
	std::map<std::string, std::size_t> moduleSets;
	for (const Item& item : items)
	{
		if (!item.module.empty())
		{
			moduleSets[item.module];
		}
	}
	for (auto& entry : moduleSets)
	{
		entry.second = sets.size();
		Set moduleSet;
		moduleSet.name = entry.first + "_" + kind + "_set";
		moduleSet.markup = makeMarkup(kind, SelectionSetRole::Module, std::string(), entry.first);
		sets.push_back(moduleSet);
		sets[modulesIndex].children.push_back(entry.second);
	}
	for (std::size_t i = 0; i < items.size(); ++i)
	{
		if (!items[i].module.empty())
		{
			sets[moduleSets[items[i].module]].items.push_back(i);
		}
	}
	return sets;
}
