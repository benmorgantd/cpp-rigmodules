#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// The 64-bit hash behind every content hash a file stores: a mesh's topology and a skeleton.
// FNV-1a is a fixed algorithm, unlike std::hash, so every build and machine gets the same value.

#include <cstdint>
#include <string>

class Fnv1a64
{
public:
	// 4 little-endian bytes.
	void addUInt32(std::uint32_t value);
	// 8 little-endian bytes, two's complement.
	void addInt64(std::int64_t value);
	// The length as addUInt32, then the bytes, so "ab", "c" and "a", "bc" hash differently.
	void addString(const std::string& text);

	std::uint64_t value() const;

private:
	void addByte(std::uint8_t byte);

	std::uint64_t fHash{ 14695981039346656037ull };
};

namespace Hash64
{
	// "0123456789abcdef": 16 lowercase hex digits. JSON numbers can't hold 64 bits safely, so files store this text.
	std::string format(std::uint64_t hash);

	// The reverse of format. False for anything but 16 lowercase hex digits.
	bool parse(const std::string& text, std::uint64_t& outHash);
}
