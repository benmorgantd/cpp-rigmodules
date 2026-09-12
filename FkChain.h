#pragma once

#include "RigModule.h"
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>
#include <maya/MArgDatabase.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>

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