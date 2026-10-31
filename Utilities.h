#pragma once

#include <maya/MMatrix.h>
#include <maya/MPoint.h>
#include <maya/MDagModifier.h>


class MathUtils
{
public:
	static MMatrix computeAimMatrix(
		const MPoint& pAim,
		const MPoint& pTarget,
		const MPoint& pUp,
		const MMatrix& fallbackMatrix,
		const float epsilon=1e-6);
};

class DagUtils
{
public:
	static void lockAndHideTranslate(MObject oControl);
	static void lockAndHideRotate(MObject oControl);
	static void lockAndHideScale(MObject oControl);
	static MStatus bakeJointTransformsToOPM(const MObject& oTransform, const MMatrix& mTransformWorld, MDagModifier& dagMod);
	static MStatus zeroLocalTransforms(const MObject& oTransform, MDagModifier& dagMod);
	static MStatus setInheritsTransforms(const MObject& transform, bool value, MDagModifier& dagMod);
	static MStatus setSegmentScaleCompensate(MObject& oTransform, bool value, MDagModifier& dagMod);
	static MDagPath getDagPathFromName(MString& name);
	static MMatrix getMatrixFromPlug(const MPlug& pMatrixPlug);
	static MStatus setMatrixPlug(MPlug& matrixPlug, MMatrix matrix, MDagModifier& dagMod);
};

class DagPlugAndSocketUtils
{
public:
	static MStatus setParentSocketName(const MObject& oModule, const unsigned int index, const char* name, MDagModifier& dagMod);
	static MStatus setChildPlugName(const MObject& oModule, const unsigned int index, const char* name, MDagModifier& dagMod);
};

class StringUtils
{
public:
	static bool nameListContains(const MString& nameList, const MString& name);  // For semicolon-separated lists to compare whole tokens.
};

class RigModuleCommandUtils
{
public:
	// TODO: moving the functionality (the implementation) back in the cpp file will improve compile times. 
	// Helper methods
	static MString getModuleNameFromArgs(const MArgDatabase& argData);
	static MDagPathArray getJointsFromArgs(const MArgDatabase& argData, MStatus& status);
	static MObject getRigRootFromArgs(const MArgDatabase& argData);
	static MSyntax createBaseModuleSyntax(MStatus& status);
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

