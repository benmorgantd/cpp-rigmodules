#include "Hash64.h"

#include <cstdio>

namespace
{
	constexpr std::uint64_t kFnvPrime{ 1099511628211ull };
}

void Fnv1a64::addByte(std::uint8_t byte)
{
	fHash ^= byte;
	fHash *= kFnvPrime;
}

void Fnv1a64::addUInt32(std::uint32_t value)
{
	for (int byte = 0; byte < 4; ++byte)
	{
		addByte(static_cast<std::uint8_t>((value >> (8 * byte)) & 0xffu));
	}
}

void Fnv1a64::addInt64(std::int64_t value)
{
	const std::uint64_t bits = static_cast<std::uint64_t>(value);
	for (int byte = 0; byte < 8; ++byte)
	{
		addByte(static_cast<std::uint8_t>((bits >> (8 * byte)) & 0xffu));
	}
}

void Fnv1a64::addString(const std::string& text)
{
	addUInt32(static_cast<std::uint32_t>(text.size()));
	for (const char c : text)
	{
		addByte(static_cast<std::uint8_t>(c));
	}
}

std::uint64_t Fnv1a64::value() const
{
	return fHash;
}

namespace Hash64
{
	std::string format(std::uint64_t hash)
	{
		char text[17];
		std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
		return text;
	}

	bool parse(const std::string& text, std::uint64_t& outHash)
	{
		if (text.size() != 16)
		{
			return false;
		}
		std::uint64_t value = 0;
		for (const char c : text)
		{
			std::uint64_t digit = 0;
			if (c >= '0' && c <= '9')
			{
				digit = static_cast<std::uint64_t>(c - '0');
			}
			else if (c >= 'a' && c <= 'f')
			{
				digit = static_cast<std::uint64_t>(c - 'a' + 10);
			}
			else
			{
				return false;
			}
			value = (value << 4) | digit;
		}
		outHash = value;
		return true;
	}
}
