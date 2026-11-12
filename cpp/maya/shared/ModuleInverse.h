#pragma once

#include "core/PoseFile.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <maya/MMatrix.h>

// What a module's inverse is given and returns (D41, inverse-solve.md section 4). A module's inverse matches world
// matrices only: it knows nothing about source skeletons, proportions or files, and reads no scene. Skeleton: no module
// implements one yet. They all return ModuleInverse::notBuilt.

// One control's derived channel values, in the pose file's shape (channel name, UI units).
using ControlValues = PoseChannels;

// A module's controls' sidecar values for one frame, by control index.
using SidecarChannels = std::vector<ControlValues>;

// What a module's inverse reads from its stored rests, once per restore.
struct ModuleInverseContext
{
	std::vector<MMatrix> controlRestWorld;   // by control index
	std::vector<MMatrix> jointRestWorld;     // by joint index (the jointOPM index)
	std::vector<std::string> derivedChannels;   // the control channels the module derives from joints. Declared, not discovered.
};

// One module's input for one frame.
struct ModuleInverseInput
{
	std::vector<std::optional<MMatrix>> jointWorld;   // by joint index. Empty: the joint is missing from the source.
	std::vector<MMatrix> parentDelta;                 // by child plug index: the parent socket's motion since rest, from the sweep
	const SidecarChannels& sidecar;
};

struct ModuleInverseResult
{
	std::vector<ControlValues> controls;   // by control index: the channels it derived
	std::vector<MMatrix> socketWorld;      // the forward result, so the sweep can feed the next module
	bool solved = false;                   // false: the controls are left to the sidecar, and reason says why
	std::string reason;
};

using ModuleInverseFn = std::function<ModuleInverseResult(const ModuleInverseContext&, const ModuleInverseInput&)>;

namespace ModuleInverse
{
	// The result of a module whose inverse isn't written. A module that can never be inverted returns the same shape
	// with its own reason, so a new module has to decide (ModuleContract checks the descriptor has one).
	inline ModuleInverseResult notBuilt(const char* moduleType)
	{
		ModuleInverseResult result;
		result.reason = std::string(moduleType) + " has no inverse yet (D41 skeleton)";
		return result;
	}
}
