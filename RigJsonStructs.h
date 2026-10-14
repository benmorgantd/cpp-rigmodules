#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <variant>
#include <unordered_map>
#include <maya/MString.h>

struct ParentPlugOverride
{
    std::string dagParent;     // Identifier format: "<module_name>.<socket_name_or_index>" or control name
    std::string matrixParent;  // Identifier format: "<module_name>.<socket_name_or_index>" (Optional)
};

struct ParentStructureData
{
    // Maps child plug key ("base", "target", "0") to parenting overrides
    std::unordered_map<std::string, ParentPlugOverride> childPlugs;
};

struct FkModuleArgs
{
    std::vector<std::string> joints;
};

struct AimModuleArgs
{
    std::string aimJoint;
    std::string targetJoint;
    std::string upJoint;
};

using ModuleArgsVariant = std::variant<std::monostate, FkModuleArgs, AimModuleArgs>;

struct RigModuleData
{
    std::string moduleType;
    std::string name;
    std::string shapeType;
    std::string side;
    ParentStructureData parentStructure;
    ModuleArgsVariant moduleArgs;
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
};

// nlohmann JSON mapping macros
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ParentPlugOverride, dagParent, matrixParent)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ParentStructureData, childPlugs)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AssetRootData, assetId, assetType)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RigRootData, rigTemplateName, rigVersion, rigType, rigAuthor, rigModules)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RigModuleData, moduleType, name, shapeType, side, parentStructure, moduleArgs)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FkModuleArgs, joints)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AimModuleArgs, aimJoint, targetJoint, upJoint)

void from_json(const nlohmann::json& j, RigModuleData& module);
bool loadRigTemplate(const MString& filePath, AssetRootData& assetData, RigRootData& rigData, MString& outErr);