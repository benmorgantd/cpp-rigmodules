#pragma once

#include <optional>
#include <string>
#include <vector>

#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>

// What a module's inverse is given and returns (D41, inverse-solve.md section 4). A module's inverse matches world
// matrices only: it knows nothing about source skeletons, proportions, files or channels, and reads no scene. Every
// vector is indexed by the module's slot, the logical index its controlInputData, jointOPM, outputSocketMatrix and
// childPlugs arrays share (FkChain: control i, joint i, socket i. Aim: aim 0, target 1, up 2. Layout: control i,
// socket i). A slot with no control, joint or child plug is an empty entry. Matrices are in Maya's internal units.

// What a module's inverse reads from its stored rests, once per solve.
struct ModuleInverseContext
{
	std::vector<MMatrix> controlRestWorld;   // by slot. Aim's slot 0 holds the aim joint's rest, there is no control.
	// How many of the leading slots are one kind of control, from the module's ikControlCount attribute when it has one
	// (Ribbon: its ik controls, and the slots after them are the tweakers). 0 for a module with a single kind. An anchored Ribbon (D80)
	// counts the two ends of its curve too, which have no control and no slot, so its leading ik control slots are this less 2.
	unsigned int ikControlCount = 0;
	// How many of the trailing slots are fk controls, from the module's fkControlCount attribute when it has one (Ribbon, D59).
	unsigned int fkControlCount = 0;
	// By ik control, from the module's fkRollShare attribute when it has one (Ribbon: the template's twistRamp, D75). Empty or
	// short: the rest of the shares are 1.
	std::vector<double> fkRollShare;
	// From the module's anchorRest, startAnchorRotation and endAnchorRotation attributes when it has them (the anchored Ribbon, D80): the
	// three anchors' rests, and what its curve takes from the start and end anchors (AnchorRotation). Empty and 0 for every other module.
	std::vector<MMatrix> anchorRest;
	short startAnchorRotation = 0;
	short endAnchorRotation = 0;
	// By slot, from the module's controlParentIndex attribute (FkHierarchy, D71): the slot of each control's parent, -1 for
	// the root. Empty for every module whose controls form one chain.
	std::vector<int> parentIndex;
	// The module's own stored rests, in its own order, from ModuleDescriptor::readInverseRest (ReverseFoot: the ankle, the ankle plug's
	// rest and its pivots). Empty for a module without the hook.
	std::vector<MMatrix> moduleRest;
};

// A scalar channel a module's inverse needs and the joints can't give (D41 amendment, D52 section 6): the name of
// the channel on the control at that slot, and the value the driver passes when the control has no such channel.
// A module lists them in ModuleDescriptor::inverseChannels, and receives their values in ModuleInverseInput::channels
// in the same order. Names are the animator's, as in a pose file.
struct ModuleInverseChannel
{
	const char* name;
	unsigned int slot;
	double defaultValue;
};

// One module's input for one frame.
struct ModuleInverseInput
{
	std::vector<std::optional<MMatrix>> jointWorld;   // by slot. Empty: the joint is missing from the source.
	std::vector<MMatrix> parentDelta;                 // by slot: the child plug's parent motion since rest, from the sweep (identity when unconnected)
	std::vector<MMatrix> fallbackLocal;               // by slot: the control's local matrix from the sidecar, or rest, for when its joint can't give it
	std::vector<double> channels;                     // by ModuleDescriptor::inverseChannels, in internal units (angles in radians): the sidecar's value, else the rig's current one
	// A TwoBoneIk's ikTarget (D74, design/reverse-foot.md section 10). For the module that feeds the target (a foot): the joint the target
	// moves, the TwoBoneIk's end, from the source. For the TwoBoneIk: the feeding module's motion F, where the target's motion since rest is
	// F * the handle's, so the handle is placed to make F reach the joints. Empty when the module has no such link.
	std::optional<MMatrix> ikTargetJointWorld;
	std::optional<MMatrix> ikTargetMotion;
	// For the module that feeds a TwoBoneIk's ikTarget: the motion since rest of the space that TwoBoneIk's IK handle hangs in (its ik child plug's
	// parent, a Layout's), so the foot's channels are read in the rig's space and a turned or tilted Layout stays in the Layout (author,
	// 2026-11-25). The identity otherwise.
	MMatrix ikHandleParentDelta;
};

struct ModuleInverseResult
{
	std::vector<std::optional<MMatrix>> controlLocal;   // by slot: the control's local matrix, when the joints gave it
	std::vector<MMatrix> socketWorld;                   // by slot: the forward result, so the sweep can feed the modules below
	bool solved = false;                                // false: a joint the module needs is missing, and reason says why
	std::string reason;
	std::vector<std::string> warnings;                  // the driver shows each distinct one once
	// By ModuleDescriptor::inverseChannels: a scalar channel the joints gave (ReverseFoot's footRoll, bank and toeTap), in internal units.
	// An empty entry, or a short vector, leaves the channel to the sidecar or the rig.
	std::vector<std::optional<double>> channels;
	// For a module that feeds a TwoBoneIk's ikTarget: its motion F, which the driver hands the TwoBoneIk as ModuleInverseInput::ikTargetMotion.
	std::optional<MMatrix> ikTargetMotion;
};

using ModuleInverseFn = ModuleInverseResult(*)(const ModuleInverseContext&, const ModuleInverseInput&);

// Reads a module's own stored rests into ModuleInverseContext::moduleRest, once per solve. Set only by a module whose inverse needs more than
// its controls' rests (ReverseFoot).
using ModuleInverseRestFn = MStatus(*)(const MObject& oModule, std::vector<MMatrix>& outRest);

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
	// that reproduces the joint, and the others use fallbackLocal. With context.parentIndex (FkHierarchy), each control
	// hangs from its own parent's motion instead of the previous control's, and the root from parentDelta[0].
	ModuleInverseResult solveChain(const ModuleInverseContext& context, const ModuleInverseInput& input, bool useJoints);
}
