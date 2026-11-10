#include "RigTemplateValidation.h"
#include "Constants.h"
#include "JointMarkup.h"
#include "StringUtils.h"
#include "TemplateMirror.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <utility>

// ValidationReport ----------------------------------------------------------
void ValidationReport::addError(const std::string& where, const std::string& message)
{
	errors.push_back(where + ": " + message);
}

void ValidationReport::addWarning(const std::string& where, const std::string& message)
{
	warnings.push_back(where + ": " + message);
}

bool ValidationReport::hasErrors() const
{
	return !errors.empty();
}

// ModuleInterface -------------------------------------------------------------
const ModulePlugInfo* ModuleInterface::findChildPlug(const std::string& name) const
{
	for (const ModulePlugInfo& plug : childPlugs)
	{
		if (plug.name == name)
		{
			return &plug;
		}
	}
	return nullptr;
}

bool ModuleInterface::hasSocket(const std::string& name) const
{
	return std::find(sockets.begin(), sockets.end(), name) != sockets.end();
}

namespace
{
	// Directed edges parent module -> child module. std::map/std::set keep cycle messages in a stable order.
	using ModuleGraph = std::map<std::string, std::set<std::string>>;

	// For a "(has: ...)" list in a message, where an empty list still needs a word.
	template <typename TNames>
	std::string namesOrNone(const TNames& names)
	{
		const std::string text = StringUtils::joinNames(names);
		return text.empty() ? "none" : text;
	}

	template <typename TNames>
	bool isOneOf(const std::string& value, const TNames& names)
	{
		for (const auto& name : names)
		{
			if (value == name)
			{
				return true;
			}
		}
		return false;
	}

	std::string childPlugNames(const ModuleInterface& moduleInterface)
	{
		std::vector<std::string> names;
		for (const ModulePlugInfo& plug : moduleInterface.childPlugs)
		{
			names.push_back(plug.name);
		}
		return namesOrNone(names);
	}

