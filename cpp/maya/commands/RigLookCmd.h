#pragma once

#include "maya/shared/LookScene.h"

#include <maya/MArgList.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

// cmds.setRigLook("hero_body:rigRoot", mesh=["hero_body:body_geo"], file=["C:/assets/hero/looks/red/weights/body_geo.json"],
//     asset="hero", look="red", publishVersion="", skeleton="C:/assets/hero/skeletons/biped/skeleton.json",
//     rigSkeleton="C:/assets/hero/skeletons/biped/skeleton.json", ignoreSkeletonHash=False) -> the lookRoot's name (D54)
//   Puts the rig in a look. The caller imports the look's meshes into the rig's namespace first and passes them, because
//   RigModules reads no asset paths (D24). Each mesh is matched to a weight file by its meshId. The command loads the
//   weights onto the rig's joints, which fails when a file's skeletonHash is not the rig's, deletes the previous look's
//   meshes (the lookRoot's members, never any other mesh), names each new mesh after its file, and creates or updates
//   the lookRoot. Controls and animation are not touched. One undo step.
//   skeleton is the look's skeleton.json and rigSkeleton the rig's: when both are given and the skeletons differ, the
//   error names every joint that differs. Both are optional, and only improve that message and fail early.
//   ignoreSkeletonHash turns a weight file's hash mismatch into a warning.
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
