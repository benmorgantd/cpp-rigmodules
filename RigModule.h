#pragma once

#include "RigJsonStructs.h"

#include <maya/MPxNode.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MString.h>
#include <maya/MStatus.h>
#include <maya/MPlug.h>
#include <maya/MDataBlock.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>
#include <vector>

class RigModuleNodeBase : public MPxNode
{
public:
	RigModuleNodeBase();
	virtual ~RigModuleNodeBase() override;

	// Core Maya evaluation engine entry point
	MStatus compute(const MPlug& plug, MDataBlock& data) override;

	// Pure virtual solver interface method implemented by concrete module subclasses
	virtual MStatus evaluateModuleSolver(const MPlug& plug, MDataBlock& data) = 0;

	// Registers base infrastructure attributes shared across all modules
	static MStatus initializeBaseAttributes();

public:
	// Data access helpers
	static MMatrix getInputMatrix(MDataBlock& data, const MObject& attr, unsigned int idx);
	static MMatrix getInputMatrix(MDataBlock& data, const MObject& attr);
	static void setOutputMatrix(MDataBlock& data, const MObject& attr, unsigned int idx, const MMatrix& mat);
	static void setOutputMatrix(MDataBlock& data, const MObject& attr, const MMatrix& mat);

	// Matrix calculation & socket parsing helpers
	static MMatrix computeOffsetParentMatrix(const MMatrix& mControlWorld, const MMatrix& mDagParentWorld);
	static int resolveSocketIndex(const MString& key, unsigned int totalElements);
	static bool parseSocketIdentifier(const MString& fullIdentifier, MString& outModuleName, MString& outSocketName);

public:
	// Base Metadata Attributes
	static MObject moduleData;              // JSON string payload for custom module serialization

	// Network Message Plugs
	static MObject aRigRoot;                 // Message link to central RigRoot node
	static MObject aRigControls;             // Message array link to controls on this module

	// Matrix Socket & Control DAG Driving Plugs
	static MObject aOutputSocketMatrix;      // Output matrix array providing connection sockets for downstream modules

	// Enum and Data Attributes
	static MObject aSide;                   // Module side (Center, Left, Right)
public:
	// Shared creation & build methods
	static MStatus connectModuleToRigRoot(MObject& oRigRoot, MDGModifier& dgMod, const MObject& oModule);
	static MObjectArray buildModules(const std::vector<RigModuleData>& flatModuleList, MObject oRigRoot, MDGModifier& dgMod, MDagModifier& dagMod);
	static void parentModuleControls(const MObjectArray& rigModules, const std::vector<RigModuleData>& flatModuleList, MDGModifier& dgMod, MDagModifier& dagMod);
};