	// Maya node names: a letter or '_', then letters, digits or '_'. This also keeps '.' out of "module.socket" identifiers.
	bool isValidNodeName(const std::string& name)
	{
		if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0])))
		{
			return false;
		}
		for (char c : name)
		{
			if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
			{
				return false;
			}
		}
		return true;
	}

	// Splits a "module.name" identifier and finds the module's planned interface.
	// Failed has been reported here. SkipFailedModule means the module failed its own plan step, which reported it.
	enum class LookupResult
	{
		Found,
		Failed,
		SkipFailedModule
	};

	LookupResult lookupModule(
		const std::string& identifier,
		const std::set<std::string>& declaredModules,
		const ModuleInterfaceMap& interfaces,
		const std::string& expectedForm,
		const std::string& where,
		ValidationReport& report,
		std::string& outModule,
		std::string& outName,
		const ModuleInterface*& pOutInterface)
	{
		if (!StringUtils::splitIdentifier(identifier, outModule, outName))
		{
			report.addError(where, "'" + identifier + "' must be '" + expectedForm + "'");
			return LookupResult::Failed;
		}

		auto it = interfaces.find(outModule);
		if (it != interfaces.end())
		{
			pOutInterface = &it->second;
			return LookupResult::Found;
		}

		if (declaredModules.count(outModule) > 0)
		{
			return LookupResult::SkipFailedModule;
		}

		report.addError(where, "no module named '" + outModule + "'");
		return LookupResult::Failed;
	}

	class CycleFinder
	{
	public:
		CycleFinder(const ModuleGraph& graph, ValidationReport& report)
			: fGraph(graph)
			, fReport(report)
		{
		}

		void run()
		{
			for (const auto& [moduleName, children] : fGraph)
			{
				if (fState[moduleName] == State::Unvisited)
				{
					visit(moduleName);
				}
			}
		}

	private:
		enum class State
		{
			Unvisited,
			OnStack,
			Done
		};

		void visit(const std::string& moduleName)
		{
			fState[moduleName] = State::OnStack;
			fStack.push_back(moduleName);

			auto it = fGraph.find(moduleName);
			if (it != fGraph.end())
			{
				for (const std::string& child : it->second)
				{
					if (fState[child] == State::OnStack)
					{
						reportCycle(child);
					}
					else if (fState[child] == State::Unvisited)
					{
						visit(child);
					}
				}
			}

			fStack.pop_back();
			fState[moduleName] = State::Done;
		}

		void reportCycle(const std::string& cycleStart)
		{
			auto it = std::find(fStack.begin(), fStack.end(), cycleStart);
			std::string path;
			for (; it != fStack.end(); ++it)
			{
				path += *it + " -> ";
			}
			path += cycleStart;
			fReport.addError("module graph", "cycle through parentStructure and constraint drivers: " + path);
		}

		const ModuleGraph& fGraph;
		ValidationReport& fReport;
		std::map<std::string, State> fState;
		std::vector<std::string> fStack;
	};

	void validateParentStructure(
		const RigModuleData& module,
		const std::string& where,
		const ModuleInterface& moduleInterface,
		const std::set<std::string>& declaredModules,
		const ModuleInterfaceMap& interfaces,
		ModuleGraph& graph,
		ValidationReport& report)
	{
		// Sorted, so messages come out in a stable order.
		const std::map<std::string, std::string> parentStructure(module.parentStructure.begin(), module.parentStructure.end());

		for (const auto& [plugName, socketIdentifier] : parentStructure)
		{
			const std::string entryPath = where + "." + TemplateKey::ParentStructure + "." + plugName;

			if (moduleInterface.findChildPlug(plugName) == nullptr)
			{
				report.addError(entryPath, "no child plug '" + plugName + "' on this module (has: " + childPlugNames(moduleInterface) + ")");
			}

			std::string parentModule;
			std::string socketName;
			const ModuleInterface* pParentInterface = nullptr;
			const std::string expectedForm = std::string("module.socket', or '") + RigNaming::LayoutModule + "." + PlugAndSocketNames::End;
			LookupResult result = lookupModule(socketIdentifier, declaredModules, interfaces, expectedForm,
				entryPath, report, parentModule, socketName, pParentInterface);
			if (result != LookupResult::Found)
			{
				continue;
			}

			if (parentModule == module.name)
			{
				// Every module input affects every output, so any child plug on the module's own socket is a DG cycle.
				report.addError(entryPath, "a module cannot parent to its own socket ('" + socketIdentifier + "'), it makes a DG cycle");
				continue;
			}

			if (!pParentInterface->hasSocket(socketName))
			{
				report.addError(entryPath, "no socket '" + socketName + "' on '" + parentModule + "' (has: " + namesOrNone(pParentInterface->sockets) + ")");
				continue;
			}

			graph[parentModule].insert(module.name);
		}
	}

	void validateConstraints(
		const std::vector<RigConstraintData>& constraints,
		const std::set<std::string>& declaredModules,
		const ModuleInterfaceMap& interfaces,
		ModuleGraph& graph,
		ValidationReport& report)
	{
		std::map<std::string, std::size_t> constraintNames;   // name -> first index
		std::map<std::string, std::string> constrainedPlugs;   // "module.childPlug" -> constraint name

		for (std::size_t c = 0; c < constraints.size(); ++c)
		{
			const RigConstraintData& constraint = constraints[c];
			const std::string where = RigTemplateValidation::constraintPath(c, constraint);

			if (!isValidNodeName(constraint.name))
			{
				report.addError(where, "name must start with a letter or '_' and contain only letters, digits and '_'");
			}
			else if (!constraintNames.emplace(constraint.name, c).second)
			{
				report.addError(where, "name is already used by " + RigTemplateValidation::constraintPath(constraintNames[constraint.name], constraints[constraintNames[constraint.name]]));
			}

			if (ConstraintTypes::indexOf(constraint.type) < 0)
			{
				report.addError(where + "." + TemplateKey::ConstraintType, "unknown constraint type '" + constraint.type + "' (has: " + namesOrNone(ConstraintTypes::All) + ")");
			}

			// Target child plug
			std::string targetModule;
			std::string targetPlug;
			const ModuleInterface* pTargetInterface = nullptr;
			const std::string targetPath = where + "." + TemplateKey::ConstraintTarget;
			LookupResult targetResult = lookupModule(constraint.target, declaredModules, interfaces, "module.childPlug",
				targetPath, report, targetModule, targetPlug, pTargetInterface);

			if (targetResult == LookupResult::Found)
			{
				const ModulePlugInfo* pPlugInfo = pTargetInterface->findChildPlug(targetPlug);
				if (pPlugInfo == nullptr)
				{
					report.addError(targetPath, "no child plug '" + targetPlug + "' on '" + targetModule + "' (has: " + childPlugNames(*pTargetInterface) + ")");
					targetResult = LookupResult::Failed;
				}
				else if (!pPlugInfo->hasControl)
				{
					report.addError(targetPath, "child plug '" + constraint.target + "' has no control to hold the space attribute");
				}
				else if (!constrainedPlugs.emplace(constraint.target, constraint.name).second)
				{
					// The control can only hold one space attribute, and the child plug one parent matrix.
					report.addError(targetPath, "child plug '" + constraint.target + "' is already the target of constraint '" + constrainedPlugs[constraint.target] + "'");
				}
			}

			// Drivers
			if (constraint.drivers.empty())
			{
				report.addError(where + "." + TemplateKey::ConstraintDrivers, "needs at least one driver");
				continue;
			}

			std::set<std::string> labels;
			double bestWeight = 0.0;
			unsigned int bestCount = 0;

			for (std::size_t d = 0; d < constraint.drivers.size(); ++d)
			{
				const RigConstraintDriverData& driver = constraint.drivers[d];
				const std::string driverPath = where + "." + TemplateKey::ConstraintDrivers + "[" + std::to_string(d) + "]";

				if (driver.label.empty())
				{
					report.addError(driverPath + "." + TemplateKey::DriverLabel, "label is empty");
				}
				else if (!labels.insert(driver.label).second)
				{
					// Labels become the space enum's fields, so two equal labels are indistinguishable to the animator.
					report.addError(driverPath + "." + TemplateKey::DriverLabel, "label '" + driver.label + "' is used by another driver of this constraint");
				}

				if (driver.weight < 0.0)
				{
					report.addError(driverPath + "." + TemplateKey::DriverWeight, "weight must be 0 or more");
				}
				else if (driver.weight > bestWeight)
				{
					bestWeight = driver.weight;
					bestCount = 1;
				}
				else if (driver.weight > 0.0 && driver.weight == bestWeight)
				{
					++bestCount;
				}

				std::string driverModule;
				std::string socketName;
				const ModuleInterface* pDriverInterface = nullptr;
				const std::string socketPath = driverPath + "." + TemplateKey::DriverSocket;
				if (lookupModule(driver.socket, declaredModules, interfaces, "module.socket",
					socketPath, report, driverModule, socketName, pDriverInterface) != LookupResult::Found)
				{
					continue;
				}

				if (!pDriverInterface->hasSocket(socketName))
				{
					report.addError(socketPath, "no socket '" + socketName + "' on '" + driverModule + "' (has: " + namesOrNone(pDriverInterface->sockets) + ")");
					continue;
				}

				if (targetResult == LookupResult::Found)
				{
					if (driverModule == targetModule)
					{
						report.addError(socketPath, "a module cannot drive its own child plug, it makes a DG cycle");
						continue;
					}
					graph[driverModule].insert(targetModule);
				}
			}

			if (bestCount > 1)
			{
				report.addWarning(where + "." + TemplateKey::ConstraintDrivers, "several drivers share the highest weight, the first one starts active");
			}
		}
	}
}

