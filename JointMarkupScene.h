#pragma once

#include "JointMarkup.h"

#include <string>

#include <maya/MDagPathArray.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>
#include <maya/MString.h>

struct ValidationReport;

// The marked joints a rig is built from. dagPaths[i] is the joint for index entry i.
struct MarkedJointSet
{
	JointMarkupIndex index;
	MDagPathArray dagPaths;
};

// Scene side of joint markup: reading the attribute and finding joints.
// readMarkup displays its own errors. collectMarkedJoints and claimJoint return theirs for the caller's report.
class JointMarkupScene
{
public:
	// Indexes exactly these joints. Every one must carry valid markup, and (side, usage) must be unique across them.
	// Adds every problem to report instead of stopping at the first.
	static void collectMarkedJoints(const MDagPathArray& joints, MarkedJointSet& outSet, ValidationReport& report);

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
