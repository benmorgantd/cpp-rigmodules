#pragma once

#include <maya/MArgList.h>
#include <maya/MDagModifier.h>
#include <maya/MPxCommand.h>
#include <maya/MStatus.h>
#include <maya/MSyntax.h>

// Skeleton files (D54, design/rigs-and-looks.md section 5): a skeleton's joints and the hash that says whether two
// skeletons are the same. The file is the skeleton's only stored form, so save and load are how a skeleton is kept.

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

// cmds.loadRigSkeleton(file="C:/assets/hero/skeletons/body/skeleton.json", namespace="hero_body")
//   -> the joints it created, in the file's order
//   Creates the file's joints at rest, marked, in the namespace (the current one when left out, made when missing).
//   Fails and creates nothing when the file is invalid or a node in the namespace already has a joint's name.
//   Undoable.
class LoadRigSkeletonCmd : public MPxCommand
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
	MDagModifier fDagMod;
	MString fNamespace;   // without colons at its ends, "" for the root
};
