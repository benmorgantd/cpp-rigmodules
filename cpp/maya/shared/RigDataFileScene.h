#pragma once

#include "core/LibraryMeta.h"

#include <string>
#include <utility>
#include <vector>

#include <maya/MArgDatabase.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>

// Scene side of the library files (D51): what a rig adds to a file's meta when it is saved, and how well a file fits a rig.
class RigDataFileScene
{
public:
	// Sets the four meta fields a rig fills (LibraryMetaKey): the asset id, the variant (the rig root's rigName), the
	// plugin version stamp of the rig, and the time now. The other fields are left as they are.
	static MStatus fillRigMeta(const MObject& oRigRoot, LibraryMeta& meta);

	// The meta for a file being saved: the caller's JSON object from the flag, if it is set (applyCallerMeta), then the
	// fields the rig fills. Reports a bad flag value and returns a failure status, so nothing is written.
	static MStatus readSaveMeta(const MArgDatabase& argData, const char* metaFlag, const char* commandName, const MObject& oRigRoot, LibraryMeta& outMeta);

	// The (side, usage) entries of a file that no control of the rig has, in the file's order.
	static MStatus findUnmatchedControls(
		const MObject& oRigRoot,
		const std::vector<std::pair<std::string, std::string>>& fileControls,
		std::vector<std::pair<std::string, std::string>>& outUnmatched);
};
