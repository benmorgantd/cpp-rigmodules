#include "JointMarkup.h"
#include "Constants.h"
#include "JsonUtils.h"

bool isValidSideName(const std::string& side)
{
	return side == SideNames::Center || side == SideNames::Left || side == SideNames::Right;
}

bool validateJointMarkup(const JointMarkup& markup, std::string& outError)
{
	if (markup.markupVersion != JointMarkupFormat::CurrentVersion)
	{
		outError = "unsupported " + std::string(JointMarkupKey::MarkupVersion) + " " + std::to_string(markup.markupVersion);
		return false;
	}

	if (markup.usage.empty())
	{
		outError = "usage is empty";
		return false;
	}

	// Strict on purpose: getSideFromString falls back to Center, which would hide a typo here.
	if (!isValidSideName(markup.side))
	{
		outError = "side '" + markup.side + "' is not one of " + SideNames::Center + ", " + SideNames::Left + ", " + SideNames::Right;
		return false;
	}

	return true;
}

void from_json(const nlohmann::json& jsonData, JointMarkup& markup)
{
	jsonData.at(JointMarkupKey::MarkupVersion).get_to(markup.markupVersion);
	jsonData.at(JointMarkupKey::Usage).get_to(markup.usage);
	jsonData.at(RigAttr::Side).get_to(markup.side);
	jsonData.at(JointMarkupKey::Export).get_to(markup.isExported);
	jsonData.at(JointMarkupKey::Skin).get_to(markup.isSkinned);
}

void to_json(nlohmann::json& jsonData, const JointMarkup& markup)
{
	jsonData =
	{
		{ JointMarkupKey::MarkupVersion, markup.markupVersion },
		{ JointMarkupKey::Usage, markup.usage },
		{ RigAttr::Side, markup.side },
		{ JointMarkupKey::Export, markup.isExported },
		{ JointMarkupKey::Skin, markup.isSkinned }
	};
}

bool parseJointMarkup(const std::string& text, JointMarkup& outMarkup, std::string& outError)
{
	if (!JsonUtils::readJsonString(text, outMarkup, outError))
	{
		return false;
	}
	return validateJointMarkup(outMarkup, outError);
}

std::string serializeJointMarkup(const JointMarkup& markup)
{
	return JsonUtils::writeJsonString(markup);
}

void from_json(const nlohmann::json& jsonData, JointSelector& selector)
{
	if (!jsonData.is_array() || jsonData.size() != 2)
	{
		throw nlohmann::json::type_error::create(302, "joint selector must be [side, usage], got " + jsonData.dump(), &jsonData);
	}

	jsonData.at(0).get_to(selector.side);
	jsonData.at(1).get_to(selector.usage);
}

void to_json(nlohmann::json& jsonData, const JointSelector& selector)
{
	jsonData = nlohmann::json::array({ selector.side, selector.usage });
}

bool JointMarkupIndex::add(const JointMarkup& markup, const std::string& jointName, std::string& outError)
{
	const std::pair<std::string, std::string> key{ markup.side, markup.usage };
	auto it = fLookup.find(key);
	if (it != fLookup.end())
	{
		outError = "joints '" + fEntries[it->second].jointName + "' and '" + jointName
			+ "' both have side " + markup.side + ", usage " + markup.usage;
		return false;
	}

	fLookup[key] = fEntries.size();
	fEntries.push_back(Entry{ jointName, std::string() });
	return true;
}

bool JointMarkupIndex::claim(const JointSelector& selector, const std::string& moduleName, std::size_t& outEntry, std::string& outError)
{
	auto it = fLookup.find({ selector.side, selector.usage });
	if (it == fLookup.end())
	{
		outError = "module '" + moduleName + "': no joint has side " + selector.side + ", usage " + selector.usage;
		return false;
	}

	Entry& entry = fEntries[it->second];
	if (!entry.claimedBy.empty())
	{
		outError = "module '" + moduleName + "': joint '" + entry.jointName + "' is already driven by module '" + entry.claimedBy + "'";
		return false;
	}

	entry.claimedBy = moduleName;
	outEntry = it->second;
	return true;
}
