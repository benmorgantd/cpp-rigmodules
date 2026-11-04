#pragma once

#include "JointMarkup.h"

#include <string>
#include <vector>

#include <maya/MDagPath.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>
#include <maya/MString.h>

// Marked joints from one namespace. dagPaths[i] is the joint for index entry i.
struct MarkedJointSet
{
	JointMarkupIndex index;
	std::vector<MDagPath> dagPaths;
};

// Scene side of joint markup: reading the attribute and finding joints. All errors are reported here.
class JointMarkupScene
{
public:
	// Collects every joint in exactly this namespace (nested namespaces excluded) that carries markup.
	// ":" is the root namespace. Fails on unparseable markup or a duplicate (side, usage).
	static MStatus collectMarkedJoints(const MString& namespaceName, MarkedJointSet& outSet);

	// outHasMarkup is false, with success, when the joint has no markup attribute.
	static MStatus readMarkup(const MObject& oJoint, bool& outHasMarkup, JointMarkup& outMarkup);

	// Resolves a module's selector to its joint and rest world matrix, and claims the joint for that module.
	static MStatus claimJoint(
		MarkedJointSet& markedJoints,
		const JointSelector& selector,
		const std::string& moduleName,
		MObject& oOutJoint,
		MMatrix& mOutRestWorld);
};
