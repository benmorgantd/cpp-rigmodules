#pragma once

#include <maya/MMatrix.h>
#include <maya/MPoint.h>
#include <maya/MDagModifier.h>
#include <maya/MObject.h>
#include <maya/MPxTransform.h>

struct TransformAttrEntry
{
	const MObject& attr;
	double value;
};

// Static handle lookup without string parsing during execution
static const TransformAttrEntry kDefaultTransformValues[] = {
	{ MPxTransform::translateX, 0.0 }, { MPxTransform::translateY, 0.0 }, { MPxTransform::translateZ, 0.0 },
	{ MPxTransform::rotateX,    0.0 }, { MPxTransform::rotateY,    0.0 }, { MPxTransform::rotateZ,    0.0 },
	{ MPxTransform::scaleX,     1.0 }, { MPxTransform::scaleY,     1.0 }, { MPxTransform::scaleZ,     1.0 }
};

class MathUtils
{
public:
	static MMatrix computeAimMatrix(
		const MPoint& pAim,
		const MPoint& pTarget,
		const MPoint& pUp,
		const MMatrix& fallbackMatrix,
		const float epsilon=1e-6);

	// Motion of a transform since rest, in the space both matrices share. Row-major: rest^-1 * world.
	// Inline because solveFkChain calls it once per control per compute.
	static inline MMatrix computeDeltaFromRest(const MMatrix& mRest, const MMatrix& mWorld)
	{
		return mRest.inverse() * mWorld;
	}
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
	static MDagPath getDagPathFromName(const MString& name);
	// Resolves any DG or DAG node by name. Reports nothing, the caller decides what a miss means.
	static MStatus getNodeFromName(const MString& name, MObject& oNode);
	static MMatrix getMatrixFromPlug(const MPlug& pMatrixPlug);
	static MStatus setMatrixPlug(MPlug& matrixPlug, MMatrix matrix, MDagModifier& dagMod);
	static MStatus getJointAndWorldMatrix(const MString& jointName, MObject& oJoint, MMatrix& mWorld);

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
	// Splits "module.name" on the first '.'. False if there is no '.', or either side is empty.
	static bool splitIdentifier(const std::string& identifier, std::string& outModule, std::string& outName);
	static MString joinStringArray(const MStringArray& stringArray, const char* joinChar);  // for joining MString arrays to char separated strings
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

