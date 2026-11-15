#include "maya/shared/Utilities.h"
#include "core/Constants.h"
#include "core/JsonUtils.h"
#include "core/RigTemplateValidation.h"
#include "maya/shared/RigTraversal.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <maya/MArgList.h>
#include <maya/MDagModifier.h>
#include <maya/MDagPath.h>
#include <maya/MFnAttribute.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnMatrixData.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MFnTransform.h>
#include <maya/MGlobal.h>
#include <maya/MItDependencyGraph.h>
#include <maya/MMatrix.h>
#include <maya/MMessage.h>
#include <maya/MModelMessage.h>
#include <maya/MNamespace.h>
#include <maya/MObject.h>
#include <maya/MObjectHandle.h>
#include <maya/MPlug.h>
#include <maya/MProfiler.h>
#include <maya/MQuaternion.h>
#include <maya/MSelectionList.h>
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MStringArray.h>
#include <maya/MTime.h>
#include <maya/MVector.h>

// ---------------------------------
// Math Utilities
// ---------------------------------

MMatrix MathUtils::computeAimMatrix(
	const MPoint& pAim,
	const MPoint& pTarget,
	const MPoint& pUp,
	const MMatrix& fallbackMatrix,
	const float epsilon)
{
	// 1. Primary Aim Axis (+X)
	MVector vAim = pTarget - pAim;
	double aimLen = vAim.length();
	MVector hatX;  // the normalized matrix x axis.

	if (aimLen > epsilon)
	{
		hatX = vAim / aimLen;
	}
	else
	{
		// Fallback to primary X axis of fallback matrix if target overlaps aim origin
		hatX = MVector(fallbackMatrix[0][0], fallbackMatrix[0][1], fallbackMatrix[0][2]).normal();
	}

	// 2. Up Direction Vector (+Z raw direction)
	MVector vUpRaw = pUp - pAim;
	if (vUpRaw.length() < epsilon)
	{
		vUpRaw = MVector(fallbackMatrix[2][0], fallbackMatrix[2][1], fallbackMatrix[2][2]);
	}

	// 3. Side Axis (+Y) via Cross Product
	// Right-handed basis where +X is Aim and +Z is Up:
	// hatZ x hatX = hatY
	MVector hatY = vUpRaw ^ hatX;
	double yLen = hatY.length();

	if (yLen > epsilon)
	{
		hatY /= yLen;
	}
	else
	{
		// Singular/Collinear Fallback: choose orthogonal reference axis
		MVector fallbackUp(0.0, 1.0, 0.0);
		if (std::abs(hatX.y) > 0.9)
		{
			fallbackUp = MVector(0.0, 0.0, 1.0);
		}
		hatY = (fallbackUp ^ hatX).normal();
	}

	// 4. Orthonormalized Up Axis (+Z)
	// hatX x hatY = hatZ
	MVector hatZ = hatX ^ hatY;

	// 5. Construct Row-Major Maya Matrix
	double mData[4][4];

	// Row 0: +X Aim Axis
	mData[0][0] = hatX.x; mData[0][1] = hatX.y; mData[0][2] = hatX.z; mData[0][3] = 0.0;

	// Row 1: +Y Side Axis
	mData[1][0] = hatY.x; mData[1][1] = hatY.y; mData[1][2] = hatY.z; mData[1][3] = 0.0;

	// Row 2: +Z Up Axis
	mData[2][0] = hatZ.x; mData[2][1] = hatZ.y; mData[2][2] = hatZ.z; mData[2][3] = 0.0;

	// Row 3: Origin Position
	mData[3][0] = pAim.x; mData[3][1] = pAim.y; mData[3][2] = pAim.z; mData[3][3] = 1.0;

	return MMatrix(mData);
}

namespace
{
	// Below these the solve has no direction or no softness (centimeters).
	constexpr double kMinIkReach{ 1.0e-4 };
	constexpr double kMinIkSoftDistance{ 1.0e-6 };
	constexpr double kMinIkFold{ 1.0e-6 };
	constexpr double kMinIkPlaneNormal{ 1.0e-4 };
	// The inverse: the end of a soft limb is held this fraction of the reach short of it, a pin below this is none, and
	// the handle's distance is bisected in this many halvings.
	constexpr double kInverseSoftMargin{ 1.0e-4 };
	constexpr double kMinInversePin{ 1.0e-9 };
	constexpr int kInverseSearchSteps{ 64 };

	// Any unit vector perpendicular to a unit vector.
	MVector perpendicularTo(const MVector& vUnit)
	{
		const MVector vReference = std::abs(vUnit.y) < 0.9 ? MVector(0.0, 1.0, 0.0) : MVector(0.0, 0.0, 1.0);
		return (vReference - vUnit * (vReference * vUnit)).normal();
	}

