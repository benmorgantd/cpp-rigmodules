#pragma once

#include "RigModule.h"

class FkChainNode : public RigModuleNodeBase
{
public:
	FkChainNode();
	virtual ~FkChainNode() override;

	MStatus evaluateModuleSolver(const MPlug& plug, MDataBlock& data) override;

	static void* creator();
	static MStatus initialize();

public:
	static MTypeId id;

	// Top-Level Input Attributes
	static MObject aInputRestMatrix;

	// Control Array Attributes
	static MObject aControlMatrix;
	static MObject aOutputJointOPM;
	static MObject aOutputControlOPM;

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