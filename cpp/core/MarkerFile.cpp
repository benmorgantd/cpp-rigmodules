#include "MarkerFile.h"
#include "Constants.h"
#include "FormatUpdate.h"
#include "Hash64.h"
#include "RigTemplateValidation.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <tuple>
#include <utility>

void sortMarkers(MarkerSet& set)
{
	std::sort(set.markers.begin(), set.markers.end(), [](const Marker& a, const Marker& b)
	{
		return std::tie(a.markup.side, a.markup.usage) < std::tie(b.markup.side, b.markup.usage);
	});
}

std::uint64_t markersHash(const MarkerSet& set)
{
	MarkerSet sorted = set;
	sortMarkers(sorted);

	Fnv1a64 hash;
	hash.addUInt32(static_cast<std::uint32_t>(sorted.markers.size()));
	for (const Marker& marker : sorted.markers)
	{
		hash.addString(marker.markup.side);
		hash.addString(marker.markup.usage);
		hash.addString(marker.joint.side);
		hash.addString(marker.joint.usage);
		hash.addUInt32(marker.isExported ? 1u : 0u);
		hash.addString(marker.name);
		for (const double value : marker.offsetLocal)
		{
			hash.addInt64(std::llround(value / MarkerFileFormat::OffsetGrid));
		}
	}
	return hash.value();
}

// Written by hand, like the skeleton file, so each marker is one line. Numbers go through std::to_chars, the shortest
// text that reads back as the same double. Strings go through json::dump, so escaping matches the parser.
std::string serializeMarkers(const MarkerSet& set)
{
	MarkerSet sorted = set;
	sortMarkers(sorted);

	const auto quoted = [](const std::string& text)
	{
		return nlohmann::json(text).dump();
	};
	const auto key = [&quoted](const char* name)
	{
		return quoted(name) + ": ";
	};
	const auto selector = [&quoted](const JointSelector& value)
	{
		return "[" + quoted(value.side) + ", " + quoted(value.usage) + "]";
	};

	std::string out = "{\n";
	out += "    " + key(MarkerFileKey::FormatVersion) + std::to_string(markerUpdateTable().currentVersion()) + ",\n";
	out += "    " + key(MarkerFileKey::MarkersHash) + quoted(Hash64::format(markersHash(sorted))) + ",\n";
	out += "    " + key(MarkerFileKey::Markers) + "[";
	for (std::size_t i = 0; i < sorted.markers.size(); ++i)
	{
		const Marker& marker = sorted.markers[i];
		out += i == 0 ? "\n        {" : ",\n        {";
		out += key(SkeletonFileKey::Markup) + selector(marker.markup) + ", ";
		out += key(MarkerFileKey::Joint) + selector(marker.joint) + ", ";
		out += key(JointMarkupKey::Export) + (marker.isExported ? "true" : "false") + ", ";
		out += key(SkeletonFileKey::Name) + quoted(marker.name) + ", ";
		out += key(MarkerFileKey::OffsetLocal) + "[";
		for (std::size_t v = 0; v < marker.offsetLocal.size(); ++v)
		{
			char buffer[32];
			const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), marker.offsetLocal[v]);
			out += v == 0 ? "" : ", ";
			out.append(buffer, result.ptr);
		}
		out += "]}";
	}
	out += sorted.markers.empty() ? "]" : "\n    ]";
	out += "\n}\n";
	return out;
}

const UpdateTable& markerUpdateTable()
{
	// Add a step here, and nowhere else, when the marker file changes shape or meaning.
	static const UpdateTable table{ 1, {} };
	return table;
}

int findMarker(const MarkerSet& set, const JointSelector& selector)
{
	for (std::size_t i = 0; i < set.markers.size(); ++i)
	{
		const JointSelector& markup = set.markers[i].markup;
		if (markup.side == selector.side && markup.usage == selector.usage)
		{
			return static_cast<int>(i);
		}
	}
	return -1;
}

