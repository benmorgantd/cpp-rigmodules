#pragma once

#include <maya/MMatrix.h>
#include <maya/MPoint.h>


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
	static MMatrix bakeJointOpmAndRestMatrix(const MDagPath& jointDag, MPlug& pRestMatrixPlug);
};