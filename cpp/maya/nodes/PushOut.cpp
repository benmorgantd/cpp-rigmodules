#include "maya/nodes/PushOut.h"
#include "core/Constants.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/RigTraversal.h"
#include "maya/shared/Utilities.h"

#include <maya/MFnDependencyNode.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MPxTransform.h>

MTypeId RigPushOutNode::id(0x002196);

MObject RigPushOutNode::aCapsuleMatrix;
MObject RigPushOutNode::aCapsuleOffset;
MObject RigPushOutNode::aRadius;
MObject RigPushOutNode::aHalfLength;
MObject RigPushOutNode::aMargin;
MObject RigPushOutNode::aMaxPush;
MObject RigPushOutNode::aWeight;
MObject RigPushOutNode::aParentMatrix;
MObject RigPushOutNode::aParentRest;
MObject RigPushOutNode::aChildRest;
MObject RigPushOutNode::aOutParentMatrix;
MObject RigPushOutNode::aRigRoot;

RigPushOutNode::RigPushOutNode()
{
}
RigPushOutNode::~RigPushOutNode()
{
}

void* RigPushOutNode::creator()
{
	return new RigPushOutNode();
}

MStatus RigPushOutNode::initialize()
{
	MStatus status;
	MFnMatrixAttribute mAttr;
	MFnNumericAttribute nAttr;
	MFnMessageAttribute msgAttr;

	aCapsuleMatrix = mAttr.create(PushOutAttr::CapsuleMatrix, PushOutAttr::CapsuleMatrix, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aCapsuleMatrix));

	aCapsuleOffset = mAttr.create(PushOutAttr::CapsuleOffset, PushOutAttr::CapsuleOffset, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	mAttr.setStorable(true);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aCapsuleOffset));

	// Rig-builder data (the template's collision entry), not animator channels: not keyable, so they stay out of poses.
	const auto addSetting = [&](MObject& oAttr, const char* name, double defaultValue, double minimum, double maximum)
	{
		oAttr = nAttr.create(name, name, MFnNumericData::kDouble, defaultValue, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		nAttr.setMin(minimum);
		nAttr.setMax(maximum);
		nAttr.setStorable(true);
		nAttr.setKeyable(false);
		return addAttribute(oAttr);
	};
	CHECK_MSTATUS_AND_RETURN_IT(addSetting(aRadius, PushOutAttr::Radius, 1.0, 1.0e-6, 1.0e6));
	CHECK_MSTATUS_AND_RETURN_IT(addSetting(aHalfLength, PushOutAttr::HalfLength, 0.0, 0.0, 1.0e6));
	CHECK_MSTATUS_AND_RETURN_IT(addSetting(aMargin, PushOutAttr::Margin, 0.0, 0.0, 1.0e6));
	CHECK_MSTATUS_AND_RETURN_IT(addSetting(aMaxPush, PushOutAttr::MaxPush, 100.0, 1.0e-6, 1.0e6));
	CHECK_MSTATUS_AND_RETURN_IT(addSetting(aWeight, PushOutAttr::Weight, 1.0, 0.0, 1.0));

	aParentMatrix = mAttr.create(PushOutAttr::ParentMatrix, PushOutAttr::ParentMatrix, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aParentMatrix));

	aParentRest = mAttr.create(PushOutAttr::ParentRest, PushOutAttr::ParentRest, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	mAttr.setStorable(true);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aParentRest));

	aChildRest = mAttr.create(PushOutAttr::ChildRest, PushOutAttr::ChildRest, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	mAttr.setStorable(true);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aChildRest));

	aOutParentMatrix = mAttr.create(PushOutAttr::OutParentMatrix, PushOutAttr::OutParentMatrix, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aOutParentMatrix));

	aRigRoot = msgAttr.create(PushOutAttr::RigRoot, PushOutAttr::RigRoot, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aRigRoot));

	// AFFECTS RELATIONSHIPS
	const MObject* inputs[]{ &aCapsuleMatrix, &aCapsuleOffset, &aRadius, &aHalfLength, &aMargin, &aMaxPush, &aWeight,
		&aParentMatrix, &aParentRest, &aChildRest };
	for (const MObject* oInput : inputs)
	{
		CHECK_MSTATUS_AND_RETURN_IT(attributeAffects(*oInput, aOutParentMatrix));
	}

	return MStatus::kSuccess;
}

// The control's would-be frame is its rest times the parent's delta. A delta maps world points, so the push is added to the
// world translation: rest * delta * T(push).
MMatrix RigPushOutNode::computeDelta(
	const PushOutSolve::Capsule& capsule,
	const MMatrix& mCapsuleWorld,
	const PushOutSolve::Settings& settings,
	const MMatrix& mChildRest,
	const MMatrix& mParentDelta)
{
	const MMatrix mWouldBe = mChildRest * mParentDelta;
	const PushOutSolve::Result solved = PushOutSolve::solve(
		capsule, MathUtils::toMat4(mCapsuleWorld), { mWouldBe[3][0], mWouldBe[3][1], mWouldBe[3][2] }, settings);
	if (!solved.isPushed)
	{
		return mParentDelta;
	}

	MMatrix mPush;
	mPush[3][0] = solved.push.x;
	mPush[3][1] = solved.push.y;
	mPush[3][2] = solved.push.z;
	return mParentDelta * mPush;
}