namespace RigTemplateValidation
{
	std::string modulePath(std::size_t index, const std::string& name)
	{
		return std::string(TemplateKey::RigModules) + "[" + std::to_string(index) + "] '" + name + "'";
	}

	std::string modulePath(std::size_t index, const RigModuleData& module)
	{
		if (!module.isMirror())
		{
			return modulePath(index, module.name);
		}
		return modulePath(static_cast<std::size_t>(module.mirrorOf), TemplateMirror::swapSideSuffix(module.name))
			+ " (mirror '" + module.name + "')";
	}

	std::string constraintPath(std::size_t index, const std::string& name)
	{
		return std::string(TemplateKey::Constraints) + "[" + std::to_string(index) + "] '" + name + "'";
	}

	std::string constraintPath(std::size_t index, const RigConstraintData& constraint)
	{
		if (!constraint.isMirror())
		{
			return constraintPath(index, constraint.name);
		}
		return constraintPath(static_cast<std::size_t>(constraint.mirrorOf), TemplateMirror::swapSideSuffix(constraint.name))
			+ " (mirror '" + constraint.name + "')";
	}

	void checkKeys(
		const nlohmann::json& jsonObject,
		std::initializer_list<const char*> requiredKeys,
		std::initializer_list<const char*> optionalKeys,
		const std::string& where,
		ValidationReport& report)
	{
		if (!jsonObject.is_object())
		{
			report.addError(where, "must be a JSON object, got " + std::string(jsonObject.type_name()));
			return;
		}

		for (const char* key : requiredKeys)
		{
			if (!jsonObject.contains(key))
			{
				report.addError(where, std::string("missing required key '") + key + "'");
			}
		}

		for (const auto& [key, value] : jsonObject.items())
		{
			if (!isOneOf(key, requiredKeys) && !isOneOf(key, optionalKeys))
			{
				std::vector<const char*> known(requiredKeys);
				known.insert(known.end(), optionalKeys.begin(), optionalKeys.end());
				report.addError(where, "unknown key '" + key + "' (known: " + namesOrNone(known) + ")");
			}
		}
	}

