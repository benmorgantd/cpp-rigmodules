#include "PoseBlend.h"

#include <algorithm>

namespace
{
	const ControlChannel* findChannel(const std::vector<ControlChannel>& channels, const std::string& name)
	{
		const auto it = std::find_if(channels.begin(), channels.end(), [&name](const ControlChannel& channel)
		{
			return channel.name == name;
		});
		return it == channels.end() ? nullptr : &*it;
	}

	// The reference value of one channel, or nullptr when ref lacks the control or the channel.
	const double* findRefValue(const PoseMap& ref, const std::string& side, const std::string& usage, const std::string& name)
	{
		const auto itSide = ref.find(side);
		if (itSide == ref.end())
		{
			return nullptr;
		}
		const auto itUsage = itSide->second.find(usage);
		if (itUsage == itSide->second.end())
		{
			return nullptr;
		}
		for (const auto& [channelName, value] : itUsage->second)
		{
			if (channelName == name)
			{
				return &value;
			}
		}
		return nullptr;
	}

	double clampToRange(const ControlChannel& channel, double value)
	{
		if (channel.hasMin && value < channel.min)
		{
			return channel.min;
		}
		if (channel.hasMax && value > channel.max)
		{
			return channel.max;
		}
		return value;
	}
}

bool isSteppedChannelType(const std::string& type)
{
	return type == "bool" || type == "short" || type == "long" || type == "enum";
}

double blendPoseValue(double ref, double pose, double rest, double t, bool stepped)
{
	if (stepped)
	{
		return t < 0.5 ? ref : pose;
	}
	if (t >= 1.0)
	{
		return pose + (t - 1.0) * (pose - rest);
	}
	return ref + t * (pose - ref);
}

void blendPoses(const PoseMap& ref, const PoseMap& pose, const PoseChannelInfo& channels, double t, PoseMap& outBlended)
{
	outBlended.clear();
	for (const auto& [side, usages] : pose)
	{
		const auto itInfoSide = channels.find(side);
		for (const auto& [usage, values] : usages)
		{
			const std::vector<ControlChannel>* pInfo = nullptr;
			if (itInfoSide != channels.end())
			{
				const auto itInfoUsage = itInfoSide->second.find(usage);
				if (itInfoUsage != itInfoSide->second.end())
				{
					pInfo = &itInfoUsage->second;
				}
			}

			PoseChannels& blended = outBlended[side][usage];
			for (const auto& [name, value] : values)
			{
				const ControlChannel* pChannel = pInfo == nullptr ? nullptr : findChannel(*pInfo, name);
				if (pChannel == nullptr)
				{
					blended.emplace_back(name, value);
					continue;
				}
				const double* pRef = findRefValue(ref, side, usage, name);
				const double start = pRef == nullptr ? pChannel->rest : *pRef;
				const double result = blendPoseValue(start, value, pChannel->rest, t, isSteppedChannelType(pChannel->type));
				blended.emplace_back(name, clampToRange(*pChannel, result));
			}
		}
	}
}
