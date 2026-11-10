#pragma once

#include "maya/shared/SkinWeightScene.h"

#include <vector>

#include <maya/MArgList.h>
#include <maya/MDGModifier.h>
#include <maya/MPxCommand.h>
#include <maya/MStatus.h>
#include <maya/MSyntax.h>

// Skin weight files (D35). Influences are keyed by joint markup, and a file loads only onto the mesh it was saved
// from: the same meshId and the same topology.

// cmds.setRigMeshId("hero:body_geo") -> ["5f0c..."]
//   Gives each mesh a GUID meshId, unless it already has one or newId=True. With no meshes named, acts on every
//   selected mesh: the one command that reads the selection (author, 2026-11-09). Returns the ids in mesh order. Undoable.
class SetRigMeshIdCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	MStatus redoIt() override;
	MStatus undoIt() override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;

private:
	MDGModifier fDGMod;
};

// cmds.saveRigSkinWeights("hero:body_geo", file="C:/assets/hero/weights/body_geo.json") -> number of influences
class SaveRigSkinWeightsCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;
};

// cmds.loadRigSkinWeights("hero:body_geo", file="C:/assets/hero/weights/body_geo.json") -> the skinCluster's name
//   Undoable. A file that fails validation or doesn't match the mesh changes nothing.
class LoadRigSkinWeightsCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	MStatus redoIt() override;
	MStatus undoIt() override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;

private:
	SkinWeightLoad fLoad;
};

// cmds.pruneToMaxInfluences("hero:body_geo", maxInfluences=4) -> number of vertices changed
//   On every vertex with more than maxInfluences non-zero weights, keeps the largest and normalizes them. Other
//   vertices are not written. With no meshes named, acts on every selected mesh. Undoable.
class PruneToMaxInfluencesCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	MStatus redoIt() override;
	MStatus undoIt() override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;

private:
	std::vector<SkinWeightPrune> fPrunes;   // one per mesh
};
