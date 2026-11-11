#include "PoseFile.h"
#include "Constants.h"
#include "FormatUpdate.h"
#include "JointMarkup.h"

const UpdateTable& poseUpdateTable()
{
	// Add a step here, and nowhere else, when the pose file changes shape or meaning.
	static const UpdateTable table{ 1, {} };
	return table;
}

std::string serializePose(const PoseMap& pose)
{
	// ordered_json keeps formatVersion first and each control's channels in channel box order.
	nlohmann::ordered_json file;
	file[PoseFileKey::FormatVersion] = poseUpdateTable().currentVersion();
	nlohmann::ordered_json controls = nlohmann::ordered_json::object();
	for (const auto& [side, usages] : pose)
	{
		for (const auto& [usage, channels] : usages)
		{
			nlohmann::ordered_json values = nlohmann::ordered_json::object();
			for (const auto& [name, value] : channels)
			{
				values[name] = value;
			}
			controls[side][usage] = values;
		}
	}
	file[PoseFileKey::Controls] = controls;
	return file.dump(4) + "\n";
}

void deserializePose(const std::string& text, PoseMap& outPose, ValidationReport& report)
{
	const std::string where = "pose file";
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
	RigTemplateValidation::checkKeys(file, { PoseFileKey::FormatVersion, PoseFileKey::Controls }, {}, where, report);
	if (report.errors.size() != errorCount)
	{
		return;
	}

	const nlohmann::json& version = file.at(PoseFileKey::FormatVersion);
	const std::string versionPath = where + "." + PoseFileKey::FormatVersion;
	if (!version.is_number_integer())
	{
		report.addError(versionPath, "must be an integer, not " + version.dump());
		return;
	}
	if (!FormatUpdate::runUpdates(poseUpdateTable(), version.get<int>(), file, versionPath, report))
	{
		return;
	}

	const nlohmann::json& controls = file.at(PoseFileKey::Controls);
	const std::string controlsPath = where + "." + PoseFileKey::Controls;
	if (!controls.is_object())
	{
		report.addError(controlsPath, "must be a JSON object of sides");
		return;
	}

	for (const auto& [side, usages] : controls.items())
	{
		const std::string sidePath = controlsPath + "." + side;
		if (!isValidSideName(side))
		{
			report.addError(sidePath, "'" + side + "' is not one of " + SideNames::Center + ", " + SideNames::Left + ", " + SideNames::Right);
			continue;
		}
		if (!usages.is_object())
		{
			report.addError(sidePath, "must be a JSON object of usages");
			continue;
		}

		for (const auto& [usage, values] : usages.items())
		{
			const std::string usagePath = sidePath + "." + usage;
			if (usage.empty())
			{
				report.addError(sidePath, "usage is empty");
				continue;
			}
			if (!values.is_object())
			{
				report.addError(usagePath, "must be a JSON object of channel values");
				continue;
			}

			PoseChannels channels;
			for (const auto& [name, value] : values.items())
			{
				if (name.empty())
				{
					report.addError(usagePath, "channel name is empty");
				}
				else if (!value.is_number())
				{
					report.addError(usagePath + "." + name, "must be a number, not " + value.dump());
				}
				else
				{
					channels.emplace_back(name, value.get<double>());
				}
			}
			outPose[side][usage] = channels;
		}
	}
}
