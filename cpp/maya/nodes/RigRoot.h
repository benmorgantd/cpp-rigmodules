#pragma once
#include "maya/shared/RigModule.h"

#include <string>
#include <unordered_map>
#include <vector>

#include <maya/MDagModifier.h>
#include <maya/MDagPathArray.h>
#include <maya/MPxNode.h>
#include <maya/MStatus.h>
#include <maya/MString.h>

// Everything createRig needs. CreateRigCmd resolves it before anything touches the scene.
struct RigBuildInputs
{
	MString templatePath;
	// The asset the rig belongs to (D29). Checked by the command. Templates carry no asset, so one template serves many.
	std::string assetId;
	// The rig's final namespace, without leading or trailing ':'. Empty is the root namespace.
	// Every node the build creates goes here, and every input joint is moved here.
	MString rigNamespace;
	// Every joint must carry markup. Names may be in any namespace.
	MDagPathArray joints;
	// Fail a mirrored module whose joint is more than 0.001 from its reflected source joint (D28).
	bool checkMirrorPositions = false;
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
	static MObject aRigVersion;
	static MObject aPluginVersion;
	static MObject aPublishVersion;
	static MObject aRigModules;
	static MObject aRigTemplateName;
	static MObject aAssetRoot;
	static MObject aChildren;
	static MObject aConstraints;
	static MObject aDisplayLayers;
	static MObject aJointDisplayLayer;
	static MObject aMeshDisplayLayer;
	static MObject aControlDisplayLayer;
	static MObject aSelectionSets;
public:
	static MObject createRigRoot(
		const MString& name,
		int version,
		const MString& templateName,
		MObject oAssetRoot,
		MDagModifier& dagMod);
	static MStatus createRig(const RigBuildInputs& inputs, MDagModifier& dagMod);
};