	struct TwoBoneIkBones
	{
		double length1 = 0.0;   // after stretch
		double length2 = 0.0;
		double base = 0.0;      // the end's distance from the root before pin
		double along = 0.0;     // the mid joint's distance along the root-to-handle axis
		double height = 0.0;    // and off it
	};

	// The two bones after soft reach and stretch for a handle at this distance (sections 4.2 and 4.2a): their lengths,
	// the distance from the root the end reaches before pin, and the mid joint's place along the axis and off it.
	// The forward solve and its inverse both call this, so they can't drift.
	TwoBoneIkBones solveTwoBoneIkBones(double length1, double length2, double stretch, double soft, double distance)
	{
		const double reach = length1 + length2;

		// Soft reach: linear up to da, then an exponential that nears the full reach and never gets there.
		const double softDistance = soft * reach;
		double effective = std::min(distance, reach);
		if (softDistance > kMinIkSoftDistance)
		{
			const double softStart = reach - softDistance;
			effective = distance <= softStart ? distance : softStart + softDistance * (1.0 - std::exp(-(distance - softStart) / softDistance));
		}

		// Stretch scales both bones so the end still reaches the handle, with the softened bend.
		const double scale = effective > kMinIkFold ? 1.0 + stretch * (distance / effective - 1.0) : 1.0;
		TwoBoneIkBones bones;
		bones.length1 = length1 * scale;
		bones.length2 = length2 * scale;
		bones.base = std::clamp(effective * scale, std::abs(bones.length1 - bones.length2) + kMinIkFold, bones.length1 + bones.length2);

		// Law of cosines.
		bones.along = (bones.base * bones.base + bones.length1 * bones.length1 - bones.length2 * bones.length2) / (2.0 * bones.base);
		bones.height = std::sqrt(std::max(0.0, bones.length1 * bones.length1 - bones.along * bones.along));
		return bones;
	}

	// A matrix as scale (row lengths) and rotation (unit rows, no shear). Returns the rotation as a quaternion.
	MQuaternion decomposeScaleRotation(const MMatrix& mMatrix, double outScale[3])
	{
		double mRotation[4][4] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 1 } };
		for (int row = 0; row < 3; ++row)
		{
			const MVector vRow(mMatrix[row][0], mMatrix[row][1], mMatrix[row][2]);
			outScale[row] = vRow.length();
			const double inverseLength = outScale[row] > 1.0e-12 ? 1.0 / outScale[row] : 1.0;
			for (int column = 0; column < 3; ++column)
			{
				mRotation[row][column] = mMatrix[row][column] * inverseLength;
			}
		}
		MQuaternion qRotation;
		qRotation = MMatrix(mRotation);
		return qRotation;
	}

	// The short way round: a quaternion and its negative are the same rotation, so the one nearer a is taken.
	MQuaternion slerpShortArc(const MQuaternion& qA, const MQuaternion& qB, double t)
	{
		double dot = qA.x * qB.x + qA.y * qB.y + qA.z * qB.z + qA.w * qB.w;
		const double sign = dot < 0.0 ? -1.0 : 1.0;
		dot *= sign;

		double weightA = 1.0 - t;
		double weightB = t;
		if (dot < 0.9995)
		{
			const double theta = std::acos(dot);
			const double inverseSine = 1.0 / std::sin(theta);
			weightA = std::sin((1.0 - t) * theta) * inverseSine;
			weightB = std::sin(t * theta) * inverseSine;
		}
		MQuaternion qResult(
			qA.x * weightA + qB.x * sign * weightB,
			qA.y * weightA + qB.y * sign * weightB,
			qA.z * weightA + qB.z * sign * weightB,
			qA.w * weightA + qB.w * sign * weightB);
		qResult.normalizeIt();
		return qResult;
	}
}

