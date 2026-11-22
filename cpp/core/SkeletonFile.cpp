#include "SkeletonFile.h"
#include "Constants.h"
#include "FormatUpdate.h"
#include "Hash64.h"
#include "RigTemplateValidation.h"
#include "StringUtils.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <tuple>
#include <utility>

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
	out += "    " + key(SkeletonFileKey::FormatVersion) + std::to_string(skeletonUpdateTable().currentVersion()) + ",\n";
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

const UpdateTable& skeletonUpdateTable()
{
	// Add a step here, and nowhere else, when the skeleton file changes shape or meaning.
	static const UpdateTable table{ 1, {} };
	return table;
}

int findSkeletonJoint(const Skeleton& skeleton, const JointSelector& selector)
{
	for (std::size_t i = 0; i < skeleton.joints.size(); ++i)
	{
		const JointMarkup& markup = skeleton.joints[i].markup;
		if (markup.side == selector.side && markup.usage == selector.usage)
		{
			return static_cast<int>(i);
		}
	}
	return -1;
}

std::vector<std::size_t> findTopSkinJoints(const Skeleton& skeleton)
{
	std::vector<std::size_t> tops;
	for (std::size_t i = 0; i < skeleton.joints.size(); ++i)
	{
		if (!skeleton.joints[i].markup.isSkinned)
		{
			continue;
		}
		// Walk up to the root. A valid skeleton has no loop, so the walk ends.
		bool hasSkinAncestor = false;
		const SkeletonJoint* joint = &skeleton.joints[i];
		while (joint->hasParent && !hasSkinAncestor)
		{
			const int parent = findSkeletonJoint(skeleton, joint->parent);
			if (parent < 0)
			{
				break;
			}
			joint = &skeleton.joints[static_cast<std::size_t>(parent)];
			hasSkinAncestor = joint->markup.isSkinned;
		}
		if (!hasSkinAncestor)
		{
			tops.push_back(i);
		}
	}
	return tops;
}

std::vector<std::string> describeSkeletonDifferences(const Skeleton& rig, const Skeleton& look)
{
	std::vector<std::string> lines;
	const auto describeJoint = [](const SkeletonJoint& joint)
	{
		return formatJointSelector({ joint.markup.side, joint.markup.usage });
	};

	for (const SkeletonJoint& rigJoint : rig.joints)
	{
		const std::string who = describeJoint(rigJoint) + ": ";
		const int index = findSkeletonJoint(look, { rigJoint.markup.side, rigJoint.markup.usage });
		if (index < 0)
		{
			lines.push_back(who + "the rig has it, the look's skeleton does not");
			continue;
		}
		const SkeletonJoint& lookJoint = look.joints[static_cast<std::size_t>(index)];
		if (rigJoint.name != lookJoint.name)
		{
			lines.push_back(who + "named '" + rigJoint.name + "' in the rig and '" + lookJoint.name + "' in the look");
		}
		if (rigJoint.hasParent != lookJoint.hasParent
			|| (rigJoint.hasParent && (rigJoint.parent.side != lookJoint.parent.side || rigJoint.parent.usage != lookJoint.parent.usage)))
		{
			lines.push_back(who + "a different parent");
		}
		if (rigJoint.rotateOrder != lookJoint.rotateOrder)
		{
			lines.push_back(who + "a different rotate order");
		}
		if (rigJoint.markup.isExported != lookJoint.markup.isExported || rigJoint.markup.isSkinned != lookJoint.markup.isSkinned)
		{
			lines.push_back(who + "a different export or skin flag");
		}
		for (std::size_t i = 0; i < rigJoint.restLocal.size(); ++i)
		{
			if (std::llround(rigJoint.restLocal[i] / SkeletonFileFormat::RestGrid) != std::llround(lookJoint.restLocal[i] / SkeletonFileFormat::RestGrid))
			{
				lines.push_back(who + "a different rest transform");
				break;
			}
		}
	}
	for (const SkeletonJoint& lookJoint : look.joints)
	{
		if (findSkeletonJoint(rig, { lookJoint.markup.side, lookJoint.markup.usage }) < 0)
		{
			lines.push_back(describeJoint(lookJoint) + ": the look's skeleton has it, the rig does not");
		}
	}
	return lines;
}

