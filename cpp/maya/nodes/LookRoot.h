#pragma once

#include <maya/MPxNode.h>
#include <maya/MStatus.h>
#include <maya/MTypeId.h>

// The record of the look a rig wears: one per rig that has had a look applied, connected to the rig root by
// rigRoot.look -> lookRoot.rigRoot. It holds the look's asset id, name and publish version, and one message connection
// per node the look brought in (the meshes), so a swap deletes exactly those and never a mesh the animator added.
// It has no compute and no inputs, so it adds nothing to evaluation. setRigLook creates and updates it.
class LookRootNode : public MPxNode
{
public:
	static MStatus initialize();
	static void* creator();

public:
	static MTypeId id;

	static MObject aAssetId;
	static MObject aLookName;
	static MObject aPublishVersion;
	static MObject aRigRoot;
	static MObject aMembers;
};