	void checkTemplateKeys(const nlohmann::json& templateJson, ValidationReport& report)
	{
		using namespace TemplateKey;

		checkKeys(templateJson,
			{ RigVersion, RigTemplateName, RigAuthor, RigModules },
			{ RigName, Constraints },
			"template", report);
		if (!templateJson.is_object())
		{
			return;
		}

		if (templateJson.contains(RigModules))
		{
			const nlohmann::json& modules = templateJson.at(RigModules);
			if (!modules.is_array())
			{
				report.addError(RigModules, "must be a JSON array");
			}
			else
			{
				for (std::size_t i = 0; i < modules.size(); ++i)
				{
					const nlohmann::json& module = modules[i];
					const std::string name = module.is_object() ? module.value(Name, std::string()) : std::string();
					checkKeys(module,
						{ ModuleType, Name },
						{ ShapeType, Side, ModuleVersion, ParentStructure, ModuleArgs, Mirror },
						modulePath(i, name), report);
				}
			}
		}

		if (templateJson.contains(Constraints))
		{
			const nlohmann::json& constraints = templateJson.at(Constraints);
			if (!constraints.is_array())
			{
				report.addError(Constraints, "must be a JSON array");
				return;
			}

			for (std::size_t c = 0; c < constraints.size(); ++c)
			{
				const nlohmann::json& constraint = constraints[c];
				const std::string name = constraint.is_object() ? constraint.value(Name, std::string()) : std::string();
				const std::string where = constraintPath(c, name);
				checkKeys(constraint, { Name, ConstraintTarget, ConstraintDrivers }, { ConstraintType, Mirror }, where, report);

				if (!constraint.is_object() || !constraint.contains(ConstraintDrivers))
				{
					continue;
				}

				const nlohmann::json& drivers = constraint.at(ConstraintDrivers);
				if (!drivers.is_array())
				{
					report.addError(where + "." + ConstraintDrivers, "must be a JSON array");
					continue;
				}

				for (std::size_t d = 0; d < drivers.size(); ++d)
				{
					checkKeys(drivers[d], { DriverSocket }, { DriverLabel, DriverWeight },
						where + "." + ConstraintDrivers + "[" + std::to_string(d) + "]", report);
				}
			}
		}
	}

	void validateHeader(const RigRootData& rigData, ValidationReport& report)
	{
		if (rigData.rigTemplateName.empty())
		{
			report.addError(TemplateKey::RigTemplateName, "is empty");
		}

		if (rigData.rigVersion < 1)
		{
			report.addError(TemplateKey::RigVersion, "must be 1 or more, got " + std::to_string(rigData.rigVersion));
		}
	}

