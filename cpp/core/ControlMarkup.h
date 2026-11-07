#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// Control markup says what a control is (side and usage), so tools find controls without parsing names.
// The build writes it: a control made for a joint copies that joint's (side, usage), and layout controls are
// (Center, layout_N). (side, usage) is unique across a rig's controls, so it keys shape files and pose data.

#include <nlohmann/json.hpp>

#include <string>

namespace ControlMarkupFormat
{
	inline constexpr int CurrentVersion{ 1 };
}

struct ControlMarkup
{
	int markupVersion = ControlMarkupFormat::CurrentVersion;
	std::string side;
	std::string usage;
};

// Markup at the current version.
ControlMarkup makeControlMarkup(const std::string& side, const std::string& usage);

// Every key is required. Fails on bad JSON, a missing or mistyped key, an empty usage,
// a side outside SideNames, or a markupVersion this build does not know.
bool parseControlMarkup(const std::string& text, ControlMarkup& outMarkup, std::string& outError);
std::string serializeControlMarkup(const ControlMarkup& markup);
bool validateControlMarkup(const ControlMarkup& markup, std::string& outError);

// Key mapping only. No validation, so callers go through parseControlMarkup.
void from_json(const nlohmann::json& jsonData, ControlMarkup& markup);
void to_json(nlohmann::json& jsonData, const ControlMarkup& markup);
