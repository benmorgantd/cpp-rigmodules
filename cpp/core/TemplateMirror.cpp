#include "TemplateMirror.h"
#include "Constants.h"
#include "NamingConvention.h"
#include "RigTemplateValidation.h"
#include "StringUtils.h"

#include <cstddef>
#include <map>
#include <vector>

namespace
{
	bool isMirrorableSide(const std::string& side)
	{
		return side == SideNames::Left || side == SideNames::Right;
	}

	// A selector is a [side, usage] pair, the only two-string array with a side first in moduleArgs (D15).
	void swapSelectorSides(nlohmann::json& value)
	{
		if (value.is_array())
		{
			if (value.size() == 2 && value[0].is_string() && value[1].is_string())
			{
				value[0] = TemplateMirror::swapSide(value[0].get<std::string>());
				return;
			}
			for (nlohmann::json& element : value)
			{
				swapSelectorSides(element);
			}
		}
		else if (value.is_object())
		{
			for (auto& item : value.items())
			{
				swapSelectorSides(item.value());
			}
		}
	}

	// A socket named from joint markup (FkHierarchy, D71) starts with its joint's side: "left_upperArm" mirrors to
	// "right_upperArm". No other module names a socket that way, so every other socket name stays as it is.
	std::string swapSocketSide(const std::string& socket)
	{
		for (const auto& [from, to] : { std::pair<const char*, const char*>{ "left_", "right_" }, { "right_", "left_" } })
		{
			if (socket.rfind(from, 0) == 0)
			{
				return to + socket.substr(std::string(from).size());
			}
		}
		return socket;
	}

	// Template module name -> side, for the modules written in the file.
	using ModuleSides = std::map<std::string, std::string>;

	// "clavicle_00_L_MOD.end" -> "clavicle_00_R_MOD.end" when clavicle_00_L_MOD is a Left or Right module. Center modules,
	// layout and unknown names stay as they are, and a malformed id is left for validateConnections to report. A socket
	// named from markup swaps its side in every case, so a mirrored module on a Center FkHierarchy's "left_shoulder"
	// attaches to its "right_shoulder".
	std::string mirrorIdentifier(const std::string& identifier, const ModuleSides& moduleSides, const std::string& where, ValidationReport& report)
	{
		// A parent rig's module is not in this template, so only its name's side token says which side it is (D55).
		// A Center module stays as it is.
		if (RigTemplateValidation::isParentSocketId(identifier))
		{
			const std::string parentId = RigTemplateValidation::stripParentPrefix(identifier);
			std::string parentModule;
			std::string parentMember;
			if (!StringUtils::splitIdentifier(parentId, parentModule, parentMember))
			{
				return identifier;
			}
			const std::string swapped = NamingConvention::swapSide(parentModule);
			return std::string(ParentRigId::Prefix) + (swapped.empty() ? parentModule : swapped) + "." + swapSocketSide(parentMember);
		}

		std::string moduleName;
		std::string memberName;
		if (!StringUtils::splitIdentifier(identifier, moduleName, memberName))
		{
			return identifier;
		}

		auto it = moduleSides.find(moduleName);
		if (it == moduleSides.end() || !isMirrorableSide(it->second))
		{
			return moduleName + "." + swapSocketSide(memberName);
		}

		const std::string mirrorModule = NamingConvention::swapSide(moduleName);
		if (mirrorModule.empty())
		{
			report.addError(where, "'" + moduleName + "' is a " + it->second + " module, but its name is not " + NamingConvention::Form
				+ " with a side of L or R, so its mirror can't be found");
			return identifier;
		}
		return mirrorModule + "." + swapSocketSide(memberName);
	}
}

namespace TemplateMirror
{
	std::string swapSide(const std::string& side)
	{
		if (side == SideNames::Left)
		{
			return SideNames::Right;
		}
		if (side == SideNames::Right)
		{
			return SideNames::Left;
		}
		return side;
	}

