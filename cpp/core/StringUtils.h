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

	// The names separated by ", ", for a message: "a, b, c". Takes any range of strings or string literals.
	template <typename TNames>
	static std::string joinNames(const TNames& names)
	{
		std::string text;
		for (const auto& name : names)
		{
			if (!text.empty())
			{
				text += ", ";
			}
			text += name;
		}
		return text;
	}

	static bool endsWith(const std::string& text, const std::string& suffix)
	{
		return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
	}

	// text without any leading or trailing ch: trim(":rig:", ':') is "rig".
	static std::string trim(const std::string& text, char ch)
	{
		const size_t first = text.find_first_not_of(ch);
		if (first == std::string::npos)
		{
			return std::string();
		}
		return text.substr(first, text.find_last_not_of(ch) - first + 1);
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
