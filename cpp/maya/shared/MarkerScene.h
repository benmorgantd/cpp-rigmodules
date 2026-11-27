#pragma once

#include "core/MarkerFile.h"
#include "maya/shared/JointMarkupScene.h"

#include <maya/MDagModifier.h>
#include <maya/MDagPathArray.h>
#include <maya/MMatrix.h>
#include <maya/MObjectArray.h>
#include <maya/MStatus.h>

#include <array>
#include <string>
#include <vector>

struct ValidationReport;

// Scene side of the marker file (design/markers.md): marker locators in an editing scene, and placing a marker set
// on a build's joints. A built rig has no node for a rig-only marker, so everything here that makes nodes is for
// editing scenes.
class MarkerScene
{
public:
	// The size the editing locators' axis visualizer is drawn at, in scene units, when loadRigMarkers is given none.
	static constexpr double DefaultDisplaySize{ 2.0 };

	// Queues adding the rigMarkerMarkup attribute to a node that has none. The caller runs dgMod.doIt() and then queues
	// newPlugValueString on MPlug(oNode, oOutAttr), as for JointMarkupScene::queueAddMarkupAttribute.
	static MStatus queueAddMarkupAttribute(const MObject& oNode, MDGModifier& dgMod, MObject& oOutAttr);

	// outHasMarkup is false, with success, when the node has no marker markup. A markup that can't be read is an
	// error in outError and a failure.
	static MStatus readMarkup(const MObject& oNode, bool& outHasMarkup, Marker& outMarker, std::string& outError);

	// The marker each node is: its markup, its name without a namespace, and its offset from the joint its markup
	// names, which is looked up among the marked joints of the node's own namespace. Every node must carry markup and
	// a name in the convention, and every joint must exist. Every problem goes into report; outSet is complete only
	// when report has no errors. The joints should be at rest.
	static void readMarkers(const MDagPathArray& nodes, MarkerSet& outSet, ValidationReport& report);

	// The marker's matrix in the world: the joint's rest world times its offset. False, with the reason in outError,
	// when the joint is not among the marked joints. Read-only.
	static bool placeMarker(const Marker& marker, const MarkedJointSet& joints, std::array<double, 16>& outWorld, std::string& outError);

	// Every marker of the set whose joint is among the joints, placed. A marker whose joint is missing is left out and
	// named in a warning in report, because the set may belong to a larger skeleton than the build was given.
	static void placeMarkers(const MarkerSet& set, const MarkedJointSet& joints, std::vector<ResolvedMarker>& outMarkers, ValidationReport& report);

	// Creates a marked, drawn locator for each marker, at its place on the joints of the current namespace (the caller
	// holds a ScopedRigNamespace), named from the marker. oOutNodes follows set.markers. Runs dagMod.doIt() itself, as
	// SkeletonScene::createSkeleton does. Fails, with everything undone, when a node in the namespace has a marker's
	// name or a marker's joint is missing: every problem goes into report.
	static MStatus createMarkerNodes(const MarkerSet& set, const MarkedJointSet& joints, double displaySize, MDagModifier& dagMod, MObjectArray& oOutNodes, ValidationReport& report);

	// Queues moving a marker node, which sits at the root of the DAG, to this world matrix (translate and rotate).
	static MStatus queueSetWorld(const MObject& oNode, const MMatrix& mWorld, MDagModifier& dagMod);

	// The mirror of a world matrix across X = 0: the position's X negated and the rotation conjugated by the reflection,
	// so a right-handed frame stays right-handed.
	static MMatrix mirrorWorld(const MMatrix& mWorld);
};
