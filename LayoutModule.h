#pragma once
#include "RigModule.h"
#include <maya/MPxCommand.h>

// A Layout module has no joints, and is only used as a control hierarchy.
// Its rest position is always at the origin.
// It is also always the first module in the hierarchy and has no parent.
class LayoutModuleNode : public MPxNode
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
	static MObject controlMatrix;
	static MObject outputSocketMatrix;
	static MObject aRigControls;
	static MObject aSide;
	static MObject childModules;
	static MObject rigRoot;
	static MObject aNumSockets;
	static const MString commandString;
public:
	static MObject createModule(MObject& oRigRoot, MDGModifier& dgMod, MDagModifier& dagMod, const unsigned short numControls);
};

class LayoutModuleSetupCmd : public MPxCommand
{
public:
	LayoutModuleSetupCmd();
	~LayoutModuleSetupCmd() override;

	MStatus doIt(const MArgList& args) override;

	static void* creator();
	static MSyntax newSyntax();
	static const MString commandString;

	// TODO: undo\redoIt()
};