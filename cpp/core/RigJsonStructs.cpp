#include "RigJsonStructs.h"
#include "RigTemplateValidation.h"

#include <fstream>

// -----------------------------------------------------------------------------
// Custom Deserializer for RigModuleData
// -----------------------------------------------------------------------------
void from_json(const nlohmann::json& jsonData, RigModuleData& module)
{
    // 1. Parse required common metadata
    jsonData.at(TemplateKey::ModuleType).get_to(module.moduleType);
    jsonData.at(TemplateKey::Name).get_to(module.name);

    // 2. Parse optional common metadata with safe defaults
    module.shapeType = jsonData.value(TemplateKey::ShapeType, "Box");
    module.side = jsonData.value(TemplateKey::Side, SideNames::Center);
    module.moduleVersion = jsonData.value(TemplateKey::ModuleVersion, 1);
    module.parentStructure = jsonData.value(TemplateKey::ParentStructure, std::unordered_map<std::string, std::string>());
    module.moduleArgs = jsonData.value(TemplateKey::ModuleArgs, nlohmann::json::object());
    module.mirror = jsonData.value(TemplateKey::Mirror, false);
}

// -----------------------------------------------------------------------------
// High-Level File Loading Function
// -----------------------------------------------------------------------------
bool loadRigTemplate(const std::string& filePath, RigRootData& rigData, ValidationReport& report)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        report.addError(filePath, "failed to open rig template");
        return false;
    }

    nlohmann::json jsonData;
    try
    {
        file >> jsonData;
    }
    catch (const std::exception& e)
    {
        report.addError(filePath, std::string("invalid JSON: ") + e.what());
        return false;
    }

    // Key checks first: they name the module or constraint, where nlohmann's own errors do not.
    RigTemplateValidation::checkTemplateKeys(jsonData, report);
    if (report.hasErrors())
    {
        return false;
    }

    try
    {
        rigData = jsonData.get<RigRootData>();
        rigData.rigName = jsonData.value(TemplateKey::RigName, rigData.rigTemplateName);

        if (jsonData.contains(TemplateKey::Constraints))
        {
            rigData.rigConstraints = jsonData.at(TemplateKey::Constraints).get<std::vector<RigConstraintData>>();
        }
    }
    catch (const std::exception& e)
    {
        // Keys are present by now, so this is a wrong value type.
        report.addError(filePath, std::string("JSON value has the wrong type: ") + e.what());
        return false;
    }

    return true;
}
