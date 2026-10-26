#include "RigJsonStructs.h"
#include <fstream>

// -----------------------------------------------------------------------------
// Custom Deserializer for RigModuleData
// -----------------------------------------------------------------------------
void from_json(const nlohmann::json& jsonData, RigModuleData& module)
{
    // 1. Parse required common metadata
    jsonData.at("moduleType").get_to(module.moduleType);
    jsonData.at("name").get_to(module.name);

    // 2. Parse optional common metadata with safe defaults
    module.shapeType = jsonData.value("shapeType", "Box");
    module.side = jsonData.value("side", "Center");

    // 3. Parse sparse parentStructure topology if declared
    std::unordered_map<std::string, std::string> emptyMap;
    module.parentStructure = jsonData.value("parentStructure", emptyMap);

    // 4. Parse module-specific argument structs
    if (jsonData.contains("moduleArgs"))
    {
        const auto& argsJson = jsonData["moduleArgs"];

        if (module.moduleType == "FkChain")
        {
            module.moduleArgs = argsJson.get<FkModuleArgs>();
        }
        else if (module.moduleType == "AimModule")
        {
            module.moduleArgs = argsJson.get<AimModuleArgs>();
        }
        // Future modules (e.g., IkChain, SplineRibbon) branch here!
    }
}

// -----------------------------------------------------------------------------
// High-Level File Loading Function
// -----------------------------------------------------------------------------
bool loadRigTemplate(const MString& filePath, AssetRootData& assetData, RigRootData& rigData, MString& outErr)
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

        // Deserialize full JSON directly into typed AssetRootData and RigRootData structs
        rigData = jsonData.get<RigRootData>();
        assetData = jsonData.get<AssetRootData>();
    }
    catch (const std::exception& e)
    {
        outErr = MString("JSON Parse Error: ") + e.what();
        return false;
    }

    return true;
}