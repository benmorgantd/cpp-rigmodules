#pragma once

#include "core/SkeletonFile.h"

#include <maya/MDagModifier.h>
#include <maya/MDagPathArray.h>
#include <maya/MObjectArray.h>
#include <maya/MStatus.h>

struct ValidationReport;

// Scene side of the skeleton file (D54): reading joints into a Skeleton and creating joints from one. The rig build's
// skeletonHash stamp reads its input joints here too (rigs-and-looks.md phase 3).
class SkeletonScene
{
public:
	// Reads exactly these joints as they are now, which the caller has at rest. Every joint must carry valid markup,
	// unique by (side, usage), as JointMarkupScene::collectMarkedJoints checks: marker joints count like any other.
	// A joint's parent is its nearest DAG ancestor among the joints, and its restLocal is its world matrix relative
	// to that parent's. Adds every problem to report; outSkeleton is complete only when report has no errors.
	static void readSkeleton(const MDagPathArray& joints, Skeleton& outSkeleton, ValidationReport& report);

	// Reads every marked joint of the namespace (a skeleton file's worth, D70). Adds every problem to report, including
	// the namespace's markup problems and a skeleton with more than one root joint; outSkeleton is complete only when
	// report has no errors.
	static void readNamespaceSkeleton(const MString& absoluteNamespace, Skeleton& outSkeleton, ValidationReport& report);

	// The topmost skin joint of the marked joints in the namespace (core findTopSkinJoints): where a mesh with no
	// weights is bound. Several is a warning naming them, and the first is used. Fails, with an error, when the
	// namespace's markup is invalid or no joint is marked skin. Only parents and markup are read, so a posed rig is fine.
	static MStatus findTopSkinJoint(const MString& absoluteNamespace, JointSelector& outJoint);

	// The inverse of readSkeleton: creates the skeleton's joints in the current namespace (the caller holds a
	// ScopedRigNamespace), parents first, each with its name, rotate order, rest and rigMarkup. oOutJoints follows
	// skeleton.joints. readSkeleton on the result gives the same skeleton and hash.
	// The rest goes into translate, jointOrient, scale and shear, with rotate left at zero, and segmentScaleCompensate
	// is off, so a joint's local matrix is exactly its restLocal.
	// Runs dagMod.doIt() itself, because nodes and attributes must exist before their plugs take values. The caller
	// undoes with dagMod.undoIt() and redoes with dagMod.doIt(), the namespace current again.
	// Fails, with everything it made undone, when a joint can't have its name because a node in the namespace has it:
	// Maya would rename the joint, and a renamed joint is another skeleton.
	static MStatus createSkeleton(const Skeleton& skeleton, MDagModifier& dagMod, MObjectArray& oOutJoints);
};
