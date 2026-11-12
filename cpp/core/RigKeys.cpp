#include "RigKeys.h"
#include "Constants.h"
#include "JointMarkup.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <set>
#include <utility>

namespace
{
	struct NamedTangent
	{
		TangentKind kind;
		const char* name;
	};

	// Fixed has no name: it is the [angle, weight] form.
	constexpr NamedTangent kTangentNames[] =
	{
		{ TangentKind::Auto, "auto" },
		{ TangentKind::Clamped, "clamped" },
		{ TangentKind::Linear, "linear" },
		{ TangentKind::Flat, "flat" },
		{ TangentKind::Step, "step" },
		{ TangentKind::Spline, "spline" },
	};

	struct NamedInfinity
	{
		Infinity infinity;
		const char* name;
	};

	constexpr NamedInfinity kInfinityNames[] =
	{
		{ Infinity::Constant, "constant" },
		{ Infinity::Linear, "linear" },
		{ Infinity::Cycle, "cycle" },
		{ Infinity::CycleRelative, "cycleRelative" },
		{ Infinity::Oscillate, "oscillate" },
	};

	std::string infinityNames()
	{
		std::string names;
		for (const NamedInfinity& named : kInfinityNames)
		{
			names += (names.empty() ? "" : ", ") + std::string(named.name);
		}
		return names;
	}

	nlohmann::ordered_json tangentToJson(const Tangent& tangent)
	{
		if (tangent.kind == TangentKind::Fixed)
		{
			return nlohmann::ordered_json::array({ tangent.angle, tangent.weight });
		}
		for (const NamedTangent& named : kTangentNames)
		{
			if (named.kind == tangent.kind)
			{
				return named.name;
			}
		}
		return "auto";
	}

	const char* infinityName(Infinity infinity)
	{
		for (const NamedInfinity& named : kInfinityNames)
		{
			if (named.infinity == infinity)
			{
				return named.name;
			}
		}
		return "constant";
	}

	bool hasAutoTangents(const Key& key)
	{
		return key.in.kind == TangentKind::Auto && key.out.kind == TangentKind::Auto;
	}

	bool hasDefaultFlags(const ChannelCurve& curve)
	{
		return !curve.isWeighted && curve.preInfinity == Infinity::Constant && curve.postInfinity == Infinity::Constant;
	}

	nlohmann::ordered_json keyToJson(const Key& key, bool isCompact)
	{
		if (isCompact && hasAutoTangents(key))
		{
			return nlohmann::ordered_json::array({ key.frame, key.value });
		}
		return nlohmann::ordered_json::array({ key.frame, key.value, tangentToJson(key.in), tangentToJson(key.out) });
	}

	nlohmann::ordered_json curveToJson(const ChannelCurve& curve, bool isCompact)
	{
		nlohmann::ordered_json keys = nlohmann::ordered_json::array();
		for (const Key& key : curve.keys)
		{
			keys.push_back(keyToJson(key, isCompact));
		}
		if (isCompact && hasDefaultFlags(curve))
		{
			return keys;
		}
		nlohmann::ordered_json result;
		result[ControlAnimKey::Weighted] = curve.isWeighted;
		result[ControlAnimKey::PreInfinity] = infinityName(curve.preInfinity);
		result[ControlAnimKey::PostInfinity] = infinityName(curve.postInfinity);
		result[ControlAnimKey::Keys] = keys;
		return result;
	}

	nlohmann::ordered_json toJson(const RigKeys& keys, bool isCompact)
	{
		nlohmann::ordered_json result = nlohmann::ordered_json::object();
		for (const auto& [side, usages] : keys)
		{
			for (const auto& [usage, channels] : usages)
			{
				for (const auto& [channel, curve] : channels)
				{
					if (!curve.keys.empty())
					{
						result[side][usage][channel] = curveToJson(curve, isCompact);
					}
				}
			}
		}
		return result;
	}

	bool parseTangent(const nlohmann::json& json, const std::string& path, Tangent& outTangent, ValidationReport& report)
	{
		if (json.is_string())
		{
			for (const NamedTangent& named : kTangentNames)
			{
				if (json.get<std::string>() == named.name)
				{
					outTangent.kind = named.kind;
					return true;
				}
			}
			report.addError(path, "tangent '" + json.get<std::string>() + "' is not auto, clamped, linear, flat, step or spline");
			return false;
		}
		if (json.is_array() && json.size() == 2 && json[0].is_number() && json[1].is_number())
		{
			outTangent.kind = TangentKind::Fixed;
			outTangent.angle = json[0].get<double>();
			outTangent.weight = json[1].get<double>();
			return true;
		}
		report.addError(path, "must be a tangent name or an [angle, weight] pair of numbers, not " + json.dump());
		return false;
	}

	bool parseInfinity(const nlohmann::json& json, const std::string& path, Infinity& outInfinity, ValidationReport& report)
	{
		if (json.is_string())
		{
			for (const NamedInfinity& named : kInfinityNames)
			{
				if (json.get<std::string>() == named.name)
				{
					outInfinity = named.infinity;
					return true;
				}
			}
		}
		report.addError(path, "must be one of " + infinityNames() + ", not " + json.dump());
		return false;
	}

