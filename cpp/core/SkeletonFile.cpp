#include "SkeletonFile.h"
#include "Constants.h"
#include "Hash64.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <tuple>

void sortSkeletonJoints(Skeleton& skeleton)
{
	std::sort(skeleton.joints.begin(), skeleton.joints.end(), [](const SkeletonJoint& a, const SkeletonJoint& b)
	{
		return std::tie(a.markup.side, a.markup.usage) < std::tie(b.markup.side, b.markup.usage);
	});
}

std::uint64_t skeletonHash(const Skeleton& skeleton)
{
	Skeleton sorted = skeleton;
	sortSkeletonJoints(sorted);

	Fnv1a64 hash;
	hash.addUInt32(static_cast<std::uint32_t>(sorted.joints.size()));
	for (const SkeletonJoint& joint : sorted.joints)
	{
		hash.addString(joint.markup.side);
		hash.addString(joint.markup.usage);
		hash.addUInt32(joint.markup.isExported ? 1u : 0u);
		hash.addUInt32(joint.markup.isSkinned ? 1u : 0u);
		hash.addString(joint.name);
		hash.addUInt32(joint.hasParent ? 1u : 0u);
		hash.addString(joint.hasParent ? joint.parent.side : std::string());
		hash.addString(joint.hasParent ? joint.parent.usage : std::string());
		hash.addUInt32(static_cast<std::uint32_t>(joint.rotateOrder));
		for (const double value : joint.restLocal)
		{
			hash.addInt64(std::llround(value / SkeletonFileFormat::RestGrid));
		}
	}
	return hash.value();
}

// Written by hand, like the weight file, so each joint is one line. Numbers go through std::to_chars, the shortest
// text that reads back as the same double. Strings go through json::dump, so escaping matches the parser.
std::string serializeSkeleton(const Skeleton& skeleton)
{
	Skeleton sorted = skeleton;
	sortSkeletonJoints(sorted);

	const auto quoted = [](const std::string& text)
	{
		return nlohmann::json(text).dump();
	};
	const auto key = [&quoted](const char* name)
	{
		return quoted(name) + ": ";
	};
	const auto selector = [&quoted](const std::string& side, const std::string& usage)
	{
		return "[" + quoted(side) + ", " + quoted(usage) + "]";
	};

	std::string out = "{\n";
	out += "    " + key(SkeletonFileKey::FormatVersion) + std::to_string(SkeletonFileFormat::CurrentVersion) + ",\n";
	out += "    " + key(SkeletonFileKey::SkeletonHash) + quoted(Hash64::format(skeletonHash(sorted))) + ",\n";
	out += "    " + key(SkeletonFileKey::Joints) + "[";
	for (std::size_t i = 0; i < sorted.joints.size(); ++i)
	{
		const SkeletonJoint& joint = sorted.joints[i];
		out += i == 0 ? "\n        {" : ",\n        {";
		out += key(SkeletonFileKey::Markup) + selector(joint.markup.side, joint.markup.usage) + ", ";
		out += key(JointMarkupKey::Export) + (joint.markup.isExported ? "true" : "false") + ", ";
		out += key(JointMarkupKey::Skin) + (joint.markup.isSkinned ? "true" : "false") + ", ";
		out += key(SkeletonFileKey::Name) + quoted(joint.name) + ", ";
		out += key(SkeletonFileKey::Parent) + (joint.hasParent ? selector(joint.parent.side, joint.parent.usage) : "null") + ", ";
		out += key(SkeletonFileKey::RotateOrder) + quoted(RotateOrders::All[static_cast<std::size_t>(joint.rotateOrder)]) + ", ";
		out += key(SkeletonFileKey::RestLocal) + "[";
		for (std::size_t v = 0; v < joint.restLocal.size(); ++v)
		{
			char buffer[32];
			const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), joint.restLocal[v]);
			out += v == 0 ? "" : ", ";
			out.append(buffer, result.ptr);
		}
		out += "]}";
	}
	out += sorted.joints.empty() ? "]" : "\n    ]";
	out += "\n}\n";
	return out;
}
