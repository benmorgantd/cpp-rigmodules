#pragma once
#include "maya/shared/RigModule.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <maya/MDagModifier.h>
#include <maya/MDagPathArray.h>
#include <maya/MPxNode.h>
#include <maya/MStatus.h>
#include <maya/MString.h>

class RigRootNode : public MPxNode
{
public:
	// instance methods first, the "behavior" of the node
	RigRootNode();
	virtual ~RigRootNode() override; 

	// Static methods next. Static because Maya calls these before an instance exists
	static MStatus initialize();
	static void* creator();
public:
	// second public block is for readability, to separate behavior methods from attributes.
	static MTypeId id;

	// Attribute handles. All node attributes have to be defined here.
	static MObject aAssetId;
	static MObject aRigName;
	static MObject aSkeletonHash;
	static MObject aMarkersHash;   // the markers file's hash at build, "" when none was passed
	static MObject aMarkers;       // every marker and its rest world as JSON text, "" when none was passed
	static MObject aRigVersion;
	static MObject aPluginVersion;
	static MObject aPublishVersion;
	static MObject aRigModules;
	static MObject aRigTemplateName;
	static MObject aConstraints;
	static MObject aCollisions;
	static MObject aJointDisplayLayer;
	static MObject aMeshDisplayLayer;
	static MObject aControlDisplayLayer;
	static MObject aSelectionSets;
	static MObject aAllGroup;
	static MObject aMeshGroup;
	static MObject aLook;
	static MObject aParentRig;     // from the parent's attachments
	static MObject aAttachments;   // to each attached rig's parentRig
public:
	static MObject createRigRoot(
		const MString& assetId,
		const MString& name,
		const MString& skeletonHash,
		const MString& markersHash,
		const MString& markersJson,
		int version,
		const MString& templateName,
		MDagModifier& dagMod);
};
