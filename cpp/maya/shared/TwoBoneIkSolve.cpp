#include "maya/shared/TwoBoneIkSolve.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/Utilities.h"

namespace
{
	// A frame with +X toward vTarget and +Z along the bend normal, the way the solve's positions orient a bone (section 4.3).
	MMatrix makeBoneFrame(const MVector& vOrigin, const MVector& vTarget, const MVector& vNormal)
	{
		const MVector vX = (vTarget - vOrigin).normal();
		const MVector vZ = TwoBoneIkSolve::perpendicularPart(vNormal, vX).normal();
		return TwoBoneIkSolve::makeFrame(vX, vZ ^ vX, vZ, vOrigin);
	}
}

MVector TwoBoneIkSolve::perpendicularPart(const MVector& v, const MVector& vAxis)
{
	return v - vAxis * (v * vAxis);
}

MMatrix TwoBoneIkSolve::makeFrame(const MVector& vX, const MVector& vY, const MVector& vZ, const MVector& vOrigin)
{
	const double mData[4][4] =
	{
		{ vX.x, vX.y, vX.z, 0.0 },
		{ vY.x, vY.y, vY.z, 0.0 },
		{ vZ.x, vZ.y, vZ.z, 0.0 },
		{ vOrigin.x, vOrigin.y, vOrigin.z, 1.0 }
	};
	return MMatrix(mData);
}

MMatrix TwoBoneIkSolve::orthonormalFrame(const MMatrix& mMatrix, const MVector& vOrigin)
{
	const MVector vX = MVector(mMatrix[0][0], mMatrix[0][1], mMatrix[0][2]).normal();
	const MVector vZ = (vX ^ MVector(mMatrix[1][0], mMatrix[1][1], mMatrix[1][2])).normal();
	return makeFrame(vX, vZ ^ vX, vZ, vOrigin);
}

void TwoBoneIkSolve::computeLimbFrames(const TwoBoneIkResult& solve, const MVector& vRoot, const MMatrix& mIkWorld, MMatrix outFrames[TwoBoneIkSlot::LimbCount])
{
	outFrames[TwoBoneIkSlot::Upper] = makeBoneFrame(vRoot, solve.mid, solve.normal);
	outFrames[TwoBoneIkSlot::Lower] = makeBoneFrame(solve.mid, solve.end, solve.normal);
	outFrames[TwoBoneIkSlot::End] = orthonormalFrame(mIkWorld, solve.end);
}

void TwoBoneIkSolve::evaluate(const TwoBoneIkEval& eval, TwoBoneIkSolveResult& outResult)
{
	// 1. FK chain. Always solved, even at blend 1: the controls' offsets stay current, and the IK root follows the start control.
	MMatrix mFkWorld[TwoBoneIkSlot::LimbCount];
	MMatrix mParentDelta = eval.mUpperDelta;
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::LimbCount; ++slot)
	{
		const ControlSolveStep step = RigModuleNodeBase::solveControlStep(eval.mLocal[slot], eval.mRest[slot], mParentDelta);
		outResult.mControlOPM[slot] = step.mOPM;
		mFkWorld[slot] = step.mWorld;
		if (slot + 1 < TwoBoneIkSlot::LimbCount)
		{
			mParentDelta = MathUtils::computeDeltaFromRest(eval.mRest[slot], step.mWorld);
		}
	}

	// 2. The IK handle, then the pole, which reads the handle's solved world in Ik space (slot order matters, section 4.4).
	const ControlSolveStep ikStep = RigModuleNodeBase::solveControlStep(eval.mLocal[TwoBoneIkSlot::Ik], eval.mRest[TwoBoneIkSlot::Ik], eval.mIkDelta);
	const MMatrix mPoleDelta = eval.poleFollowsIk
		? MathUtils::computeDeltaFromRest(eval.mRest[TwoBoneIkSlot::Ik], ikStep.mWorld)
		: eval.mPoleParentDelta;
	const ControlSolveStep poleStep = RigModuleNodeBase::solveControlStep(eval.mLocal[TwoBoneIkSlot::Pole], eval.mRest[TwoBoneIkSlot::Pole], mPoleDelta);
	const ControlSolveStep settingsStep = RigModuleNodeBase::solveControlStep(eval.mLocal[TwoBoneIkSlot::Settings], eval.mRest[TwoBoneIkSlot::Settings], eval.mSettingsDelta);

	outResult.mControlOPM[TwoBoneIkSlot::Ik] = ikStep.mOPM;
	outResult.mControlOPM[TwoBoneIkSlot::Pole] = poleStep.mOPM;
	outResult.mControlOPM[TwoBoneIkSlot::Settings] = settingsStep.mOPM;
	outResult.mJointWorld[TwoBoneIkSlot::Ik] = ikStep.mWorld;
	outResult.mJointWorld[TwoBoneIkSlot::Pole] = poleStep.mWorld;
	outResult.mJointWorld[TwoBoneIkSlot::Settings] = settingsStep.mWorld;

	// 3. The limb joints: FK at blend 0 and IK at blend 1 exactly, the solve skipped at 0, a blend between.
	if (eval.blend <= 0.0)
	{
		for (unsigned int slot = 0; slot < TwoBoneIkSlot::LimbCount; ++slot)
		{
			outResult.mJointWorld[slot] = mFkWorld[slot];
		}
		return;
	}

	const MVector vRoot = MathUtils::translationOf(mFkWorld[TwoBoneIkSlot::Upper]);   // the start control's rotation does nothing in IK
	TwoBoneIkInput input;
	input.root = vRoot;
	input.handle = MathUtils::translationOf(ikStep.mWorld);
	input.pole = MathUtils::translationOf(poleStep.mWorld);
	input.length1 = eval.rest.length1;
	input.length2 = eval.rest.length2;
	input.stretch = eval.stretch;
	input.soft = eval.soft;
	input.pin = eval.pin;
	input.rollRadians = eval.rollRadians;
	input.restPlaneAngle = eval.rest.restPlaneAngle;
	input.restBendNormal = eval.rest.restBendNormal;
	input.restAxis = MathUtils::translationOf(eval.mRest[TwoBoneIkSlot::Ik]) - MathUtils::translationOf(eval.mRest[TwoBoneIkSlot::Upper]);

	MMatrix mFrames[TwoBoneIkSlot::LimbCount];
	computeLimbFrames(MathUtils::computeTwoBoneIk(input), vRoot, ikStep.mWorld, mFrames);
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::LimbCount; ++slot)
	{
		outResult.mJointWorld[slot] = MathUtils::blendMatrices(mFkWorld[slot], eval.rest.ikRestOffset[slot] * mFrames[slot], eval.blend);
	}
}
