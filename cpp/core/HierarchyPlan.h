#pragma once

// Maya-free core: no Maya headers in this file or its .cpp.
// The rule behind FkHierarchy (D71): which of the joints under a root become controls, and who each control's parent
// is. The Maya layer walks the scene and hands over every joint below the root in depth-first order. This turns that
// list into the module's control order, so the rule is tested without Maya.

#include "JointMarkup.h"

#include <string>
#include <vector>

// One joint below (and including) the root, in the order a depth-first walk visits it.
struct HierarchyJoint
{
	std::string label;        // The joint's name, for messages
	bool hasMarkup = false;   // False for a joint with no rigMarkup: an error if it is kept
	JointSelector selector;   // Its markup, when hasMarkup
	int parent = -1;          // Index of its parent in this list, or -1 for the root. Always lower than its own index.
};

struct HierarchyPlan
{
	std::vector<std::size_t> joints;   // Indices into the input list of the joints that get a control, in control order
	std::vector<int> parentIndex;      // By control: the control index of its parent, or -1 for the root
	std::vector<std::string> errors;
	std::vector<std::string> warnings;
};

namespace HierarchyPlanner
{
	// Keeps the root and every joint below it, except what sits below a terminator. A terminator is kept: it ends its
	// branch. The input is in depth-first order and joints[0] is the root.
	// A terminator the walk never meets is an error, or with dropMissingTerminators (a mirrored module, D28) a warning,
	// since the other side's hierarchy may differ. A kept joint with no markup is an error.
	HierarchyPlan plan(const std::vector<HierarchyJoint>& joints, const std::vector<JointSelector>& terminators, bool dropMissingTerminators);

	// The socket a joint's control is reached through: the markup's side lowercased, then its usage as written, joined
	// with an underscore ("left_upperArm"). Fails on a usage that would break a "module.socket" id (D71).
	bool socketName(const JointSelector& selector, std::string& outName, std::string& outError);
}
