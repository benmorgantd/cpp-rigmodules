#pragma once

#include <optional>
#include <string>

#include <maya/MDagModifier.h>
#include <maya/MDagPathArray.h>
#include <maya/MObject.h>
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

// The two-pass rig build behind createRigFromTemplate: a read phase that validates the whole template before the scene is touched,
// then a write phase that creates the rig root, the layout, every module, the constraints, the groups, layers and selection sets.
namespace RigBuild
{
	// outRigNamespace is the namespace the rig was built into, inputs.rigNamespace or the default, for the command's redo.
	MStatus createRig(const RigBuildInputs& inputs, MDagModifier& dagMod, MString& outRigNamespace);
}
