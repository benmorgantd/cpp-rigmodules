#pragma once

#include "RigModule.h"
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>

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
	static MObject inputRestMatrix;

	// Control Array Attributes
	static MObject controlMatrix;
	static MObject outputControlOPM;
	static MObject outputJointOPM;

	static const MString commandString;
public:
	static MObject createModule(
		const MString& moduleName,
		const MDagPathArray& jointDags,
		MObject oParentModule,
		unsigned int parentModuleSocketIndex,
		MObject oRigRoot,
		MDGModifier& dgMod,
		MDagModifier& dagMod,
		MStatus* status = nullptr
	);
	static MObject createModule(
		const RigModuleData& moduleData,
		MObject rigRoot,
		MObject parentModule,
		MDGModifier& dgMod,
		MDagModifier& dagMod
	);
};

class FkChainNodeSetupCmd : public MPxCommand
{
public:
	FkChainNodeSetupCmd();
	~FkChainNodeSetupCmd() override;

	MStatus doIt(const MArgList& args) override;

	static void* creator();
	static MSyntax newSyntax();
	static const MString commandString;

	// TODO: undo\redoIt()
};