	// The keys of one channel. False when any key is wrong, so the channel isn't used.
	bool parseKeys(const nlohmann::json& keys, const std::string& path, std::vector<Key>& outKeys, ValidationReport& report)
	{
		if (!keys.is_array())
		{
			report.addError(path, "must be an array of keys");
			return false;
		}

		bool isValid = true;
		std::set<double> frames;
		for (std::size_t k = 0; k < keys.size(); ++k)
		{
			const nlohmann::json& json = keys[k];
			const std::string keyPath = path + "[" + std::to_string(k) + "]";
			if (!json.is_array() || (json.size() != 2 && json.size() != 4) || !json[0].is_number() || !json[1].is_number())
			{
				report.addError(keyPath, "must be [frame, value] or [frame, value, in, out], with numbers for the frame and value, not " + json.dump());
				isValid = false;
				continue;
			}

			Key key;
			key.frame = json[0].get<double>();
			key.value = json[1].get<double>();
			if (json.size() == 4)
			{
				// Both are parsed, so both are reported.
				const bool isInValid = parseTangent(json[2], keyPath + "[2]", key.in, report);
				const bool isOutValid = parseTangent(json[3], keyPath + "[3]", key.out, report);
				isValid = isValid && isInValid && isOutValid;
			}
			if (!frames.insert(key.frame).second)
			{
				report.addError(keyPath, "frame " + json[0].dump() + " is already keyed in this channel");
				isValid = false;
				continue;
			}
			outKeys.push_back(key);
		}
		return isValid;
	}

	bool parseCurve(const nlohmann::json& json, const std::string& path, ChannelCurve& outCurve, ValidationReport& report)
	{
		if (json.is_array())
		{
			return parseKeys(json, path, outCurve.keys, report);
		}
		if (!json.is_object())
		{
			report.addError(path, "must be an array of keys or a curve object with keys");
			return false;
		}

		const std::size_t errorCount = report.errors.size();
		RigTemplateValidation::checkKeys(
			json,
			{ ControlAnimKey::Keys },
			{ ControlAnimKey::Weighted, ControlAnimKey::PreInfinity, ControlAnimKey::PostInfinity },
			path,
			report);
		if (report.errors.size() != errorCount)
		{
			return false;
		}

		bool isValid = true;
		if (json.contains(ControlAnimKey::Weighted))
		{
			const nlohmann::json& weighted = json.at(ControlAnimKey::Weighted);
			if (weighted.is_boolean())
			{
				outCurve.isWeighted = weighted.get<bool>();
			}
			else
			{
				report.addError(path + "." + ControlAnimKey::Weighted, "must be true or false, not " + weighted.dump());
				isValid = false;
			}
		}
		if (json.contains(ControlAnimKey::PreInfinity))
		{
			isValid = parseInfinity(json.at(ControlAnimKey::PreInfinity), path + "." + ControlAnimKey::PreInfinity, outCurve.preInfinity, report) && isValid;
		}
		if (json.contains(ControlAnimKey::PostInfinity))
		{
			isValid = parseInfinity(json.at(ControlAnimKey::PostInfinity), path + "." + ControlAnimKey::PostInfinity, outCurve.postInfinity, report) && isValid;
		}
		return parseKeys(json.at(ControlAnimKey::Keys), path + "." + ControlAnimKey::Keys, outCurve.keys, report) && isValid;
	}
}

bool Tangent::operator==(const Tangent& other) const
{
	return kind == other.kind && (kind != TangentKind::Fixed || (angle == other.angle && weight == other.weight));
}

std::string serializeRigKeys(const RigKeys& keys)
{
	return toJson(keys, true).dump();
}

nlohmann::ordered_json rigKeysToJson(const RigKeys& keys)
{
	return toJson(keys, false);
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
	parseRigKeys(file, where, outKeys, report);
}

void parseRigKeys(const nlohmann::json& keys, const std::string& where, RigKeys& outKeys, ValidationReport& report)
{
	if (!keys.is_object())
	{
		report.addError(where, "must be a JSON object of sides");
		return;
	}

	for (const auto& [side, usages] : keys.items())
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

			for (const auto& [channel, curveJson] : channels.items())
			{
				const std::string channelPath = usagePath + "." + channel;
				if (channel.empty())
				{
					report.addError(usagePath, "channel name is empty");
					continue;
				}

				ChannelCurve curve;
				if (parseCurve(curveJson, channelPath, curve, report))
				{
					outKeys[side][usage][channel] = curve;
				}
			}
		}
	}
}

ChannelCurve negateCurve(const ChannelCurve& curve)
{
	ChannelCurve result = curve;
	for (Key& key : result.keys)
	{
		key.value = -key.value;
		for (Tangent* pTangent : { &key.in, &key.out })
		{
			if (pTangent->kind == TangentKind::Fixed)
			{
				pTangent->angle = -pTangent->angle;
			}
		}
	}
	return result;
}

void offsetKeys(RigKeys& keys, double offset)
{
	for (auto& [side, usages] : keys)
	{
		for (auto& [usage, channels] : usages)
		{
			for (auto& [channel, curve] : channels)
			{
				for (Key& key : curve.keys)
				{
					key.frame += offset;
				}
			}
		}
	}
}
