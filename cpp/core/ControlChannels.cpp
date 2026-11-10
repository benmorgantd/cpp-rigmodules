#include "ControlChannels.h"
#include "Constants.h"

#include <cmath>

#include <nlohmann/json.hpp>

namespace
{
	// How far a rest axis may lean off world X and still count as lying along it. Rest matrices come from joints
	// placed by hand, so this is looser than float noise.
	constexpr double kAxisTolerance{ 1e-4 };
}

std::string serializeControlChannels(const ControlChannelList& controls)
{
	nlohmann::json result = nlohmann::json::object();
	for (const auto& [controlName, channels] : controls)
	{
		nlohmann::json list = nlohmann::json::array();
		for (const ControlChannel& channel : channels)
		{
			nlohmann::json entry;
			entry[ControlChannelKey::Name] = channel.name;
			entry[ControlChannelKey::Type] = channel.type;
			entry[ControlChannelKey::Rest] = channel.rest;
			entry[ControlChannelKey::Min] = channel.hasMin ? nlohmann::json(channel.min) : nlohmann::json(nullptr);
			entry[ControlChannelKey::Max] = channel.hasMax ? nlohmann::json(channel.max) : nlohmann::json(nullptr);
			entry[ControlChannelKey::Mirror] = channel.negate ? ChannelMirror::Negate : ChannelMirror::Copy;
			list.push_back(entry);
		}
		result[controlName] = list;
	}
	return result.dump();
}

int findMirrorAxis(const double xOfLocalAxes[3])
{
	for (int axis = 0; axis < 3; ++axis)
	{
		if (std::abs(std::abs(xOfLocalAxes[axis]) - 1.0) < kAxisTolerance)
		{
			return axis;
		}
	}
	return -1;
}
