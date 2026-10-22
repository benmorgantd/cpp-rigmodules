#pragma once
#include "RigModule.h"

// A Layout module has no joints, and is only used as a control hierarchy.
// Its rest position is always at the origin.
// It is also always the first module in the hierarchy and has no parent.
class LayoutModuleNode : RigModuleNodeBase
{
public:
	LayoutModuleNode();
	virtual ~LayoutModuleNode() override;

	MStatus compute(const MPlug& plug, MDataBlock& data) override;

	static void* creator();
	static MStatus initialize();

public:
	static MTypeId id;

	// Control Array Attributes
	static MObject aControlMatrix;

public: // ---- Shared Attributes All Modules Have (but that must be unique) ----
	static MObject aModuleData;              // JSON string payload for custom module serialization
	static MObject aRigRoot;                 // Message link to central RigRoot node
	static MObject aRigControls;             // Message array link to controls on this module
	static MObject aOutputSocketMatrix;      // Output matrix array providing connection sockets for downstream modules
	static MObject aSide;                    // Module side (Center, Left, Right)
	// Command string
	static const MString commandString;
public:
	static RigModuleBuildResult createModule(MObject& oRigRoot, MDGModifier& dgMod, MDagModifier& dagMod, const unsigned short numControls);
};