TwoBoneIkResult MathUtils::computeTwoBoneIk(const TwoBoneIkInput& input)
{
	// 1. Reach and axis. A handle on the root has no direction, so the rest axis stands in.
	const MVector vToHandle = input.handle - input.root;
	const double distance = vToHandle.length();
	const MVector vAxis = distance > kMinIkReach ? vToHandle / distance : input.restAxis.normal();

	// 2. Soft reach and stretch (sections 4.2 and 4.2a), then the bones' triangle.
	const TwoBoneIkBones bones = solveTwoBoneIkBones(input.length1, input.length2, input.stretch, input.soft, distance);

	// 3. The bend plane: the pole's side of the axis, turned by the rest plane angle and the roll (which pin fades out).
	// With the pole on the axis the rest bend normal stands in. It already holds the rest plane angle, so only the roll applies.
	const MVector vToPole = input.pole - input.root;
	MVector vBase = vToPole - vAxis * (vToPole * vAxis);
	double angle = input.rollRadians * (1.0 - input.pin);
	if (vBase.length() > kMinIkPlaneNormal)
	{
		vBase = vBase.normal();
		angle += input.restPlaneAngle;
	}
	else
	{
		const MVector vRest = input.restBendNormal - vAxis * (input.restBendNormal * vAxis);
		vBase = vRest.length() > kMinIkPlaneNormal ? vRest.normal() : perpendicularTo(vAxis);
	}
	const MVector vNormal = (vBase * std::cos(angle) + (vAxis ^ vBase) * std::sin(angle)).normal();

	const MVector vMidFree = input.root + vAxis * bones.along + vNormal * bones.height;
	const MVector vEndFree = input.root + vAxis * bones.base;

	// 4. Pin pulls the elbow onto the pole and the wrist onto the handle. It wins over stretch and soft.
	TwoBoneIkResult result;
	result.mid = vMidFree + (input.pole - vMidFree) * input.pin;
	result.end = vEndFree + (input.handle - vEndFree) * input.pin;
	result.normal = vNormal;
	return result;
}

TwoBoneIkInverseResult MathUtils::computeTwoBoneIkInverse(const TwoBoneIkInverseInput& input)
{
	const double reach = input.length1 + input.length2;

	// 1. The end's direction is the handle's: with pin the end is on the root-to-handle ray too.
	const MVector vToEnd = input.end - input.root;
	double endDistance = vToEnd.length();
	const MVector vAxis = endDistance > kMinIkReach ? vToEnd / endDistance : input.restAxis.normal();

	// 2. The handle's distance. The end's distance is dc * (1 - pin) + d * pin, which never falls as d grows, so bisect.
	// A limb that neither stretches nor pins stops at its reach, and soft reach only nears it: the end is clamped to
	// 1e-4 of the reach short of it, so a soft handle lands a few soft distances past the wrist instead of at infinity.
	if (input.stretch <= 0.0 && input.pin <= 0.0)
	{
		const bool isSoft = input.soft * reach > kMinIkSoftDistance;
		endDistance = std::min(endDistance, isSoft ? reach * (1.0 - kInverseSoftMargin) : reach);
	}
	const auto endDistanceAt = [&input](double distance)
	{
		const TwoBoneIkBones bones = solveTwoBoneIkBones(input.length1, input.length2, input.stretch, input.soft, distance);
		return bones.base * (1.0 - input.pin) + distance * input.pin;
	};
	double high = std::max(endDistance, reach);
	for (int grow = 0; grow < kInverseSearchSteps && endDistanceAt(high) < endDistance; ++grow)
	{
		high *= 2.0;
	}
	double low = 0.0;
	for (int step = 0; step < kInverseSearchSteps; ++step)
	{
		const double middle = 0.5 * (low + high);
		(endDistanceAt(middle) < endDistance ? low : high) = middle;
	}
	const double distance = high;

	TwoBoneIkInverseResult result;
	result.handle = input.root + vAxis * distance;

	// 3. The pole. The bones at that distance give the mid joint where the pole's plane would put it (midFree).
	const TwoBoneIkBones bones = solveTwoBoneIkBones(input.length1, input.length2, input.stretch, input.soft, distance);
	const MVector vMid = input.mid - input.root;
	const double midAlong = vMid * vAxis;
	const MVector vMidPerp = vMid - vAxis * midAlong;
	const double angle = input.restPlaneAngle + input.rollRadians * (1.0 - input.pin);

	// The plane perpendicular to the axis in complex form: e1 is 1 and e2 is i, so a rotation about the axis multiplies.
	const MVector vE1 = perpendicularTo(vAxis);
	const MVector vE2 = vAxis ^ vE1;
	const auto planeAngleOf = [&](const MVector& vDirection)
	{
		return std::atan2(vDirection * vE2, vDirection * vE1);
	};

	double poleAlong = midAlong;
	double poleHeight = 0.0;
	double poleAngle = 0.0;
	const MVector vRestAxis = input.restAxis.normal();
	const MVector vRestPole = input.restPoleOffset - vRestAxis * (input.restPoleOffset * vRestAxis);
	if (input.pin > kMinInversePin)
	{
		// mid = (1 - pin) * midFree + pin * pole, with midFree = along * a + height * rotate(n0, angle) and the pole at
		// height q along n0. In the plane that is m = e^(i phi) * (pin * q + c * e^(i angle)) for c = (1 - pin) * height,
		// so |m| fixes q (a quadratic: its positive root) and the argument fixes phi.
		poleAlong = (midAlong - (1.0 - input.pin) * bones.along) / input.pin;
		const double c = (1.0 - input.pin) * bones.height;
		const double m = vMidPerp.length();
		const double cosAngle = std::cos(angle);
		const double sinAngle = std::sin(angle);
		const double root = std::sqrt(std::max(0.0, m * m - c * c * sinAngle * sinAngle));
		if (cosAngle <= 0.0)
		{
			poleHeight = (root - c * cosAngle) / input.pin;
		}
		else if (root + c * cosAngle > 0.0)
		{
			poleHeight = (m * m - c * c) / (input.pin * (root + c * cosAngle));   // the same root without the cancellation
		}
		poleHeight = std::max(0.0, poleHeight);

		const double dReal = input.pin * poleHeight + c * cosAngle;
		const double dImaginary = c * sinAngle;
		const double mReal = vMidPerp * vE1;
		const double mImaginary = vMidPerp * vE2;
		if (std::hypot(dReal, dImaginary) > kMinIkPlaneNormal && m > kMinIkPlaneNormal)
		{
			poleAngle = std::atan2(mImaginary * dReal - mReal * dImaginary, mReal * dReal + mImaginary * dImaginary);
		}
		else if (vRestPole.length() > kMinIkPlaneNormal)
		{
			poleAngle = planeAngleOf(vRestPole);
		}
	}
	else
	{
		// No pin: the pole only sets the plane, so it goes at its rest height on the side that gives the observed bend.
		poleHeight = vRestPole.length();
		if (vMidPerp.length() > kMinIkPlaneNormal)
		{
			poleAngle = planeAngleOf(vMidPerp) - angle;
		}
		else if (vRestPole.length() > kMinIkPlaneNormal)
		{
			poleAngle = planeAngleOf(vRestPole);   // a straight limb has no bend: keep the rest side
		}
	}
	result.pole = input.root + vAxis * poleAlong + (vE1 * std::cos(poleAngle) + vE2 * std::sin(poleAngle)) * poleHeight;
	return result;
}

