#include "SkinWeightFile.h"
#include "Constants.h"
#include "FormatUpdate.h"
#include "Hash64.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <numeric>
#include <set>
#include <tuple>

namespace
{
	// Version 2 added the skeleton's hash (D54). An older file doesn't know its skeleton, which "" says.
	void addSkeletonHash(nlohmann::json& data)
	{
		if (data.is_object() && !data.contains(SkinWeightFileKey::SkeletonHash))
		{
			data[SkinWeightFileKey::SkeletonHash] = "";
		}
	}

	// Version 3 added the skeleton's structure (D70). An older file has none, so a joint it names that is gone can't fold.
	void addSkeleton(nlohmann::json& data)
	{
		if (data.is_object() && !data.contains(SkinWeightFileKey::Skeleton))
		{
			data[SkinWeightFileKey::Skeleton] = nlohmann::json::array();
		}
	}

	bool isCount(const nlohmann::json& value)
	{
		return value.is_number_unsigned() || (value.is_number_integer() && value.get<long long>() >= 0);
	}

	// One vertex's [index, weight, ...] list. Reports problems under weightsPath[vertex].
	void parseVertexWeights(
		const nlohmann::json& entry,
		std::size_t influenceCount,
		const std::string& weightsPath,
		std::size_t vertex,
		std::vector<std::pair<unsigned int, double>>& outPairs,
		ValidationReport& report)
	{
		// Built only on an error: one string per vertex is a real cost on a dense mesh.
		const auto where = [&weightsPath, vertex]()
		{
			return weightsPath + "[" + std::to_string(vertex) + "]";
		};
		if (!entry.is_array() || entry.size() % 2 != 0)
		{
			report.addError(where(), "must be a list of influence index and weight pairs");
			return;
		}
		for (std::size_t i = 0; i < entry.size(); i += 2)
		{
			const nlohmann::json& index = entry[i];
			const nlohmann::json& weight = entry[i + 1];
			if (!isCount(index) || index.get<std::size_t>() >= influenceCount)
			{
				report.addError(where(), index.dump() + " is not an influence index");
				continue;
			}
			if (!weight.is_number() || weight.get<double>() < 0.0)
			{
				report.addError(where(), weight.dump() + " is not a weight of at least 0");
				continue;
			}
			const unsigned int influence = index.get<unsigned int>();
			const bool isListed = std::any_of(outPairs.begin(), outPairs.end(), [influence](const std::pair<unsigned int, double>& pair)
			{
				return pair.first == influence;
			});
			if (isListed)
			{
				report.addError(where(), "influence " + index.dump() + " is listed twice");
				continue;
			}
			outPairs.emplace_back(influence, weight.get<double>());
		}
	}
}

std::uint64_t topologyHash(unsigned int vertexCount, const std::vector<int>& polygonCounts, const std::vector<int>& polygonConnects)
{
	Fnv1a64 hash;
	hash.addUInt32(vertexCount);
	hash.addUInt32(static_cast<std::uint32_t>(polygonCounts.size()));
	for (const int count : polygonCounts)
	{
		hash.addUInt32(static_cast<std::uint32_t>(count));
	}
	for (const int vertex : polygonConnects)
	{
		hash.addUInt32(static_cast<std::uint32_t>(vertex));
	}
	return hash.value();
}

void setRigidSkinWeights(SkinWeights& weights, const JointSelector& influence)
{
	weights.skinMethod = SkinMethods::Linear;
	weights.influences = { influence };
	weights.weights.assign(weights.vertexCount, { { 0u, 1.0 } });
	weights.blendWeights.clear();
}

