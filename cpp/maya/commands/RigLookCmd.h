#pragma once

#include "maya/shared/LookScene.h"

#include <maya/MArgList.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

// cmds.setRigLook("hero_body:rigRoot", mesh=["hero_body:body_geo"], file=["C:/assets/hero/looks/red/weights/body_geo.json"],
//     rigidMesh=["hero_body:cigarette_geo"], asset="hero", look="red", publishVersion="",
//     skeleton="C:/assets/hero/skeletons/biped/skeleton.json", rigSkeleton="C:/assets/hero/skeletons/biped/skeleton.json",
//     ) -> the look report, JSON text
//   Puts the rig in a look. The caller imports the look's meshes into the rig's namespace first and passes them, because
//   RigModules reads no asset paths. Each mesh is matched to a weight file by its meshId. The command loads the
//   weights onto the rig's joints, deletes the previous look's meshes (the lookRoot's members, never any other mesh), names each new mesh after its file, and creates or updates
//   the lookRoot. Controls and animation are not touched. One undo step.
//   rigidMesh names meshes that have a meshId and no weight file yet: each is bound whole to the rig's topmost skin
//   joint and becomes a member like the others, keeping its name, so a look can carry meshes not skinned yet. A
//   weight save then writes their first file. A call needs at least one mesh or rigidMesh.
//   skeleton is the look's skeleton.json and rigSkeleton the rig's: when both are given and the skeletons differ, the
//   warning names every joint that differs, and the report's skeletonStale is true. Both are optional.
//   A build never fails over a weight file that no longer fits. The skeletonHash only marks a file stale. A mesh
//   whose file names a joint that is gone gets that weight folded into the next skin joint up, and a mesh whose topology
//   changed, or that can't be folded, is bound rigid to the topmost skin joint. The result says which, per mesh (core
//   LookReport): {"lookRoot": name, "skeletonStale": bool, "meshes": [{"meshId", "meshName", "result": "loaded" |
//   "folded" | "rigid", "reasons": [text], "staleSkeleton": bool}]}.
class SetRigLookCmd : public MPxCommand
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
	LookSwap fSwap;
};
