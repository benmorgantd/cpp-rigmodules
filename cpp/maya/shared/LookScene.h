#pragma once

#include "core/SkinWeightFile.h"
#include "maya/shared/SkinWeightScene.h"

#include <memory>
#include <string>
#include <vector>

#include <maya/MDagModifier.h>
#include <maya/MDagPathArray.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>
#include <maya/MString.h>

// A look in a rig's scene (D54, design/rigs-and-looks.md section 4.1): its meshes, its weight files and the lookRoot
// node that records them. Every function displays its own errors.
class LookScene
{
public:
	// Compares the rig's stamped skeletonHash with the look's skeleton file text. A mismatch is an error naming both
	// hashes and, when rigSkeletonText is not empty, every joint that differs (core describeSkeletonDifferences). Either
	// text that doesn't parse is an error too.
	static MStatus checkSkeleton(const MObject& oRigRoot, const std::string& lookSkeletonText, const std::string& rigSkeletonText);
};

// What setRigLook was given, after the command has read the files.
struct LookSwapInputs
{
	MObject oRigRoot;
	MDagPathArray meshes;               // the look's imported meshes, in the rig's namespace
	std::vector<SkinWeights> weights;   // the weight files, in any order: each is matched to a mesh by its meshId
	MDagPathArray rigidMeshes;          // meshes with a meshId and no weight file yet: bound whole to the top skin joint
	std::string assetId;                // the look's asset, which may differ from the rig's
	std::string lookName;
	std::string publishVersion;
	bool ignoreSkeletonHash = false;    // a weight file for another skeleton loads with a warning
};

// Puts a rig in a look, undoably, as one step (D54). prepare checks everything and changes nothing. apply and undo then
// run as the command's redoIt and undoIt.
// apply loads each file's weights onto its mesh (SkinWeightLoad, which checks the file's skeletonHash against the
// rig), binds each rigid mesh whole to the topmost skin joint (SkeletonScene::findTopSkinJoint), deletes the previous
// look's meshes and their history, names each new mesh after its file's meshName (a rigid mesh keeps its own), and
// creates or updates the lookRoot. Rigid meshes are members like the others. The controls and animation are never
// touched.
// Only the lookRoot's members are deleted, so a mesh the animator added is safe. Materials a look brings are not members
// and are left in the scene.
class LookSwap
{
public:
	MStatus prepare(const LookSwapInputs& inputs);
	MStatus apply();
	// Safe after a failed or partial apply: undoes whatever ran.
	MStatus undo();
	MString lookRootName() const;

private:
	MObject fRigRoot;
	MString fNamespace;                     // the rig's namespace without colons
	MDagPathArray fMeshes;                  // in weight order
	std::vector<std::string> fMeshNames;    // from the files, to name the meshes
	std::vector<std::unique_ptr<SkinWeightLoad>> fLoads;   // a load owns modifiers, so it is held by pointer
	std::string fAssetId;
	std::string fLookName;
	std::string fPublishVersion;

	std::size_t fLoadsApplied = 0;
	std::unique_ptr<MDagModifier> fSceneMod;   // deletes, renames and the lookRoot, rebuilt on every apply
	MObject fLookRoot;
};