namespace
{
	// One entry of "markers". False, with every problem reported, when the marker can't be used.
	bool parseMarker(const nlohmann::json& entry, const std::string& where, Marker& outMarker, ValidationReport& report)
	{
		const std::size_t errorCount = report.errors.size();
		RigTemplateValidation::checkKeys(entry,
			{
				SkeletonFileKey::Markup,
				MarkerFileKey::Joint,
				JointMarkupKey::Export,
				SkeletonFileKey::Name,
				MarkerFileKey::OffsetLocal
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

		RigTemplateValidation::parseSelector(entry.at(SkeletonFileKey::Markup), path(SkeletonFileKey::Markup), outMarker.markup, report);
		RigTemplateValidation::parseSelector(entry.at(MarkerFileKey::Joint), path(MarkerFileKey::Joint), outMarker.joint, report);

		const nlohmann::json& isExported = entry.at(JointMarkupKey::Export);
		if (!isExported.is_boolean())
		{
			report.addError(path(JointMarkupKey::Export), "must be true or false");
		}
		else
		{
			outMarker.isExported = isExported.get<bool>();
		}

		const nlohmann::json& name = entry.at(SkeletonFileKey::Name);
		if (!name.is_string() || name.get<std::string>().empty())
		{
			report.addError(path(SkeletonFileKey::Name), "must be a non-empty string");
		}
		else
		{
			outMarker.name = name.get<std::string>();
		}

		const nlohmann::json& offset = entry.at(MarkerFileKey::OffsetLocal);
		const bool isMatrix = offset.is_array() && offset.size() == outMarker.offsetLocal.size()
			&& std::all_of(offset.begin(), offset.end(), [](const nlohmann::json& value)
			{
				return value.is_number();
			});
		if (!isMatrix)
		{
			report.addError(path(MarkerFileKey::OffsetLocal), "must be 16 numbers");
		}
		else
		{
			for (std::size_t v = 0; v < outMarker.offsetLocal.size(); ++v)
			{
				outMarker.offsetLocal[v] = offset[v].get<double>();
			}
		}

		return report.errors.size() == errorCount;
	}
}

void deserializeMarkers(const std::string& text, MarkerSet& outSet, ValidationReport& report)
{
	const std::string where = "markers file";
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
	if (!file.is_object() || !file.contains(MarkerFileKey::FormatVersion))
	{
		report.addError(where, std::string("must be a JSON object with ") + MarkerFileKey::FormatVersion);
		return;
	}
	const nlohmann::json& version = file.at(MarkerFileKey::FormatVersion);
	if (!version.is_number_integer())
	{
		report.addError(path(MarkerFileKey::FormatVersion), "must be a whole number, not " + version.dump());
		return;
	}
	if (!FormatUpdate::runUpdates(markerUpdateTable(), version.get<int>(), file, path(MarkerFileKey::FormatVersion), report))
	{
		return;
	}

	const std::size_t errorCount = report.errors.size();
	RigTemplateValidation::checkKeys(file,
		{ MarkerFileKey::FormatVersion, MarkerFileKey::MarkersHash, MarkerFileKey::Markers },
		{}, where, report);
	if (report.errors.size() != errorCount)
	{
		return;
	}

	const nlohmann::json& markers = file.at(MarkerFileKey::Markers);
	const std::string markersPath = path(MarkerFileKey::Markers);
	if (!markers.is_array())
	{
		report.addError(markersPath, "must be a list of markers");
		return;
	}

	MarkerSet set;
	std::set<std::pair<std::string, std::string>> seen;
	for (std::size_t i = 0; i < markers.size(); ++i)
	{
		const std::string markerPath = markersPath + "[" + std::to_string(i) + "]";
		Marker marker;
		if (!parseMarker(markers[i], markerPath, marker, report))
		{
			continue;
		}
		if (!seen.insert({ marker.markup.side, marker.markup.usage }).second)
		{
			report.addError(markerPath, formatJointSelector(marker.markup) + " is listed twice");
			continue;
		}
		set.markers.push_back(marker);
	}
	if (report.errors.size() != errorCount)
	{
		return;
	}

	sortMarkers(set);
	const nlohmann::json& storedHash = file.at(MarkerFileKey::MarkersHash);
	const std::string computedHash = Hash64::format(markersHash(set));
	if (!storedHash.is_string() || storedHash.get<std::string>() != computedHash)
	{
		report.addWarning(path(MarkerFileKey::MarkersHash), "is " + storedHash.dump() + " but the markers hash to \"" + computedHash
			+ "\". The file was edited outside saveRigMarkers: the markers are used, and the next save writes the right hash");
	}

	outSet = std::move(set);
}
