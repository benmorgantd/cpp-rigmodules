#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

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
	bool mirror = false;          // template key: build a copy on the other side too (D28)
	int mirrorOf = -1;            // set on a generated mirror: the template index of its source

	bool isMirror() const
	{
		return mirrorOf >= 0;
	}
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
    bool mirror = false;          // template key: build a copy on the other side too (D28)
    int mirrorOf = -1;            // set on a generated mirror: the template index of its source

    // Plan steps read this for args that change on the other side (D28). The joints need nothing: selectors are already swapped.
    bool isMirror() const
    {
        return mirrorOf >= 0;
    }
};

struct RigRootData
{
    std::string rigTemplateName;
    std::string rigName;          // Optional key, defaults to rigTemplateName. Not in the macro below, which requires every field.
    int rigVersion = -1;
    std::string rigAuthor;
    std::vector<RigModuleData> rigModules; // 1D Flat List of modules
	std::vector<RigConstraintData> rigConstraints;
};

// nlohmann JSON mapping macros
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RigRootData, rigTemplateName, rigVersion, rigAuthor, rigModules)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RigModuleData, moduleType, name, shapeType, side, moduleVersion, parentStructure, moduleArgs)

void from_json(const nlohmann::json& j, RigModuleData& module);

struct UpdateTable;
// The template format's update steps (D33). Its current version is what the formatVersion key may hold at most.
const UpdateTable& templateUpdateTable();

// Reads the file, runs the template's update steps, checks required and unknown keys, then parses. Every problem goes into the report.
// False if the template could not be parsed into the structs. Errors in the report still need checking on true.
bool loadRigTemplate(const std::string& filePath, RigRootData& rigData, ValidationReport& report);

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
	constraint.mirror = jsonData.value(TemplateKey::Mirror, false);
}

inline void to_json(nlohmann::json& jsonData, const RigConstraintData& constraint)
{
	jsonData = nlohmann::json{ { TemplateKey::Name, constraint.name }, { TemplateKey::ConstraintType, constraint.type },
		{ TemplateKey::ConstraintTarget, constraint.target }, { TemplateKey::ConstraintDrivers, constraint.drivers } };
}