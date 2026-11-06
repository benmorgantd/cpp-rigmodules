#pragma once

// Maya-free core: no Maya headers here. RigTemplateValidation.cpp includes this file.

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
};
