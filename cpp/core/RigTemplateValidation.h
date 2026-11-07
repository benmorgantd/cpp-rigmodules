#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// Read-phase checks on a rig template. Nothing here touches the scene, so all of it runs
// before createRig queues its first dagMod operation.

#include "ControlMarkup.h"
#include "RigJsonStructs.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <vector>

// Collects every problem instead of stopping at the first, so one build attempt reports them all.
struct ValidationReport
{
	std::vector<std::string> errors;
	std::vector<std::string> warnings;

	// where: the JSON location, e.g. modulePath(1, "fkchain_2") + ".parentStructure.start"
	void addError(const std::string& where, const std::string& message);
	void addWarning(const std::string& where, const std::string& message);
	bool hasErrors() const;
};

struct ModulePlugInfo
{
	std::string name;
	bool hasControl = true;   // A constraint needs a control on its target child plug to host the space attribute.
};

// The child plug and socket names a module will create, known before anything is built.
struct ModuleInterface
{
	std::vector<ModulePlugInfo> childPlugs;
	std::vector<std::string> sockets;
	std::vector<ControlMarkup> controls;   // One per control the module creates, in creation order

	const ModulePlugInfo* findChildPlug(const std::string& name) const;
	bool hasSocket(const std::string& name) const;
};

// Module name -> interface. Includes the layout module.
using ModuleInterfaceMap = std::unordered_map<std::string, ModuleInterface>;

namespace RigTemplateValidation
{
	// "rigModules[1] 'fkchain_2'", the prefix for every message about one module.
	std::string modulePath(std::size_t index, const std::string& name);
	// The same for a module in the expanded list. A generated mirror names the template entry it came from,
	// "rigModules[3] 'arm_l' (mirror 'arm_r')", because its own index is not in the file (D28).
	std::string modulePath(std::size_t index, const RigModuleData& module);

	// "constraints[0] 'arm_space_l'", and the mirror form like modulePath.
	std::string constraintPath(std::size_t index, const std::string& name);
	std::string constraintPath(std::size_t index, const RigConstraintData& constraint);

	// Reports a non-object, a missing required key, or any key in neither list.
	// Unknown keys are errors: a typo in an optional key would otherwise fall back to its default silently.
	void checkKeys(
		const nlohmann::json& jsonObject,
		std::initializer_list<const char*> requiredKeys,
		std::initializer_list<const char*> optionalKeys,
		const std::string& where,
		ValidationReport& report);

	// Keys at the top level and on every module, constraint and driver. Each module's plan step checks its own moduleArgs.
	void checkTemplateKeys(const nlohmann::json& templateJson, ValidationReport& report);

	// rigTemplateName, rigVersion, rigType and assetType.
	void validateHeader(const RigRootData& rigData, const AssetRootData& assetData, ValidationReport& report);

	// Unique, not the reserved layout name, and usable both as a Maya node name and inside "module.socket".
	void validateModuleNames(const std::vector<RigModuleData>& modules, ValidationReport& report);

	// side, shapeType and moduleVersion. Missing keys already took their defaults, so only present values can fail.
	void validateModuleFields(const RigModuleData& module, const std::string& where, int currentVersion, ValidationReport& report);

	// parentStructure and constraints against the planned interfaces, then cycle detection over both.
	// A module missing from interfaces failed its own plan step. References to it are skipped, since that error is already reported.
	void validateConnections(const RigRootData& rigData, const ModuleInterfaceMap& interfaces, ValidationReport& report);

	// (side, usage) must be unique across every control in the rig, because shape files and pose data key on it.
	// Joint markup is already unique, so this only fails when a joint's usage matches a layout control's (layout_N).
	void validateControlMarkup(const RigRootData& rigData, const ModuleInterfaceMap& interfaces, ValidationReport& report);
}
