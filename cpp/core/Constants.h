#pragma once

// No Maya headers here: the Maya-free core (JointMarkup) includes this file.
// Maya-typed constants live in Colors.h.
#include <array>
#include <string_view>

// The plugin version MFnPlugin registers, and the one stamped on every rig root a build creates (D29).
namespace PluginInfo
{
	inline constexpr const char* Vendor{ "Ben Morgan" };
	inline constexpr const char* Version{ "0.1" };
	// Maya Profiler category for the plugin's events (ProfilerUtils).
	inline constexpr const char* ProfilerCategory{ "RigModules" };
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
	inline constexpr const char* Xray{ "xray" };
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

// Keys in a pose file, see PoseFile.h. Public API: saved poses depend on them.
namespace PoseFileKey
{
	inline constexpr const char* FormatVersion{ "formatVersion" };
	inline constexpr const char* Controls{ "controls" };
}

// Keys in a skin weight file, see SkinWeightFile.h. Public API: saved weight files depend on them.
namespace SkinWeightFileKey
{
	inline constexpr const char* FormatVersion{ "formatVersion" };
	inline constexpr const char* MeshId{ "meshId" };
	inline constexpr const char* MeshName{ "meshName" };
	inline constexpr const char* VertexCount{ "vertexCount" };
	inline constexpr const char* FaceCount{ "faceCount" };
	inline constexpr const char* TopologyHash{ "topologyHash" };
	inline constexpr const char* SkinMethod{ "skinMethod" };
	inline constexpr const char* Influences{ "influences" };
	inline constexpr const char* Weights{ "weights" };
	inline constexpr const char* BlendWeights{ "blendWeights" };   // only for WeightBlended
}

// skinCluster.skinningMethod, in Maya's enum order. Weight files store the names.
namespace SkinMethods
{
	inline constexpr const char* Linear{ "Linear" };
	inline constexpr const char* DualQuaternion{ "DualQuaternion" };
	inline constexpr const char* WeightBlended{ "WeightBlended" };

	inline constexpr std::array<const char*, 3> All{ Linear, DualQuaternion, WeightBlended };

	// The enum index of a method name, or -1 for an unknown one.
	constexpr int indexOf(std::string_view method)
	{
		for (std::size_t i = 0; i < All.size(); ++i)
		{
			if (method == All[i])
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}
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
	inline constexpr const char* JointRestWorld{ "jointRestWorld" };

	// Skin weights (D35): a GUID string on a mesh transform, stamped once by setRigMeshId
	inline constexpr const char* MeshId{ "meshId" };

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

// How a control channel's value goes to the other side in a mirror (D34). Public API: the categories are stored
// on every custom channel attribute in built and published rigs, and the names are in getRigControlChannels' result.
namespace ChannelMirror
{
	inline constexpr const char* Copy{ "copy" };
	inline constexpr const char* Negate{ "negate" };
	// Attribute categories (MFnAttribute::addToCategory). Every custom channel has exactly one of them.
	inline constexpr const char* CopyCategory{ "mirrorCopy" };
	inline constexpr const char* NegateCategory{ "mirrorNegate" };
}

// Keys of one channel in getRigControlChannels' JSON result. Public API, read through the rigmodules Python package.
namespace ControlChannelKey
{
	inline constexpr const char* Name{ "name" };
	inline constexpr const char* Type{ "type" };
	inline constexpr const char* Rest{ "rest" };
	inline constexpr const char* Min{ "min" };
	inline constexpr const char* Max{ "max" };
	inline constexpr const char* Mirror{ "mirror" };
}

// Keys of one control in getRigControlInfo's JSON result (D34). Public API, read through the rigmodules Python package.
namespace ControlInfoKey
{
	inline constexpr const char* ModuleType{ "moduleType" };
	inline constexpr const char* Module{ "module" };
	inline constexpr const char* Side{ "side" };
	inline constexpr const char* Usage{ "usage" };
	inline constexpr const char* Mirror{ "mirror" };
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
	inline constexpr const char* FormatVersion{ "formatVersion" };   // optional, 1 when missing (D33)
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

// The driver gives only its own component; the rest comes from the child plug's parentStructure parent (D31).
namespace ConstraintTypes
{
	inline constexpr const char* Parent{ "Parent" };   // translate, rotate and scale from the driver
	inline constexpr const char* Orient{ "Orient" };   // rotate from the driver
	inline constexpr const char* Point{ "Point" };     // translate from the driver

	// The rigConstraint node's constraintType enum, in this order. Scenes store the index, so append new types.
	inline constexpr std::array<const char*, 3> All{ Parent, Orient, Point };

	// The enum index of a type name, or -1 for an unknown one.
	constexpr int indexOf(std::string_view type)
	{
		for (std::size_t i = 0; i < All.size(); ++i)
		{
			if (type == All[i])
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}
}

namespace ConstraintAttr
{
	inline constexpr const char* Drivers{ "drivers" };
	inline constexpr const char* DriverMatrix{ "driverMatrix" };
	inline constexpr const char* DriverRest{ "driverRest" };
	inline constexpr const char* DriverLabel{ "driverLabel" };
	inline constexpr const char* ActiveDriver{ "activeDriver" };
	inline constexpr const char* ConstraintType{ "constraintType" };
	inline constexpr const char* ParentMatrix{ "parentMatrix" };
	inline constexpr const char* ParentRest{ "parentRest" };
	inline constexpr const char* ChildRest{ "childRest" };
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
