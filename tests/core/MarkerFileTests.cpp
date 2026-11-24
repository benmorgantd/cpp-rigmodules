// core/MarkerFile: the file's read and write, every parse error, the hash and its version table (D73, design/markers.md).

#include "CoreTests.h"

#include "core/FormatUpdate.h"
#include "core/Hash64.h"
#include "core/MarkerFile.h"
#include "core/RigTemplateValidation.h"

#include <algorithm>
#include <sstream>

namespace
{
	Marker makeMarker(const char* side, const char* usage, const char* jointSide, const char* jointUsage, const char* name, double y)
	{
		Marker marker;
		marker.markup = { side, usage };
		marker.joint = { jointSide, jointUsage };
		marker.name = name;
		marker.offsetLocal[13] = y;
		return marker;
	}

	// Deliberately not in (side, usage) order.
	MarkerSet makeSet()
	{
		MarkerSet set;
		set.markers.push_back(makeMarker("Right", "heel", "Right", "ankle", "heel_00_R_MRK", -9.5));
		set.markers.push_back(makeMarker("Left", "heel", "Left", "ankle", "heel_00_L_MRK", -9.5));
		set.markers.push_back(makeMarker("Left", "ball", "Left", "ankle", "ball_00_L_MRK", -8.7));
		set.markers[2].isExported = true;
		return set;
	}

	struct Parsed
	{
		MarkerSet set;
		ValidationReport report;
	};

	Parsed parse(const std::string& text)
	{
		Parsed parsed;
		deserializeMarkers(text, parsed.set, parsed.report);
		return parsed;
	}

	bool anyContains(const std::vector<std::string>& lines, const std::string& part)
	{
		return std::any_of(lines.begin(), lines.end(), [&part](const std::string& line)
		{
			return line.find(part) != std::string::npos;
		});
	}

	// One marker entry with the given pieces replaced, for the parse-error cases.
	std::string fileWith(const std::string& marker)
	{
		return "{\"formatVersion\": 1, \"markersHash\": \"x\", \"markers\": [" + marker + "]}";
	}

	const char* GoodMarker = "{\"markup\": [\"Left\", \"heel\"], \"joint\": [\"Left\", \"ankle\"], \"export\": false, \"name\": \"heel_00_L_MRK\", "
		"\"offsetLocal\": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -9.5, -1.2, 1]}";

	void checkSingleError(const std::string& text, const std::string& part)
	{
		const Parsed parsed = parse(text);
		CHECK(parsed.report.hasErrors());
		CHECK(anyContains(parsed.report.errors, part));
		CHECK(parsed.set.markers.empty());
	}
}

CORE_TEST(markerFileRoundTrips)
{
	const MarkerSet set = makeSet();
	const Parsed parsed = parse(serializeMarkers(set));
	CHECK(!parsed.report.hasErrors());
	CHECK(parsed.report.warnings.empty());
	CHECK(parsed.set.markers.size() == 3);
	// Sorted by side then usage: Left ball, Left heel, Right heel.
	CHECK(parsed.set.markers[0].name == "ball_00_L_MRK");
	CHECK(parsed.set.markers[1].name == "heel_00_L_MRK");
	CHECK(parsed.set.markers[2].name == "heel_00_R_MRK");
	CHECK(parsed.set.markers[0].isExported);
	CHECK(!parsed.set.markers[1].isExported);
	CHECK(parsed.set.markers[0].joint.usage == "ankle");
	CHECK_CLOSE(parsed.set.markers[1].offsetLocal[13], -9.5, 0.0);
	CHECK(markersHash(parsed.set) == markersHash(set));
}

CORE_TEST(markerFileWritesOneLinePerMarker)
{
	std::istringstream lines(serializeMarkers(makeSet()));
	std::string line;
	int markerLines = 0;
	while (std::getline(lines, line))
	{
		if (line.find("\"markup\"") != std::string::npos)
		{
			++markerLines;
			CHECK(line.find("\"offsetLocal\"") != std::string::npos);
		}
	}
	CHECK(markerLines == 3);
}