void sortSkinInfluences(SkinWeights& weights)
{
	std::vector<unsigned int> order(weights.influences.size());
	std::iota(order.begin(), order.end(), 0u);
	std::sort(order.begin(), order.end(), [&weights](unsigned int a, unsigned int b)
	{
		const JointSelector& left = weights.influences[a];
		const JointSelector& right = weights.influences[b];
		return std::tie(left.side, left.usage) < std::tie(right.side, right.usage);
	});

	std::vector<unsigned int> newIndex(order.size());
	std::vector<JointSelector> sorted;
	sorted.reserve(order.size());
	for (unsigned int i = 0; i < order.size(); ++i)
	{
		newIndex[order[i]] = i;
		sorted.push_back(weights.influences[order[i]]);
	}
	weights.influences = sorted;

	for (std::vector<std::pair<unsigned int, double>>& vertex : weights.weights)
	{
		for (std::pair<unsigned int, double>& pair : vertex)
		{
			pair.first = newIndex[pair.first];
		}
		std::sort(vertex.begin(), vertex.end());
	}
}

std::vector<unsigned int> findUnnormalizedVertices(const SkinWeights& weights)
{
	std::vector<unsigned int> vertices;
	for (std::size_t v = 0; v < weights.weights.size(); ++v)
	{
		double sum = 0.0;
		for (const auto& [index, weight] : weights.weights[v])
		{
			sum += weight;
		}
		if (std::abs(sum - 1.0) > SkinWeightFileFormat::NormalizeTolerance)
		{
			vertices.push_back(static_cast<unsigned int>(v));
		}
	}
	return vertices;
}

std::string describeUnnormalizedVertices(const SkinWeights& weights, const std::vector<unsigned int>& vertices)
{
	constexpr std::size_t kMaxListed{ 10 };
	std::string text = std::to_string(vertices.size()) + (vertices.size() == 1 ? " vertex is" : " vertices are") + " not normalized: ";
	for (std::size_t i = 0; i < vertices.size() && i < kMaxListed; ++i)
	{
		double sum = 0.0;
		for (const auto& [index, weight] : weights.weights[vertices[i]])
		{
			sum += weight;
		}
		text += (i == 0 ? "" : ", ") + std::to_string(vertices[i]) + " (sum " + nlohmann::json(sum).dump() + ")";
	}
	return text + (vertices.size() > kMaxListed ? ", ..." : "");
}

// Written by hand instead of json::dump(4), which puts every number on its own line. Here a vertex is one line.
// Numbers go through std::to_chars, the shortest text that reads back as the same double, which is also much faster
// than a json value per number. Keys and strings go through json::dump, so escaping matches the parser.
const UpdateTable& skinWeightUpdateTable()
{
	// Add a step here, and nowhere else, when the weight file changes shape or meaning. Step 0 takes version 1 to 2, step 1 takes 2 to 3.
	static const UpdateTable table{ 1, { &addSkeletonHash, &addSkeleton } };
	return table;
}

