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

// Everything createRig needs. CreateRigCmd resolves it before anything touches the scene.
struct RigBuildInputs
{
	MString templatePath;
	// The asset the rig belongs to (D29). Checked by the command. Templates carry no asset, so one template serves many.
	std::string assetId;
	// The rig's final namespace, without leading or trailing ':'. Empty is the root namespace. Unset means the default,
	// "{asset name}_{rigName}" (D54), which needs the template, so createRig resolves it in its read phase.
	// Every node the build creates goes here, and every input joint is moved here. A namespace holds one rig.
	std::optional<MString> rigNamespace;
	// Every joint must carry markup. Names may be in any namespace.
	MDagPathArray joints;
	// Fail a mirrored module whose joint is more than 0.001 from its reflected source joint (D28).
	bool checkMirrorPositions = false;
	// The rig root this rig is attached to (D55), or null. A template with "parent:" ids needs one, and one without
	// them refuses it. The "parent:module.socket" ids resolve in its namespace.
	MObject oParentRigRoot;
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
	static MObject aAssetId;
	static MObject aRigName;
	static MObject aSkeletonHash;
	static MObject aRigVersion;
	static MObject aPluginVersion;
	static MObject aPublishVersion;
	static MObject aRigModules;
	static MObject aRigTemplateName;
	static MObject aChildren;
	static MObject aConstraints;
	static MObject aJointDisplayLayer;
	static MObject aMeshDisplayLayer;
	static MObject aControlDisplayLayer;
	static MObject aSelectionSets;
	static MObject aAllGroup;
	static MObject aMeshGroup;
	static MObject aLook;
	static MObject aParentRig;     // from the parent's attachments (D55)
	static MObject aAttachments;   // to each attached rig's parentRig
public:
	static MObject createRigRoot(
		const MString& assetId,
		const MString& name,
		const MString& skeletonHash,
		int version,
		const MString& templateName,
		MDagModifier& dagMod);
	// outRigNamespace is the namespace the rig was built into, inputs.rigNamespace or the default, for the command's redo.
	static MStatus createRig(const RigBuildInputs& inputs, MDagModifier& dagMod, MString& outRigNamespace);
};
