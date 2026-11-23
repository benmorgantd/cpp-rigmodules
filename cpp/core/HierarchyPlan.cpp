#include "HierarchyPlan.h"

#include <cctype>

namespace
{
	bool sameSelector(const JointSelector& a, const JointSelector& b)
	{
		return a.side == b.side && a.usage == b.usage;
	}
}

HierarchyPlan HierarchyPlanner::plan(const std::vector<HierarchyJoint>& joints, const std::vector<JointSelector>& terminators, bool dropMissingTerminators)
{
	HierarchyPlan result;

	// Control index of each kept joint, -1 for a joint cut off below a terminator.
	std::vector<int> controlOfJoint(joints.size(), -1);
	std::vector<bool> isTerminator(joints.size(), false);
	std::vector<bool> terminatorMet(terminators.size(), false);

	for (std::size_t i = 0; i < joints.size(); ++i)
	{
		const HierarchyJoint& joint = joints[i];
		const bool hasParent = joint.parent >= 0;
		if (hasParent)
		{
			const std::size_t parent = static_cast<std::size_t>(joint.parent);
			if (controlOfJoint[parent] < 0 || isTerminator[parent])
			{
				continue;
			}
		}

		if (!joint.hasMarkup)
		{
			result.errors.push_back("joint '" + joint.label + "' has no joint markup, and the hierarchy reaches it. Mark it, or end the walk above it with a terminator");
			continue;
		}

		controlOfJoint[i] = static_cast<int>(result.joints.size());
		result.joints.push_back(i);
		result.parentIndex.push_back(hasParent ? controlOfJoint[static_cast<std::size_t>(joint.parent)] : -1);

		for (std::size_t t = 0; t < terminators.size(); ++t)
		{
			if (sameSelector(joint.selector, terminators[t]))
			{
				isTerminator[i] = true;
				terminatorMet[t] = true;
			}
		}
	}

	for (std::size_t t = 0; t < terminators.size(); ++t)
	{
		if (terminatorMet[t])
		{
			continue;
		}
		const std::string message = "terminator " + formatJointSelector(terminators[t]) + " is not at or below the root";
		if (dropMissingTerminators)
		{
			result.warnings.push_back(message + ", so it is dropped");
		}
		else
		{
			result.errors.push_back(message);
		}
	}
	return result;
}

bool HierarchyPlanner::socketName(const JointSelector& selector, std::string& outName, std::string& outError)
{
	if (selector.usage.find('.') != std::string::npos)
	{
		outError = "joint usage '" + selector.usage + "' contains '.', which a socket id can't carry";
		return false;
	}

	outName = selector.side;
	for (char& c : outName)
	{
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	outName += "_" + selector.usage;
	return true;
}