std::string serializeSkinWeights(const SkinWeights& weights)
{
	const auto quoted = [](const std::string& text)
	{
		return nlohmann::json(text).dump();
	};
	const auto key = [&quoted](const char* name)
	{
		return "    " + quoted(name) + ": ";
	};
	const auto appendNumber = [](std::string& out, double value)
	{
		char buffer[32];
		const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
		out.append(buffer, result.ptr);
	};

	std::string out = "{\n";
	out += key(SkinWeightFileKey::FormatVersion) + std::to_string(skinWeightUpdateTable().currentVersion()) + ",\n";
	out += key(SkinWeightFileKey::MeshId) + quoted(weights.meshId) + ",\n";
	out += key(SkinWeightFileKey::MeshName) + quoted(weights.meshName) + ",\n";
	out += key(SkinWeightFileKey::VertexCount) + std::to_string(weights.vertexCount) + ",\n";
	out += key(SkinWeightFileKey::FaceCount) + std::to_string(weights.faceCount) + ",\n";
	out += key(SkinWeightFileKey::TopologyHash) + quoted(Hash64::format(weights.topologyHash)) + ",\n";
	out += key(SkinWeightFileKey::SkeletonHash) + quoted(weights.skeletonHash) + ",\n";
	out += key(SkinWeightFileKey::SkinMethod) + quoted(weights.skinMethod) + ",\n";

	out += key(SkinWeightFileKey::Influences) + "[";
	for (std::size_t i = 0; i < weights.influences.size(); ++i)
	{
		out += std::string(i == 0 ? "" : ",") + "\n        " + nlohmann::json(weights.influences[i]).dump(-1, ' ', false);
	}
	out += std::string(weights.influences.empty() ? "" : "\n    ") + "],\n";

	out += key(SkinWeightFileKey::Skeleton) + "[";
	for (std::size_t i = 0; i < weights.skeleton.size(); ++i)
	{
		const SkeletonLink& link = weights.skeleton[i];
		out += std::string(i == 0 ? "" : ",") + "\n        [" + nlohmann::json(link.joint).dump(-1, ' ', false) + ", "
			+ (link.hasParent ? nlohmann::json(link.parent).dump(-1, ' ', false) : std::string("null")) + "]";
	}
	out += std::string(weights.skeleton.empty() ? "" : "\n    ") + "],\n";

	// Room for about four pairs per vertex, so a dense mesh's text grows without repeated reallocation.
	out.reserve(out.size() + weights.weights.size() * 96 + weights.blendWeights.size() * 32);
	out += key(SkinWeightFileKey::Weights) + "[";
	for (std::size_t v = 0; v < weights.weights.size(); ++v)
	{
		out += v == 0 ? "\n        [" : ",\n        [";
		bool isFirst = true;
		for (const auto& [index, weight] : weights.weights[v])
		{
			if (!isFirst)
			{
				out += ", ";
			}
			out += std::to_string(index);
			out += ", ";
			appendNumber(out, weight);
			isFirst = false;
		}
		out += "]";
	}
	out += weights.weights.empty() ? "]" : "\n    ]";

	// One value per line, like the weights, so changing one vertex's blend weight changes one line.
	if (!weights.blendWeights.empty())
	{
		out += ",\n" + key(SkinWeightFileKey::BlendWeights) + "[";
		for (std::size_t v = 0; v < weights.blendWeights.size(); ++v)
		{
			out += v == 0 ? "\n        " : ",\n        ";
			appendNumber(out, weights.blendWeights[v]);
		}
		out += "\n    ]";
	}
	out += "\n}\n";
	return out;
}

