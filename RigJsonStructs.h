#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <maya/MString.h>

struct RigConstraintDriverData
{
	std::string socket;           // "module.socket"
	std::string label;            // enum label. Defaults to the socket string.
	double weight = 0.0;          // > 0 marks the initial active driver
};

struct RigConstraintData
{
	std::string name;
	std::string type = "Parent";
	std::string target;           // "module.childPlug"
	std::vector<RigConstraintDriverData> drivers;
	bool exposeSpace = true;  // For constraints with 1 parent we don't always need it switchable in the channel box
};

struct RigModuleData
{
    std::string moduleType;
    std::string name;
    std::string shapeType;
    std::string side;
    int moduleVersion = 1;
    std::unordered_map<std::string, std::string> parentStructure;
    nlohmann::json moduleArgs;
};

struct AssetRootData
{
    std::string assetId;
    std::string assetType;
};

struct RigRootData
{
    std::string rigTemplateName;
    std::string rigType;
    int rigVersion = -1;
    std::string rigAuthor;
    std::vector<RigModuleData> rigModules; // 1D Flat List of modules
	std::vector<RigConstraintData> rigConstraints;
};

// nlohmann JSON mapping macros
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AssetRootData, assetId, assetType)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RigRootData, rigTemplateName, rigVersion, rigType, rigAuthor, rigModules)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RigModuleData, moduleType, name, shapeType, side, moduleVersion, parentStructure, moduleArgs)

void from_json(const nlohmann::json& j, RigModuleData& module);
bool loadRigTemplate(const MString& filePath, AssetRootData& assetData, RigRootData& rigData, MString& outErr);

// Constraints ==================================================================

inline void from_json(const nlohmann::json& jsonData, RigConstraintDriverData& driver)
{
	jsonData.at("socket").get_to(driver.socket);
	driver.label = jsonData.value("label", driver.socket);
	driver.weight = jsonData.value("weight", 0.0);
}

inline void to_json(nlohmann::json& jsonData, const RigConstraintDriverData& driver)
{
	jsonData = nlohmann::json{ { "socket", driver.socket }, { "label", driver.label }, { "weight", driver.weight } };
}

inline void from_json(const nlohmann::json& jsonData, RigConstraintData& constraint)
{
	jsonData.at("name").get_to(constraint.name);
	jsonData.at("target").get_to(constraint.target);
	jsonData.at("drivers").get_to(constraint.drivers);
	constraint.type = jsonData.value("type", "Parent");
	// A single driver defaults to hidden in the Channel Box
	constraint.exposeSpace = jsonData.value("exposeSpace", constraint.drivers.size() > 1);
}

inline void to_json(nlohmann::json& jsonData, const RigConstraintData& constraint)
{
	jsonData = nlohmann::json{ { "name", constraint.name }, { "type", constraint.type },
		{ "target", constraint.target }, { "drivers", constraint.drivers }, { "exposeSpace", constraint.exposeSpace } };
}