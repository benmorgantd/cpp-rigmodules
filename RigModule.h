#pragma once

#include "RigJsonStructs.h"

#include <maya/MPxNode.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MString.h>
#include <maya/MStatus.h>
#include <maya/MDataBlock.h>
#include <maya/MDGModifier.h>
#include <maya/MDagModifier.h>
#include <vector>

// This class is just a class with shared helper methods for rig module nodes. 
// You CAN NOT have "inheritance" for Maya nodes because of MObject pointers.
class RigModuleNodeBase : public MPxNode
{
public:
	RigModuleNodeBase();
	virtual ~RigModuleNodeBase() override;
	virtual MStatus compute(const MPlug& plug, MDataBlock& data) override;
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
	// Shared creation & build methods
	static MStatus connectModuleToRigRoot(MObject& oRigRoot, MDGModifier& dgMod, const MObject& oModule, MObject& aRigRoot);
	static MObjectArray buildModules(const std::vector<RigModuleData>& flatModuleList, MObject oRigRoot, MDGModifier& dgMod, MDagModifier& dagMod);
	static void parentModuleControls(const MObjectArray& rigModules, const std::vector<RigModuleData>& flatModuleList, MDGModifier& dgMod, MDagModifier& dagMod);
};