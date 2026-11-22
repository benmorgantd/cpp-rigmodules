#include "SkinWeightFold.h"

#include <algorithm>
#include <map>
#include <utility>

namespace
{
	using Key = std::pair<std::string, std::string>;

	Key keyOf(const JointSelector& selector)
	{
		return { selector.side, selector.usage };
	}
}

FoldResult foldSkinWeights(SkinWeights& weights, const std::vector<CurrentJoint>& joints)
{
	FoldResult result;
	std::map<Key, bool> current;   // (side, usage) -> is a skin joint
	for (const CurrentJoint& joint : joints)
	{
		current[keyOf(joint.joint)] = joint.isSkinned;
	}
	std::map<Key, const SkeletonLink*> saved;
	for (const SkeletonLink& link : weights.skeleton)
	{
		saved[keyOf(link.joint)] = &link;
	}

	// Where each influence goes: itself when the rig has it, otherwise the first skin joint above it.
	std::vector<JointSelector> targets(weights.influences.size());
	for (std::size_t i = 0; i < weights.influences.size(); ++i)
	{
		const JointSelector& influence = weights.influences[i];
		if (current.count(keyOf(influence)) > 0)
		{
			targets[i] = influence;
			continue;
		}
		bool isFound = false;
		const SkeletonLink* link = saved.count(keyOf(influence)) > 0 ? saved[keyOf(influence)] : nullptr;
		// A saved skeleton has no loop, but a hand-edited file may: the walk is bounded by the joint count.
		for (std::size_t step = 0; link != nullptr && link->hasParent && !isFound && step <= weights.skeleton.size(); ++step)
		{
			const auto parent = current.find(keyOf(link->parent));
			if (parent != current.end() && parent->second)
			{
				targets[i] = link->parent;
				isFound = true;
			}
			else
			{
				const auto next = saved.find(keyOf(link->parent));
				link = next == saved.end() ? nullptr : next->second;
			}
		}
		if (isFound)
		{
			result.folded.push_back({ influence, targets[i] });
		}
		else
		{
			result.unfoldable.push_back(influence);
		}
	}
	if (!result.unfoldable.empty() || result.folded.empty())
	{
		return result;
	}

	// The new influence list: every target once, in the order first met.
	std::vector<JointSelector> influences;
	std::vector<unsigned int> newIndex(targets.size());
	for (std::size_t i = 0; i < targets.size(); ++i)
	{
		const auto found = std::find_if(influences.begin(), influences.end(), [&](const JointSelector& other)
		{
			return keyOf(other) == keyOf(targets[i]);
		});
		newIndex[i] = static_cast<unsigned int>(found - influences.begin());
		if (found == influences.end())
		{
			influences.push_back(targets[i]);
		}
	}
	for (std::vector<std::pair<unsigned int, double>>& vertex : weights.weights)
	{
		std::vector<std::pair<unsigned int, double>> merged;
		for (const auto& [index, weight] : vertex)
		{
			const unsigned int target = newIndex[index];
			const auto existing = std::find_if(merged.begin(), merged.end(), [target](const std::pair<unsigned int, double>& pair)
			{
				return pair.first == target;
			});
			if (existing == merged.end())
			{
				merged.emplace_back(target, weight);
			}
			else
			{
				existing->second += weight;
			}
		}
		vertex = std::move(merged);
	}
	weights.influences = std::move(influences);
	sortSkinInfluences(weights);
	return result;
}

std::vector<SkeletonLink> skeletonLinks(const Skeleton& skeleton)
{
	std::vector<SkeletonLink> links;
	links.reserve(skeleton.joints.size());
	for (const SkeletonJoint& joint : skeleton.joints)
	{
		links.push_back({ { joint.markup.side, joint.markup.usage }, joint.hasParent, joint.parent });
	}
	std::sort(links.begin(), links.end(), [](const SkeletonLink& a, const SkeletonLink& b)
	{
		return keyOf(a.joint) < keyOf(b.joint);
	});
	return links;
}

std::vector<CurrentJoint> currentJoints(const Skeleton& skeleton)
{
	std::vector<CurrentJoint> joints;
	joints.reserve(skeleton.joints.size());
	for (const SkeletonJoint& joint : skeleton.joints)
	{
		joints.push_back({ { joint.markup.side, joint.markup.usage }, joint.markup.isSkinned });
	}
	return joints;
}
