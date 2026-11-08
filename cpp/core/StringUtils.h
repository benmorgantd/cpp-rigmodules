#pragma once

// Maya-free core: no Maya headers here. RigTemplateValidation.cpp includes this file.

#include <cctype>
#include <string>

class StringUtils
{
public:
	// Splits "module.name" on the first '.'. False if there is no '.', or either side is empty.
	static bool splitIdentifier(const std::string& identifier, std::string& outModule, std::string& outName)
	{
		const size_t dot = identifier.find('.');
		if (dot == std::string::npos || dot == 0 || dot + 1 >= identifier.size())
		{
			return false;
		}
		outModule = identifier.substr(0, dot);
		outName = identifier.substr(dot + 1);
		return true;
	}

	// ASCII only, which is all an asset id may contain.
	static bool equalsIgnoreCase(const std::string& a, const std::string& b)
	{
		if (a.size() != b.size())
		{
			return false;
		}
		for (size_t i = 0; i < a.size(); ++i)
		{
			if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
			{
				return false;
			}
		}
		return true;
	}
};
