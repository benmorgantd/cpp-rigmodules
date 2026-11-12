#pragma once

#include <optional>
#include <string>
#include <vector>

#include <maya/MMatrix.h>

// What a module's inverse is given and returns (D41, inverse-solve.md section 4). A module's inverse matches world
// matrices only: it knows nothing about source skeletons, proportions, files or channels, and reads no scene. Every
// vector is indexed by the module's slot, the logical index its controlInputData, jointOPM, outputSocketMatrix and
// childPlugs arrays share (FkChain: control i, joint i, socket i. Aim: aim 0, target 1, up 2. Layout: control i,
// socket i). A slot with no control, joint or child plug is an empty entry. Matrices are in Maya's internal units.

// What a module's inverse reads from its stored rests, once per solve.
struct ModuleInverseContext
{
	std::vector<MMatrix> controlRestWorld;   // by slot. Aim's slot 0 holds the aim joint's rest, there is no control.
};

// One module's input for one frame.
struct ModuleInverseInput
{
	std::vector<std::optional<MMatrix>> jointWorld;   // by slot. Empty: the joint is missing from the source.
	std::vector<MMatrix> parentDelta;                 // by slot: the child plug's parent motion since rest, from the sweep (identity when unconnected)
	std::vector<MMatrix> fallbackLocal;               // by slot: the control's local matrix from the sidecar, or rest, for when its joint can't give it
};

struct ModuleInverseResult
{
	std::vector<std::optional<MMatrix>> controlLocal;   // by slot: the control's local matrix, when the joints gave it
	std::vector<MMatrix> socketWorld;                   // by slot: the forward result, so the sweep can feed the modules below
	bool solved = false;                                // false: a joint the module needs is missing, and reason says why
	std::string reason;
	std::vector<std::string> warnings;                  // the driver shows each distinct one once
};

using ModuleInverseFn = ModuleInverseResult(*)(const ModuleInverseContext&, const ModuleInverseInput&);

namespace ModuleInverse
{
	// The control local that puts a control at world: world = local * rest * parentDelta (solveControlStep), so
	// local = world * parentDelta^-1 * rest^-1.
	inline MMatrix localFromWorld(const MMatrix& mWorld, const MMatrix& mRestWorld, const MMatrix& mParentDelta)
	{
		return mWorld * mParentDelta.inverse() * mRestWorld.inverse();
	}

	// A chain of controls where each is the parent of the next and the first hangs from parentDelta[0]: FkChain, and
	// Layout (no joints, identity rests, no parent). With useJoints, a control whose joint is present takes the local
	// that reproduces the joint, and the others use fallbackLocal.
	ModuleInverseResult solveChain(const ModuleInverseContext& context, const ModuleInverseInput& input, bool useJoints);
}