std::vector<std::size_t> skeletonParentsFirst(const Skeleton& skeleton)
{
	const std::size_t count = skeleton.joints.size();
	std::vector<int> parents(count, -1);
	for (std::size_t i = 0; i < count; ++i)
	{
		if (skeleton.joints[i].hasParent)
		{
			parents[i] = findSkeletonJoint(skeleton, skeleton.joints[i].parent);
		}
	}

	// Each pass takes the joints whose parent is already placed. A valid skeleton places at least one per pass.
	std::vector<std::size_t> order;
	std::vector<bool> placed(count, false);
	order.reserve(count);
	bool progressed = true;
	while (order.size() < count && progressed)
	{
		progressed = false;
		for (std::size_t i = 0; i < count; ++i)
		{
			if (!placed[i] && (parents[i] < 0 || placed[static_cast<std::size_t>(parents[i])]))
			{
				placed[i] = true;
				order.push_back(i);
				progressed = true;
			}
		}
	}
	return order;
}

namespace
{
	// A [side, usage] pair with a known side and a usage. Reports what is wrong at where.
	bool parseSelector(const nlohmann::json& entry, const std::string& where, JointSelector& outSelector, ValidationReport& report)
	{
		if (!entry.is_array() || entry.size() != 2 || !entry[0].is_string() || !entry[1].is_string())
		{
			report.addError(where, "must be [side, usage], got " + entry.dump());
			return false;
		}
		outSelector = JointSelector{ entry[0].get<std::string>(), entry[1].get<std::string>() };
		if (!isValidSideName(outSelector.side))
		{
			report.addError(where, "'" + outSelector.side + "' is not one of " + SideNames::Center + ", " + SideNames::Left + ", " + SideNames::Right);
			return false;
		}
		if (outSelector.usage.empty())
		{
			report.addError(where, "usage is empty");
			return false;
		}
		return true;
	}

	// One entry of "joints". False, with every problem reported, when the joint can't be used.
	bool parseSkeletonJoint(const nlohmann::json& entry, const std::string& where, SkeletonJoint& outJoint, ValidationReport& report)
	{
		const std::size_t errorCount = report.errors.size();
		RigTemplateValidation::checkKeys(entry,
			{
				SkeletonFileKey::Markup,
				JointMarkupKey::Export,
				JointMarkupKey::Skin,
				SkeletonFileKey::Name,
				SkeletonFileKey::Parent,
				SkeletonFileKey::RotateOrder,
				SkeletonFileKey::RestLocal
			},
			{}, where, report);
		if (report.errors.size() != errorCount)
		{
			return false;
		}
		const auto path = [&where](const char* key)
		{
			return where + "." + key;
		};

		JointSelector markup;
		if (parseSelector(entry.at(SkeletonFileKey::Markup), path(SkeletonFileKey::Markup), markup, report))
		{
			outJoint.markup.side = markup.side;
			outJoint.markup.usage = markup.usage;
		}

		const auto readFlag = [&entry, &path, &report](const char* key, bool& outValue)
		{
			const nlohmann::json& value = entry.at(key);
			if (!value.is_boolean())
			{
				report.addError(path(key), "must be true or false");
				return;
			}
			outValue = value.get<bool>();
		};
		readFlag(JointMarkupKey::Export, outJoint.markup.isExported);
		readFlag(JointMarkupKey::Skin, outJoint.markup.isSkinned);

		const nlohmann::json& name = entry.at(SkeletonFileKey::Name);
		if (!name.is_string() || name.get<std::string>().empty())
		{
			report.addError(path(SkeletonFileKey::Name), "must be a non-empty string");
		}
		else
		{
			outJoint.name = name.get<std::string>();
		}

		const nlohmann::json& parent = entry.at(SkeletonFileKey::Parent);
		outJoint.hasParent = !parent.is_null();
		if (outJoint.hasParent)
		{
			parseSelector(parent, path(SkeletonFileKey::Parent), outJoint.parent, report);
		}

		const nlohmann::json& rotateOrder = entry.at(SkeletonFileKey::RotateOrder);
		const auto found = rotateOrder.is_string()
			? std::find(RotateOrders::All.begin(), RotateOrders::All.end(), rotateOrder.get<std::string>())
			: RotateOrders::All.end();
		if (found == RotateOrders::All.end())
		{
			report.addError(path(SkeletonFileKey::RotateOrder), "must be one of xyz, yzx, zxy, xzy, yxz, zyx, got " + rotateOrder.dump());
		}
		else
		{
			outJoint.rotateOrder = static_cast<int>(found - RotateOrders::All.begin());
		}

		const nlohmann::json& restLocal = entry.at(SkeletonFileKey::RestLocal);
		const bool isMatrix = restLocal.is_array() && restLocal.size() == outJoint.restLocal.size()
			&& std::all_of(restLocal.begin(), restLocal.end(), [](const nlohmann::json& value)
			{
				return value.is_number();
			});
		if (!isMatrix)
		{
			report.addError(path(SkeletonFileKey::RestLocal), "must be 16 numbers");
		}
		else
		{
			for (std::size_t v = 0; v < outJoint.restLocal.size(); ++v)
			{
				outJoint.restLocal[v] = restLocal[v].get<double>();
			}
		}

		return report.errors.size() == errorCount;
	}
}

