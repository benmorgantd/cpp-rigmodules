#pragma once

#include "RigModule.h"
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>
#include <maya/MArgDatabase.h>

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
	//static MResultType currentResultType;

	// TODO: undo\redoIt()

private:
	static const char* kNameFlagShort;
	static const char* kNameFlagLong;
	static const char* kJointsFlagShort;
	static const char* kJointsFlagLong;
	static const char* kParentModuleShort;
	static const char* kParentModuleLong;
	static const char* kParentSocketIndexLong;
	static const char* kParentSocketIndexShort;
};