#include "maya/shared/NamingScene.h"
#include "maya/nodes/Constraints.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RigModule.h"

#include <maya/MFn.h>
#include <maya/MFnDependencyNode.h>

bool NamingScene::getKind(const MObject& oNode, NamingConvention::Kind& outKind)
{
	if (oNode.hasFn(MFn::kJoint))
	{
		outKind = NamingConvention::Kind::Joint;
		return true;
	}

	MStatus status;
	const MFnDependencyNode fnNode(oNode, &status);
	if (!status)
	{
		return false;
	}

	if (fnNode.typeId() == RigControlNode::id)
	{
		outKind = NamingConvention::Kind::Control;
		return true;
	}
	if (fnNode.typeId() == RigConstraintNode::id)
	{
		outKind = NamingConvention::Kind::Constraint;
		return true;
	}
	if (RigModuleNodeBase::isRigModule(oNode))
	{
		outKind = NamingConvention::Kind::Module;
		return true;
	}
	return false;
}
