#pragma once

#include "core/RibbonMath.h"

#include <maya/MMatrix.h>

// The Ribbon module's forward evaluation (D56), as plain values in and out, with no node and no plug. RibbonModuleNode calls it
// from compute() and its inverse, and the Limb module (D62, design/limb.md) calls it for its ribbon. A change here changes both:
// read design/limb.md first.
namespace RibbonSolve
{
	inline constexpr unsigned int MaxSlots{ RibbonMath::MaxControls + RibbonMath::MaxBindJoints };

	// What one evaluation produces, by slot, in world space.
	struct Result
	{
		MMatrix mControlOPM[MaxSlots];   // rest carried by the parent's motion (the ik controls on the curve), no local
		MMatrix mWorld[MaxSlots];        // local * controlOPM
	};

	// mRest and mLocal are by slot; the start's parent motion carries the whole ribbon and the end's is its own.
	void evaluate(
		const RibbonMath::Setup& setup,
		const RibbonMath::Params& params,
		const MMatrix* mRest,
		const MMatrix* mLocal,
		const MMatrix& mStartDelta,
		const MMatrix& mEndDelta,
		Result& outSolve);
}