void deserializeSkeleton(const std::string& text, Skeleton& outSkeleton, ValidationReport& report)
{
	const std::string where = "skeleton file";
	nlohmann::json file;
	try
	{
		file = nlohmann::json::parse(text);
	}
	catch (const nlohmann::json::exception& e)
	{
		report.addError(where, e.what());
		return;
	}

	const auto path = [&where](const char* key)
	{
		return where + "." + key;
	};

	// The update steps run before the key check, so the check sees the current shape.
	if (!file.is_object() || !file.contains(SkeletonFileKey::FormatVersion))
	{
		report.addError(where, std::string("must be a JSON object with ") + SkeletonFileKey::FormatVersion);
		return;
	}
	const nlohmann::json& version = file.at(SkeletonFileKey::FormatVersion);
	if (!version.is_number_integer())
	{
		report.addError(path(SkeletonFileKey::FormatVersion), "must be a whole number, not " + version.dump());
		return;
	}
	if (!FormatUpdate::runUpdates(skeletonUpdateTable(), version.get<int>(), file, path(SkeletonFileKey::FormatVersion), report))
	{
		return;
	}

	const std::size_t errorCount = report.errors.size();
	RigTemplateValidation::checkKeys(file,
		{ SkeletonFileKey::FormatVersion, SkeletonFileKey::SkeletonHash, SkeletonFileKey::Joints },
		{}, where, report);
	if (report.errors.size() != errorCount)
	{
		return;
	}

	const nlohmann::json& joints = file.at(SkeletonFileKey::Joints);
	const std::string jointsPath = path(SkeletonFileKey::Joints);
	if (!joints.is_array())
	{
		report.addError(jointsPath, "must be a list of joints");
		return;
	}

	Skeleton skeleton;
	std::set<std::pair<std::string, std::string>> seen;
	for (std::size_t i = 0; i < joints.size(); ++i)
	{
		const std::string jointPath = jointsPath + "[" + std::to_string(i) + "]";
		SkeletonJoint joint;
		if (!parseSkeletonJoint(joints[i], jointPath, joint, report))
		{
			continue;
		}
		if (!seen.insert({ joint.markup.side, joint.markup.usage }).second)
		{
			report.addError(jointPath, formatJointSelector({ joint.markup.side, joint.markup.usage }) + " is listed twice");
			continue;
		}
		skeleton.joints.push_back(joint);
	}
	if (report.errors.size() != errorCount)
	{
		return;
	}

	sortSkeletonJoints(skeleton);
	for (const SkeletonJoint& joint : skeleton.joints)
	{
		if (joint.hasParent && findSkeletonJoint(skeleton, joint.parent) < 0)
		{
			report.addError(jointsPath, joint.name + ": its parent " + formatJointSelector(joint.parent) + " is not a joint in the file");
		}
	}
	if (report.errors.size() != errorCount)
	{
		return;
	}
	// Every parent exists, so the only joints the ordering can't place are those in a loop of parents.
	const std::vector<std::size_t> order = skeletonParentsFirst(skeleton);
	if (order.size() != skeleton.joints.size())
	{
		std::vector<bool> placed(skeleton.joints.size(), false);
		for (const std::size_t index : order)
		{
			placed[index] = true;
		}
		std::vector<std::string> looped;
		for (std::size_t i = 0; i < skeleton.joints.size(); ++i)
		{
			if (!placed[i])
			{
				looped.push_back(skeleton.joints[i].name);
			}
		}
		report.addError(jointsPath, "these joints' parents form a loop: " + StringUtils::joinNames(looped));
		return;
	}

	const nlohmann::json& storedHash = file.at(SkeletonFileKey::SkeletonHash);
	const std::string computedHash = Hash64::format(skeletonHash(skeleton));
	if (!storedHash.is_string() || storedHash.get<std::string>() != computedHash)
	{
		report.addWarning(path(SkeletonFileKey::SkeletonHash), "is " + storedHash.dump() + " but the joints hash to \"" + computedHash
			+ "\". The file was edited outside saveRigSkeleton: the joints are used, and the next save writes the right hash");
	}

	outSkeleton = std::move(skeleton);
}
