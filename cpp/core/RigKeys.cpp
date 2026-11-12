#include "RigKeys.h"
#include "Constants.h"
#include "JointMarkup.h"

#include <nlohmann/json.hpp>

#include <set>

std::string serializeRigKeys(const RigKeys& keys)
{
	nlohmann::ordered_json result = nlohmann::ordered_json::object();
	for (const auto& [side, usages] : keys)
	{
		for (const auto& [usage, channels] : usages)
		{
			for (const auto& [channel, channelKeys] : channels)
			{
				nlohmann::ordered_json pairs = nlohmann::ordered_json::array();
				for (const auto& [frame, value] : channelKeys)
				{
					pairs.push_back({ frame, value });
				}
				result[side][usage][channel] = pairs;
			}
		}
	}
	return result.dump();
}

void deserializeRigKeys(const std::string& text, RigKeys& outKeys, ValidationReport& report)
{
	const std::string where = "keys";
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
	if (!file.is_object())
	{
		report.addError(where, "must be a JSON object of sides");
		return;
	}

	for (const auto& [side, usages] : file.items())
	{
		const std::string sidePath = where + "." + side;
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

		for (const auto& [usage, channels] : usages.items())
		{
			const std::string usagePath = sidePath + "." + usage;
			if (usage.empty())
			{
				report.addError(sidePath, "usage is empty");
				continue;
			}
			if (!channels.is_object())
			{
				report.addError(usagePath, "must be a JSON object of channels");
				continue;
			}

			for (const auto& [channel, pairs] : channels.items())
			{
				const std::string channelPath = usagePath + "." + channel;
				if (channel.empty())
				{
					report.addError(usagePath, "channel name is empty");
					continue;
				}
				if (!pairs.is_array())
				{
					report.addError(channelPath, "must be an array of [frame, value] keys");
					continue;
				}

				ChannelKeys channelKeys;
				std::set<double> frames;
				bool isValid = true;
				for (std::size_t k = 0; k < pairs.size(); ++k)
				{
					const nlohmann::json& pair = pairs[k];
					const std::string keyPath = channelPath + "[" + std::to_string(k) + "]";
					if (!pair.is_array() || pair.size() != 2 || !pair[0].is_number() || !pair[1].is_number())
					{
						report.addError(keyPath, "must be a [frame, value] pair of numbers, not " + pair.dump());
						isValid = false;
						continue;
					}
					if (!frames.insert(pair[0].get<double>()).second)
					{
						report.addError(keyPath, "frame " + pair[0].dump() + " is already keyed in this channel");
						isValid = false;
						continue;
					}
					channelKeys.emplace_back(pair[0].get<double>(), pair[1].get<double>());
				}
				if (isValid)
				{
					outKeys[side][usage][channel] = channelKeys;
				}
			}
		}
	}
}
