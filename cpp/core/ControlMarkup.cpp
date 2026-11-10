#include "ControlMarkup.h"
#include "Constants.h"
#include "JointMarkup.h"
#include "JsonUtils.h"

ControlMarkup makeControlMarkup(const std::string& side, const std::string& usage)
{
	ControlMarkup markup;
	markup.side = side;
	markup.usage = usage;
	return markup;
}

bool validateControlMarkup(const ControlMarkup& markup, std::string& outError)
{
	if (markup.markupVersion != ControlMarkupFormat::CurrentVersion)
	{
		outError = "unsupported " + std::string(JointMarkupKey::MarkupVersion) + " " + std::to_string(markup.markupVersion);
		return false;
	}

	if (markup.usage.empty())
	{
		outError = "usage is empty";
		return false;
	}

	if (!isValidSideName(markup.side))
	{
		outError = "side '" + markup.side + "' is not one of " + SideNames::Center + ", " + SideNames::Left + ", " + SideNames::Right;
		return false;
	}

	return true;
}

void from_json(const nlohmann::json& jsonData, ControlMarkup& markup)
{
	jsonData.at(JointMarkupKey::MarkupVersion).get_to(markup.markupVersion);
	jsonData.at(RigAttr::Side).get_to(markup.side);
	jsonData.at(JointMarkupKey::Usage).get_to(markup.usage);
}

void to_json(nlohmann::json& jsonData, const ControlMarkup& markup)
{
	jsonData =
	{
		{ JointMarkupKey::MarkupVersion, markup.markupVersion },
		{ RigAttr::Side, markup.side },
		{ JointMarkupKey::Usage, markup.usage }
	};
}

bool parseControlMarkup(const std::string& text, ControlMarkup& outMarkup, std::string& outError)
{
	if (!JsonUtils::readJsonString(text, outMarkup, outError))
	{
		return false;
	}
	return validateControlMarkup(outMarkup, outError);
}

std::string serializeControlMarkup(const ControlMarkup& markup)
{
	return JsonUtils::writeJsonString(markup);
}

std::string serializeControlInfo(const ControlInfoList& controls)
{
	nlohmann::json result = nlohmann::json::object();
	for (const auto& [controlName, info] : controls)
	{
		nlohmann::json entry;
		entry[ControlInfoKey::ModuleType] = info.moduleType;
		entry[ControlInfoKey::Module] = info.module;
		entry[ControlInfoKey::Side] = info.markup.side;
		entry[ControlInfoKey::Usage] = info.markup.usage;
		entry[ControlInfoKey::Mirror] = info.hasMirror ? nlohmann::json(info.mirror) : nlohmann::json(nullptr);
		result[controlName] = entry;
	}
	return result.dump();
}
