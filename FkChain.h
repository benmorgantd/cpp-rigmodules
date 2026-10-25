#pragma once

#include "RigModule.h"

class FkChainNode : public RigModuleNodeBase
{
public:
	FkChainNode();
	virtual ~FkChainNode() override;

	static void* creator();
	static MStatus initialize();
	virtual MStatus compute(const MPlug& plug, MDataBlock& data) override;

public:
	static MTypeId id;

	// Top-Level Input Attributes
	static MObject aControlInputData;
	static MObject aControlMatrix;
	static MObject aControlRestOffset;

	// Compound attribute for child plug relationships to their parent.
	static MObject aChildPlugs;
	static MObject aChildPlugName;
	static MObject aChildPlugParentMatrix;

	// Output attributes
	static MObject aControlOPM;
	static MObject aJointOPM;

	// Message attributes
	static MObject aControlMessage;

public: // ---- Shared Attributes All Modules Have (but that must be unique) ----
	static MObject aModuleData;              // JSON string payload for custom module serialization
	static MObject aRigRoot;                 // Message link to central RigRoot node
	static MObject aRigControls;             // Message array link to controls on this module
	static MObject aOutputSocketMatrix;      // Output matrix array providing connection sockets for downstream modules
	static MObject aSide;                    // Module side (Center, Left, Right)
	// Command String
	static const MString aCommandString;
public:
	static MStatus reparentChildPlugs(       // Function used when parenting child transforms to other module parents
		RigModuleDagReparentArgs
	);

public:
	static RigModuleBuildResult createModule(
		const MString& moduleName,
		const MDagPathArray& jointDags,
		MObject oRigRoot,
		MDGModifier& dgMod,
		MDagModifier& dagMod
	);
	static RigModuleBuildResult createModule(
		const RigModuleData& moduleData,
		MObject rigRoot,
		MDGModifier& dgMod,
		MDagModifier& dagMod
	);
};