CORE_TEST(markerFileWritesAnEmptySet)
{
	const Parsed parsed = parse(serializeMarkers(MarkerSet{}));
	CHECK(!parsed.report.hasErrors());
	CHECK(parsed.set.markers.empty());
}

CORE_TEST(markersHashIsOrderIndependent)
{
	MarkerSet set = makeSet();
	const std::uint64_t before = markersHash(set);
	std::reverse(set.markers.begin(), set.markers.end());
	CHECK(markersHash(set) == before);
}

CORE_TEST(markersHashIgnoresNoiseBelowTheGrid)
{
	MarkerSet set = makeSet();
	const std::uint64_t before = markersHash(set);
	set.markers[0].offsetLocal[13] += 1e-6;
	CHECK(markersHash(set) == before);
}

CORE_TEST(markersHashRoundsToTheNearestStep)
{
	// A grid step is 0.0001: +-0.00006 land on +-1 steps and 0.00004 on 0, so only the first two differ from rest.
	MarkerSet positive = makeSet();
	MarkerSet negative = makeSet();
	MarkerSet zero = makeSet();
	positive.markers[0].offsetLocal[12] = 0.00006;
	negative.markers[0].offsetLocal[12] = -0.00006;
	zero.markers[0].offsetLocal[12] = 0.00004;
	MarkerSet base = makeSet();
	CHECK(markersHash(zero) == markersHash(base));
	CHECK(markersHash(positive) != markersHash(base));
	CHECK(markersHash(negative) != markersHash(base));
	CHECK(markersHash(positive) != markersHash(negative));
}

CORE_TEST(markersHashChangesWithAnyField)
{
	const MarkerSet base = makeSet();
	const std::uint64_t original = markersHash(base);

	MarkerSet moved = base;
	moved.markers[1].offsetLocal[13] += 0.001;
	CHECK(markersHash(moved) != original);

	MarkerSet renamed = base;
	renamed.markers[1].name = "heel_01_L_MRK";
	CHECK(markersHash(renamed) != original);

	MarkerSet rejoined = base;
	rejoined.markers[1].joint.usage = "foot";
	CHECK(markersHash(rejoined) != original);

	MarkerSet exported = base;
	exported.markers[1].isExported = !exported.markers[1].isExported;
	CHECK(markersHash(exported) != original);

	MarkerSet usage = base;
	usage.markers[1].markup.usage = "heel2";
	CHECK(markersHash(usage) != original);

	MarkerSet fewer = base;
	fewer.markers.pop_back();
	CHECK(markersHash(fewer) != original);
}

CORE_TEST(markerFileEditedLineChangesTheHashByOneLine)
{
	// Editing one marker's text in the file changes the recomputed hash, and the stale stored one is a warning.
	std::string text = serializeMarkers(makeSet());
	const std::size_t at = text.find("-8.7");
	CHECK(at != std::string::npos);
	text.replace(at, 4, "-8.9");
	const Parsed parsed = parse(text);
	CHECK(!parsed.report.hasErrors());
	CHECK(parsed.report.warnings.size() == 1);
	CHECK(parsed.report.warnings[0].find("markersHash") != std::string::npos);
	CHECK_CLOSE(parsed.set.markers[0].offsetLocal[14], 0.0, 0.0);
	CHECK_CLOSE(parsed.set.markers[0].offsetLocal[13], -8.9, 0.0);
}

CORE_TEST(markerFileStoredHashIsRecomputedNotTrusted)
{
	// A matching stored hash is silent. The same file with a wrong hash is a warning, and the markers still load.
	const std::string text = serializeMarkers(makeSet());
	CHECK(parse(text).report.warnings.empty());

	std::string wrong = text;
	const std::string stored = Hash64::format(markersHash(makeSet()));
	const std::size_t at = wrong.find(stored);
	CHECK(at != std::string::npos);
	wrong.replace(at, stored.size(), "0000000000000000");
	const Parsed parsed = parse(wrong);
	CHECK(!parsed.report.hasErrors());
	CHECK(parsed.report.warnings.size() == 1);
	CHECK(parsed.report.warnings[0].find(stored) != std::string::npos);
	CHECK(parsed.set.markers.size() == 3);
}

