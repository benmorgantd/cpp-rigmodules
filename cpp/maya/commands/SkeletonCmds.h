#pragma once

#include <maya/MArgList.h>
#include <maya/MPxCommand.h>
#include <maya/MStatus.h>
#include <maya/MSyntax.h>

// Skeleton files (D54, design/rigs-and-looks.md section 5): a skeleton's joints and the hash that says whether two
// skeletons are the same.

// cmds.saveRigSkeleton("skel:root_jnt", "skel:spine_1_jnt", ..., file="C:/assets/hero/skeletons/body/skeleton.json")
//   -> the skeleton hash, 16 hex digits
//   Writes the named joints as they are now, so they should be at rest. Every one must carry markup. Not undoable:
//   it changes no scene data.
class SaveRigSkeletonCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;
};