	void expandMirrors(RigRootData& rigData, ValidationReport& report)
	{
		ModuleSides moduleSides;
		for (const RigModuleData& module : rigData.rigModules)
		{
			moduleSides.emplace(module.name, module.side);
		}

		std::vector<RigModuleData> mirrorModules;
		for (std::size_t i = 0; i < rigData.rigModules.size(); ++i)
		{
			const RigModuleData& source = rigData.rigModules[i];
			if (!source.mirror)
			{
				continue;
			}

			const std::string where = RigTemplateValidation::modulePath(i, source.name);
			if (!isMirrorableSide(source.side))
			{
				report.addError(where + "." + TemplateKey::Mirror, "only Left or Right modules can be mirrored, this one is '" + source.side + "'");
				continue;
			}
			// validateModuleNames checks the whole name and that its side is the module's. This only needs it to swap.
			const std::string mirrorName = NamingConvention::swapSide(source.name);
			if (mirrorName.empty())
			{
				report.addError(where + "." + TemplateKey::Name, "a mirrored " + source.side + " module's name must be "
					+ NamingConvention::Form + " with a side of L or R, so its mirror's name can swap the side");
				continue;
			}

			RigModuleData mirror = source;
			mirror.mirror = false;
			mirror.mirrorOf = static_cast<int>(i);
			mirror.name = mirrorName;
			mirror.side = swapSide(source.side);
			swapSelectorSides(mirror.moduleArgs);
			for (auto& [plugName, socketIdentifier] : mirror.parentStructure)
			{
				const std::string entryPath = where + "." + TemplateKey::ParentStructure + "." + plugName;
				socketIdentifier = mirrorIdentifier(socketIdentifier, moduleSides, entryPath, report);
			}
			mirrorModules.push_back(std::move(mirror));
		}
		rigData.rigModules.insert(rigData.rigModules.end(), mirrorModules.begin(), mirrorModules.end());

		std::vector<RigConstraintData> mirrorConstraints;
		for (std::size_t c = 0; c < rigData.rigConstraints.size(); ++c)
		{
			const RigConstraintData& source = rigData.rigConstraints[c];
			if (!source.mirror)
			{
				continue;
			}

			const std::string where = RigTemplateValidation::constraintPath(c, source.name);
			std::string targetModule;
			std::string targetPlug;
			if (!StringUtils::splitIdentifier(source.target, targetModule, targetPlug))
			{
				continue;   // validateConnections reports the malformed target
			}
			auto it = moduleSides.find(targetModule);
			if (it == moduleSides.end() || !isMirrorableSide(it->second))
			{
				report.addError(where + "." + TemplateKey::Mirror, "only constraints on a Left or Right module can be mirrored, '"
					+ targetModule + "' is not one");
				continue;
			}
			const std::string mirrorName = NamingConvention::swapSide(source.name);
			if (mirrorName.empty())
			{
				report.addError(where + "." + TemplateKey::Name, std::string("a mirrored constraint's name must be ")
					+ NamingConvention::Form + " with a side of L or R, so its mirror's name can swap the side");
				continue;
			}

			RigConstraintData mirror = source;
			mirror.mirror = false;
			mirror.mirrorOf = static_cast<int>(c);
			mirror.name = mirrorName;
			mirror.target = mirrorIdentifier(source.target, moduleSides, where + "." + TemplateKey::ConstraintTarget, report);
			for (std::size_t d = 0; d < mirror.drivers.size(); ++d)
			{
				const std::string driverPath = where + "." + TemplateKey::ConstraintDrivers + "[" + std::to_string(d) + "]." + TemplateKey::DriverSocket;
				mirror.drivers[d].socket = mirrorIdentifier(source.drivers[d].socket, moduleSides, driverPath, report);
				// A label left out defaults to the socket id. Re-reading the mirror's own JSON would default to its own socket.
				if (source.drivers[d].label == source.drivers[d].socket)
				{
					mirror.drivers[d].label = mirror.drivers[d].socket;
				}
			}
			mirrorConstraints.push_back(std::move(mirror));
		}
		rigData.rigConstraints.insert(rigData.rigConstraints.end(), mirrorConstraints.begin(), mirrorConstraints.end());
	}
}
