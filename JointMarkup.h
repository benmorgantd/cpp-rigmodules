#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// Joint markup says what a joint is (side and usage) without relying on its name.
// It never names a module. Modules pick joints with [side, usage] selectors at build time.

#include <nlohmann/json.hpp>

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace JointMarkupFormat
{
	inline constexpr int CurrentVersion{ 1 };
}

struct JointMarkup
{
	int markupVersion = JointMarkupFormat::CurrentVersion;
	std::string usage;
	std::string side;
	bool isExported = false;   // JSON key "export". `export` is a C++ keyword.
	bool isSkinned = false;    // JSON key "skin"
};

// Every key is required. Fails on bad JSON, a missing or mistyped key, an empty usage,
// a side outside SideNames, or a markupVersion this build does not know.
bool parseJointMarkup(const std::string& text, JointMarkup& outMarkup, std::string& outError);
std::string serializeJointMarkup(const JointMarkup& markup);
bool validateJointMarkup(const JointMarkup& markup, std::string& outError);
bool isValidSideName(const std::string& side);

// Key mapping only. No validation, so callers go through parseJointMarkup.
void from_json(const nlohmann::json& jsonData, JointMarkup& markup);
void to_json(nlohmann::json& jsonData, const JointMarkup& markup);

// A module arg that picks one joint: ["Left", "upperArm"].
struct JointSelector
{
	std::string side;
	std::string usage;
};

void from_json(const nlohmann::json& jsonData, JointSelector& selector);
// Required by NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE on module args structs, which always generates both directions.
void to_json(nlohmann::json& jsonData, const JointSelector& selector);

// Every marked joint a rig is built from, keyed by (side, usage).
// Entry indices follow add order, so a caller can keep scene handles in a parallel array.
class JointMarkupIndex
{
public:
	// Fails if another joint already has this (side, usage).
	bool add(const JointMarkup& markup, const std::string& jointName, std::string& outError);

	// Finds the joint for a selector and records that moduleName drives it.
	// Fails if nothing matches, or the joint is already claimed (by any module, including this one),
	// because a joint's OPM can only have one driver.
	// Also fails on a selector side outside SideNames, so a typo is not reported as a missing joint.
	bool claim(const JointSelector& selector, const std::string& moduleName, std::size_t& outEntry, std::string& outError);

	// Joints marked for export that no module claimed. Nothing drives them, so they stay at rest in exported animation.
	std::vector<std::string> unclaimedExportedJoints() const;

private:
	struct Entry
	{
		std::string jointName;
		std::string claimedBy;
		bool isExported = false;
	};

	std::vector<Entry> fEntries;
	std::map<std::pair<std::string, std::string>, std::size_t> fLookup;   // (side, usage) -> entry
};
