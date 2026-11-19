#include "maya/shared/RibbonSolve.h"
#include "maya/shared/Utilities.h"

#include <maya/MTransformationMatrix.h>

void RibbonSolve::evaluate(
	const RibbonMath::Setup& setup,
	const RibbonMath::Params& params,
	const MMatrix* mRest,
	const MMatrix* mLocal,
	const MMatrix& mStartDelta,
	const MMatrix& mEndDelta,
	Result& outSolve)
{
	const int controlCount = setup.controlCount;

	// The curve is solved in the rest world, where the start and end sit at their rests, and the start's parent motion carries
	// the result. The end follows its own parent's motion, so a socket from another module can drive it (section 3.4).
	RibbonMath::Input input;
	for (int j = 0; j < controlCount; ++j)
	{
		input.local[j] = MathUtils::toMat4(mLocal[j]);
	}
	input.startOPM = MathUtils::toMat4(mRest[0]);
	// With an fk chain the whole ribbon rides the start's parent and the end rides the chain, so the end has no motion of its own.
	const int fkFirst = controlCount + setup.bindCount;
	for (int k = 0; k < setup.fkCount; ++k)
	{
		input.fkLocal[k] = MathUtils::toMat4(mLocal[fkFirst + k]);
	}
	// The plug deltas are motions in world axes (endOPM = endRest * delta), so the end's twist about its own X is read from the
	// motion in the end control's frame: rest rotation * delta * rest rotation^-1.
	const MMatrix mEndMotion = setup.fkCount > 0 ? MMatrix::identity : mEndDelta * mStartDelta.inverse();
	const MMatrix mEndRestRotation = MTransformationMatrix(mRest[controlCount - 1]).asRotateMatrix();
	input.endDelta = MathUtils::toMat4(mEndRestRotation * mEndMotion * mEndRestRotation.inverse());
	input.endOPM = MathUtils::toMat4(mRest[controlCount - 1] * mEndMotion);

	RibbonMath::Output output;
	RibbonMath::evaluate(setup, params, input, output);

	for (int j = 0; j < controlCount; ++j)
	{
		outSolve.mControlOPM[j] = MathUtils::toMMatrix(output.controlOPM[j]) * mStartDelta;
		outSolve.mWorld[j] = mLocal[j] * outSolve.mControlOPM[j];
	}
	for (int i = 0; i < setup.bindCount; ++i)
	{
		const unsigned int slot = static_cast<unsigned int>(controlCount + i);
		outSolve.mControlOPM[slot] = MathUtils::toMMatrix(output.bindOPM[i]) * mStartDelta;
		outSolve.mWorld[slot] = mLocal[slot] * outSolve.mControlOPM[slot];
	}
	for (int k = 0; k < setup.fkCount; ++k)
	{
		const unsigned int slot = static_cast<unsigned int>(fkFirst + k);
		outSolve.mControlOPM[slot] = MathUtils::toMMatrix(output.fkOPM[k]) * mStartDelta;
		outSolve.mWorld[slot] = mLocal[slot] * outSolve.mControlOPM[slot];
	}
}
