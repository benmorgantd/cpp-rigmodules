#pragma once

#include "RigModule.h"

#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>
#include <maya/MArgDatabase.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>

// Concrete Module Node
class SingleJointFKNode : public RigModuleNodeBase
{
public:
	SingleJointFKNode();
	virtual ~SingleJointFKNode() override;

	MStatus evaluateModuleSolver(const MPlug& plug, MDataBlock& data) override;

	static void* creator();
	static MStatus initialize();

public:
	static MTypeId id;

	// Top-Level Input Attributes
	static MObject inputRestMatrix;

	// Control Compound Attributes
	static MObject control;
	static MObject controlWorldMatrix;
	static MObject jointParentWorldMatrix;
	static MObject outputControlOPM;
	static MObject outputJointOPM;

	static const MString commandString;
};

// Module Factory Command
class SingleJointFKCmd : public MPxCommand
{
public:
	SingleJointFKCmd();
	virtual ~SingleJointFKCmd() override;

	virtual MStatus doIt(const MArgList& args) override;
	virtual MStatus redoIt() override;
	virtual MStatus undoIt() override;
	virtual bool isUndoable() const override { return true; }

	static void* creator();
	static MSyntax newSyntax();

	static const MString commandString;

private:
	MDGModifier fDgModifier;
	MDagModifier fDagModifier;
};