MMatrix MathUtils::blendMatrices(const MMatrix& a, const MMatrix& b, double t)
{
	if (t <= 0.0)
	{
		return a;
	}
	if (t >= 1.0)
	{
		return b;
	}

	double scaleA[3];
	double scaleB[3];
	const MQuaternion qRotation = slerpShortArc(decomposeScaleRotation(a, scaleA), decomposeScaleRotation(b, scaleB), t);
	const MMatrix mRotation = qRotation.asMatrix();

	double mData[4][4];
	for (int row = 0; row < 3; ++row)
	{
		const double scale = scaleA[row] + (scaleB[row] - scaleA[row]) * t;
		for (int column = 0; column < 3; ++column)
		{
			mData[row][column] = mRotation[row][column] * scale;
		}
		mData[row][3] = 0.0;
	}
	for (int column = 0; column < 3; ++column)
	{
		mData[3][column] = a[3][column] + (b[3][column] - a[3][column]) * t;
	}
	mData[3][3] = 1.0;
	return MMatrix(mData);
}

// ---------------------------------
// DAG Utilities
// ---------------------------------
// Locks and hides rotation and scale channels on a control object
double TimeUtils::getSceneFrameRate()
{
	return 1.0 / MTime(1.0, MTime::uiUnit()).as(MTime::kSeconds);
}

bool TimeUtils::isSameFrameRate(double a, double b)
{
	constexpr double kFrameRateTolerance{ 1e-3 };
	return std::abs(a - b) <= kFrameRateTolerance;
}

void DagUtils::lockAndHideTranslate(MObject oControl)
{
	MFnTransform fnControl(oControl);
	MPlug pTranslate = fnControl.findPlug("translate", false);
	pTranslate.setLocked(true);
	pTranslate.setKeyable(false);
	pTranslate.setChannelBox(false);
}

void DagUtils::lockAndHideRotate(MObject oControl)
{
	MFnTransform fnControl(oControl);
	MPlug pRotation = fnControl.findPlug("rotate", false);
	pRotation.setLocked(true);
	pRotation.setKeyable(false);
	pRotation.setChannelBox(false);
}

void DagUtils::lockAndHideScale(MObject oControl)
{
	MFnTransform fnControl(oControl);
	MPlug pScale = fnControl.findPlug("scale", false);
	pScale.setLocked(true);
	pScale.setKeyable(false);
	pScale.setChannelBox(false);
}