CORE_TEST(markerFileGoodMarkerParses)
{
	const Parsed parsed = parse(fileWith(GoodMarker));
	CHECK(!parsed.report.hasErrors());
	CHECK(parsed.set.markers.size() == 1);
	CHECK_CLOSE(parsed.set.markers[0].offsetLocal[14], -1.2, 0.0);
}

CORE_TEST(markerFileBadJson)
{
	checkSingleError("{ markers", "markers file");
}

CORE_TEST(markerFileNotAnObject)
{
	checkSingleError("[1, 2]", "must be a JSON object with formatVersion");
}

CORE_TEST(markerFileMissingFormatVersion)
{
	checkSingleError("{\"markers\": []}", "must be a JSON object with formatVersion");
}

CORE_TEST(markerFileFormatVersionNotAWholeNumber)
{
	checkSingleError("{\"formatVersion\": \"1\", \"markersHash\": \"x\", \"markers\": []}", "must be a whole number");
}

CORE_TEST(markerFileUnknownVersionIsRefused)
{
	// The format has no steps yet: version 1 is current, so 0 is below the minimum and 2 is from a newer build.
	checkSingleError("{\"formatVersion\": 2, \"markersHash\": \"x\", \"markers\": []}", "formatVersion");
	checkSingleError("{\"formatVersion\": 0, \"markersHash\": \"x\", \"markers\": []}", "formatVersion");
}

CORE_TEST(markerFileVersionTableIsVersionOne)
{
	CHECK(markerUpdateTable().minVersion == 1);
	CHECK(markerUpdateTable().currentVersion() == 1);
}

CORE_TEST(markerFileUnknownAndMissingFileKeys)
{
	checkSingleError("{\"formatVersion\": 1, \"markersHash\": \"x\", \"markers\": [], \"extra\": 1}", "unknown key 'extra'");
	checkSingleError("{\"formatVersion\": 1, \"markers\": []}", "missing required key 'markersHash'");
	checkSingleError("{\"formatVersion\": 1, \"markersHash\": \"x\"}", "missing required key 'markers'");
}

CORE_TEST(markerFileMarkersMustBeAList)
{
	checkSingleError("{\"formatVersion\": 1, \"markersHash\": \"x\", \"markers\": {}}", "must be a list of markers");
}

CORE_TEST(markerFileUnknownAndMissingMarkerKeys)
{
	checkSingleError(fileWith("{\"markup\": [\"Left\", \"heel\"]}"), "missing required key 'joint'");
	std::string extra = GoodMarker;
	extra.insert(1, "\"colour\": 1, ");
	checkSingleError(fileWith(extra), "unknown key 'colour'");
	checkSingleError(fileWith("5"), "must be a JSON object");
}

CORE_TEST(markerFileBadMarkup)
{
	std::string badSide = GoodMarker;
	badSide.replace(badSide.find("\"Left\", \"heel\""), 14, "\"Up\", \"heel\"");
	checkSingleError(fileWith(badSide), "markers[0].markup");

	std::string noUsage = GoodMarker;
	noUsage.replace(noUsage.find("\"Left\", \"heel\""), 14, "\"Left\", \"\"");
	checkSingleError(fileWith(noUsage), "usage is empty");

	std::string notAPair = GoodMarker;
	notAPair.replace(notAPair.find("[\"Left\", \"heel\"]"), 16, "\"heel\"");
	checkSingleError(fileWith(notAPair), "must be [side, usage]");
}

