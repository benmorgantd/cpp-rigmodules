#pragma once

#include "JointMarkup.h"

#include <string>

#include <maya/MDagPathArray.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>
#include <maya/MString.h>

// Marked joints from one namespace. dagPaths[i] is the joint for index entry i.
struct MarkedJointSet
{
	JointMarkupIndex index;
	MDagPathArray dagPaths;
};

// Scene side of joint markup: reading the attribute and finding joints.
// collectMarkedJoints and readMarkup report their own errors. claimJoint returns its error for the caller's report.
class JointMarkupScene
{
public:
	// Collects every joint in exactly this namespace (nested namespaces excluded) that carries markup.
	// ":" is the root namespace. Fails on unparseable markup or a duplicate (side, usage).
	static MStatus collectMarkedJoints(const MString& namespaceName, MarkedJointSet& outSet);

	// outHasMarkup is false, with success, when the joint has no markup attribute.
	static MStatus readMarkup(const MObject& oJoint, bool& outHasMarkup, JointMarkup& outMarkup);

	// Resolves a module's selector to its joint and rest world matrix, and claims the joint for that module.
	// Read-only, so module plan steps call it before anything is queued. Also fails if something already
	// drives the joint's offsetParentMatrix, because the module's connection to it would fail at doIt.
	static MStatus claimJoint(
		MarkedJointSet& markedJoints,
		const JointSelector& selector,
		const std::string& moduleName,
		MObject& oOutJoint,
		MMatrix& mOutRestWorld,
		std::string& outError);
};
