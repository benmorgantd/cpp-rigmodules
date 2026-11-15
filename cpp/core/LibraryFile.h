#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// What the library browser needs to know about a pose file or a ControlAnim file without loading it onto a rig (D51):
// its kind, its meta and its size, and an edit of its meta alone. The format and its update steps stay in PoseFile and
// ControlAnimFile, so an old file is brought forward here as it is everywhere else.

#include "LibraryMeta.h"
#include "RigTemplateValidation.h"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

enum class DataFileKind
{
	kPose,
	kAnimation
};

struct DataFileSummary
{
	DataFileKind kind = DataFileKind::kPose;
	int formatVersion = 0;                  // as stored, before any update
	LibraryMeta meta;
	double frameRate = 0.0;                 // animation only
	double startFrame = 0.0;                // animation only
	double endFrame = 0.0;                  // animation only
	std::vector<std::pair<std::string, std::string>> controls;   // side and usage of every control the file holds
	int channelCount = 0;
};

// Reads and validates the file (every problem goes into report) and fills summary. A file with an `frameRate` key is
// a ControlAnim, any other is a pose.
void summarizeDataFile(const std::string& text, DataFileSummary& outSummary, ValidationReport& report);

// The JSON for one file in getRigDataFileInfo's result (DataFileInfoKey). unmatched is the part of summary.controls the
// rig has no control for.
nlohmann::ordered_json dataFileInfoToJson(const std::string& file, const DataFileSummary& summary, const std::vector<std::pair<std::string, std::string>>& unmatched);

// The JSON for a file that could not be read: the file and its errors joined into one message.
nlohmann::ordered_json dataFileErrorToJson(const std::string& file, const ValidationReport& report);

// The file with its meta edited (applyCallerMeta) and nothing else changed, written at the current format version.
// outText is empty when report has errors.
void editDataFileMeta(const std::string& text, const std::string& editJsonText, std::string& outText, ValidationReport& report);
