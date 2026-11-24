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
	// Sets the five meta fields a rig fills (LibraryMetaKey): the asset id, rigName and skeletonHash of the rig root
	// (D54), its plugin version stamp, and the time now. The other fields are left as they are.
	static MStatus fillRigMeta(const MObject& oRigRoot, LibraryMeta& meta);

	// The meta for a file being saved: the caller's JSON object from the flag, if it is set (applyCallerMeta), then the
	// fields the rig fills. Reports a bad flag value and returns a failure status, so nothing is written.
	static MStatus readSaveMeta(const MArgDatabase& argData, const char* metaFlag, const char* commandName, const MObject& oRigRoot, LibraryMeta& outMeta);

	// A status, never an error (D70, D73): warns when a file was saved from a rig built with a markers file (its meta has a
	// markersHash) and this rig was built with another one, or with none. The values are still valid, but a foot rolls
	// about other pivots, so the result may look different. A file with no markersHash (made before markers, or from a rig
	// with none) reads as unknown and gets no warning. Returns true when it warned.
	static bool warnIfMarkersDiffer(const MObject& oRigRoot, const LibraryMeta& fileMeta, const char* commandName);

	// The (side, usage) entries of a file that no control of the rig has, in the file's order.
	static MStatus findUnmatchedControls(
		const MObject& oRigRoot,
		const std::vector<std::pair<std::string, std::string>>& fileControls,
		std::vector<std::pair<std::string, std::string>>& outUnmatched);
};
