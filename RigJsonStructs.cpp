#include "RigJsonStructs.h"
#include <fstream>

// -----------------------------------------------------------------------------
// Custom Deserializer for RigModuleData
// -----------------------------------------------------------------------------
void from_json(const nlohmann::json& jsonData, RigModuleData& module) 
{
    // 1. Parse common required properties
    jsonData.at("moduleType").get_to(module.moduleType);
    jsonData.at("name").get_to(module.name);

    // 2. Parse common optional properties with safe defaults
    module.shapeType = jsonData.value("shapeType", "Cube");
    module.side = jsonData.value("side", "Center");

    if (jsonData.contains("joints")) {
        module.joints = jsonData["joints"].get<std::vector<std::string>>();
    }

    module.parentSocketIndex = jsonData.value("parentSocketIndex", 0);

    // 3. Solution 1 "If/Then" Switchboard for moduleArgs
    if (jsonData.contains("moduleArgs")) {
        const auto& argsJson = jsonData["moduleArgs"];

        // TODO: use this syntax when we have modules with custom argument structs
        //if (module.moduleType == "FkChain") 
        //{
        //    module.moduleArgs = argsJson.get<FkModuleArgs>();
        //}
        // Future modules (e.g., IkChain, SplineRibbon) get added as else-if branches here!
    }

    // 4. Recursive parsing of nested child modules
    if (jsonData.contains("children")) {
        module.children = jsonData["children"].get<std::vector<RigModuleData>>();
    }
}

// -----------------------------------------------------------------------------
// High-Level File Loading Function
// -----------------------------------------------------------------------------
bool loadRigTemplate(const MString& filePath, RigRootData& outData, MString& outErr) 
{
    std::ifstream file(filePath.asChar());
    if (!file.is_open()) 
    {
        outErr = MString("Failed to open rig JSON file at path: ") + filePath;
        return false;
    }

    try 
    {
        nlohmann::json jsonData;
        file >> jsonData;

        // Deserialize full JSON directly into the typed RigDescription struct
        outData = jsonData.get<RigRootData>();
    }
    catch (const std::exception& e) 
    {
        outErr = MString("JSON Parse Error: ") + e.what();
        return false;
    }

    return true;
}