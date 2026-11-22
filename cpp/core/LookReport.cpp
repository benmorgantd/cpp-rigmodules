#include "LookReport.h"
#include "Constants.h"

#include <nlohmann/json.hpp>

std::string serializeLookReport(const LookReport& report)
{
	nlohmann::json meshes = nlohmann::json::array();
	for (const LookMeshReport& mesh : report.meshes)
	{
		nlohmann::json entry = nlohmann::json::object();
		entry[LookReportKey::MeshId] = mesh.meshId;
		entry[LookReportKey::MeshName] = mesh.meshName;
		entry[LookReportKey::Result] = mesh.result;
		entry[LookReportKey::Reasons] = mesh.reasons;
		entry[LookReportKey::StaleSkeleton] = mesh.staleSkeleton;
		meshes.push_back(std::move(entry));
	}
	nlohmann::json out = nlohmann::json::object();
	out[LookReportKey::LookRoot] = report.lookRoot;
	out[LookReportKey::SkeletonStale] = report.skeletonStale;
	out[LookReportKey::Meshes] = std::move(meshes);
	return out.dump();
}