void deserializeSkinWeights(const std::string& text, SkinWeights& outWeights, ValidationReport& report)
{
	const std::string where = "weight file";
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
	if (!file.is_object() || !file.contains(SkinWeightFileKey::FormatVersion))
	{
		report.addError(where, std::string("must be a JSON object with ") + SkinWeightFileKey::FormatVersion);
		return;
	}
	const nlohmann::json& version = file.at(SkinWeightFileKey::FormatVersion);
	if (!version.is_number_integer())
	{
		report.addError(path(SkinWeightFileKey::FormatVersion), "must be a whole number, not " + version.dump());
		return;
	}
	if (!FormatUpdate::runUpdates(skinWeightUpdateTable(), version.get<int>(), file, path(SkinWeightFileKey::FormatVersion), report))
	{
		return;
	}

	const std::size_t errorCount = report.errors.size();
	RigTemplateValidation::checkKeys(file,
		{
			SkinWeightFileKey::FormatVersion,
			SkinWeightFileKey::MeshId,
			SkinWeightFileKey::MeshName,
			SkinWeightFileKey::VertexCount,
			SkinWeightFileKey::FaceCount,
			SkinWeightFileKey::TopologyHash,
			SkinWeightFileKey::SkeletonHash,
			SkinWeightFileKey::SkinMethod,
			SkinWeightFileKey::Influences,
			SkinWeightFileKey::Skeleton,
			SkinWeightFileKey::Weights
		},
		{ SkinWeightFileKey::BlendWeights }, where, report);
	if (report.errors.size() != errorCount)
	{
		return;
	}

	const nlohmann::json& meshId = file.at(SkinWeightFileKey::MeshId);
	if (!meshId.is_string() || meshId.get<std::string>().empty())
	{
		report.addError(path(SkinWeightFileKey::MeshId), "must be a non-empty string");
	}
	const nlohmann::json& meshName = file.at(SkinWeightFileKey::MeshName);
	if (!meshName.is_string())
	{
		report.addError(path(SkinWeightFileKey::MeshName), "must be a string");
	}

	const nlohmann::json& vertexCount = file.at(SkinWeightFileKey::VertexCount);
	const nlohmann::json& faceCount = file.at(SkinWeightFileKey::FaceCount);
	for (const auto& [key, value] : { std::pair{ SkinWeightFileKey::VertexCount, &vertexCount }, std::pair{ SkinWeightFileKey::FaceCount, &faceCount } })
	{
		if (!isCount(*value))
		{
			report.addError(path(key), "must be a whole number of at least 0");
		}
	}

	const nlohmann::json& hash = file.at(SkinWeightFileKey::TopologyHash);
	std::uint64_t parsedHash = 0;
	if (!hash.is_string() || !Hash64::parse(hash.get<std::string>(), parsedHash))
	{
		report.addError(path(SkinWeightFileKey::TopologyHash), "must be 16 lowercase hex digits");
	}
	// Recorded, and a status only (D70): a load whose hash isn't the rig's still loads.
	const nlohmann::json& skeletonHash = file.at(SkinWeightFileKey::SkeletonHash);
	std::uint64_t parsedSkeletonHash = 0;
	if (!skeletonHash.is_string()
		|| (!skeletonHash.get<std::string>().empty() && !Hash64::parse(skeletonHash.get<std::string>(), parsedSkeletonHash)))
	{
		report.addError(path(SkinWeightFileKey::SkeletonHash), "must be \"\" or 16 lowercase hex digits");
	}

	const nlohmann::json& skinMethod = file.at(SkinWeightFileKey::SkinMethod);
	const bool isMethod = skinMethod.is_string() && SkinMethods::indexOf(skinMethod.get<std::string>()) >= 0;
	if (!isMethod)
	{
		report.addError(path(SkinWeightFileKey::SkinMethod),
			skinMethod.dump() + " is not one of " + SkinMethods::Linear + ", " + SkinMethods::DualQuaternion + ", " + SkinMethods::WeightBlended);
	}

	const nlohmann::json& influences = file.at(SkinWeightFileKey::Influences);
	const std::string influencesPath = path(SkinWeightFileKey::Influences);
	std::vector<JointSelector> parsedInfluences;
	if (!influences.is_array())
	{
		report.addError(influencesPath, "must be a list of [side, usage]");
	}
	else
	{
		std::set<std::pair<std::string, std::string>> seen;
		for (std::size_t i = 0; i < influences.size(); ++i)
		{
			const std::string entryPath = influencesPath + "[" + std::to_string(i) + "]";
			const nlohmann::json& entry = influences[i];
			if (!entry.is_array() || entry.size() != 2 || !entry[0].is_string() || !entry[1].is_string())
			{
				report.addError(entryPath, "must be [side, usage], got " + entry.dump());
				continue;
			}
			JointSelector selector{ entry[0].get<std::string>(), entry[1].get<std::string>() };
			if (!isValidSideName(selector.side))
			{
				report.addError(entryPath, "'" + selector.side + "' is not one of " + SideNames::Center + ", " + SideNames::Left + ", " + SideNames::Right);
			}
			if (selector.usage.empty())
			{
				report.addError(entryPath, "usage is empty");
			}
			if (!seen.insert({ selector.side, selector.usage }).second)
			{
				report.addError(entryPath, formatJointSelector(selector) + " is listed twice");
			}
			parsedInfluences.push_back(selector);
		}
	}

	const nlohmann::json& skeleton = file.at(SkinWeightFileKey::Skeleton);
	const std::string skeletonPath = path(SkinWeightFileKey::Skeleton);
	std::vector<SkeletonLink> parsedSkeleton;
	if (!skeleton.is_array())
	{
		report.addError(skeletonPath, "must be a list of [[side, usage], [side, usage] or null]");
	}
	else
	{
		std::set<std::pair<std::string, std::string>> seenJoints;
		const auto readSelector = [](const nlohmann::json& entry, JointSelector& out)
		{
			if (!entry.is_array() || entry.size() != 2 || !entry[0].is_string() || !entry[1].is_string())
			{
				return false;
			}
			out = { entry[0].get<std::string>(), entry[1].get<std::string>() };
			return isValidSideName(out.side) && !out.usage.empty();
		};
		for (std::size_t i = 0; i < skeleton.size(); ++i)
		{
			const std::string entryPath = skeletonPath + "[" + std::to_string(i) + "]";
			const nlohmann::json& entry = skeleton[i];
			SkeletonLink link;
			if (!entry.is_array() || entry.size() != 2 || !readSelector(entry[0], link.joint))
			{
				report.addError(entryPath, "must be [[side, usage], parent], got " + entry.dump());
				continue;
			}
			link.hasParent = !entry[1].is_null();
			if (link.hasParent && !readSelector(entry[1], link.parent))
			{
				report.addError(entryPath, "the parent must be [side, usage] or null, got " + entry[1].dump());
				continue;
			}
			if (!seenJoints.insert({ link.joint.side, link.joint.usage }).second)
			{
				report.addError(entryPath, formatJointSelector(link.joint) + " is listed twice");
				continue;
			}
			parsedSkeleton.push_back(link);
		}
	}

	if (report.errors.size() != errorCount)
	{
		return;
	}
	const unsigned int parsedVertexCount = vertexCount.get<unsigned int>();

	const nlohmann::json& weightList = file.at(SkinWeightFileKey::Weights);
	const std::string weightsPath = path(SkinWeightFileKey::Weights);
	std::vector<std::vector<std::pair<unsigned int, double>>> parsedWeights;
	if (!weightList.is_array() || weightList.size() != parsedVertexCount)
	{
		report.addError(weightsPath, "must be a list with one entry per vertex (" + std::to_string(parsedVertexCount) + ")");
	}
	else
	{
		parsedWeights.resize(parsedVertexCount);
		for (std::size_t v = 0; v < weightList.size(); ++v)
		{
			parseVertexWeights(weightList[v], parsedInfluences.size(), weightsPath, v, parsedWeights[v], report);
		}
	}

	const bool isWeightBlended = skinMethod.get<std::string>() == SkinMethods::WeightBlended;
	const std::string blendPath = path(SkinWeightFileKey::BlendWeights);
	std::vector<double> parsedBlend;
	if (file.contains(SkinWeightFileKey::BlendWeights))
	{
		const nlohmann::json& blend = file.at(SkinWeightFileKey::BlendWeights);
		if (!isWeightBlended)
		{
			report.addError(blendPath, std::string("is only for ") + SkinMethods::WeightBlended);
		}
		else if (!blend.is_array() || blend.size() != parsedVertexCount)
		{
			report.addError(blendPath, "must be a list with one number per vertex (" + std::to_string(parsedVertexCount) + ")");
		}
		else
		{
			for (const nlohmann::json& value : blend)
			{
				if (!value.is_number())
				{
					report.addError(blendPath, value.dump() + " is not a number");
					break;
				}
				parsedBlend.push_back(value.get<double>());
			}
		}
	}
	else if (isWeightBlended)
	{
		report.addError(where, std::string("needs ") + SkinWeightFileKey::BlendWeights + " for " + SkinMethods::WeightBlended);
	}

	if (report.errors.size() != errorCount)
	{
		return;
	}

	outWeights.meshId = meshId.get<std::string>();
	outWeights.meshName = meshName.get<std::string>();
	outWeights.vertexCount = parsedVertexCount;
	outWeights.faceCount = faceCount.get<unsigned int>();
	outWeights.topologyHash = parsedHash;
	outWeights.skeletonHash = skeletonHash.get<std::string>();
	outWeights.skinMethod = skinMethod.get<std::string>();
	outWeights.influences = std::move(parsedInfluences);
	outWeights.skeleton = std::move(parsedSkeleton);
	outWeights.weights = std::move(parsedWeights);
	outWeights.blendWeights = std::move(parsedBlend);

	// Loading never normalizes, so a file whose weights don't sum to 1 is refused, not fixed.
	const std::vector<unsigned int> unnormalized = findUnnormalizedVertices(outWeights);
	if (!unnormalized.empty())
	{
		report.addError(path(SkinWeightFileKey::Weights), describeUnnormalizedVertices(outWeights, unnormalized));
	}
}
