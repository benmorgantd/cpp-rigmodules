#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// The `meta` object of a pose file and a ControlAnim file (D51): what a library browser searches and shows beyond the
// control data. Every field is optional.
// {
//     "assetId": "Characters|Hero", "variant": "default", "pluginVersion": "0.1", "created": "2026-11-14T10:22:00Z",
//     "template": "Human", "skeletonId": "Biped", "author": "bmorgan",
//     "tags": ["hands", "fist"], "description": "Clenched right hand", "category": "Expressions",
//     "rigRegions": ["hands"], "status": "scratch"
// }
// The first four are filled from the rig when a file is saved, and a caller can't set them. The rest come from the
// caller. RigModules checks the shape of a value (a string, or a list of strings), never the vocabulary: the lists of
// regions and statuses are project data that the library browser checks.

#include "RigTemplateValidation.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

struct LibraryMeta
{
	// Filled from the rig.
	std::string assetId;
	std::string variant;
	std::string pluginVersion;
	std::string created;

	// From the caller.
	std::string templateName;
	std::string skeletonId;
	std::string author;
	std::string description;
	std::string category;
	std::string status;
	std::vector<std::string> tags;
	std::vector<std::string> rigRegions;
};

// The meta object for a file: fields with a value only, in a fixed order, so saved files diff cleanly.
nlohmann::ordered_json libraryMetaToJson(const LibraryMeta& meta);

// Reads a file's meta object. Reports a value that is not a string or a list of non-empty strings, and a key that is
// not a meta key. outMeta holds what parsed.
void parseLibraryMeta(const nlohmann::json& metaJson, const std::string& where, LibraryMeta& outMeta, ValidationReport& report);

// Applies the caller's JSON object text onto meta: each key it has replaces that field, and an empty string or empty
// list clears one. Reports bad JSON, a value of the wrong shape, an unknown key, and a key the rig fills (assetId,
// variant, pluginVersion, created). meta is unchanged when report has errors.
void applyCallerMeta(const std::string& jsonText, LibraryMeta& meta, ValidationReport& report);

// The current time as ISO 8601 UTC, "2026-11-14T10:22:00Z", for the `created` field.
std::string utcTimestampNow();
