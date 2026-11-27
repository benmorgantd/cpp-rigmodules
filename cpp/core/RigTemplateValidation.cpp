#include "RigTemplateValidation.h"
#include "AssetId.h"
#include "Constants.h"
#include "JointMarkup.h"
#include "NamingConvention.h"
#include "PushOutSolve.h"
#include "StringUtils.h"
#include "TemplateMirror.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
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

bool ModuleInterface::isMatrixOnly(const std::string& socketName) const
{
	return std::find(matrixOnlySockets.begin(), matrixOnlySockets.end(), socketName) != matrixOnlySockets.end();
}

namespace
{
	// Directed edges parent module -> child module, in the order they were found. An edge remembers the socket it leaves the parent by and
	// the child plug it enters the child through, because a plug can declare sockets it does not reach (ModulePlugInfo::blindSockets).
	struct ModuleEdge
	{
		std::string child;
		std::string socket;
		std::string plug;
	};
	using ModuleGraph = std::map<std::string, std::vector<ModuleEdge>>;

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

	// A name in the naming convention (D58) of the given kind. This also keeps '.' out of "module.socket" identifiers.
	bool isConventionName(const std::string& name, NamingConvention::Kind kind)
	{
		NamingConvention::Name parsed;
		return NamingConvention::parse(name, parsed) && parsed.kind == kind;
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

	void checkParentSocketForm(const std::string& identifier, const std::string& where, ValidationReport& report)
	{
		std::string moduleName;
		std::string socketName;
		if (!StringUtils::splitIdentifier(RigTemplateValidation::stripParentPrefix(identifier), moduleName, socketName))
		{
			report.addError(where, "'" + identifier + "' must be '" + ParentRigId::Prefix + "module.socket'");
		}
	}

	// What a socket in a parent entry resolved to. Cross is another rig's socket (D55), which only its own scene can check. Invalid was reported.
	enum class ParentSocketResult
	{
		Local,
		Cross,
		Invalid
	};

	// Checks one parentStructure or matrixParentStructure value: its form, that the module exists and has the socket, and that it is not
	// the module's own. A local socket comes back as parentModule, socketName and its interface.
	ParentSocketResult resolveParentSocket(
		const RigModuleData& module,
		const std::string& socketIdentifier,
		const std::string& entryPath,
		const std::set<std::string>& declaredModules,
		const ModuleInterfaceMap& interfaces,
		ValidationReport& report,
		std::string& parentModule,
		std::string& socketName,
		const ModuleInterface*& pParentInterface)
	{
		// A parent rig's socket can only be checked against that rig's scene, which the attach step does (D55).
		if (RigTemplateValidation::isParentSocketId(socketIdentifier))
		{
			checkParentSocketForm(socketIdentifier, entryPath, report);
			return ParentSocketResult::Cross;
		}

		const std::string expectedForm = std::string("module.socket', or '") + RigNaming::LayoutModule + "." + PlugAndSocketNames::End;
		if (lookupModule(socketIdentifier, declaredModules, interfaces, expectedForm, entryPath, report, parentModule, socketName, pParentInterface)
			!= LookupResult::Found)
		{
			return ParentSocketResult::Invalid;
		}

		if (parentModule == module.name)
		{
			// Every module input affects every output, so any child plug on the module's own socket is a DG cycle.
			report.addError(entryPath, "a module cannot parent to its own socket ('" + socketIdentifier + "'), it makes a DG cycle");
			return ParentSocketResult::Invalid;
		}

		if (!pParentInterface->hasSocket(socketName))
		{
			report.addError(entryPath, "no socket '" + socketName + "' on '" + parentModule + "' (has: " + namesOrNone(pParentInterface->sockets) + ")");
			return ParentSocketResult::Invalid;
		}
		return ParentSocketResult::Local;
	}

	class CycleFinder
	{
	public:
		CycleFinder(const ModuleGraph& graph, const ModuleInterfaceMap& interfaces, ValidationReport& report, const char* edgeKind)
			: fGraph(graph)
			, fInterfaces(interfaces)
			, fReport(report)
			, fEdgeKind(edgeKind)
		{
		}

		void run()
		{
			for (const auto& [moduleName, edges] : fGraph)
			{
				if (fState[{ moduleName, std::string() }] == State::Unvisited)
				{
					visit(moduleName, std::string());
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

		// A visit is a module and the child plug it was entered through (empty where a search starts): a module entered through a plug that
		// is blind to a socket does not depend on that plug for the socket, so a path may not leave by it.
		using Visit = std::pair<std::string, std::string>;

		bool isBlind(const Visit& visit, const std::string& socket) const
		{
			const auto itInterface = fInterfaces.find(visit.first);
			const ModulePlugInfo* pPlug = !visit.second.empty() && itInterface != fInterfaces.end() ? itInterface->second.findChildPlug(visit.second) : nullptr;
			return pPlug != nullptr && isOneOf(socket, pPlug->blindSockets);
		}

		void visit(const std::string& moduleName, const std::string& enteredBy)
		{
			const Visit here{ moduleName, enteredBy };
			fState[here] = State::OnStack;
			fStack.push_back(here);

			auto it = fGraph.find(moduleName);
			if (it != fGraph.end())
			{
				for (const ModuleEdge& edge : it->second)
				{
					if (isBlind(here, edge.socket))
					{
						continue;
					}
					const Visit next{ edge.child, edge.plug };
					if (fState[next] == State::OnStack)
					{
						reportCycle(next);
					}
					else if (fState[next] == State::Unvisited)
					{
						visit(edge.child, edge.plug);
					}
				}
			}

			fStack.pop_back();
			fState[here] = State::Done;
		}

		void reportCycle(const Visit& cycleStart)
		{
			auto it = std::find(fStack.begin(), fStack.end(), cycleStart);
			std::string path;
			for (; it != fStack.end(); ++it)
			{
				path += it->first + " -> ";
			}
			path += cycleStart.first;
			fReport.addError("module graph", std::string("cycle through ") + fEdgeKind + ": " + path);
		}

		const ModuleGraph& fGraph;
		const ModuleInterfaceMap& fInterfaces;
		ValidationReport& fReport;
		const char* fEdgeKind;   // "parentStructure and constraint drivers" or "DAG parents", for the message
		std::map<Visit, State> fState;
		std::vector<Visit> fStack;
	};

	void validateParentStructure(
		const RigModuleData& module,
		const std::string& where,
		const ModuleInterface& moduleInterface,
		const std::set<std::string>& declaredModules,
		const ModuleInterfaceMap& interfaces,
		ModuleGraph& graph,
		ModuleGraph& dagGraph,
		ValidationReport& report)
	{
		// Sorted, so messages come out in a stable order.
		const std::map<std::string, std::string> parentStructure(module.parentStructure.begin(), module.parentStructure.end());

		for (const ModulePlugInfo& plug : moduleInterface.childPlugs)
		{
			if (plug.required && module.parentStructure.count(plug.name) == 0 && module.matrixParentStructure.count(plug.name) == 0)
			{
				report.addError(where + "." + TemplateKey::ParentStructure, "child plug '" + plug.name + "' has no default parent, so it needs an entry");
			}
		}

		for (const auto& [plugName, socketIdentifier] : parentStructure)
		{
			const std::string entryPath = where + "." + TemplateKey::ParentStructure + "." + plugName;

			const ModulePlugInfo* pPlugInfo = moduleInterface.findChildPlug(plugName);
			if (pPlugInfo == nullptr)
			{
				report.addError(entryPath, "no child plug '" + plugName + "' on this module (has: " + childPlugNames(moduleInterface) + ")");
			}
			else if (!pPlugInfo->followsPlug.empty())
			{
				report.addError(entryPath, "child plug '" + plugName + "' is internal: it follows the parent of '" + pPlugInfo->followsPlug
					+ "', so it takes no parentStructure entry");
				continue;
			}

			std::string parentModule;
			std::string socketName;
			const ModuleInterface* pParentInterface = nullptr;
			if (resolveParentSocket(module, socketIdentifier, entryPath, declaredModules, interfaces, report, parentModule, socketName, pParentInterface)
				!= ParentSocketResult::Local)
			{
				continue;
			}

			// The entry is the DAG parent when the plug has a control to parent (D78). A socket with no control transform cannot be one, and
			// a plug with no control (an aim plug, a limb's ikTarget) has nothing to parent, so its entry is only a matrix parent.
			const bool hasControl = pPlugInfo == nullptr || pPlugInfo->hasControl;
			if (hasControl && pParentInterface->isMatrixOnly(socketName))
			{
				std::vector<std::string> withControl;
				for (const std::string& name : pParentInterface->sockets)
				{
					if (!pParentInterface->isMatrixOnly(name))
					{
						withControl.push_back(name);
					}
				}
				report.addError(entryPath, "'" + socketIdentifier + "' is a matrix-only socket and has no control to be a DAG parent. A valid dagParent must be given here: "
					"a socket with a control (on '" + parentModule + "': " + namesOrNone(withControl) + "). To keep following its matrix, name '"
					+ socketIdentifier + "' in matrixParentStructure for '" + plugName + "'");
				continue;
			}
			if (pPlugInfo != nullptr && pPlugInfo->hostsChannels && pParentInterface->isMatrixOnly(socketName))
			{
				report.addError(entryPath, "'" + socketIdentifier + "' is a matrix-only socket, and '" + plugName + "' needs a socket with a control "
					"to hold this module's channels");
				continue;
			}

			if (hasControl)
			{
				dagGraph[parentModule].push_back({ module.name, socketName, plugName });
			}
			// A plug with a matrixParentStructure entry takes its matrix from that, and validateMatrixParentStructure adds the edge.
			if (module.matrixParentStructure.count(plugName) == 0)
			{
				graph[parentModule].push_back({ module.name, socketName, plugName });
			}
		}
	}

	// The matrix parents named apart from the DAG parent (D78): any socket, with or without a control.
	void validateMatrixParentStructure(
		const RigModuleData& module,
		const std::string& where,
		const ModuleInterface& moduleInterface,
		const std::set<std::string>& declaredModules,
		const ModuleInterfaceMap& interfaces,
		ModuleGraph& graph,
		ValidationReport& report)
	{
		const std::map<std::string, std::string> matrixParentStructure(module.matrixParentStructure.begin(), module.matrixParentStructure.end());

		for (const auto& [plugName, socketIdentifier] : matrixParentStructure)
		{
			const std::string entryPath = where + "." + TemplateKey::MatrixParentStructure + "." + plugName;

			const ModulePlugInfo* pPlugInfo = moduleInterface.findChildPlug(plugName);
			if (pPlugInfo == nullptr)
			{
				report.addError(entryPath, "no child plug '" + plugName + "' on this module (has: " + childPlugNames(moduleInterface) + ")");
				continue;
			}
			if (!pPlugInfo->followsPlug.empty())
			{
				report.addError(entryPath, "child plug '" + plugName + "' is internal: it follows the parent of '" + pPlugInfo->followsPlug
					+ "', so it takes no matrixParentStructure entry");
				continue;
			}

			std::string parentModule;
			std::string socketName;
			const ModuleInterface* pParentInterface = nullptr;
			if (resolveParentSocket(module, socketIdentifier, entryPath, declaredModules, interfaces, report, parentModule, socketName, pParentInterface)
				== ParentSocketResult::Local)
			{
				graph[parentModule].push_back({ module.name, socketName, plugName });
			}
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

			if (!isConventionName(constraint.name, NamingConvention::Kind::Constraint))
			{
				report.addError(where, "name must be " + std::string(NamingConvention::Form) + " ending in " + NamingConvention::kindToken(NamingConvention::Kind::Constraint));
			}
			else if (!constraintNames.emplace(constraint.name, c).second)
			{
				report.addError(where, "name is already used by " + RigTemplateValidation::constraintPath(constraintNames[constraint.name], constraints[constraintNames[constraint.name]]));
			}

			if (ConstraintTypes::indexOf(constraint.type) < 0)
			{
				report.addError(where + "." + TemplateKey::ConstraintType, "unknown constraint type '" + constraint.type + "' (has: " + namesOrNone(ConstraintTypes::All) + ")");
			}

			if (!(constraint.constraintWeight >= 0.0 && constraint.constraintWeight <= 1.0))
			{
				report.addError(where + "." + TemplateKey::ConstraintWeight, "must be between 0 and 1");
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
				else if (!pPlugInfo->allowsConstraint)
				{
					report.addError(targetPath, "child plug '" + constraint.target + "' does not take a constraint: its module owns its space");
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

				const std::string socketPath = driverPath + "." + TemplateKey::DriverSocket;
				if (RigTemplateValidation::isParentSocketId(driver.socket))
				{
					checkParentSocketForm(driver.socket, socketPath, report);
					continue;
				}

				std::string driverModule;
				std::string socketName;
				const ModuleInterface* pDriverInterface = nullptr;
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
					graph[driverModule].push_back({ targetModule, socketName, targetPlug });
				}
			}

			if (bestCount > 1)
			{
				report.addWarning(where + "." + TemplateKey::ConstraintDrivers, "several drivers share the highest weight, the first one starts active");
			}
		}
	}

	// Everything about a collision that needs no scene: its name, type, numbers, follow selector and target plug. Whether the
	// followed joint exists, and which module drives it, are checked by the build, which has the joints.
	void validateCollisions(
		const std::vector<RigCollisionData>& collisions,
		const std::vector<RigConstraintData>& constraints,
		const std::set<std::string>& declaredModules,
		const ModuleInterfaceMap& interfaces,
		ValidationReport& report)
	{
		std::map<std::string, std::size_t> names;
		std::map<std::string, std::string> collidedPlugs;   // "module.childPlug" -> collision name

		for (std::size_t c = 0; c < collisions.size(); ++c)
		{
			const RigCollisionData& collision = collisions[c];
			const std::string where = RigTemplateValidation::collisionPath(c, collision);

			// A collision node is named like a constraint (_CON), and both live in one namespace.
			if (!isConventionName(collision.name, NamingConvention::Kind::Constraint))
			{
				report.addError(where, "name must be " + std::string(NamingConvention::Form) + " ending in " + NamingConvention::kindToken(NamingConvention::Kind::Constraint));
			}
			else if (!names.emplace(collision.name, c).second)
			{
				report.addError(where, "name is already used by collision " + RigTemplateValidation::collisionPath(names[collision.name], collisions[names[collision.name]]));
			}
			else if (std::any_of(constraints.begin(), constraints.end(), [&collision](const RigConstraintData& constraint)
				{
					return constraint.name == collision.name;
				}))
			{
				report.addError(where, "name is already used by a constraint");
			}

			if (collision.type != CollisionTypes::PushOut)
			{
				report.addError(where + "." + TemplateKey::ConstraintType, "'" + collision.type + "' is not a collision type (the only one is " + CollisionTypes::PushOut + ")");
			}

			std::string numberError;
			const PushOutSolve::Capsule capsule{ collision.radius, collision.halfLength };
			const PushOutSolve::Settings settings{ collision.margin, collision.maxPush, collision.weight };
			if (!PushOutSolve::validate(capsule, settings, numberError))
			{
				// The message starts with the key it is about, which is in the capsule object or the entry.
				const bool isInCapsule = numberError.rfind("radius", 0) == 0 || numberError.rfind("halfLength", 0) == 0;
				const char* key = numberError.rfind("radius", 0) == 0 ? TemplateKey::CapsuleRadius
					: numberError.rfind("halfLength", 0) == 0 ? TemplateKey::CapsuleHalfLength
					: numberError.rfind("margin", 0) == 0 ? TemplateKey::CollisionMargin
					: numberError.rfind("maxPush", 0) == 0 ? TemplateKey::CollisionMaxPush
					: TemplateKey::ConstraintWeight;
				report.addError(where + (isInCapsule ? std::string(".") + TemplateKey::CollisionCapsule : std::string()) + "." + key, numberError);
			}

			const std::string followPath = where + "." + TemplateKey::CollisionCapsule + "." + TemplateKey::CapsuleFollow;
			if (!isValidSideName(collision.follow[0]))
			{
				report.addError(followPath, "'" + collision.follow[0] + "' is not one of " + SideNames::Center + ", " + SideNames::Left + ", " + SideNames::Right);
			}
			if (collision.follow[1].empty())
			{
				report.addError(followPath, "usage is empty");
			}
			const auto isFinite = [](const std::array<double, 3>& values)
			{
				return std::all_of(values.begin(), values.end(), [](double value)
				{
					return std::isfinite(value);
				});
			};
			if (!isFinite(collision.translate))
			{
				report.addError(where + "." + TemplateKey::CollisionCapsule + "." + TemplateKey::CapsuleOffset + "." + TemplateKey::OffsetTranslate, "must be three finite numbers");
			}
			if (!isFinite(collision.rotate))
			{
				report.addError(where + "." + TemplateKey::CollisionCapsule + "." + TemplateKey::CapsuleOffset + "." + TemplateKey::OffsetRotate, "must be three finite numbers");
			}

			std::string targetModule;
			std::string targetPlug;
			const ModuleInterface* pTargetInterface = nullptr;
			const std::string targetPath = where + "." + TemplateKey::ConstraintTarget;
			if (lookupModule(collision.target, declaredModules, interfaces, "module.childPlug",
				targetPath, report, targetModule, targetPlug, pTargetInterface) != LookupResult::Found)
			{
				continue;
			}
			const ModulePlugInfo* pPlugInfo = pTargetInterface->findChildPlug(targetPlug);
			if (pPlugInfo == nullptr)
			{
				report.addError(targetPath, "no child plug '" + targetPlug + "' on '" + targetModule + "' (has: " + childPlugNames(*pTargetInterface) + ")");
			}
			else if (!pPlugInfo->hasControl)
			{
				report.addError(targetPath, "child plug '" + collision.target + "' has no control to push out");
			}
			else if (!pPlugInfo->allowsConstraint)
			{
				// The same rule as a constraint: a plug whose input the module owns cannot take the feed over.
				report.addError(targetPath, "child plug '" + collision.target + "' does not take a collision: its module owns its space");
			}
			else if (!collidedPlugs.emplace(collision.target, collision.name).second)
			{
				report.addError(targetPath, "child plug '" + collision.target + "' is already the target of collision '" + collidedPlugs[collision.target] + "'");
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
		return modulePath(static_cast<std::size_t>(module.mirrorOf), NamingConvention::swapSide(module.name))
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
		return constraintPath(static_cast<std::size_t>(constraint.mirrorOf), NamingConvention::swapSide(constraint.name))
			+ " (mirror '" + constraint.name + "')";
	}

	std::string collisionPath(std::size_t index, const std::string& name)
	{
		return std::string(TemplateKey::Collisions) + "[" + std::to_string(index) + "] '" + name + "'";
	}

	std::string collisionPath(std::size_t index, const RigCollisionData& collision)
	{
		if (!collision.isMirror())
		{
			return collisionPath(index, collision.name);
		}
		return collisionPath(static_cast<std::size_t>(collision.mirrorOf), NamingConvention::swapSide(collision.name))
			+ " (mirror '" + collision.name + "')";
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

	bool parseSelector(const nlohmann::json& entry, const std::string& where, JointSelector& outSelector, ValidationReport& report)
	{
		if (!entry.is_array() || entry.size() != 2 || !entry[0].is_string() || !entry[1].is_string())
		{
			report.addError(where, "must be [side, usage], got " + entry.dump());
			return false;
		}
		outSelector = JointSelector{ entry[0].get<std::string>(), entry[1].get<std::string>() };
		if (!isValidSideName(outSelector.side))
		{
			report.addError(where, "'" + outSelector.side + "' is not one of " + SideNames::Center + ", " + SideNames::Left + ", " + SideNames::Right);
			return false;
		}
		if (outSelector.usage.empty())
		{
			report.addError(where, "usage is empty");
			return false;
		}
		return true;
	}

	// The collisions list's keys, the same shape as the constraints' check in checkTemplateKeys, with the capsule object and
	// its offset one level down. A number list that is not three numbers is reported here, with its path.
	void checkCollisionKeys(const nlohmann::json& templateJson, ValidationReport& report)
	{
		using namespace TemplateKey;
		if (!templateJson.is_object() || !templateJson.contains(Collisions))
		{
			return;
		}
		const nlohmann::json& collisions = templateJson.at(Collisions);
		if (!collisions.is_array())
		{
			report.addError(Collisions, "must be a JSON array");
			return;
		}

		const auto checkVector = [&report](const nlohmann::json& parent, const char* key, const std::string& where)
		{
			if (!parent.contains(key))
			{
				return;
			}
			const nlohmann::json& values = parent.at(key);
			if (!values.is_array() || values.size() != 3 || !std::all_of(values.begin(), values.end(), [](const nlohmann::json& value)
				{
					return value.is_number();
				}))
			{
				report.addError(where + "." + key, "must be three numbers, got " + values.dump());
			}
		};

		for (std::size_t c = 0; c < collisions.size(); ++c)
		{
			const nlohmann::json& collision = collisions[c];
			const std::string name = collision.is_object() ? collision.value(Name, std::string()) : std::string();
			const std::string where = collisionPath(c, name);
			checkKeys(collision, { Name, ConstraintType, ConstraintTarget, CollisionCapsule },
				{ CollisionMargin, CollisionMaxPush, ConstraintWeight, Mirror }, where, report);
			if (!collision.is_object() || !collision.contains(CollisionCapsule))
			{
				continue;
			}

			const nlohmann::json& capsule = collision.at(CollisionCapsule);
			const std::string capsulePath = where + "." + CollisionCapsule;
			checkKeys(capsule, { CapsuleFollow, CapsuleRadius }, { CapsuleHalfLength, CapsuleOffset }, capsulePath, report);
			if (!capsule.is_object())
			{
				continue;
			}
			if (capsule.contains(CapsuleFollow))
			{
				JointSelector follow;
				parseSelector(capsule.at(CapsuleFollow), capsulePath + "." + CapsuleFollow, follow, report);
			}
			if (capsule.contains(CapsuleOffset))
			{
				const nlohmann::json& offset = capsule.at(CapsuleOffset);
				const std::string offsetPath = capsulePath + "." + CapsuleOffset;
				checkKeys(offset, {}, { OffsetTranslate, OffsetRotate }, offsetPath, report);
				if (offset.is_object())
				{
					checkVector(offset, OffsetTranslate, offsetPath);
					checkVector(offset, OffsetRotate, offsetPath);
				}
			}
			for (const char* key : { CapsuleRadius, CapsuleHalfLength })
			{
				if (capsule.contains(key) && !capsule.at(key).is_number())
				{
					report.addError(capsulePath + "." + key, "must be a number, got " + capsule.at(key).dump());
				}
			}
		}
	}

	void checkTemplateKeys(const nlohmann::json& templateJson, ValidationReport& report)
	{
		using namespace TemplateKey;

		checkKeys(templateJson,
			{ RigVersion, RigTemplateName, RigAuthor, RigModules },
			{ FormatVersion, RigName, Constraints, Collisions },
			"template", report);
		if (!templateJson.is_object())
		{
			return;
		}

		checkCollisionKeys(templateJson, report);

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
						{ ShapeType, Side, ModuleVersion, ParentStructure, MatrixParentStructure, ModuleArgs, Mirror },
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
				checkKeys(constraint, { Name, ConstraintTarget, ConstraintDrivers }, { ConstraintType, ConstraintWeight, Mirror }, where, report);

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
		if (!AssetId::isValidName(rigData.rigName))
		{
			// The rig name becomes a folder in the asset (D54). A default from rigTemplateName is already snake_case.
			report.addError(TemplateKey::RigName, "'" + rigData.rigName + "' must be lowercase letters, digits and '_'");
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

			if (!isConventionName(name, NamingConvention::Kind::Module))
			{
				report.addError(where, "name must be " + std::string(NamingConvention::Form) + " ending in " + NamingConvention::kindToken(NamingConvention::Kind::Module));
				continue;
			}

			// The side letter and the side field say the same thing: the mirror swaps both, and the controls take the side.
			NamingConvention::Name parsedName;
			NamingConvention::parse(name, parsedName);
			if (parsedName.side != modules[i].side)
			{
				report.addError(where, "name has side " + NamingConvention::sideToken(parsedName.side) + ", but the module's side is '" + modules[i].side + "'");
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

	void validateModuleFields(const RigModuleData& module, const std::string& where, ValidationReport& report)
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
	}

	void validateConnections(const RigRootData& rigData, const ModuleInterfaceMap& interfaces, ValidationReport& report,
		const std::vector<CollisionDependency>& collisionDependencies)
	{
		std::set<std::string> declaredModules;
		for (const RigModuleData& module : rigData.rigModules)
		{
			declaredModules.insert(module.name);
		}

		ModuleGraph graph;      // matrix edges: what a module's evaluation depends on
		ModuleGraph dagGraph;   // DAG parent edges: Maya refuses a DAG loop at the reparent, so the template says so first

		for (std::size_t i = 0; i < rigData.rigModules.size(); ++i)
		{
			const RigModuleData& module = rigData.rigModules[i];
			auto it = interfaces.find(module.name);
			if (it == interfaces.end())
			{
				continue;   // failed planning, already reported
			}
			validateParentStructure(module, modulePath(i, module), it->second, declaredModules, interfaces, graph, dagGraph, report);
			validateMatrixParentStructure(module, modulePath(i, module), it->second, declaredModules, interfaces, graph, report);
		}

		validateConstraints(rigData.rigConstraints, declaredModules, interfaces, graph, report);
		validateCollisions(rigData.rigCollisions, rigData.rigConstraints, declaredModules, interfaces, report);

		// The module that drives the capsule's joint is upstream of the collided module (D79). The edge leaves by no socket
		// in particular, so no plug is blind to it.
		for (const CollisionDependency& dependency : collisionDependencies)
		{
			std::string targetModule;
			std::string targetPlug;
			if (!StringUtils::splitIdentifier(dependency.target, targetModule, targetPlug) || interfaces.find(targetModule) == interfaces.end())
			{
				continue;   // reported by validateCollisions
			}
			graph[dependency.driverModule].push_back({ targetModule, std::string(), targetPlug });
		}

		CycleFinder(graph, interfaces, report, "parentStructure, matrixParentStructure and constraint drivers").run();
		CycleFinder(dagGraph, interfaces, report, "DAG parents (parentStructure)").run();
	}

	bool isParentSocketId(const std::string& identifier)
	{
		return StringUtils::startsWith(identifier, ParentRigId::Prefix);
	}

	std::string stripParentPrefix(const std::string& identifier)
	{
		return isParentSocketId(identifier) ? identifier.substr(std::string(ParentRigId::Prefix).size()) : identifier;
	}

	bool isDrivenByParent(const RigConstraintData& constraint)
	{
		return !constraint.drivers.empty() && std::all_of(constraint.drivers.begin(), constraint.drivers.end(),
			[](const RigConstraintDriverData& driver)
			{
				return isParentSocketId(driver.socket);
			});
	}

	std::vector<std::string> parentSocketIds(const RigRootData& rigData)
	{
		std::set<std::string> ids;
		for (const RigModuleData& module : rigData.rigModules)
		{
			for (const auto* pStructure : { &module.parentStructure, &module.matrixParentStructure })
			{
				for (const auto& [plugName, socketIdentifier] : *pStructure)
				{
					if (isParentSocketId(socketIdentifier))
					{
						ids.insert(stripParentPrefix(socketIdentifier));
					}
				}
			}
		}
		for (const RigConstraintData& constraint : rigData.rigConstraints)
		{
			for (const RigConstraintDriverData& driver : constraint.drivers)
			{
				if (isParentSocketId(driver.socket))
				{
					ids.insert(stripParentPrefix(driver.socket));
				}
			}
		}
		return std::vector<std::string>(ids.begin(), ids.end());
	}

	void validateBlendDrivers(const RigRootData& rigData, const ModuleInterfaceMap& interfaces, ValidationReport& report)
	{
		for (std::size_t i = 0; i < rigData.rigModules.size(); ++i)
		{
			const RigModuleData& module = rigData.rigModules[i];
			const auto follower = interfaces.find(module.name);
			if (follower == interfaces.end() || follower->second.blendDriver.empty())
			{
				continue;
			}
			const std::string& driverName = follower->second.blendDriver;
			const std::string where = modulePath(i, module) + "." + TemplateKey::ModuleArgs + "." + RigAttr::IkFkDriver;

			const bool isInTemplate = std::any_of(rigData.rigModules.begin(), rigData.rigModules.end(), [&driverName](const RigModuleData& other)
			{
				return other.name == driverName;
			});
			if (!isInTemplate)
			{
				report.addError(where, "'" + driverName + "' is not a module in the template");
				continue;
			}
			const auto driver = interfaces.find(driverName);
			if (driver == interfaces.end())
			{
				continue;   // failed planning, already reported
			}
			if (driver->second.blendSource.empty())
			{
				report.addError(where, "'" + driverName + "' has no ik/fk blend to follow: it must be a Limb or a TwoBoneIk");
				continue;
			}

			// The follower stands on the driver: each of its child plugs must hang from one of the driver's sockets (a plug with no
			// parentStructure entry takes layout_00_M_MOD.end, which is not).
			for (const ModulePlugInfo& plug : follower->second.childPlugs)
			{
				const std::string* pEntry = module.matrixParentOf(plug.name);
				const std::string parentId = pEntry != nullptr ? *pEntry : std::string(RigNaming::LayoutModule) + ".end";
				const std::string parentModule = isParentSocketId(parentId) ? parentId : parentId.substr(0, parentId.find('.'));
				if (parentModule != driverName)
				{
					report.addError(where, "the child plug '" + plug.name + "' hangs from '" + parentId + "', and the foot follows the blend of the module it hangs from, "
						+ "so it must hang from a socket of '" + driverName + "'");
				}
			}

			// The driver must read the follower's output, or nothing the animator does on the follower reaches the joints.
			const std::string& driverPlug = follower->second.driverPlug;
			if (driverPlug.empty())
			{
				continue;
			}
			const std::string expectedParent = module.name + "." + follower->second.driverPlugSocket;
			if (driver->second.findChildPlug(driverPlug) == nullptr)
			{
				report.addError(where, "'" + driverName + "' has no child plug '" + driverPlug + "' to take '" + expectedParent
					+ "', so the foot could not move its ankle: it must be a Limb");
				continue;
			}
			const auto driverModule = std::find_if(rigData.rigModules.begin(), rigData.rigModules.end(), [&driverName](const RigModuleData& other)
			{
				return other.name == driverName;
			});
			const std::string* pEntry = driverModule->matrixParentOf(driverPlug);
			if (pEntry == nullptr || *pEntry != expectedParent)
			{
				const std::string has = pEntry == nullptr ? "has no '" + driverPlug + "' entry" : "takes '" + *pEntry + "'";
				report.addError(where, "'" + driverName + "' " + has + " in its parentStructure, so the foot's channels would not move the ankle. Add \""
					+ driverPlug + "\": \"" + expectedParent + "\" to the parentStructure of '" + driverName + "'");
			}
		}
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