// Bakes joint world transform relative to its parent into its opm and zeroes its transforms.
// TODO: pass in transform matrix gathered from read phase.
// NOTE: this is already happening when inheritsTransform has been turned off, so we just need to write the world matrix to OPM
MStatus DagUtils::bakeJointTransformsToOPM(const MObject& oJoint, const MMatrix& mTransformWorld, MDagModifier& dagMod)
{
	MStatus status;
	MFnTransform fnTransform(oJoint);
	if (!fnTransform.object().hasFn(MFn::kJoint))
	{
		MGlobal::displayError("Given object must be a joint");
		return MStatus::kFailure;
	}
	
	// NOTE: we can't do a verification pass here of the current value of inheritsTransform because dagMod hasn't run yet.

	// First zero out the current local matrix joint orient so that we can position the joint in world space locally using OPM
	// TODO: separate this into a fn
	for (const char* axis : { "X", "Y", "Z" })
	{
		MPlug pJointOrient = fnTransform.findPlug(MString("jointOrient") + axis, false, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = dagMod.newPlugValueDouble(pJointOrient, 0.0);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}

	// Zero out local matrix
	status = zeroLocalTransforms(oJoint, dagMod);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// Now that we've zeroed out all transforms, we can place the joint in world space using its OPM (because inherits transforms is off)
	MPlug pOffsetParentMatrix = fnTransform.findPlug("offsetParentMatrix", false, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	status = DagUtils::setMatrixPlug(pOffsetParentMatrix, mTransformWorld, dagMod);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	return status;
}

MStatus DagUtils::zeroLocalTransforms(const MObject& oTransform, MDagModifier& dagMod)
{
	if (oTransform.isNull() || !oTransform.hasFn(MFn::kTransform))
	{
		MGlobal::displayError("Given object is null or is not a transform");
		return MStatus::kFailure;
	}

	MFnTransform fnTransform(oTransform);
	for (const auto& transform : kDefaultTransformValues)
	{
		MPlug pTransform(oTransform, transform.attr);

		if (!pTransform.isNull() && !pTransform.isLocked())
		{
			dagMod.newPlugValueDouble(pTransform, transform.value);
		}
		else
		{
			MFnAttribute fnAttr(transform.attr);
			MGlobal::displayError(MString("Plug was locked and could not be zeroed: " + fnAttr.name()));
		}
	}
	return MStatus::kSuccess;
}

MStatus DagUtils::setInheritsTransform(const MObject& transform, bool value, MDagModifier& dagMod)
{
	if (!transform.hasFn(MFn::kTransform))
	{
		MGlobal::displayError("Given object is not a DAG transform, cannot set inheritsTransform.");
		return MStatus::kFailure;
	}

	MFnDependencyNode fnTransform(transform);
	MPlug pInheritsTransform = fnTransform.findPlug("inheritsTransform", false);

	if (pInheritsTransform.isNull())
	{
		return MStatus::kFailure;
	}

	return dagMod.newPlugValueBool(pInheritsTransform, value);
}

MStatus DagUtils::setSegmentScaleCompensate(MObject& oTransform, bool value, MDagModifier& dagMod)
{
	if (!oTransform.hasFn(MFn::kJoint))
	{
		MGlobal::displayError("Given object is not a DAG joint, cannot set segment scale compensate.");
		return MStatus::kFailure;
	}

	MFnDependencyNode fnTransform(oTransform);
	MPlug pSegmentScaleCompensate = fnTransform.findPlug("segmentScaleCompensate", false);

	if (pSegmentScaleCompensate.isNull())
	{
		return MStatus::kFailure;
	}

	return dagMod.newPlugValueBool(pSegmentScaleCompensate, value);
}

MMatrix DagUtils::getMatrixFromPlug(const MPlug& pMatrixPlug)
{
	MMatrix matrixValue;

	MObject dataObj;
	MStatus status = pMatrixPlug.getValue(dataObj);

	if (status && !dataObj.isNull() && dataObj.hasFn(MFn::kMatrixData))
	{
		MFnMatrixData fnMatrixData(dataObj);
		matrixValue = fnMatrixData.matrix();
	}

	return matrixValue;
}


MStatus DagUtils::setMatrixPlug(MPlug& matrixPlug, MMatrix matrix, MDagModifier& dagMod)
{
	if (!matrixPlug.isNull() && !matrixPlug.isArray())
	{
		MFnMatrixData fnMatrixData;
		MObject oMatrixData = fnMatrixData.create(matrix);
		return dagMod.newPlugValue(matrixPlug, oMatrixData);
	}
	else
	{
		MGlobal::displayError("The given matrix plug was null or an array.");
		return MStatus::kFailure;
	}
}

MStatus DagUtils::getDagPathFromName(const MString& name, MDagPath& outPath, NodeKind kind)
{
	MSelectionList sel;
	MDagPath path;
	if (!sel.add(name) || !sel.getDagPath(0, path))
	{
		MGlobal::displayError(MString("No DAG node named ") + name);
		return MStatus::kFailure;
	}

	if (kind == NodeKind::kJoint && !path.hasFn(MFn::kJoint))
	{
		MGlobal::displayError(name + " is not a joint.");
		return MStatus::kFailure;
	}
	if (kind == NodeKind::kMesh)
	{
		if (!MeshUtils::findMeshTransform(path, outPath))
		{
			MGlobal::displayError(name + " is not a mesh.");
			return MStatus::kFailure;
		}
		return MStatus::kSuccess;
	}

	outPath = path;
	return MStatus::kSuccess;
}

bool DagUtils::isChannelWritable(const MPlug& pChannel)
{
	if (pChannel.isLocked())
	{
		return false;
	}
	const MPlug pSource = pChannel.source();
	return pSource.isNull() || pSource.node().hasFn(MFn::kAnimCurve);
}

MStatus DagUtils::getMObjectFromName(const MString& name, MObject& oNode)
{
	MSelectionList sel;
	MStatus status = sel.add(name);
	if (!status)
	{
		return status;
	}
	return sel.getDependNode(0, oNode);
}

MString DagUtils::getAbsoluteNamespace(const MObject& oNode)
{
	const MString nodeNamespace = MNamespace::getNamespaceFromName(MFnDependencyNode(oNode).absoluteName());
	if (nodeNamespace.length() == 0 || nodeNamespace == ":")
	{
		return ":";
	}
	return nodeNamespace.substring(0, 0) == ":" ? nodeNamespace : MString(":") + nodeNamespace;
}

MString DagUtils::getAbsoluteName(const MString& namespaceName, const MString& shortName)
{
	if (namespaceName.length() == 0)
	{
		return MString(":") + shortName;
	}
	return MString(":") + namespaceName + ":" + shortName;
}

MString DagUtils::getPartialPathName(const MObject& oDagNode)
{
	MDagPath dagPath;
	MFnDagNode(oDagNode).getPath(dagPath);
	return dagPath.partialPathName();
}

MObject DagUtils::findUpstreamNode(const MObject& oNode, MFn::Type filter)
{
	MStatus status;
	MObject oRoot = oNode;
	MItDependencyGraph itGraph(oRoot, filter, MItDependencyGraph::kUpstream, MItDependencyGraph::kBreadthFirst,
		MItDependencyGraph::kNodeLevel, &status);
	if (!status || itGraph.isDone())
	{
		return MObject::kNullObj;
	}
	return itGraph.currentItem();
}

MObjectArray DagUtils::getTopmostNodes(const MObjectArray& oNodes)
{
	std::vector<MObjectHandle> handles;
	for (unsigned int i = 0; i < oNodes.length(); ++i)
	{
		handles.emplace_back(oNodes[i]);
	}

	MObjectArray oTopmost;
	for (unsigned int i = 0; i < oNodes.length(); ++i)
	{
		MStatus status;
		const MFnDagNode fnNode(oNodes[i], &status);
		const bool hasParentInSet = status && fnNode.parentCount() > 0
			&& std::find(handles.begin(), handles.end(), MObjectHandle(fnNode.parent(0))) != handles.end();
		if (!hasParentInSet)
		{
			oTopmost.append(oNodes[i]);
		}
	}
	return oTopmost;
}

MStatus DagUtils::queueLockAndHideTransform(const MObject& oTransform, MDagModifier& dagMod, const std::vector<std::string>& keepChannels)
{
	MStatus status;
	const MFnDagNode fnTransform(oTransform, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// The children, not the compound plugs: setAttr -lock on a compound leaves the children keyable in the channel box.
	for (const char* channel : { "tx", "ty", "tz", "rx", "ry", "rz", "sx", "sy", "sz" })
	{
		if (std::find(keepChannels.begin(), keepChannels.end(), channel) != keepChannels.end())
		{
			continue;
		}
		status = dagMod.commandToExecute(MString("setAttr -lock true -keyable false -channelBox false ")
			+ MelUtils::quoted(fnTransform.fullPathName() + "." + channel));
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	return MStatus::kSuccess;
}

// ---------------------------------
// Mesh, MEL and command utilities
// ---------------------------------
bool MeshUtils::findMeshTransform(const MDagPath& path, MDagPath& outTransform)
{
	MDagPath shape = path;
	if (!shape.extendToShape() || !shape.hasFn(MFn::kMesh))
	{
		return false;
	}
	outTransform = shape;
	outTransform.pop();
	return true;
}

MStatus MeshUtils::getMeshShape(const MDagPath& mesh, MDagPath& outShape)
{
	outShape = mesh;
	MStatus status = outShape.extendToShape();
	if (!status || !outShape.hasFn(MFn::kMesh))
	{
		MGlobal::displayError(mesh.partialPathName() + " has no mesh shape.");
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MObject MeshUtils::createAllVerticesComponent(unsigned int vertexCount)
{
	MFnSingleIndexedComponent fnComponent;
	MObject oComponent = fnComponent.create(MFn::kMeshVertComponent);
	fnComponent.setCompleteData(static_cast<int>(vertexCount));
	return oComponent;
}

MString MelUtils::quoted(const MString& text)
{
	return MString("\"") + text + "\"";
}

MStatus CommandUtils::readRigRoot(const MArgDatabase& argData, MStatus parseStatus, const char* commandName, MObject& oOutRigRoot)
{
	if (!parseStatus)
	{
		MGlobal::displayError(MString("Error parsing ") + commandName + " arguments.");
		return parseStatus;
	}

	MString rigRootName;
	MStatus status = readSingleObject(argData, commandName, "rig root", rigRootName);
	if (!status)
	{
		return status;
	}
	return RigTraversal::getRigRoot(rigRootName, oOutRigRoot);
}

MStatus CommandUtils::readControlScope(const MArgDatabase& argData, MStatus parseStatus, const char* commandName, RigControlScope& outScope)
{
	if (!parseStatus)
	{
		MGlobal::displayError(MString("Error parsing ") + commandName + " arguments.");
		return parseStatus;
	}

	MStringArray names;
	MStatus status = argData.getObjects(names);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	return RigTraversal::getControlScope(names, outScope);
}

MSyntax CommandUtils::controlScopeSyntax()
{
	MSyntax syntax;
	syntax.setObjectType(MSyntax::kStringObjects, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus CommandUtils::readSingleObject(const MArgDatabase& argData, const char* commandName, const char* what, MString& outName)
{
	MStringArray objects;
	MStatus status = argData.getObjects(objects);
	if (!status || objects.length() != 1)
	{
		MGlobal::displayError(MString(commandName) + " takes exactly one " + what + ".");
		return MStatus::kFailure;
	}
	outName = objects[0];
	return MStatus::kSuccess;
}

MStatus CommandUtils::readStringList(const MArgDatabase& argData, const char* flag, std::vector<std::string>& outValues)
{
	const unsigned int uses = argData.numberOfFlagUses(flag);
	for (unsigned int i = 0; i < uses; ++i)
	{
		MArgList flagArgs;
		MStatus status = argData.getFlagArgumentList(flag, i, flagArgs);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		outValues.push_back(flagArgs.asString(0).asChar());
	}
	return MStatus::kSuccess;
}

MStatus CommandUtils::readFilePath(const MArgDatabase& argData, const char* flag, const char* commandName, MString& outPath)
{
	if (!argData.isFlagSet(flag) || argData.getFlagArgument(flag, 0, outPath) != MStatus::kSuccess || outPath.length() == 0)
	{
		MGlobal::displayError(MString(commandName) + " needs " + flag + ".");
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus CommandUtils::readMeshesOrSelection(const MArgDatabase& argData, const char* commandName, MDagPathArray& outMeshes)
{
	MStringArray objects;
	argData.getObjects(objects);
	for (unsigned int i = 0; i < objects.length(); ++i)
	{
		MDagPath mesh;
		MStatus status = DagUtils::getDagPathFromName(objects[i], mesh, DagUtils::NodeKind::kMesh);
		if (!status)
		{
			return status;
		}
		outMeshes.append(mesh);
	}
	if (objects.length() > 0)
	{
		return MStatus::kSuccess;
	}

	MSelectionList selection;
	MGlobal::getActiveSelectionList(selection);
	for (unsigned int i = 0; i < selection.length(); ++i)
	{
		MDagPath path;
		MDagPath mesh;
		if (selection.getDagPath(i, path) && MeshUtils::findMeshTransform(path, mesh))
		{
			outMeshes.append(mesh);
		}
	}
	if (outMeshes.length() == 0)
	{
		MGlobal::displayError(MString(commandName) + " needs meshes: name them or select them.");
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus CommandUtils::readTextFile(const MString& path, const char* kind, std::string& outText)
{
	std::ifstream file(path.asChar(), std::ios::in | std::ios::binary);
	if (!file)
	{
		MGlobal::displayError(MString("Could not read the ") + kind + " file: " + path);
		return MStatus::kFailure;
	}
	std::stringstream text;
	text << file.rdbuf();
	outText = text.str();
	return MStatus::kSuccess;
}

MStatus CommandUtils::writeTextFile(const MString& path, const char* kind, const std::string& text)
{
	std::ofstream file(path.asChar(), std::ios::out | std::ios::trunc);
	file << text;
	file.close();
	if (!file)
	{
		MGlobal::displayError(MString("Could not write the ") + kind + " file: " + path);
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

void ReportUtils::displayReport(const ValidationReport& report)
{
	for (const std::string& warning : report.warnings)
	{
		MGlobal::displayWarning(warning.c_str());
	}
	for (const std::string& error : report.errors)
	{
		MGlobal::displayError(error.c_str());
	}
}

// Profiler utils -----------------------------------------------------------
namespace
{
	int gProfilerCategory{ -1 };
}

void ProfilerUtils::addCategory()
{
	gProfilerCategory = MProfiler::addCategory(PluginInfo::ProfilerCategory, "RigModules plugin events");
}

void ProfilerUtils::removeCategory()
{
	MProfiler::removeCategory(PluginInfo::ProfilerCategory);
	gProfilerCategory = -1;
}

int ProfilerUtils::category()
{
	return gProfilerCategory;
}

// Active selection ---------------------------------------------------------
namespace
{
	struct ObjectHandleHash
	{
		std::size_t operator()(const MObjectHandle& handle) const
		{
			return handle.hashCode();
		}
	};

	std::unordered_set<MObjectHandle, ObjectHandleHash> gSelectedNodes;
	unsigned int gSelectionLength{ 0 };
	MCallbackId gSelectionCallback{ 0 };
	bool gSelectionCallbackActive{ false };

	void readActiveSelection(void*)
	{
		MSelectionList selection;
		MGlobal::getActiveSelectionList(selection);
		gSelectionLength = selection.length();
		gSelectedNodes.clear();
		for (unsigned int i = 0; i < gSelectionLength; ++i)
		{
			MObject oNode;
			if (selection.getDependNode(i, oNode) == MStatus::kSuccess && !oNode.isNull())
			{
				gSelectedNodes.insert(MObjectHandle(oNode));
			}
		}
	}
}

MStatus ActiveSelection::start()
{
	MStatus status;
	gSelectionCallback = MModelMessage::addCallback(MModelMessage::kActiveListModified, readActiveSelection, nullptr, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	gSelectionCallbackActive = true;
	readActiveSelection(nullptr);
	return MStatus::kSuccess;
}

void ActiveSelection::stop()
{
	if (gSelectionCallbackActive)
	{
		MMessage::removeCallback(gSelectionCallback);
		gSelectionCallbackActive = false;
	}
	gSelectedNodes.clear();
	gSelectionLength = 0;
}

bool ActiveSelection::contains(const MObject& oNode)
{
	// A deleted node's memory can be reused by a new node, so a stale entry must not match it.
	const auto it = gSelectedNodes.find(MObjectHandle(oNode));
	return it != gSelectedNodes.end() && it->isValid();
}

unsigned int ActiveSelection::length()
{
	return gSelectionLength;
}

// DAG plug and socket utils -----------------------------------------------
MStatus DagPlugAndSocketUtils::setParentSocketNames(const MObject& oModule, const unsigned int index, const std::vector<std::string>& names, MDagModifier& dagMod)
{
	if (oModule.isNull())
	{
		return MStatus::kFailure;
	}

	MFnDependencyNode fnModule(oModule);

	MPlug pSocketNames = fnModule.findPlug(RigAttr::SocketNames, false);
	if (pSocketNames.isNull())
	{
		return MStatus::kFailure;
	}

	MPlug pSocketNamesElement = pSocketNames.elementByLogicalIndex(index);
	if (pSocketNamesElement.isNull())
	{
		return MStatus::kFailure;
	}

	return dagMod.newPlugValueString(pSocketNamesElement, MString(JsonUtils::writeJsonString(names).c_str()));
}


MStatus DagPlugAndSocketUtils::setChildPlugName(const MObject& oModule, const unsigned int index, const char* name, MDagModifier& dagMod)
{
	if (oModule.isNull())
	{
		return MStatus::kFailure;
	}

	MFnDependencyNode fnModule(oModule);

	MPlug pChildPlugs = fnModule.findPlug(RigAttr::ChildPlugs, false);
	if (pChildPlugs.isNull())
	{
		return MStatus::kFailure;
	}

	MPlug pChildPlugsElement = pChildPlugs.elementByLogicalIndex(index);
	if (pChildPlugsElement.isNull())
	{
		return MStatus::kFailure;
	}

	MObject oChildPlugName = fnModule.attribute(RigAttr::ChildPlugName);
	MPlug pChildPlugsElementName = pChildPlugsElement.child(oChildPlugName);

	return dagMod.newPlugValueString(pChildPlugsElementName, MString(name));
}

// ---------------------------------
// Scoped rig namespace
// ---------------------------------

ScopedRigNamespace::ScopedRigNamespace(const MString& rigNamespace)
	: fOriginalNamespace(MNamespace::currentNamespace())
{
	const MString namespacePath = MString(":") + rigNamespace;
	if (!MNamespace::namespaceExists(namespacePath))
	{
		fStatus = MNamespace::addNamespace(namespacePath);
		if (!fStatus)
		{
			MGlobal::displayError("Could not create the rig namespace '" + namespacePath + "': " + fStatus.errorString());
			return;
		}
	}
	fStatus = MNamespace::setCurrentNamespace(namespacePath);
}

ScopedRigNamespace::~ScopedRigNamespace()
{
	MNamespace::setCurrentNamespace(fOriginalNamespace);
}
