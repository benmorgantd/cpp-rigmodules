#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// Module and constraint mirroring (D28). A template entry with "mirror": true gets a copy on the other side,
// added before validation, so every check and the build treat it like an entry written by hand.

#include "RigJsonStructs.h"

#include <string>

struct ValidationReport;

namespace TemplateMirror
{
	// "Left" <-> "Right". Anything else, Center included, comes back unchanged.
	std::string swapSide(const std::string& side);

	// "arm_l" <-> "arm_r", with the RigNaming side suffixes. Empty when the name ends in neither.
	std::string swapSideSuffix(const std::string& name);

	// Appends a mirror for every module and constraint with "mirror": true. A mirror is its source with:
	//   side Left <-> Right, the name's _l <-> _r suffix swapped, and every [side, usage] selector in moduleArgs swapped
	//   (Center selectors stay, so they fail later as joints claimed twice);
	//   each "module.x" id in parentStructure, target and drivers pointing at the other side's module when the
	//   named module is Left or Right (the socket or child plug name never changes);
	//   mirror false and mirrorOf set to the source's index.
	// Mirrors go after every source entry, so the source indices in error paths still match the file.
	void expandMirrors(RigRootData& rigData, ValidationReport& report);
}
