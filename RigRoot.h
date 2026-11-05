#pragma once
#include "RigModule.h"

#include <unordered_map>
#include <vector>
#include <string>

#include <maya/MStatus.h>
#include <maya/MPxNode.h>
#include <maya/MPxCommand.h>
#include <maya/MDagModifier.h>
#include <maya/MDagPathArray.h>
#include <maya/MString.h>

// Everything createRig needs. CreateRigCmd resolves it before anything touches the scene.
struct RigBuildInputs
{
	MString templatePath;
	// The rig's final namespace, without leading or trailing ':'. Empty is the root namespace.
	// Every node the build creates goes here, and every input joint is moved here.
	MString rigNamespace;
	// Every joint must carry markup. Names may be in any namespace.
	MDagPathArray joints;
};

class RigRootNode : public MPxNode
{
public:
	// instance methods first, the "behavior" of the node
	RigRootNode();
	virtual ~RigRootNode() override; 

	// Define the compute fn
	MStatus compute(const MPlug& plug, MDataBlock& data) override;

	// Static methods next. Static because Maya calls these before an instance exists
	static MStatus initialize();
	static void* creator();
public:
	// second public block is for readability, to separate behavior methods from attributes.
	static MTypeId id;

	// Attribute handles. All node attributes have to be defined here.
	static MObject aRigName;
	static MObject aRigType;
	static MObject aRigVersion;
	static MObject aRigModules;
	static MObject aRigTemplateName;
	static MObject aAssetRoot;
	static MObject aChildren;
	static MObject aConstraints;
public:
	static MObject createRigRoot(
		const MString& name,
		const MString& type,
		int version,
		const MString& templateName,
		MObject oAssetRoot,
		MDagModifier& dagMod);
	static MStatus createRig(const RigBuildInputs& inputs, MDagModifier& dagMod);
};


// Joints are always passed by full name. The selection is never used.
// createRigFromTemplate -template "C:/rigs/LayoutRig.json" -namespace "rig" skel:root_jnt skel:spine_jnt
// -namespace is the rig's final namespace (default "rig"). The joints are moved into it.
class CreateRigCmd : public MPxCommand
{
public:
	CreateRigCmd();
	virtual ~CreateRigCmd() override;

	MStatus doIt(const MArgList& args) override;
	MStatus redoIt() override;
	MStatus undoIt() override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();

	static const char* aCommandString;

private:
	MDagModifier fDagMod;
	// Redo must run fDagMod with the rig namespace current, because its node names are relative to it.
	MString fRigNamespace;
};