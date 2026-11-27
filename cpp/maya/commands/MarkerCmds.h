#pragma once

#include "maya/shared/Utilities.h"

#include <maya/MArgList.h>
#include <maya/MDagModifier.h>
#include <maya/MDGModifier.h>
#include <maya/MPxCommand.h>
#include <maya/MStatus.h>
#include <maya/MSyntax.h>

// Markers (design/markers.md): placed points that follow one joint, kept in a file of their own. A built rig has no
// node for a rig-only marker, so the offsets are authored on locators in an editing scene.

// cmds.rigMarkerMarkup(locator, usage="heel", side="Left", joint="skel:ankle_00_L_JNT", export=False)
//   Marks transforms as markers. Create needs usage and side. joint defaults to the nearest marked joint above the node,
//   export to False. cmds.rigMarkerMarkup(locator, edit=True, export=True) changes the flags given, query=True returns
//   the markup as JSON text (or the one value asked for: usage, side, joint as "[side, usage]" text, export), and
//   remove=True deletes the markup. Undoable except for a query.
class MarkerMarkupCmd : public MPxCommand
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
	bool fIsUndoable = true;
};

// cmds.saveRigMarkers(locator1, locator2, ..., file="C:/assets/hero/markers/body/markers.json")
//   -> the markers hash, 16 hex digits
//   Writes the named marked nodes: each offset is the node's world times the inverse of its followed joint's world,
//   so the joints should be at rest. The joint is found among the marked joints of the node's namespace. Every node
//   needs markup and a name in the naming convention (heel_00_L_MRK). Not undoable: it changes no scene data.
class SaveRigMarkersCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;
};

// cmds.loadRigMarkers(file="C:/assets/hero/markers/body/markers.json", namespace="skel", displaySize=2.0)
//   -> the locators it created, in the file's order
//   Creates a marked locator for each marker at its place on the marked joints of the namespace (the current one when
//   left out), drawn at displaySize. Fails and creates nothing when the file is invalid, a joint is missing or a node in
//   the namespace has a marker's name. Undoable.
class LoadRigMarkersCmd : public MPxCommand
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
	CreatedNamespaces fCreatedNamespaces;
};

// cmds.mirrorRigMarkers(locator1, locator2, ...)
//   -> the locators it wrote, the other side's
//   The authoring mirror: reflects each marker's world matrix across X = 0 and writes the other side's marker, a node
//   named and marked for the other side that follows the same usage of joint there (Left and Right swap, Center fails).
//   A node that is already there moves, otherwise one is made. The nodes must all be in one namespace. The build never
//   mirrors markers: a template's "mirror": true only swaps the selector's side. Undoable.
class MirrorRigMarkersCmd : public MPxCommand
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
	MString fNamespace;   // the nodes' namespace without colons at its ends, current again for the redo
};

// cmds.getRigMarkers(rigNode)
//   -> JSON text: a list of {markup, joint, export, name, world}, one per marker of the file the rig was built with, with
//   the world matrix at rest (16 numbers, row by row). "[]" for a rig built with no file. Any node of the rig works.
class GetRigMarkersCmd : public MPxCommand
{
public:
	MStatus doIt(const MArgList& args) override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;
};
