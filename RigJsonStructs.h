#pragma once

#include <nlohmann/json.hpp>  // NOTE: I downloaded this from github. It was found under the "assets" category and is ~1mb. Large, single file.
#include <string>
#include <vector>
#include <variant>
#include <maya/MString.h>


struct FkModuleArgs {};  // Empty Fk Module struct for now, but this gives us a place to go.

// TODO: we can have structs for modules that require custom arguments. 
// These will go in sub dictionaries within the json dict, named "moduleArgs".\
// NOTE: all possible variants of module args have to go into this! 
using ModuleArgsVariant = std::variant<std::monostate, FkModuleArgs>;

struct RigModuleData {
    std::string moduleType;
    std::string name;
    std::string shapeType;
    std::string side;
    std::vector<std::string> joints;
    int parentSocketIndex = 0;
    // Holds a strongly-typed substruct.
    ModuleArgsVariant moduleArgs;
    std::vector<RigModuleData> children;
};

struct RigRootData {
    std::string rigName;
    int rigVersion = -1;
    std::string rigType;
    std::string author;
    std::vector<RigModuleData> rigModules;
};

// lohmann macro mapping JSON keys directly to struct fields
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RigRootData, rigName, rigVersion, rigType, author, rigModules)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RigModuleData, moduleType, name, shapeType, side, joints, parentSocketIndex, moduleArgs, children)
//NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FkModuleArgs)  // we do not need this line for empty structs.


// Custom deserializer for RigModuleData to handle the std::variant switch
void from_json(const nlohmann::json& j, RigModuleData& module);

// Function declaration for reading the JSON file from disk
bool loadRigTemplate(const MString& filePath, RigRootData& outDesc, MString& outErr);

