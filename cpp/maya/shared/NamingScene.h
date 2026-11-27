#pragma once

#include "core/NamingConvention.h"

#include <maya/MObject.h>

// Scene side of the naming convention: which kind of rig node a scene node is.
class NamingScene
{
public:
	// The kind a node is named as: a joint, a rigControlNode, a rig module, a rigConstraint or the transform of a mesh.
	// False for any other node.
	static bool getKind(const MObject& oNode, NamingConvention::Kind& outKind);
};
