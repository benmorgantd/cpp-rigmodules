#pragma once

#include <nlohmann/json.hpp>  // NOTE: I downloaded this from github. It was found under the "assets" category and is ~1mb. Large, single file.
#include <string>
#include <vector>
#include <variant>
#include <maya/MString.h>


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

// TODO: we can have structs for modules that require custom arguments. 
// These will go in sub dictionaries within the json dict, named "moduleArgs".\
// NOTE: all possible variants of module args have to go into this! 
using ModuleArgsVariant = std::variant<std::monostate, FkModuleArgs, AimModuleArgs>;

struct RigModuleData {
    std::string moduleType;
    std::string name;
    std::string shapeType;
    std::string side;
    int parentSocketIndex = 0;
    // Holds a strongly-typed substruct.
    ModuleArgsVariant moduleArgs;
    std::vector<RigModuleData> children;
};

struct  AssetRootData
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
    std::vector<RigModuleData> rigModules;
};

// lohmann macro mapping JSON keys directly to struct fields
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AssetRootData, assetId, assetType)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RigRootData, rigTemplateName, rigVersion, rigType, rigAuthor, rigModules)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RigModuleData, moduleType, name, shapeType, side, parentSocketIndex, moduleArgs, children)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FkModuleArgs, joints)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AimModuleArgs, aimJoint, targetJoint, upJoint)


// Custom deserializer for RigModuleData to handle the std::variant switch
void from_json(const nlohmann::json& j, RigModuleData& module);

// Function declaration for reading the JSON file from disk
bool loadRigTemplate(const MString& filePath, AssetRootData& assetData, RigRootData& rigData, MString& outErr);

