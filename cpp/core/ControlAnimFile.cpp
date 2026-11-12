#include "ControlAnimFile.h"
#include "Constants.h"
#include "FormatUpdate.h"

#include <cstddef>

const UpdateTable& controlAnimUpdateTable()
{
	// Add a step here, and nowhere else, when the ControlAnim file changes shape or meaning.
	static const UpdateTable table{ 1, {} };
	return table;
}

std::string serializeControlAnim(const ControlAnim& anim)
{
	// ordered_json keeps formatVersion first.
	nlohmann::ordered_json file;
	file[ControlAnimKey::FormatVersion] = controlAnimUpdateTable().currentVersion();
	file[ControlAnimKey::FrameRate] = anim.frameRate;
	file[ControlAnimKey::StartFrame] = anim.startFrame;
	file[ControlAnimKey::EndFrame] = anim.endFrame;
	file[ControlAnimKey::Controls] = rigKeysToJson(anim.controls);
	return file.dump(4) + "\n";
}

void deserializeControlAnim(const std::string& text, ControlAnim& outAnim, ValidationReport& report)
{
	const std::string where = "ControlAnim file";
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

	const std::size_t errorCount = report.errors.size();
	RigTemplateValidation::checkKeys(
		file,
		{ ControlAnimKey::FormatVersion, ControlAnimKey::FrameRate, ControlAnimKey::StartFrame, ControlAnimKey::EndFrame, ControlAnimKey::Controls },
		{},
		where,
		report);
	if (report.errors.size() != errorCount)
	{
		return;
	}

	const nlohmann::json& version = file.at(ControlAnimKey::FormatVersion);
	const std::string versionPath = where + "." + ControlAnimKey::FormatVersion;
	if (!version.is_number_integer())
	{
		report.addError(versionPath, "must be an integer, not " + version.dump());
		return;
	}
	if (!FormatUpdate::runUpdates(controlAnimUpdateTable(), version.get<int>(), file, versionPath, report))
	{
		return;
	}

	ControlAnim anim;
	const nlohmann::json& frameRate = file.at(ControlAnimKey::FrameRate);
	if (!frameRate.is_number() || frameRate.get<double>() <= 0.0)
	{
		report.addError(where + "." + ControlAnimKey::FrameRate, "must be a number above 0, not " + frameRate.dump());
		return;
	}
	anim.frameRate = frameRate.get<double>();

	const nlohmann::json& start = file.at(ControlAnimKey::StartFrame);
	const nlohmann::json& end = file.at(ControlAnimKey::EndFrame);
	if (!start.is_number() || !end.is_number())
	{
		report.addError(where + "." + ControlAnimKey::StartFrame, std::string("and ") + ControlAnimKey::EndFrame + " must be numbers, not " + start.dump() + " and " + end.dump());
		return;
	}
	anim.startFrame = start.get<double>();
	anim.endFrame = end.get<double>();
	if (anim.endFrame < anim.startFrame)
	{
		report.addError(where + "." + ControlAnimKey::EndFrame, "is " + end.dump() + ", before startFrame " + start.dump());
		return;
	}

	const std::size_t keysErrorCount = report.errors.size();
	parseRigKeys(file.at(ControlAnimKey::Controls), where + "." + ControlAnimKey::Controls, anim.controls, report);
	if (report.errors.size() != keysErrorCount)
	{
		return;
	}

	for (const auto& [side, usages] : anim.controls)
	{
		for (const auto& [usage, channels] : usages)
		{
			for (const auto& [channel, curve] : channels)
			{
				for (std::size_t k = 0; k < curve.keys.size(); ++k)
				{
					const double frame = curve.keys[k].frame;
					if (frame < anim.startFrame || frame > anim.endFrame)
					{
						report.addError(
							where + "." + ControlAnimKey::Controls + "." + side + "." + usage + "." + channel + ".keys[" + std::to_string(k) + "]",
							"frame " + std::to_string(frame) + " is outside startFrame " + start.dump() + " to endFrame " + end.dump());
					}
				}
			}
		}
	}
	outAnim = anim;
}