	void validateModuleNames(const std::vector<RigModuleData>& modules, ValidationReport& report)
	{
		std::map<std::string, std::size_t> firstIndex;

		for (std::size_t i = 0; i < modules.size(); ++i)
		{
			const std::string& name = modules[i].name;
			const std::string where = modulePath(i, modules[i]);

			if (!isValidNodeName(name))
			{
				report.addError(where, "name must start with a letter or '_' and contain only letters, digits and '_'");
				continue;
			}

			if (name == RigNaming::LayoutModule)
			{
				report.addError(where, std::string("'") + RigNaming::LayoutModule + "' is reserved for the implicit layout module");
				continue;
			}

			auto [it, inserted] = firstIndex.emplace(name, i);
			if (!inserted)
			{
				report.addError(where, "name is already used by " + modulePath(it->second, modules[it->second]));
			}
		}
	}

	void validateModuleFields(const RigModuleData& module, const std::string& where, int currentVersion, ValidationReport& report)
	{
		// Strict on purpose: getSideFromString and getShapeTypeFromString fall back silently, which hides typos.
		if (!isValidSideName(module.side))
		{
			report.addError(where + "." + TemplateKey::Side, "'" + module.side + "' is not one of "
				+ SideNames::Center + ", " + SideNames::Left + ", " + SideNames::Right);
		}

		if (!isOneOf(module.shapeType, RigControlNodeAttr::ShapeTypes::All))
		{
			report.addError(where + "." + TemplateKey::ShapeType, "'" + module.shapeType + "' is not one of " + namesOrNone(RigControlNodeAttr::ShapeTypes::All));
		}

		if (module.moduleVersion < 1 || module.moduleVersion > currentVersion)
		{
			report.addError(where + "." + TemplateKey::ModuleVersion, std::to_string(module.moduleVersion)
				+ " is not supported, this build reads versions 1 to " + std::to_string(currentVersion));
		}
	}

	void validateConnections(const RigRootData& rigData, const ModuleInterfaceMap& interfaces, ValidationReport& report)
	{
		std::set<std::string> declaredModules;
		for (const RigModuleData& module : rigData.rigModules)
		{
			declaredModules.insert(module.name);
		}

		ModuleGraph graph;

		for (std::size_t i = 0; i < rigData.rigModules.size(); ++i)
		{
			const RigModuleData& module = rigData.rigModules[i];
			auto it = interfaces.find(module.name);
			if (it == interfaces.end())
			{
				continue;   // failed planning, already reported
			}
			validateParentStructure(module, modulePath(i, module), it->second, declaredModules, interfaces, graph, report);
		}

		validateConstraints(rigData.rigConstraints, declaredModules, interfaces, graph, report);

		CycleFinder(graph, report).run();
	}

	void validateControlMarkup(const RigRootData& rigData, const ModuleInterfaceMap& interfaces, ValidationReport& report)
	{
		// (side, usage) -> where the first control with it came from. The layout goes first, as it is built first.
		std::map<std::pair<std::string, std::string>, std::string> owners;
		auto checkModule = [&](const std::string& moduleName, const std::string& where)
		{
			auto it = interfaces.find(moduleName);
			if (it == interfaces.end())
			{
				return;   // failed planning, already reported
			}
			for (const ControlMarkup& markup : it->second.controls)
			{
				auto [owner, isNew] = owners.emplace(std::make_pair(markup.side, markup.usage), where);
				if (!isNew)
				{
					report.addError(where, "a control with side " + markup.side + ", usage " + markup.usage
						+ " already exists in " + owner->second + ". Control (side, usage) pairs must be unique in a rig");
				}
			}
		};

		checkModule(RigNaming::LayoutModule, RigNaming::LayoutModule);
		for (std::size_t i = 0; i < rigData.rigModules.size(); ++i)
		{
			const RigModuleData& module = rigData.rigModules[i];
			checkModule(module.name, modulePath(i, module));
		}
	}
}