MStatus RigPushOutNode::compute(const MPlug& plug, MDataBlock& data)
{
	if (plug.attribute() != aOutParentMatrix)
	{
		return MStatus::kUnknownParameter;
	}

	const PushOutSolve::Capsule capsule{ data.inputValue(aRadius).asDouble(), data.inputValue(aHalfLength).asDouble() };
	const PushOutSolve::Settings settings{ data.inputValue(aMargin).asDouble(), data.inputValue(aMaxPush).asDouble(), data.inputValue(aWeight).asDouble() };
	const MMatrix mCapsuleWorld = data.inputValue(aCapsuleOffset).asMatrix() * data.inputValue(aCapsuleMatrix).asMatrix();
	const MMatrix mParentDelta = MathUtils::computeDeltaFromRest(
		data.inputValue(aParentRest).asMatrix(), data.inputValue(aParentMatrix).asMatrix());

	data.outputValue(aOutParentMatrix).setMMatrix(
		computeDelta(capsule, mCapsuleWorld, settings, data.inputValue(aChildRest).asMatrix(), mParentDelta));
	data.setClean(aOutParentMatrix);
	return MStatus::kSuccess;
}

// ------------------------------------------------------------------
// Creation
// ------------------------------------------------------------------
MStatus RigPushOutNode::createPushOut(const PushOutBuildArgs& args, MDagModifier& dagMod, MObject& oOutPushOut)
{
	MStatus status;

	if (args.oModule.isNull() || args.oControl.isNull() || args.oFollowJoint.isNull())
	{
		MGlobal::displayError("A push out needs a module, its control and the joint its capsule follows.");
		return MStatus::kFailure;
	}
	std::string error;
	if (!PushOutSolve::validate(args.capsule, args.settings, error))
	{
		MGlobal::displayError(MString("Push out ") + args.name.c_str() + ": " + error.c_str());
		return MStatus::kFailure;
	}

	MPlug pPlugParentMatrix;
	MPlug pPlugParentRest;
	status = RigModuleNodeBase::getChildPlugMatrixPlugs(args.oModule, args.plugIndex, pPlugParentMatrix, pPlugParentRest);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// What feeds the plug now: a socket or a constraint. The push out takes it over and sits after it.
	const MPlug pSource = pPlugParentMatrix.source();
	if (pSource.isNull())
	{
		MGlobal::displayError(MString("The pushed child plug of ") + MFnDependencyNode(args.oControl).name() + " has no parent connected.");
		return MStatus::kFailure;
	}
	const MMatrix mParentRest = DagUtils::getMatrixFromPlug(pPlugParentRest);

	MMatrix mChildRest;
	status = RigTraversal::getControlRestWorld(args.oControl, mChildRest);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MObject oPushOut = dagMod.MDGModifier::createNode(RigPushOutNode::id);
	dagMod.renameNode(oPushOut, args.name.c_str());
	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MPlug pJointWorld = MPlug(args.oFollowJoint, MPxTransform::worldMatrix).elementByLogicalIndex(0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	status = dagMod.connect(pJointWorld, MPlug(oPushOut, aCapsuleMatrix));
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MPlug pOffset(oPushOut, aCapsuleOffset);
	DagUtils::setMatrixPlug(pOffset, args.mOffsetLocal, dagMod);
	dagMod.newPlugValueDouble(MPlug(oPushOut, aRadius), args.capsule.radius);
	dagMod.newPlugValueDouble(MPlug(oPushOut, aHalfLength), args.capsule.halfLength);
	dagMod.newPlugValueDouble(MPlug(oPushOut, aMargin), args.settings.margin);
	dagMod.newPlugValueDouble(MPlug(oPushOut, aMaxPush), args.settings.maxPush);
	dagMod.newPlugValueDouble(MPlug(oPushOut, aWeight), args.settings.weight);
	MPlug pChildRest(oPushOut, aChildRest);
	MPlug pParentRestStored(oPushOut, aParentRest);
	DagUtils::setMatrixPlug(pChildRest, mChildRest, dagMod);
	DagUtils::setMatrixPlug(pParentRestStored, mParentRest, dagMod);

	dagMod.disconnect(pSource, pPlugParentMatrix);
	status = dagMod.connect(pSource, MPlug(oPushOut, aParentMatrix));
	CHECK_MSTATUS_AND_RETURN_IT(status);
	status = dagMod.connect(MPlug(oPushOut, aOutParentMatrix), pPlugParentMatrix);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// The push out's output is already a delta, so the plug's rest is identity, as after a constraint.
	DagUtils::setMatrixPlug(pPlugParentRest, MMatrix::identity, dagMod);

	if (!args.oRigRoot.isNull())
	{
		MPlug pRigRootCollisions = MFnDependencyNode(args.oRigRoot).findPlug(RigAttr::Collisions, false, &status);
		if (status && !pRigRootCollisions.isNull())
		{
			MPlug pSlot = pRigRootCollisions.elementByLogicalIndex(pRigRootCollisions.evaluateNumElements());
			dagMod.connect(pSlot, MPlug(oPushOut, aRigRoot));
		}
	}

	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	oOutPushOut = oPushOut;
	return MStatus::kSuccess;
}
