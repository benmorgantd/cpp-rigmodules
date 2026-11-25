#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// Read-phase checks on a rig template. Nothing here touches the scene, so all of it runs
// before createRig queues its first dagMod operation.

#include "ControlMarkup.h"
#include "JointMarkup.h"
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
	// False for a plug whose input the module owns (a pole's module-owned space, an internal plug): a constraint would
	// take the input over, so a template constraint on it is a read-phase error (D52 decision 14).
	bool allowsConstraint = true;
	// Not empty for an internal plug: the name of the module's own child plug whose parentStructure parent it takes.
	// The template can't name a parent for it, so a parentStructure entry for it is an error too.
	std::string followsPlug;
	// Not empty for a plug the template may parent but need not: with no parentStructure entry it takes the parent of this
	// plug of the same module, where any other unlisted plug takes layout_00_M_MOD.end (Ribbon's end follows its start).
	std::string defaultsToPlug;
	// True for a plug the template may leave unparented, and then it stays unconnected and the module falls back to its own behaviour
	// (Limb's ikTarget: the end joint reaches the IK handle). Every other plug with no entry takes layout_00_M_MOD.end.
	bool optional = false;
	// The module's sockets whose value does not depend on this plug's input (D62 section 8: ikTarget leaves ik, pole and settings alone).
	// The module-cycle check does not follow a path that enters the module through this plug and leaves by one of them, so a foot that
	// hangs from the handle and feeds ikTarget is not a loop. The module's attributeAffects and compute() must really keep to this.
	std::vector<std::string> blindSockets;
};

// The child plug and socket names a module will create, known before anything is built.
struct ModuleInterface
{
	std::vector<ModulePlugInfo> childPlugs;
	std::vector<std::string> sockets;
	std::vector<ControlMarkup> controls;   // One per control the module creates, in creation order
	// The module's attribute that carries its ik/fk blend, which another module can follow (TwoBoneIk and Limb: "ikFkBlend"), or empty for a
	// module with none (D74). The default is no blend to follow.
	std::string blendSource;
	// For a module that follows another module's blend (the ReverseFoot): the name of that module, or empty. validateBlendDrivers checks it.
	std::string blendDriver;

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

	// "constraints[0] 'armSpace_00_L_CON'", and the mirror form like modulePath.
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

	// A [side, usage] pair with a known side and a usage, as the skeleton and marker files write a joint or marker
	// selector. False, with what is wrong reported at `where`, for anything else.
	bool parseSelector(const nlohmann::json& entry, const std::string& where, JointSelector& outSelector, ValidationReport& report);

	// Keys at the top level and on every module, constraint and driver. Each module's plan step checks its own moduleArgs.
	void checkTemplateKeys(const nlohmann::json& templateJson, ValidationReport& report);

	// rigTemplateName and rigVersion.
	void validateHeader(const RigRootData& rigData, ValidationReport& report);

	// Unique, not the reserved layout name, and usable both as a Maya node name and inside "module.socket".
	void validateModuleNames(const std::vector<RigModuleData>& modules, ValidationReport& report);

	// side and shapeType. Missing keys already took their defaults, so only present values can fail.
	// moduleVersion is checked by the module's update table (FormatUpdate::runUpdates, D33).
	void validateModuleFields(const RigModuleData& module, const std::string& where, ValidationReport& report);

	// parentStructure and constraints against the planned interfaces, then cycle detection over both.
	// A module missing from interfaces failed its own plan step. References to it are skipped, since that error is already reported.
	void validateConnections(const RigRootData& rigData, const ModuleInterfaceMap& interfaces, ValidationReport& report);

	// True for a "parent:module.socket" id, a socket of the rig this one is attached to (D55).
	bool isParentSocketId(const std::string& identifier);

	// "parent:head.end" -> "head.end". Any other id comes back unchanged.
	std::string stripParentPrefix(const std::string& identifier);

	// True when every driver of the constraint is a parent socket: the body drives the control, so an attached rig
	// hides, locks and zeroes it (D55).
	bool isDrivenByParent(const RigConstraintData& constraint);

	// Every distinct "module.socket" the template takes from its parent rig (parentStructure values and constraint
	// drivers), sorted, without the prefix. Empty for a template that is not an attachment.
	std::vector<std::string> parentSocketIds(const RigRootData& rigData);

	// A module that follows another's blend (ModuleInterface::blendDriver, D74) needs: that module to be in the template, to have a blend to
	// follow (ModuleInterface::blendSource), and to be the module every child plug of the follower hangs from, so the foot follows the limb
	// it stands on. A driver that failed its own plan step is skipped, since that error is already reported.
	void validateBlendDrivers(const RigRootData& rigData, const ModuleInterfaceMap& interfaces, ValidationReport& report);

	// (side, usage) must be unique across every control in the rig, because shape files and pose data key on it.
	// Joint markup is already unique, so this only fails when a joint's usage matches a layout control's (layout_N).
	void validateControlMarkup(const RigRootData& rigData, const ModuleInterfaceMap& interfaces, ValidationReport& report);
}
