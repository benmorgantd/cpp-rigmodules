#pragma once
#include <maya/MColor.h>


namespace RigControlNodeAttr
{
	inline constexpr const char* ShapeType{ "shapeType" };
	inline constexpr const char* ShapeTransform{ "shapeTransform" };
	inline constexpr const char* WireframeColor{ "wireframeColor" };
	inline constexpr const char* WireframeAlpha{ "wireframeAlpha" };
	inline constexpr const char* Filled{ "filled" };
	inline constexpr const char* RigModule{ "rigModule" };
}

// Compute stays on the statics. Only construction and tool code uses the name lookups.
// This is used in situations where we are sharing construction code. Within the modules themselves 
// we should continue to reference their static attributes.
namespace RigAttr
{
	// AssetRoot layer
	inline constexpr const char* AssetId{ "assetId" };
	inline constexpr const char* AssetRoot{ "assetRoot" };
	inline constexpr const char* AssetType{ "assetType" };
	inline constexpr const char* AssetData{ "assetData" };
	inline constexpr const char* Children{ "children" };

	// RigRoot layer

	// RigModuleNode layer
	inline constexpr const char* ModuleData{ "moduleData" };
	inline constexpr const char* RigRoot{ "rigRoot" };
	inline constexpr const char* Side{ "side" };

	// RigModuleWithControlsNode layer
	inline constexpr const char* ControlInputData{ "controlData" };
	inline constexpr const char* ControlLocalMatrix{ "controlLocalMatrix" };
	inline constexpr const char* ControlRestWorld{ "controlRestWorld" };
	inline constexpr const char* ChildPlugs{ "childPlugs" };
	inline constexpr const char* ChildPlugName{ "childPlugName" };
	inline constexpr const char* ChildPlugParentMatrix{ "childPlugParentMatrix" };
	inline constexpr const char* ChildPlugParentRestMatrix{ "childPlugParentRest" };
	inline constexpr const char* ControlOPM{ "controlOPM" };
	inline constexpr const char* ControlMessage{ "controlMessage" };
	inline constexpr const char* OutputSocketMatrix{ "outputSocketMatrix" };
	inline constexpr const char* SocketNames{ "socketNames" };

	// RigModuleWithJointsNode layer
	inline constexpr const char* JointOPM{ "jointOPM" };

	// RigRootNode
	inline constexpr const char* RigModules{ "rigModules" };
}

namespace SideNames
{
	inline constexpr const char* Center{ "Center" };
	inline constexpr const char* Left{ "Left" };
	inline constexpr const char* Right{ "Right" };
}

namespace Colors
{
	// Control Colors
	inline MColor RigLeft(0.0f, 1.0f, 0.0f);
	inline MColor RigRight(1.0f, 0.0f, 0.0f);
	inline MColor RigCenter(1.0f, 1.0f, 0.0f);

	// Selection Colors
	inline MColor ActiveLead(1.0f, 1.0f, 1.0f);  // White
	inline MColor ActiveSelected(0.0f, 1.0f, 0.64f);  // Green
}