#include "maya/shared/NamingScene.h"
#include "core/Constants.h"
#include "maya/nodes/Constraints.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RigModule.h"

#include <maya/MDagPath.h>
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

	// A marker locator (D73) is a transform carrying markup. loadRigMarkers makes them rigControlNodes, so this comes before
	// the control test.
	if (fnNode.hasAttribute(RigAttr::MarkerMarkup))
	{
		outKind = NamingConvention::Kind::Marker;
		return true;
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

	// A mesh is its transform. The shape under it is named by Maya, and a skinned mesh's intermediate shape is skipped.
	MDagPath meshPath;
	if (oNode.hasFn(MFn::kTransform) && MDagPath::getAPathTo(oNode, meshPath) && meshPath.extendToShape() && meshPath.hasFn(MFn::kMesh))
	{
		outKind = NamingConvention::Kind::Mesh;
		return true;
	}
	return false;
}
