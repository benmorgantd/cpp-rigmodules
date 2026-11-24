#include "NamingConvention.h"
#include "Constants.h"
#include "StringUtils.h"

#include <cctype>
#include <cstddef>
#include <vector>

namespace
{
	constexpr std::size_t kMaxIndexDigits = 9;   // keeps the index inside an int
	constexpr std::size_t kPartCount = 4;        // base, index, side, kind

	std::vector<std::string> splitOnUnderscores(const std::string& text)
	{
		std::vector<std::string> parts;
		std::size_t start = 0;
		while (true)
		{
			const std::size_t underscore = text.find('_', start);
			if (underscore == std::string::npos)
			{
				parts.push_back(text.substr(start));
				return parts;
			}
			parts.push_back(text.substr(start, underscore - start));
			start = underscore + 1;
		}
	}

	bool isAllDigits(const std::string& text)
	{
		if (text.empty())
		{
			return false;
		}
		for (char c : text)
		{
			if (!std::isdigit(static_cast<unsigned char>(c)))
			{
				return false;
			}
		}
		return true;
	}

	bool sideFromToken(const std::string& token, std::string& outSide)
	{
		for (const char* side : { SideNames::Left, SideNames::Right, SideNames::Center })
		{
			if (NamingConvention::sideToken(side) == token)
			{
				outSide = side;
				return true;
			}
		}
		return false;
	}

	bool kindFromToken(const std::string& token, NamingConvention::Kind& outKind)
	{
		using NamingConvention::Kind;
		for (Kind kind : { Kind::Joint, Kind::Control, Kind::Module, Kind::Constraint, Kind::Mesh, Kind::Marker })
		{
			if (token == NamingConvention::kindToken(kind))
			{
				outKind = kind;
				return true;
			}
		}
		return false;
	}

	std::string indexText(int index)
	{
		const std::string text = std::to_string(index);
		return text.size() < 2 ? "0" + text : text;
	}
}

namespace NamingConvention
{
	std::string sideToken(const std::string& side)
	{
		if (side == SideNames::Left)
		{
			return "L";
		}
		if (side == SideNames::Right)
		{
			return "R";
		}
		if (side == SideNames::Center)
		{
			return "M";
		}
		return std::string();
	}

	const char* kindToken(Kind kind)
	{
		switch (kind)
		{
			case Kind::Joint: return "JNT";
			case Kind::Control: return "CTRL";
			case Kind::Module: return "MOD";
			case Kind::Constraint: return "CON";
			case Kind::Mesh: return "GEO";
			case Kind::Marker: return "MRK";
		}
		return "";
	}

	bool isValidBase(const std::string& base)
	{
		if (base.empty() || !std::islower(static_cast<unsigned char>(base[0])))
		{
			return false;
		}
		for (char c : base)
		{
			if (!std::isalnum(static_cast<unsigned char>(c)))
			{
				return false;
			}
		}
		return true;
	}

	std::string format(const Name& name)
	{
		return name.base + "_" + indexText(name.index) + "_" + sideToken(name.side) + "_" + kindToken(name.kind);
	}

	bool parse(const std::string& text, Name& outName)
	{
		const std::vector<std::string> parts = splitOnUnderscores(text);
		if (parts.size() != kPartCount || !isValidBase(parts[0]))
		{
			return false;
		}
		if (parts[1].size() < 2 || parts[1].size() > kMaxIndexDigits || !isAllDigits(parts[1]))
		{
			return false;
		}

		Name name;
		name.base = parts[0];
		name.index = std::stoi(parts[1]);
		if (!sideFromToken(parts[2], name.side) || !kindFromToken(parts[3], name.kind))
		{
			return false;
		}

		// "arm_007_L_JNT" reads as index 7, but formats as "arm_07_L_JNT". Only the canonical text is a name.
		if (format(name) != text)
		{
			return false;
		}
		outName = name;
		return true;
	}

	bool fromUsage(const std::string& usage, const std::string& side, Kind kind, Name& outName)
	{
		Name name;
		name.base = StringUtils::stripNumberSuffix(usage);
		if (name.base != usage)
		{
			const std::string digits = usage.substr(name.base.size() + 1);
			if (digits.size() > kMaxIndexDigits)
			{
				return false;
			}
			name.index = std::stoi(digits);
		}
		name.side = side;
		name.kind = kind;
		if (!isValidBase(name.base) || sideToken(side).empty())
		{
			return false;
		}
		outName = name;
		return true;
	}

	bool controlName(const std::string& source, const std::string& side, std::string& outName)
	{
		Name name;
		if (parse(source, name) && name.kind == Kind::Joint)
		{
			name.side = side;
			name.kind = Kind::Control;
		}
		else if (!fromUsage(source, side, Kind::Control, name))
		{
			return false;
		}
		outName = format(name);
		return true;
	}

	std::string swapSide(const std::string& text)
	{
		Name name;
		if (!parse(text, name) || name.side == SideNames::Center)
		{
			return std::string();
		}
		name.side = name.side == SideNames::Left ? SideNames::Right : SideNames::Left;
		return format(name);
	}
}
