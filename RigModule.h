#pragma once

#include <maya/MPxNode.h>
#include <maya/MPxCommand.h>
#include <maya/MStatus.h>
#include <maya/MObject.h>
#include <maya/MTypeId.h>
#include <maya/MPlug.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>
#include <maya/MString.h>

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
	// Public helper functions
	// For data read\write
	static MMatrix getInputMatrix(MDataBlock& data, const MObject& attr, unsigned int idx);
	static MMatrix getInputMatrix(MDataBlock& data, const MObject& attr);
	static void setOutputMatrix(MDataBlock& data, const MObject& attr, unsigned int idx, const MMatrix& mat);

	// Networking functionality
	static MObjectArray getChildModules(MObject& moduleNode);
	static MObject getParentModule(MObject& moduleNode);

	// Control creation
	static MObject createRigControl(MObject& moduleNode, MDagModifier& dagMod, const MString& jointName);

public:
	// Base Metadata Attributes
	static MObject moduleData;          // JSON string payload for custom module serialization

	// Network Message Plugs
	static MObject rigRoot;             // Message link to central RigRoot node
	static MObject parentModule;        // Message link to parent module
	static MObject childModules;        // Message array link to child modules

	// Base Transformation Matrix Plugs
	static MObject parentWorldMatrix;   // Driving input matrix from parent socket or layout hook
	static MObject parentModuleOffset;  // Matrix for storing the offset transformation to the parent module
	static MObject outputSocketMatrix;  // Output matrix array providing connection sockets for children
};