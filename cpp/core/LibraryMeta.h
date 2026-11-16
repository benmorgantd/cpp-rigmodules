#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// The `meta` object of a pose file and a ControlAnim file (D51): what a library browser searches and shows beyond the
// control data. Every field is optional.
// {
//     "assetId": "characters\\hero", "rigName": "body", "skeletonHash": "16 hex digits", "pluginVersion": "0.1",
//     "created": "2026-11-14T10:22:00Z",
//     "template": "Human", "skeleton": "body", "author": "bmorgan",
//     "tags": ["hands", "fist"], "description": "Clenched right hand", "category": "Expressions",
//     "rigRegions": ["hands"], "status": "scratch"
// }
// The first five are filled from the rig when a file is saved, and a caller can't set them. The rest come from the
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
	std::string rigName;
	std::string skeletonHash;   // the rig root's stamp (D54), Hash64::format text
	std::string pluginVersion;
	std::string created;

	// From the caller.
	std::string templateName;
	std::string skeleton;       // the skeleton's name (D54)
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
// rigName, skeletonHash, pluginVersion, created). meta is unchanged when report has errors.
void applyCallerMeta(const std::string& jsonText, LibraryMeta& meta, ValidationReport& report);

// The version 2 to 3 update step of the pose and ControlAnim formats (D33, D54), which both keep their meta under
// "meta": `variant` becomes `rigName` and `skeletonId` becomes `skeleton`. A version 2 file has no skeletonHash.
void renameVersion2MetaKeys(nlohmann::json& file);

// The current time as ISO 8601 UTC, "2026-11-14T10:22:00Z", for the `created` field.
std::string utcTimestampNow();