CORE_TEST(markerFileBadJoint)
{
	std::string badSide = GoodMarker;
	badSide.replace(badSide.find("\"Left\", \"ankle\""), 15, "\"Up\", \"ankle\"");
	checkSingleError(fileWith(badSide), "markers[0].joint");

	std::string noUsage = GoodMarker;
	noUsage.replace(noUsage.find("\"Left\", \"ankle\""), 15, "\"Left\", \"\"");
	checkSingleError(fileWith(noUsage), "markers[0].joint: usage is empty");
}

CORE_TEST(markerFileBadExport)
{
	std::string text = GoodMarker;
	text.replace(text.find("false"), 5, "\"no\"");
	checkSingleError(fileWith(text), "markers[0].export: must be true or false");
}

CORE_TEST(markerFileBadName)
{
	std::string text = GoodMarker;
	text.replace(text.find("\"heel_00_L_MRK\""), 15, "\"\"");
	checkSingleError(fileWith(text), "markers[0].name: must be a non-empty string");

	std::string number = GoodMarker;
	number.replace(number.find("\"heel_00_L_MRK\""), 15, "7");
	checkSingleError(fileWith(number), "markers[0].name");
}

CORE_TEST(markerFileBadOffset)
{
	std::string fifteen = GoodMarker;
	fifteen.replace(fifteen.find(", -1.2, 1]"), 10, ", -1.2]");
	checkSingleError(fileWith(fifteen), "markers[0].offsetLocal: must be 16 numbers");

	std::string text = GoodMarker;
	text.replace(text.find("-9.5"), 4, "\"a\"");
	checkSingleError(fileWith(text), "markers[0].offsetLocal: must be 16 numbers");

	std::string notAList = GoodMarker;
	const std::size_t start = notAList.find("[1, 0, 0, 0,");
	notAList.replace(start, notAList.find("1]}", start) + 2 - start, "5");
	checkSingleError(fileWith(notAList), "markers[0].offsetLocal: must be 16 numbers");
}

CORE_TEST(markerFileDuplicateMarkup)
{
	const std::string text = fileWith(std::string(GoodMarker) + ", " + GoodMarker);
	const Parsed parsed = parse(text);
	CHECK(parsed.report.hasErrors());
	CHECK(anyContains(parsed.report.errors, "markers[1]"));
	CHECK(anyContains(parsed.report.errors, "is listed twice"));
}

CORE_TEST(markerFileReportsEveryBadMarker)
{
	// Two bad markers give two errors in one pass, so one load names them all.
	const std::string text = fileWith("{\"markup\": [\"Up\", \"a\"], \"joint\": [\"Left\", \"ankle\"], \"export\": false, \"name\": \"a\", \"offsetLocal\": "
		"[1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]}, {\"markup\": [\"Left\", \"b\"], \"joint\": [\"Left\", \"ankle\"], \"export\": 3, \"name\": \"b\", "
		"\"offsetLocal\": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]}");
	const Parsed parsed = parse(text);
	CHECK(parsed.report.errors.size() == 2);
	CHECK(anyContains(parsed.report.errors, "markers[0].markup"));
	CHECK(anyContains(parsed.report.errors, "markers[1].export"));
}

CORE_TEST(markerFileMarkupNamespaceIsItsOwn)
{
	// A marker and its joint can share a [side, usage]: the same [Left, ankle] as markup and as joint is fine.
	const std::string text = fileWith("{\"markup\": [\"Left\", \"ankle\"], \"joint\": [\"Left\", \"ankle\"], \"export\": false, \"name\": \"ankle_00_L_MRK\", "
		"\"offsetLocal\": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]}");
	const Parsed parsed = parse(text);
	CHECK(!parsed.report.hasErrors());
	CHECK(parsed.set.markers.size() == 1);
}

CORE_TEST(findMarkerBySelector)
{
	MarkerSet set = makeSet();
	sortMarkers(set);
	CHECK(findMarker(set, { "Left", "ball" }) == 0);
	CHECK(findMarker(set, { "Right", "heel" }) == 2);
	CHECK(findMarker(set, { "Right", "ball" }) == -1);
}
