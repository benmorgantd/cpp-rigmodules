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
	static MObject aInputRestMatrix;

	// Control Array Attributes
	static MObject aControlMatrix;
	static MObject aOutputJointOPM;
	static MObject aOutputControlOPM;

public: // ---- Shared Attributes All Modules Have (but that must be unique) ----
	static MObject aModuleData;               // JSON string payload for custom module serialization
	static MObject aRigRoot;                 // Message link to central RigRoot node
	static MObject aRigControls;             // Message array link to controls on this module
	static MObject aOutputSocketMatrix;      // Output matrix array providing connection sockets for downstream modules
	static MObject aSide;                    // Module side (Center, Left, Right)

	// Command String
	static const MString aCommandString;
public:
	static MObject createModule(
		const MString& moduleName,
		const MDagPathArray& jointDags,
		MObject oRigRoot,
		MDGModifier& dgMod,
		MDagModifier& dagMod,
		MStatus* status = nullptr
	);
	static MObject createModule(
		const RigModuleData& moduleData,
		MObject rigRoot,
		MDGModifier& dgMod,
		MDagModifier& dagMod
	);

	//static void parentControls()
};