#include "LibraryMeta.h"
#include "Constants.h"

#include <array>
#include <ctime>
#include <set>
#include <utility>

namespace
{
	struct StringField
	{
		const char* key;
		std::string LibraryMeta::* member;
		bool isFilledFromRig;
	};

	struct ListField
	{
		const char* key;
		std::vector<std::string> LibraryMeta::* member;
	};

	// The tables are the one list of meta fields: parsing, writing and the caller's edit all read them, so a new field is
	// one line here and its constant.
	const StringField kStringFields[] =
	{
		{ LibraryMetaKey::AssetId, &LibraryMeta::assetId, true },
		{ LibraryMetaKey::RigName, &LibraryMeta::rigName, true },
		{ LibraryMetaKey::SkeletonHash, &LibraryMeta::skeletonHash, true },
		{ LibraryMetaKey::PluginVersion, &LibraryMeta::pluginVersion, true },
		{ LibraryMetaKey::Created, &LibraryMeta::created, true },
		{ LibraryMetaKey::Template, &LibraryMeta::templateName, false },
		{ LibraryMetaKey::Skeleton, &LibraryMeta::skeleton, false },
		{ LibraryMetaKey::Author, &LibraryMeta::author, false },
		{ LibraryMetaKey::Description, &LibraryMeta::description, false },
		{ LibraryMetaKey::Category, &LibraryMeta::category, false },
		{ LibraryMetaKey::Status, &LibraryMeta::status, false },
	};

	const ListField kListFields[] =
	{
		{ LibraryMetaKey::Tags, &LibraryMeta::tags },
		{ LibraryMetaKey::RigRegions, &LibraryMeta::rigRegions },
	};

	// Applies every key of metaJson onto meta. allowRigFields says whether the keys the rig fills may appear: a file
	// has them, a caller's edit must not.
	void applyFields(const nlohmann::json& metaJson, const std::string& where, bool allowRigFields, LibraryMeta& meta, ValidationReport& report)
	{
		if (!metaJson.is_object())
		{
			report.addError(where, "must be a JSON object");
			return;
		}

		std::set<std::string> known;
		for (const StringField& field : kStringFields)
		{
			known.insert(field.key);
		}
		for (const ListField& field : kListFields)
		{
			known.insert(field.key);
		}

		for (const auto& [key, value] : metaJson.items())
		{
			if (known.count(key) == 0)
			{
				report.addError(where + "." + key, "is not a meta key");
			}
		}

		for (const StringField& field : kStringFields)
		{
			const auto it = metaJson.find(field.key);
			if (it == metaJson.end())
			{
				continue;
			}
			if (field.isFilledFromRig && !allowRigFields)
			{
				report.addError(where + "." + field.key, "is filled from the rig and can't be set");
			}
			else if (!it->is_string())
			{
				report.addError(where + "." + field.key, "must be a string, not " + it->dump());
			}
			else
			{
				meta.*(field.member) = it->get<std::string>();
			}
		}

		for (const ListField& field : kListFields)
		{
			const auto it = metaJson.find(field.key);
			if (it == metaJson.end())
			{
				continue;
			}
			if (!it->is_array())
			{
				report.addError(where + "." + field.key, "must be a list of strings, not " + it->dump());
				continue;
			}
			std::vector<std::string> values;
			for (const nlohmann::json& element : *it)
			{
				if (!element.is_string() || element.get<std::string>().empty())
				{
					report.addError(where + "." + field.key, "must hold non-empty strings, not " + element.dump());
					break;
				}
				values.push_back(element.get<std::string>());
			}
			meta.*(field.member) = values;
		}
	}
}

nlohmann::ordered_json libraryMetaToJson(const LibraryMeta& meta)
{
	nlohmann::ordered_json json = nlohmann::ordered_json::object();
	for (const StringField& field : kStringFields)
	{
		if (!(meta.*(field.member)).empty())
		{
			json[field.key] = meta.*(field.member);
		}
	}
	for (const ListField& field : kListFields)
	{
		if (!(meta.*(field.member)).empty())
		{
			json[field.key] = meta.*(field.member);
		}
	}
	return json;
}

void parseLibraryMeta(const nlohmann::json& metaJson, const std::string& where, LibraryMeta& outMeta, ValidationReport& report)
{
	outMeta = LibraryMeta();
	applyFields(metaJson, where, true, outMeta, report);
}

void applyCallerMeta(const std::string& jsonText, LibraryMeta& meta, ValidationReport& report)
{
	const std::string where = "meta";
	nlohmann::json editJson;
	try
	{
		editJson = nlohmann::json::parse(jsonText);
	}
	catch (const nlohmann::json::exception& e)
	{
		report.addError(where, e.what());
		return;
	}

	// Applied to a copy, so a bad key leaves the caller's meta as it was.
	LibraryMeta edited = meta;
	const std::size_t errorCount = report.errors.size();
	applyFields(editJson, where, false, edited, report);
	if (report.errors.size() == errorCount)
	{
		meta = edited;
	}
}

void renameVersion2MetaKeys(nlohmann::json& file)
{
	if (!file.is_object())
	{
		return;
	}
	const auto itMeta = file.find(PoseFileKey::Meta);
	if (itMeta == file.end() || !itMeta->is_object())
	{
		return;
	}

	const std::pair<const char*, const char*> renames[] =
	{
		{ LibraryMetaKey::V2Variant, LibraryMetaKey::RigName },
		{ LibraryMetaKey::V2SkeletonId, LibraryMetaKey::Skeleton },
	};
	for (const auto& [oldKey, newKey] : renames)
	{
		const auto it = itMeta->find(oldKey);
		if (it != itMeta->end() && !itMeta->contains(newKey))
		{
			(*itMeta)[newKey] = *it;
			itMeta->erase(oldKey);
		}
	}
}

std::string utcTimestampNow()
{
	const std::time_t now = std::time(nullptr);
	std::tm utc{};
#ifdef _WIN32
	gmtime_s(&utc, &now);
#else
	gmtime_r(&now, &utc);
#endif
	std::array<char, 32> text{};
	std::strftime(text.data(), text.size(), "%Y-%m-%dT%H:%M:%SZ", &utc);
	return text.data();
}
