#pragma once

#include <maya/MPxNode.h>

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
	static MObject aRigControls;        // Message array to controls on this module.

	// Base Transformation Matrix Plugs
	static MObject parentWorldMatrix;   // Driving input matrix from parent socket or layout hook
	static MObject parentModuleOffset;  // Matrix for storing the offset transformation to the parent module
	static MObject outputSocketMatrix;  // Output matrix array providing connection sockets for children

	// Enum attributes
	static MObject aSide;               // Stores the side for the module. All controls of this module will have this side.
};

class RigModuleCommandHelpers
{
public:
	// TODO: moving the functionality (the implementation) back in the cpp file will improve compile times. 
	// Helper methods
	static MString getModuleNameFromArgs(const MArgDatabase& argData);
	static MDagPathArray getJointsFromArgs(const MArgDatabase& argData, MStatus& status);
	static MStatus connectModuleToRigRoot(const MArgDatabase& argData, MDGModifier& dgMod, const MObject& moduleNode);
	static MSyntax createBaseModuleSyntax(MStatus& status);
	static unsigned int getParentModuleSocketIndex(const MArgDatabase& argData);
	static MObject getParentModule(const MArgDatabase& argData);
	static MStatus connectToParentModule(const MObject& oParentModule, const MObject oModule, static unsigned int parentModuleSocketIndex, MDGModifier& dgMod, MMatrix mFirstControlWorld);

public: 
	// Shared attributes
	static const char* kNameFlagShort;
	static const char* kNameFlagLong;
	static const char* kJointsFlagShort;
	static const char* kJointsFlagLong;
	static const char* kParentModuleShort;
	static const char* kParentModuleLong;
	static const char* kParentSocketIndexShort;
	static const char* kParentSocketIndexLong;
	static const char* kRigRootLong;
	static const char* kRigRootShort;
};

