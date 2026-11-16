#pragma once

#include "core/SkeletonFile.h"

#include <maya/MDagPathArray.h>

struct ValidationReport;

// Scene side of the skeleton file (D54): reading joints into a Skeleton. The rig build's skeletonHash stamp reads
// its input joints here too (rigs-and-looks.md phase 3).
class SkeletonScene
{
public:
	// Reads exactly these joints as they are now, which the caller has at rest. Every joint must carry valid markup,
	// unique by (side, usage), as JointMarkupScene::collectMarkedJoints checks: marker joints count like any other.
	// A joint's parent is its nearest DAG ancestor among the joints, and its restLocal is its world matrix relative
	// to that parent's. Adds every problem to report; outSkeleton is complete only when report has no errors.
	static void readSkeleton(const MDagPathArray& joints, Skeleton& outSkeleton, ValidationReport& report);
};
