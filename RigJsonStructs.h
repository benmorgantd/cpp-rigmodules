#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <maya/MString.h>


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
};

// nlohmann JSON mapping macros
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AssetRootData, assetId, assetType)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RigRootData, rigTemplateName, rigVersion, rigType, rigAuthor, rigModules)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RigModuleData, moduleType, name, shapeType, side, moduleVersion, parentStructure, moduleArgs)

void from_json(const nlohmann::json& j, RigModuleData& module);
bool loadRigTemplate(const MString& filePath, AssetRootData& assetData, RigRootData& rigData, MString& outErr);