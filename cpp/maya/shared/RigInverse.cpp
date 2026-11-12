#include "maya/shared/RigInverse.h"

#include <maya/MGlobal.h>

MStatus RigInverse::solve(const MObject&, const JointClip&, const std::vector<PoseMap>&, std::vector<PoseMap>&)
{
	MGlobal::displayError("The inverse solve is not built yet (D41 skeleton), so no control values were produced.");
	return MStatus::kNotImplemented;
}
