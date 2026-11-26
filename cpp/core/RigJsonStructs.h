#pragma once

#include <array>
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
	double constraintWeight = 1.0;   // 0 follows the parent, 1 is the full constraint (D63)
	std::vector<RigConstraintDriverData> drivers;
	bool mirror = false;          // template key: build a copy on the other side too (D28)
	int mirrorOf = -1;            // set on a generated mirror: the template index of its source

	bool isMirror() const
	{
		return mirrorOf >= 0;
	}
};

// One entry of the template's collisions list (D79, design/push-out.md). PushOut is the only type: it keeps the control on
// `target` out of one capsule that follows a joint.
struct RigCollisionData
{
	std::string name;
	std::string type = CollisionTypes::PushOut;
	std::string target;                           // "module.childPlug"
	std::array<std::string, 2> follow;            // [side, usage] of the joint the capsule follows
	double radius = 1.0;
	double halfLength = 0.0;
	std::array<double, 3> translate{ 0.0, 0.0, 0.0 };   // the capsule's offset from the joint
	std::array<double, 3> rotate{ 0.0, 0.0, 0.0 };      // degrees, XYZ
	double margin = 0.0;
	double maxPush = 100.0;
	double weight = 1.0;                          // template key constraintWeight
	bool mirror = false;          // template key: build a copy on the other side too (D28)
	int mirrorOf = -1;            // set on a generated mirror: the template index of its source. Its offset is not read: the build
	                              // places the capsule where the source's is reflected across X (D79).

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
    std::string rigName;          // Optional key, defaults to rigTemplateName in snake_case. Not in the macro below, which requires every field.
    int rigVersion = -1;
    std::string rigAuthor;
    std::vector<RigModuleData> rigModules; // 1D Flat List of modules
	std::vector<RigConstraintData> rigConstraints;
	std::vector<RigCollisionData> rigCollisions;
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
	constraint.constraintWeight = jsonData.value(TemplateKey::ConstraintWeight, 1.0);
	constraint.mirror = jsonData.value(TemplateKey::Mirror, false);
}

// Collisions ===================================================================

// checkCollisionKeys has already checked every key, so a missing one is not reached here. A wrong type of value throws, and
// loadRigTemplate reports it.
inline void from_json(const nlohmann::json& jsonData, RigCollisionData& collision)
{
	jsonData.at(TemplateKey::Name).get_to(collision.name);
	jsonData.at(TemplateKey::ConstraintType).get_to(collision.type);
	jsonData.at(TemplateKey::ConstraintTarget).get_to(collision.target);
	collision.margin = jsonData.value(TemplateKey::CollisionMargin, collision.margin);
	collision.maxPush = jsonData.value(TemplateKey::CollisionMaxPush, collision.maxPush);
	collision.weight = jsonData.value(TemplateKey::ConstraintWeight, collision.weight);
	collision.mirror = jsonData.value(TemplateKey::Mirror, false);

	const nlohmann::json& capsule = jsonData.at(TemplateKey::CollisionCapsule);
	capsule.at(TemplateKey::CapsuleFollow).get_to(collision.follow);
	capsule.at(TemplateKey::CapsuleRadius).get_to(collision.radius);
	collision.halfLength = capsule.value(TemplateKey::CapsuleHalfLength, collision.halfLength);
	if (capsule.contains(TemplateKey::CapsuleOffset))
	{
		const nlohmann::json& offset = capsule.at(TemplateKey::CapsuleOffset);
		collision.translate = offset.value(TemplateKey::OffsetTranslate, collision.translate);
		collision.rotate = offset.value(TemplateKey::OffsetRotate, collision.rotate);
	}
}

inline void to_json(nlohmann::json& jsonData, const RigConstraintData& constraint)
{
	jsonData = nlohmann::json{ { TemplateKey::Name, constraint.name }, { TemplateKey::ConstraintType, constraint.type },
		{ TemplateKey::ConstraintTarget, constraint.target }, { TemplateKey::ConstraintDrivers, constraint.drivers } };
	if (constraint.constraintWeight != 1.0)
	{
		jsonData[TemplateKey::ConstraintWeight] = constraint.constraintWeight;
	}
}