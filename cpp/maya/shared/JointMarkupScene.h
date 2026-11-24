#pragma once

#include "core/HierarchyPlan.h"
#include "core/JointMarkup.h"

#include <string>
#include <vector>

#include <maya/MDagPathArray.h>
#include <maya/MDGModifier.h>
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

	// Indexes every joint in the namespace (":rig", or ":" for the root) that has a rigMarkup attribute, through
	// collectMarkedJoints, so invalid markup and two joints sharing a (side, usage) go into report. Unmarked joints
	// are skipped. Walks MItDag filtered to joints, which hands over each path.
	static void collectNamespaceJoints(const MString& absoluteNamespace, MarkedJointSet& outSet, ValidationReport& report);

	// The root joint and every joint below it, in depth-first order (the order MItDag visits children, D71), for
	// HierarchyPlanner. Only a joint whose parent is a collected joint counts: a joint under a locator or group between
	// two joints is not reached. Markup that is missing or invalid gives hasMarkup false. Read-only.
	static MStatus collectJointSubtree(const MDagPath& rootPath, std::vector<HierarchyJoint>& outJoints);

	// outHasMarkup is false, with success, when the joint has no markup attribute.
	static MStatus readMarkup(const MObject& oJoint, bool& outHasMarkup, JointMarkup& outMarkup);

	// The [side, usage] of a marked joint. Reports, and fails, for a node that is not a joint or has no valid markup.
	static MStatus selectorOfJoint(const MObject& oJoint, JointSelector& outSelector);

	// The nearest marked joint above the node in the DAG. False when there is none.
	static bool findMarkedAncestor(const MDagPath& node, JointSelector& outSelector);

	// Queues adding the rigMarkup attribute to a joint that has none. The attribute must exist before its plug takes
	// a value, so the caller runs dgMod.doIt() and then queues newPlugValueString on MPlug(oJoint, oOutMarkupAttr).
	static MStatus queueAddMarkupAttribute(const MObject& oJoint, MDGModifier& dgMod, MObject& oOutMarkupAttr);

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
