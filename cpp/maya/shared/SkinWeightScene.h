#pragma once

#include "core/SkinWeightFile.h"
#include "core/SkinWeightSmooth.h"

#include <memory>
#include <string>
#include <vector>

#include <maya/MDagPath.h>
#include <maya/MDagPathArray.h>
#include <maya/MDGModifier.h>
#include <maya/MDoubleArray.h>
#include <maya/MIntArray.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>
#include <maya/MString.h>

// Scene side of skin weight files (D35): the mesh id, reading a mesh's weights and applying a file to a mesh.
// Influences are joints found by their markup, in the mesh's namespace. Every function displays its own errors.
class SkinWeightScene
{
public:
	// The mesh's meshId, empty when it has none.
	static MStatus getMeshId(const MDagPath& mesh, std::string& outId);

	// Queues giving the mesh a new GUID meshId, adding the attribute if it is missing.
	static MStatus queueNewMeshId(const MDagPath& mesh, MDGModifier& dgMod, std::string& outId);

	// Reads the mesh's skinCluster into outWeights exactly, influences sorted for saving. Fails when the mesh has no
	// meshId or shares it with another mesh in its namespace, has no skinCluster, has an influence without joint
	// markup or with skin false, or has a vertex whose weights aren't normalized.
	static MStatus readSkinWeights(const MDagPath& mesh, SkinWeights& outWeights);
};

// Applies a weight file to a mesh, undoably. prepare checks everything and changes nothing. apply and undo then
// run as the command's redoIt and undoIt.
// An existing skinCluster is updated in place, so it keeps its place in the deformer stack: influences the file
// doesn't list are removed, missing ones are added, and every weight is set. A mesh without one gets a new one.
// New influences bind at their joint's rest (RigTraversal::getJointRestWorld), so loading on a posed rig binds the
// build pose.
// The file's skeletonHash is checked against the rig the influences belong to (D54): a mismatch fails the load, unless
// ignoreSkeletonHash is set, which makes it a warning. A file with no hash, or joints in no rig, can't be checked: the
// first warns and the second says nothing.
class SkinWeightLoad
{
public:
	MStatus prepare(const MDagPath& mesh, const SkinWeights& weights, bool ignoreSkeletonHash = false);
	MStatus apply();
	MStatus undo();
	MString skinClusterName() const;

private:
	MStatus findSkinCluster(MObject& oOutSkinCluster) const;

	MDagPath fMesh;
	SkinWeights fWeights;
	MDagPathArray fInfluences;              // fWeights.influences resolved to joints, same order
	std::vector<bool> fBindAtRest;          // per influence: set its bindPreMatrix from the joint's rest
	bool fCreatesSkinCluster = false;

	MDGModifier fInfluenceMod;              // skinCluster creation or influence edits
	std::unique_ptr<MDGModifier> fSettingsMod;   // bindPreMatrix and skinningMethod, rebuilt on every apply
	MObject fSkinCluster;
	MIntArray fInfluenceIndices;
	MDoubleArray fOldWeights;
	MDoubleArray fOldBlendWeights;
};

// Prunes one mesh's skinCluster to maxInfluences per vertex (core pruneToMaxInfluences), undoably. prepare reads the
// whole weight table in one getWeights call and prunes it. apply and undo each write the changed vertices' rows, new or
// old, in one setWeights call. Vertices that didn't need pruning are never written.
class SkinWeightPrune
{
public:
	MStatus prepare(const MDagPath& mesh, unsigned int maxInfluences);
	MStatus apply();
	MStatus undo();
	unsigned int changedVertexCount() const;

private:
	MStatus write(MDoubleArray& values);

	MDagPath fShape;
	MObject fSkinCluster;
	MObject fChangedVertices;       // one component holding every changed vertex
	MIntArray fInfluenceIndices;    // every influence, so each changed row is written whole
	MDoubleArray fNewValues;        // the changed rows, pruned
	MDoubleArray fOldValues;        // the same rows before pruning
	unsigned int fChangedCount = 0;
};

// Smooths one mesh's skin weights over their neighbors (core smoothSkinWeights, D64), undoably. bind finds the
// skinCluster and reads which influences are locked and the cluster's max influences. prepare then works out one
// smoothing of some vertices, reading only those and their neighbors (asked of the mesh for those vertices alone, so a
// ring of vertices costs the same on a small and a dense mesh; a whole mesh builds its neighbor map in bulk), and apply
// and undo each write the changed vertices' rows in one setWeights call, over only the influences that moved. Nothing
// about the topology is kept, so a bound object never goes stale on a mesh edit. A locked influence is never written. prepare replaces the last
// preparation, so a command prepares, applies and keeps the object for undo and redo.
class SkinWeightSmooth
{
public:
	MStatus bind(const MDagPath& mesh);
	// vertices null is every vertex. Fails (and changes nothing) on iterations below 1 or a strength outside (0, 1].
	MStatus prepare(const std::vector<unsigned int>* vertices, unsigned int iterations, double strength);
	MStatus apply();
	MStatus undo();
	unsigned int changedVertexCount() const;

private:
	MStatus write(const std::vector<double>& rows);

	MDagPath fShape;
	MObject fSkinCluster;
	unsigned int fInfluenceCount = 0;
	unsigned int fVertexCount = 0;
	std::vector<bool> fLocked;
	unsigned int fMaxInfluences = 0;        // 0 when the cluster doesn't maintain a maximum

	SmoothedWeights fSmoothed;
	MObject fChangedVertices;               // one component holding every changed vertex
	MIntArray fInfluenceIndices;            // the influences fSmoothed lists
};
