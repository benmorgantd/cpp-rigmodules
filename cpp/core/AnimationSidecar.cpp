#include "AnimationSidecar.h"
#include "Constants.h"
#include "FormatUpdate.h"

#include <cstddef>
#include <string>

const UpdateTable& animationSidecarUpdateTable()
{
	// Add a step here, and nowhere else, when the sidecar file changes shape or meaning.
	static const UpdateTable table{ 1, {} };
	return table;
}

void deserializeAnimationSidecar(const std::string& text, AnimationSidecar& outSidecar, ValidationReport& report)
{
	const std::string where = "animation sidecar";
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
		{ AnimationSidecarKey::FormatVersion, AnimationSidecarKey::FrameRate, AnimationSidecarKey::Frames },
		{},
		where,
		report);
	if (report.errors.size() != errorCount)
	{
		return;
	}

	const nlohmann::json& version = file.at(AnimationSidecarKey::FormatVersion);
	const std::string versionPath = where + "." + AnimationSidecarKey::FormatVersion;
	if (!version.is_number_integer())
	{
		report.addError(versionPath, "must be an integer, not " + version.dump());
		return;
	}
	if (!FormatUpdate::runUpdates(animationSidecarUpdateTable(), version.get<int>(), file, versionPath, report))
	{
		return;
	}

	AnimationSidecar sidecar;
	const nlohmann::json& frameRate = file.at(AnimationSidecarKey::FrameRate);
	if (!frameRate.is_number() || frameRate.get<double>() <= 0.0)
	{
		report.addError(where + "." + AnimationSidecarKey::FrameRate, "must be a number above 0, not " + frameRate.dump());
		return;
	}
	sidecar.frameRate = frameRate.get<double>();

	const nlohmann::json& frames = file.at(AnimationSidecarKey::Frames);
	const std::string framesPath = where + "." + AnimationSidecarKey::Frames;
	if (!frames.is_object())
	{
		report.addError(framesPath, "must be a JSON object of frame numbers");
		return;
	}

	for (const auto& [name, controls] : frames.items())
	{
		const std::string framePath = framesPath + "." + name;
		std::size_t used = 0;
		int frame = 0;
		try
		{
			frame = std::stoi(name, &used);
		}
		catch (const std::exception&)
		{
			used = 0;
		}
		if (used == 0 || used != name.size())
		{
			report.addError(framePath, "a frame must be a whole number");
			continue;
		}

		deserializePoseControls(controls, framePath, sidecar.frames[frame], report);
	}

	if (report.errors.size() == errorCount)
	{
		outSidecar = std::move(sidecar);
	}
}
