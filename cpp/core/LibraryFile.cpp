#include "LibraryFile.h"
#include "Constants.h"
#include "ControlAnimFile.h"
#include "PoseFile.h"
#include "StringUtils.h"

namespace
{
	// Parses the text and says whether it is a ControlAnim. False, with an error, when it isn't JSON.
	bool readKind(const std::string& text, DataFileKind& outKind, int& outVersion, ValidationReport& report)
	{
		nlohmann::json file;
		try
		{
			file = nlohmann::json::parse(text);
		}
		catch (const nlohmann::json::exception& e)
		{
			report.addError("file", e.what());
			return false;
		}
		outKind = file.is_object() && file.contains(ControlAnimKey::FrameRate) ? DataFileKind::kAnimation : DataFileKind::kPose;
		outVersion = file.is_object() && file.contains(PoseFileKey::FormatVersion) && file.at(PoseFileKey::FormatVersion).is_number_integer()
			? file.at(PoseFileKey::FormatVersion).get<int>()
			: 0;
		return true;
	}
}

void summarizeDataFile(const std::string& text, DataFileSummary& outSummary, ValidationReport& report)
{
	DataFileSummary summary;
	if (!readKind(text, summary.kind, summary.formatVersion, report))
	{
		return;
	}

	const std::size_t errorCount = report.errors.size();
	if (summary.kind == DataFileKind::kPose)
	{
		PoseMap pose;
		deserializePose(text, pose, report, &summary.meta);
		for (const auto& [side, usages] : pose)
		{
			for (const auto& [usage, channels] : usages)
			{
				summary.controls.emplace_back(side, usage);
				summary.channelCount += static_cast<int>(channels.size());
			}
		}
	}
	else
	{
		ControlAnim anim;
		deserializeControlAnim(text, anim, report);
		summary.meta = anim.meta;
		summary.frameRate = anim.frameRate;
		summary.startFrame = anim.startFrame;
		summary.endFrame = anim.endFrame;
		for (const auto& [side, usages] : anim.controls)
		{
			for (const auto& [usage, channels] : usages)
			{
				summary.controls.emplace_back(side, usage);
				summary.channelCount += static_cast<int>(channels.size());
			}
		}
	}

	if (report.errors.size() == errorCount)
	{
		outSummary = summary;
	}
}

nlohmann::ordered_json dataFileInfoToJson(const std::string& file, const DataFileSummary& summary, const std::vector<std::pair<std::string, std::string>>& unmatched)
{
	nlohmann::ordered_json info;
	info[DataFileInfoKey::File] = file;
	info[DataFileInfoKey::Kind] = summary.kind == DataFileKind::kPose ? DataFileInfoKey::KindPose : DataFileInfoKey::KindAnimation;
	info[DataFileInfoKey::FormatVersion] = summary.formatVersion;
	info[DataFileInfoKey::Meta] = libraryMetaToJson(summary.meta);
	if (summary.kind == DataFileKind::kAnimation)
	{
		info[DataFileInfoKey::FrameRate] = summary.frameRate;
		info[DataFileInfoKey::StartFrame] = summary.startFrame;
		info[DataFileInfoKey::EndFrame] = summary.endFrame;
	}
	info[DataFileInfoKey::ControlCount] = summary.controls.size();
	info[DataFileInfoKey::ChannelCount] = summary.channelCount;

	// Each unmatched control as [side, usage], the pair control markup is made of.
	nlohmann::ordered_json unmatchedJson = nlohmann::ordered_json::array();
	for (const auto& [side, usage] : unmatched)
	{
		unmatchedJson.push_back({ side, usage });
	}
	info[DataFileInfoKey::UnmatchedControls] = unmatchedJson;
	return info;
}

nlohmann::ordered_json dataFileErrorToJson(const std::string& file, const ValidationReport& report)
{
	nlohmann::ordered_json info;
	info[DataFileInfoKey::File] = file;
	info[DataFileInfoKey::Error] = StringUtils::joinNames(report.errors);
	return info;
}

void editDataFileMeta(const std::string& text, const std::string& editJsonText, std::string& outText, ValidationReport& report)
{
	outText.clear();
	DataFileKind kind = DataFileKind::kPose;
	int version = 0;
	if (!readKind(text, kind, version, report))
	{
		return;
	}

	// The file is read in full and written again, so the update to the current version and the validation both apply to
	// the part that isn't meta. Both writers are canonical, so nothing else in the file changes.
	const std::size_t errorCount = report.errors.size();
	PoseMap pose;
	ControlAnim anim;
	LibraryMeta meta;
	if (kind == DataFileKind::kPose)
	{
		deserializePose(text, pose, report, &meta);
	}
	else
	{
		deserializeControlAnim(text, anim, report);
		meta = anim.meta;
	}
	if (report.errors.size() != errorCount)
	{
		return;
	}

	applyCallerMeta(editJsonText, meta, report);
	if (report.errors.size() != errorCount)
	{
		return;
	}

	if (kind == DataFileKind::kPose)
	{
		outText = serializePose(pose, meta);
	}
	else
	{
		anim.meta = meta;
		outText = serializeControlAnim(anim);
	}
}
