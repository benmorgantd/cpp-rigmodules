#include "maya/shared/ModuleInverse.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/Utilities.h"

ModuleInverseResult ModuleInverse::solveChain(const ModuleInverseContext& context, const ModuleInverseInput& input, bool useJoints)
{
	const std::size_t count = context.controlRestWorld.size();

	ModuleInverseResult result;
	result.controlLocal.resize(count);
	result.socketWorld.resize(count);
	result.solved = true;

	const MMatrix mRootDelta = input.parentDelta.empty() ? MMatrix::identity : input.parentDelta[0];
	MMatrix mParentDelta = mRootDelta;

	// A hierarchy keeps each control's motion for its children. A chain only carries it to the next control.
	const bool isHierarchy = context.parentIndex.size() == count;
	std::vector<MMatrix> motion(isHierarchy ? count : 0);
	for (std::size_t i = 0; i < count; ++i)
	{
		const MMatrix& mRestWorld = context.controlRestWorld[i];
		if (isHierarchy)
		{
			const int parent = context.parentIndex[i];
			mParentDelta = parent < 0 ? mRootDelta : motion[static_cast<std::size_t>(parent)];
		}

		MMatrix mLocal = input.fallbackLocal[i];
		if (useJoints && input.jointWorld[i].has_value())
		{
			mLocal = localFromWorld(*input.jointWorld[i], mRestWorld, mParentDelta);
			result.controlLocal[i] = mLocal;
		}
		else if (useJoints)
		{
			result.solved = false;
			result.reason = "a joint is missing from the source";
		}

		const ControlSolveStep step = RigModuleNodeBase::solveControlStep(mLocal, mRestWorld, mParentDelta);
		result.socketWorld[i] = step.mWorld;
		const MMatrix mDelta = MathUtils::computeDeltaFromRest(mRestWorld, step.mWorld);
		if (isHierarchy)
		{
			motion[i] = mDelta;
		}
		else
		{
			mParentDelta = mDelta;
		}
	}
	return result;
}
