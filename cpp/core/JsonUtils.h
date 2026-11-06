#pragma once

// Maya-free core: no Maya headers here. JointMarkup.cpp includes this file.
// Every string attribute that holds structured data stores JSON text (see D16).
// Both functions work with any type that has from_json/to_json, including std::vector<std::string>.

#include <nlohmann/json.hpp>

#include <string>

namespace JsonUtils
{
	// Parses text and converts it to T. False with outError set on malformed JSON or a type mismatch.
	template <typename T>
	bool readJsonString(const std::string& text, T& outValue, std::string& outError)
	{
		try
		{
			nlohmann::json::parse(text).get_to(outValue);
		}
		catch (const nlohmann::json::exception& e)
		{
			outError = e.what();
			return false;
		}
		return true;
	}

	// Compact, single line.
	template <typename T>
	std::string writeJsonString(const T& value)
	{
		return nlohmann::json(value).dump();
	}
}
