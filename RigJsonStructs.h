#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>

#include "Constants.h"

// Maya-free core: no Maya headers in this file or its .cpp.

struct ValidationReport;

struct RigConstraintDriverData
{
	std::string socket;           // "module.socket"
	std::string label;            // enum label. Defaults to the socket string.
	double weight = 0.0;          // > 0 marks the initial active driver
};

struct RigConstraintData
{
	std::string name;
	std::string type = ConstraintTypes::Parent;
	std::string target;           // "module.childPlug"
	std::vector<RigConstraintDriverData> drivers;
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
    std::string rigName;          // Optional key, defaults to rigTemplateName. Not in the macro below, which requires every field.
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
// Reads the file, checks required and unknown keys, then parses. Every problem goes into the report.
// False if the template could not be parsed into the structs. Errors in the report still need checking on true.
bool loadRigTemplate(const std::string& filePath, AssetRootData& assetData, RigRootData& rigData, ValidationReport& report);

// Constraints ==================================================================

inline void from_json(const nlohmann::json& jsonData, RigConstraintDriverData& driver)
{
	jsonData.at(TemplateKey::DriverSocket).get_to(driver.socket);
	driver.label = jsonData.value(TemplateKey::DriverLabel, driver.socket);
	driver.weight = jsonData.value(TemplateKey::DriverWeight, 0.0);
}

inline void to_json(nlohmann::json& jsonData, const RigConstraintDriverData& driver)
{
	jsonData = nlohmann::json{ { TemplateKey::DriverSocket, driver.socket }, { TemplateKey::DriverLabel, driver.label }, { TemplateKey::DriverWeight, driver.weight } };
}

inline void from_json(const nlohmann::json& jsonData, RigConstraintData& constraint)
{
	jsonData.at(TemplateKey::Name).get_to(constraint.name);
	jsonData.at(TemplateKey::ConstraintTarget).get_to(constraint.target);
	jsonData.at(TemplateKey::ConstraintDrivers).get_to(constraint.drivers);
	constraint.type = jsonData.value(TemplateKey::ConstraintType, ConstraintTypes::Parent);
}

inline void to_json(nlohmann::json& jsonData, const RigConstraintData& constraint)
{
	jsonData = nlohmann::json{ { TemplateKey::Name, constraint.name }, { TemplateKey::ConstraintType, constraint.type },
		{ TemplateKey::ConstraintTarget, constraint.target }, { TemplateKey::ConstraintDrivers, constraint.drivers } };
}