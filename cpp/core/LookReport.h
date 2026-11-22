#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// What setRigLook did with each mesh (D70). A build never fails over a weight file that no longer fits, so the report is
// how a stale or degraded mesh is seen: it goes to the caller as JSON, and the Asset Editor shows it.
// {
//     "lookRoot": "hero:lookRoot", "skeletonStale": false,
//     "meshes": [
//         {"meshId": "...", "meshName": "body_00_M_GEO", "result": "folded",
//          "reasons": ["[Left, wristTwist] was folded into [Left, wrist]"], "staleSkeleton": true}
//     ]
// }
// result is one of LookResult (core/Constants.h). Status is computed by each build and never stored (D44).

#include <string>
#include <vector>

struct LookMeshReport
{
	std::string meshId;
	std::string meshName;
	std::string result;                  // a LookResult
	std::vector<std::string> reasons;    // why the mesh is not simply "loaded", one line each
	bool staleSkeleton = false;          // the weight file was saved from another skeleton than the rig's
};

struct LookReport
{
	std::string lookRoot;
	bool skeletonStale = false;          // the look's skeleton file is not the rig's
	std::vector<LookMeshReport> meshes;
};

// The JSON text above, compact.
std::string serializeLookReport(const LookReport& report);
