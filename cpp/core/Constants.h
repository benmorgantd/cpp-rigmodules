#pragma once

// No Maya headers here: the Maya-free core (JointMarkup) includes this file.
// Maya-typed constants live in Colors.h.
#include <array>

// The plugin version MFnPlugin registers, and the one stamped on every rig root a build creates (D29).
namespace PluginInfo
{
	inline constexpr const char* Vendor{ "Ben Morgan" };
	inline constexpr const char* Version{ "0.1" };
}

namespace RigControlNodeAttr
{
	inline constexpr const char* ShapeType{ "shapeType" };
	// Same order as the ShapeType enum in ControlShapes.h, which static_asserts the count.
	// Public API: templates and shape files store these names.
	namespace ShapeTypes
	{
		inline constexpr std::array All =
		{
			"Box",
			"Sphere",
			"Circle",
			"Capsule",
			"Triangle",
			"Cylinder",
			"Square",
			"HalfCircleArrow",
			"ArchedDoubleArrow",
			"PointedCircle",
			"Diamond",
			"CircleArrow",
			"Dome",
			"SingleArrow",
			"FourArrow",
			"CircleStick",
			"Pivot"
		};
	}
	inline constexpr const char* ShapeTransform{ "shapeTransform" };
	inline constexpr const char* WireframeColor{ "wireframeColor" };
	inline constexpr const char* WireframeAlpha{ "wireframeAlpha" };
	inline constexpr const char* Filled{ "filled" };
	inline constexpr const char* RigModule{ "rigModule" };
	// One JSON string per control, see ControlMarkup.h. Its keys reuse JointMarkupKey and RigAttr::Side.
	inline constexpr const char* ControlMarkup{ "controlMarkup" };
}

// Keys in a control shape file, see ControlShapeFile.h. Public API: saved shape files depend on them.
// The per-control keys reuse the RigControlNodeAttr attribute names.
namespace ControlShapeFileKey
{
	inline constexpr const char* FormatVersion{ "formatVersion" };
	inline constexpr const char* Controls{ "controls" };
}

// Compute stays on the statics. Only construction and tool code uses the name lookups.
// This is used in situations where we are sharing construction code. Within the modules themselves 
// we should continue to reference their static attributes.
namespace RigAttr
{
	// AssetRoot layer
	inline constexpr const char* AssetId{ "assetId" };
	inline constexpr const char* AssetRoot{ "assetRoot" };
	inline constexpr const char* Children{ "children" };

	// RigRoot layer
	inline constexpr const char* RigName{ "rigName" };
	inline constexpr const char* RigVersion{ "rigVersion" };
	// Import stamps (D29): the plugin that built the rig, and the asset publish it came from (0 until published).
	// Tools compare them with the current plugin and the asset registry's latest publish.
	inline constexpr const char* PluginVersion{ "pluginVersion" };
	inline constexpr const char* PublishVersion{ "publishVersion" };

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

	// Joint markup: one JSON string per joint, see JointMarkup.h
	inline constexpr const char* JointMarkup{ "rigMarkup" };

	// RigRootNode
	inline constexpr const char* RigModules{ "rigModules" };

	// Constraints
	inline constexpr const char* Constraints{ "constraints" };      // RigRoot message array
	inline constexpr const char* ControlSpace{ "space" };           // dynamic enum on a constrained control
}

// Keys of getRigInfo's JSON result. Public API: mayadev reads them through the rigmodules Python package.
// The attribute values reuse their RigAttr names (rigName, assetId, ...). The node names reuse
// RigAttr::RigRoot and RigAttr::AssetRoot. Only the namespace has no attribute of its own.
namespace RigInfoKey
{
	inline constexpr const char* Namespace{ "namespace" };
}

namespace PlugAndSocketNames
{
	inline constexpr const char* Start{ "start" };
	inline constexpr const char* End{ "end" };
	inline constexpr const char* Index{ "index_" };
}

// JSON keys in a rig template. Public API: every saved template depends on them.
// RigRootData maps its keys through NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE, which uses the field names,
// so those field names must match the keys listed here.
namespace TemplateKey
{
	inline constexpr const char* RigName{ "rigName" };
	inline constexpr const char* RigVersion{ "rigVersion" };
	inline constexpr const char* RigTemplateName{ "rigTemplateName" };
	inline constexpr const char* RigAuthor{ "rigAuthor" };
	inline constexpr const char* RigModules{ "rigModules" };
	inline constexpr const char* Constraints{ "constraints" };

	inline constexpr const char* ModuleType{ "moduleType" };
	inline constexpr const char* Name{ "name" };
	inline constexpr const char* ShapeType{ "shapeType" };
	inline constexpr const char* Side{ "side" };
	inline constexpr const char* ModuleVersion{ "moduleVersion" };
	inline constexpr const char* ParentStructure{ "parentStructure" };
	inline constexpr const char* ModuleArgs{ "moduleArgs" };
	inline constexpr const char* Mirror{ "mirror" };   // modules and constraints (D28)

	inline constexpr const char* ConstraintType{ "type" };
	inline constexpr const char* ConstraintTarget{ "target" };
	inline constexpr const char* ConstraintDrivers{ "drivers" };
	inline constexpr const char* DriverSocket{ "socket" };
	inline constexpr const char* DriverLabel{ "label" };
	inline constexpr const char* DriverWeight{ "weight" };
}

namespace ConstraintTypes
{
	inline constexpr const char* Parent{ "Parent" };
}

namespace ConstraintAttr
{
	inline constexpr const char* Drivers{ "drivers" };
	inline constexpr const char* DriverMatrix{ "driverMatrix" };
	inline constexpr const char* DriverRest{ "driverRest" };
	inline constexpr const char* DriverLabel{ "driverLabel" };
	inline constexpr const char* ActiveDriver{ "activeDriver" };
	inline constexpr const char* OutParentMatrix{ "outParentMatrix" };
	inline constexpr const char* RigRoot{ "rigRoot" };
}

// Keys inside the RigAttr::JointMarkup JSON. The side key reuses RigAttr::Side.
// These are public API: saved markup on every skeleton depends on them.
namespace JointMarkupKey
{
	inline constexpr const char* MarkupVersion{ "markupVersion" };
	inline constexpr const char* Usage{ "usage" };
	inline constexpr const char* Export{ "export" };
	inline constexpr const char* Skin{ "skin" };
}

namespace SideNames
{
	inline constexpr const char* Center{ "Center" };
	inline constexpr const char* Left{ "Left" };
	inline constexpr const char* Right{ "Right" };
}

namespace RigNaming
{
	// Reserved module name for the implicit layout module. Templates address it as "layout.end".
	inline constexpr const char* LayoutModule{ "layout" };
	inline constexpr const char* JointSuffix{ "_jnt" };
	inline constexpr const char* ControlSuffix{ "_ctrl" };
	inline constexpr const char* LeftSuffix{ "_l" };
	inline constexpr const char* RightSuffix{ "_r" };
	inline constexpr const char* CenterSuffix{ "_c" };
}
