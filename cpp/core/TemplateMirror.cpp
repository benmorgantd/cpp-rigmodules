#include "TemplateMirror.h"
#include "Constants.h"
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

	const char* suffixForSide(const std::string& side)
	{
		return side == SideNames::Left ? RigNaming::LeftSuffix : RigNaming::RightSuffix;
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

	// Template module name -> side, for the modules written in the file.
	using ModuleSides = std::map<std::string, std::string>;

	// "clavicle_l.end" -> "clavicle_r.end" when clavicle_l is a Left or Right module. Center modules, layout and
	// unknown names stay as they are, and a malformed id is left for validateConnections to report.
	std::string mirrorIdentifier(const std::string& identifier, const ModuleSides& moduleSides, const std::string& where, ValidationReport& report)
	{
		std::string moduleName;
		std::string memberName;
		if (!StringUtils::splitIdentifier(identifier, moduleName, memberName))
		{
			return identifier;
		}

		auto it = moduleSides.find(moduleName);
		if (it == moduleSides.end() || !isMirrorableSide(it->second))
		{
			return identifier;
		}

		const std::string mirrorModule = TemplateMirror::swapSideSuffix(moduleName);
		if (mirrorModule.empty())
		{
			report.addError(where, "'" + moduleName + "' is a " + it->second + " module, but its name doesn't end in '"
				+ RigNaming::LeftSuffix + "' or '" + RigNaming::RightSuffix + "', so its mirror can't be found");
			return identifier;
		}
		return mirrorModule + "." + memberName;
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

	std::string swapSideSuffix(const std::string& name)
	{
		const std::string left = RigNaming::LeftSuffix;
		const std::string right = RigNaming::RightSuffix;
		if (StringUtils::endsWith(name, left))
		{
			return name.substr(0, name.size() - left.size()) + right;
		}
		if (StringUtils::endsWith(name, right))
		{
			return name.substr(0, name.size() - right.size()) + left;
		}
		return std::string();
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
			const char* suffix = suffixForSide(source.side);
			if (!StringUtils::endsWith(source.name, suffix))
			{
				report.addError(where + "." + TemplateKey::Name, "a mirrored " + source.side + " module's name must end in '"
					+ suffix + "', so its mirror's name can swap the suffix");
				continue;
			}

			RigModuleData mirror = source;
			mirror.mirror = false;
			mirror.mirrorOf = static_cast<int>(i);
			mirror.name = swapSideSuffix(source.name);
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
			const std::string mirrorName = swapSideSuffix(source.name);
			if (mirrorName.empty())
			{
				report.addError(where + "." + TemplateKey::Name, std::string("a mirrored constraint's name must end in '")
					+ RigNaming::LeftSuffix + "' or '" + RigNaming::RightSuffix + "', so its mirror's name can swap the suffix");
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
