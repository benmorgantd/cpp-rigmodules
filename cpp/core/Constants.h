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
	inline constexpr const char* Meta{ "meta" };
	inline constexpr const char* Controls{ "controls" };
}

// Keys of the `meta` object in a pose file and a ControlAnim file, see LibraryMeta.h (D51). Public API: saved library
// files and the library browser's search depend on them. The first block is filled from the rig when a file is saved,
// and the rest comes from the caller.
namespace LibraryMetaKey
{
	inline constexpr const char* AssetId{ "assetId" };
	inline constexpr const char* RigName{ "rigName" };
	inline constexpr const char* SkeletonHash{ "skeletonHash" };
	inline constexpr const char* PluginVersion{ "pluginVersion" };
	inline constexpr const char* Created{ "created" };

	inline constexpr const char* Template{ "template" };
	inline constexpr const char* Skeleton{ "skeleton" };

	// Version 2 names, renamed by the version 3 update step (D54). Read only by that step.
	inline constexpr const char* V2Variant{ "variant" };
	inline constexpr const char* V2SkeletonId{ "skeletonId" };
	inline constexpr const char* Author{ "author" };
	inline constexpr const char* Description{ "description" };
	inline constexpr const char* Category{ "category" };
	inline constexpr const char* Status{ "status" };
	inline constexpr const char* Tags{ "tags" };
	inline constexpr const char* RigRegions{ "rigRegions" };
}

// Keys of one file in getRigDataFileInfo's JSON result, see LibraryFile.h (D51). Public API: the library browser reads them.
namespace DataFileInfoKey
{
	inline constexpr const char* File{ "file" };
	inline constexpr const char* Error{ "error" };
	inline constexpr const char* Kind{ "kind" };
	inline constexpr const char* FormatVersion{ "formatVersion" };
	inline constexpr const char* Meta{ "meta" };
	inline constexpr const char* FrameRate{ "frameRate" };
	inline constexpr const char* StartFrame{ "startFrame" };
	inline constexpr const char* EndFrame{ "endFrame" };
	inline constexpr const char* ControlCount{ "controlCount" };
	inline constexpr const char* ChannelCount{ "channelCount" };
	inline constexpr const char* UnmatchedControls{ "unmatchedControls" };
	inline constexpr const char* KindPose{ "pose" };
	inline constexpr const char* KindAnimation{ "animation" };
}

// Keys in a JointClip file, see JointClipFile.h. Public API: the Python capture writes them.
namespace JointClipFileKey
{
	inline constexpr const char* FormatVersion{ "formatVersion" };
	inline constexpr const char* FrameRate{ "frameRate" };
	inline constexpr const char* StartFrame{ "startFrame" };
	inline constexpr const char* Joints{ "joints" };
	inline constexpr const char* Poses{ "poses" };
	inline constexpr const char* Path{ "path" };
	inline constexpr const char* Markup{ "markup" };
	inline constexpr const char* Rest{ "rest" };
	inline constexpr const char* Translate{ "translate" };
	inline constexpr const char* Rotate{ "rotate" };
	inline constexpr const char* Scale{ "scale" };
}

// Keys in a ControlAnim file and in the keys payload of getRigKeys and setRigKeys, see ControlAnimFile.h and RigKeys.h.
// Public API: saved animation depends on them.
namespace ControlAnimKey
{
	inline constexpr const char* FormatVersion{ "formatVersion" };
	inline constexpr const char* FrameRate{ "frameRate" };
	inline constexpr const char* StartFrame{ "startFrame" };
	inline constexpr const char* EndFrame{ "endFrame" };
	inline constexpr const char* Meta{ "meta" };
	inline constexpr const char* Controls{ "controls" };
	inline constexpr const char* Weighted{ "weighted" };
	inline constexpr const char* PreInfinity{ "preInfinity" };
	inline constexpr const char* PostInfinity{ "postInfinity" };
	inline constexpr const char* Keys{ "keys" };
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
	inline constexpr const char* SkeletonHash{ "skeletonHash" };   // version 2 (D54)
	inline constexpr const char* SkinMethod{ "skinMethod" };
	inline constexpr const char* Influences{ "influences" };
	inline constexpr const char* Weights{ "weights" };
	inline constexpr const char* BlendWeights{ "blendWeights" };   // only for WeightBlended
	inline constexpr const char* Skeleton{ "skeleton" };           // version 3 (D70): every marked joint and its parent
}

// Keys of the look report setRigLook returns, see LookReport.h (D70). Public API: mayadev reads them.
namespace LookReportKey
{
	inline constexpr const char* LookRoot{ "lookRoot" };
	inline constexpr const char* SkeletonStale{ "skeletonStale" };
	inline constexpr const char* Meshes{ "meshes" };
	inline constexpr const char* MeshId{ "meshId" };
	inline constexpr const char* MeshName{ "meshName" };
	inline constexpr const char* Result{ "result" };
	inline constexpr const char* Reasons{ "reasons" };
	inline constexpr const char* StaleSkeleton{ "staleSkeleton" };
}

// A mesh's result in the look report: how far down the fallback ladder it went (D70).
namespace LookResult
{
	inline constexpr const char* Loaded{ "loaded" };           // the file's weights as saved
	inline constexpr const char* Folded{ "folded" };           // some influences moved up to the next skin joint
	inline constexpr const char* Transferred{ "transferred" }; // the weights of another surface, by closest vertices
	inline constexpr const char* Rigid{ "rigid" };             // bound whole to the topmost skin joint
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

// Keys in a skeleton file, see SkeletonFile.h (D54). Public API: skeleton files and the hashes stamped from them
// depend on them. A joint's markup flags reuse JointMarkupKey::Export and JointMarkupKey::Skin.
namespace SkeletonFileKey
{
	inline constexpr const char* FormatVersion{ "formatVersion" };
	inline constexpr const char* SkeletonHash{ "skeletonHash" };
	inline constexpr const char* Joints{ "joints" };
	inline constexpr const char* Markup{ "markup" };
	inline constexpr const char* Name{ "name" };
	inline constexpr const char* Parent{ "parent" };
	inline constexpr const char* RotateOrder{ "rotateOrder" };
	inline constexpr const char* RestLocal{ "restLocal" };
}

// A transform's rotateOrder, in Maya's enum order. Skeleton files store the names.
namespace RotateOrders
{
	inline constexpr std::array<const char*, 6> All{ "xyz", "yzx", "zxy", "xzy", "yxz", "zyx" };
}

// Compute stays on the statics. Only construction and tool code uses the name lookups.
// This is used in situations where we are sharing construction code. Within the modules themselves 
// we should continue to reference their static attributes.
namespace RigAttr
{
	// RigRoot layer
	// The asset the rig belongs to (D29). On the rig root since D54 merged the asset root into it.
	inline constexpr const char* AssetId{ "assetId" };
	inline constexpr const char* RigName{ "rigName" };
	// The skeleton's hash (D54): skeletonHash of the build's input joints at rest, as Hash64::format text.
	inline constexpr const char* SkeletonHash{ "skeletonHash" };
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
	// FkHierarchy (D71): int array, one per control slot: the slot of the control's parent, or -1 for the root. A parent's slot is always lower.
	inline constexpr const char* ControlParentIndex{ "controlParentIndex" };

	// Skin weights (D35): a GUID string on a mesh transform, stamped once by setRigMeshId
	inline constexpr const char* MeshId{ "meshId" };

	// Joint markup: one JSON string per joint, see JointMarkup.h
	inline constexpr const char* JointMarkup{ "rigMarkup" };

	// RigRootNode
	inline constexpr const char* RigModules{ "rigModules" };
	inline constexpr const char* RigTemplateName{ "rigTemplateName" };
	// Display layers the build creates (D38), one message attribute each, so a tool finds the joint, mesh or
	// control layer without reading names.
	inline constexpr const char* JointDisplayLayer{ "jointDisplayLayer" };
	inline constexpr const char* MeshDisplayLayer{ "meshDisplayLayer" };
	inline constexpr const char* ControlDisplayLayer{ "controlDisplayLayer" };
	// The groups the build creates (D43): rig_all, the parent of the rig's top joints and controls, and mesh_grp under it,
	// which the model import fills. One message attribute each.
	inline constexpr const char* AllGroup{ "allGroup" };
	inline constexpr const char* MeshGroup{ "meshGroup" };
	// Selection sets the build creates (D39): every set, nested module subsets included, on one message array.
	// Each set says what it is in its own SetMarkup attribute.
	inline constexpr const char* SelectionSets{ "selectionSets" };
	inline constexpr const char* SetMarkup{ "setMarkup" };

	// The look a rig wears (D54). The rig root's `look` message connects to the lookRoot node's `rigRoot`. The lookRoot
	// holds the look's asset id (AssetId), its name, its publishVersion (PublishVersion) and one `lookMembers` message per
	// node the look brought in, so a swap deletes exactly those.
	inline constexpr const char* Look{ "look" };

	// The rig an attachment follows (D55): this rig root's `parentRig` message connects from the parent's `attachments` array.
	inline constexpr const char* ParentRig{ "parentRig" };
	inline constexpr const char* Attachments{ "attachments" };
	inline constexpr const char* LookName{ "lookName" };
	inline constexpr const char* LookMembers{ "lookMembers" };

	// Constraints
	inline constexpr const char* Constraints{ "constraints" };      // RigRoot message array
	inline constexpr const char* ControlSpace{ "space" };           // dynamic enum on a constrained control

	// TwoBoneIk channels (D52). Each is the attribute on its control and the input of the same name on the module.
	// Public API: saved poses and animation key on them.
	inline constexpr const char* IkFkBlend{ "ikFkBlend" };   // settings control: 0 is FK, 1 is IK
	inline constexpr const char* Stretch{ "stretch" };       // settings control
	inline constexpr const char* Soft{ "soft" };             // settings control
	inline constexpr const char* Pin{ "pin" };               // pole control
	inline constexpr const char* Roll{ "roll" };             // IK handle control
	inline constexpr const char* PoleSpace{ "poleSpace" };   // module input, fed by the pole control's ControlSpace enum

	// TwoBoneIk match outputs (D52 section 4.1), pulled by setRigIkFkBlend: the control locals that reproduce the current
	// pose on the FK side (slots 0 to 2) and the IK side (slots 3 and 4), and a JSON list of what the match can't do.
	inline constexpr const char* FkMatchLocal{ "fkMatchLocal" };
	inline constexpr const char* IkMatchLocal{ "ikMatchLocal" };
	inline constexpr const char* MatchWarnings{ "matchWarnings" };

	// Ribbon channels (D56), all on the first control. Each is the attribute on that control and the input of the same
	// name on the module. Public API: saved poses and animation key on them.
	inline constexpr const char* StartTwist{ "startTwist" };                   // the secondary twist angles that pick a branch past 180 degrees
	inline constexpr const char* EndTwist{ "endTwist" };
	inline constexpr const char* SineAmplitude{ "sineAmplitude" };
	inline constexpr const char* SineFrequency{ "sineFrequency" };
	inline constexpr const char* SinePhase{ "sinePhase" };
	inline constexpr const char* SineOrient{ "sineOrient" };
	inline constexpr const char* SineStartFalloff{ "sineStartFalloff" };
	inline constexpr const char* SineEndFalloff{ "sineEndFalloff" };
	inline constexpr const char* StartAimBlend{ "startAimBlend" };
	inline constexpr const char* EndAimBlend{ "endAimBlend" };
	inline constexpr const char* BendRoundness{ "bendRoundness" };
	inline constexpr const char* VolumePreserve{ "volumePreserve" };
	inline constexpr const char* ScaleStart{ "scaleStart" };
	inline constexpr const char* ScaleMid{ "scaleMid" };
	inline constexpr const char* ScaleEnd{ "scaleEnd" };

	// Ribbon rest data, stored at build and never changed. The module reads these and the controls' rests: nothing else
	// describes the rest pose.
	inline constexpr const char* IkControlCount{ "ikControlCount" };   // how many of the leading slots are ik controls: the rest are tweakers
	inline constexpr const char* FkControlCount{ "fkControlCount" };   // how many of the trailing slots are fk controls (0 with no fk chain, D59)
	inline constexpr const char* KnotU{ "knotU" };
	inline constexpr const char* BindU{ "bindU" };
	inline constexpr const char* RestLength{ "restLength" };
	inline constexpr const char* RestUp{ "restUp" };
	inline constexpr const char* RestTangent{ "restTangent" };
	inline constexpr const char* ChainSign{ "chainSign" };   // +1 when the controls' X axes run down the chain, -1 when they point back
	inline constexpr const char* IkRestOffset{ "ikRestOffset" };
	inline constexpr const char* BindRestOffset{ "bindRestOffset" };
	// The fk chain's rest roll about the curve, one per fk control, stored so no evaluation rebuilds the rest curve to measure it.
	inline constexpr const char* FkRollRest{ "fkRollRest" };
	inline constexpr const char* FkRollRestIn{ "fkRollRestIn" };
	// Limb (D62): the ribbon ik controls' offsets from the curve, apart from IkRestOffset, which is the two-bone limb's.
	inline constexpr const char* RibbonIkRestOffset{ "ribbonIkRestOffset" };
	// Limb channel on the settings control: how round the elbow is. 0 is a sharp corner (the default), 1 the smooth curve.
	inline constexpr const char* BendSoftness{ "bendSoftness" };
	// Limb (D62 phase 4): whether the optional ikTarget child plug has a parent, set at build when the template wires it.
	inline constexpr const char* IkTargetConnected{ "ikTargetConnected" };
	// Limb's ikTarget input (a matrix and its rest) and the output array that holds the ik, pole and settings sockets apart from
	// OutputSocketMatrix, so that nothing the target moves is on the same attribute as what the foot hangs from (see design/limb.md 8.1).
	inline constexpr const char* IkTargetMatrix{ "ikTargetMatrix" };
	inline constexpr const char* IkTargetRestMatrix{ "ikTargetRestMatrix" };
	inline constexpr const char* HandleSocketMatrix{ "handleSocketMatrix" };

	// TwoBoneIk rest data (D52), stored at build. Its ik controls' offsets are IkRestOffset above.
	inline constexpr const char* RestBendNormal{ "restBendNormal" };
	inline constexpr const char* RestPlaneAngle{ "restPlaneAngle" };
	inline constexpr const char* LimbLengths{ "limbLengths" };

	// Aim rest data, stored at build.
	inline constexpr const char* AimRestOffset{ "aimRestOffset" };
}

// Maya's own joint attributes, which the API has no static MObject for. Where the API has one (MPxTransform::translate,
// MPxTransform::offsetParentMatrix, MPxNode::message), use it instead: it needs no name lookup. The attributes of a node
// type that one scene file owns stay file-local constants there (DisplayLayerScene, SelectionSetScene, SkinWeightScene).
namespace MayaAttr
{
	inline constexpr const char* JointOrientX{ "jointOrientX" };
	inline constexpr const char* JointOrientY{ "jointOrientY" };
	inline constexpr const char* JointOrientZ{ "jointOrientZ" };
	inline constexpr const char* SegmentScaleCompensate{ "segmentScaleCompensate" };
	inline constexpr const char* LockInfluenceWeights{ "lockInfluenceWeights" };
}

// Maya's own node types that the build creates by name (D67). This plugin's node types are registered in ModuleRegistry.
namespace MayaNodeType
{
	inline constexpr const char* Transform{ "transform" };
	inline constexpr const char* Joint{ "joint" };
	inline constexpr const char* DisplayLayer{ "displayLayer" };
	inline constexpr const char* ObjectSet{ "objectSet" };
}

// Labels of the pole control's module-owned space enum (D52 section 4.4). Append-only (D34): the index is the value.
namespace PoleSpaceLabels
{
	inline constexpr const char* Parent{ "Parent" };
	inline constexpr const char* Ik{ "Ik" };
}

// Keys of getRigInfo's JSON result. Public API: mayadev reads them through the rigmodules Python package.
// The attribute values reuse their RigAttr names (rigName, assetId, skeletonHash, ...). The rig root's node name
// reuses RigAttr::RigRoot. Only the namespace has no attribute of its own.
namespace RigInfoKey
{
	inline constexpr const char* Namespace{ "namespace" };
	// The look the rig wears: null, or an object with the keys below (D54). The look's own asset id may differ from the rig's.
	inline constexpr const char* Look{ "look" };
	inline constexpr const char* LookAssetId{ "assetId" };
	inline constexpr const char* LookName{ "name" };
	inline constexpr const char* LookPublishVersion{ "publishVersion" };
	// The rig root the rig is attached to (D55): null, or its node name.
	inline constexpr const char* ParentRigRoot{ "parentRigRoot" };
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

// Keys of getRigControlSpace's JSON result. Public API, read through the rigmodules Python package.
namespace ControlSpaceKey
{
	inline constexpr const char* Constraint{ "constraint" };
	inline constexpr const char* Module{ "module" };
	inline constexpr const char* Labels{ "labels" };
	inline constexpr const char* Current{ "current" };
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
	inline constexpr const char* Ik{ "ik" };
	inline constexpr const char* Pole{ "pole" };
	inline constexpr const char* Settings{ "settings" };
	// Limb's optional child plug (D62 section 8): the world matrix the end joint should reach, left unparented for the IK handle's own.
	inline constexpr const char* IkTarget{ "ikTarget" };
	// A Limb's bind joint sockets: bind_0, bind_1, ... in bind joint order (D62), so a child can hang from a twist joint.
	inline constexpr const char* Bind{ "bind_" };
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
	inline constexpr const char* ConstraintWeight{ "constraintWeight" };   // 0..1, default 1 (D63). Not the driver's `weight`.
	inline constexpr const char* DriverSocket{ "socket" };
	inline constexpr const char* DriverLabel{ "label" };
	inline constexpr const char* DriverWeight{ "weight" };
}

// An attachment's template names sockets of the rig it follows with this prefix, in a parentStructure value or a
// constraint driver: "parent:head.end" (D55). Public API: every attachment template depends on it.
namespace ParentRigId
{
	inline constexpr const char* Prefix{ "parent:" };
}

// The driver gives only its own component; the rest comes from the child plug's parentStructure parent (D31).
namespace ConstraintTypes
{
	inline constexpr const char* Parent{ "Parent" };   // translate, rotate and scale from the driver
	inline constexpr const char* Orient{ "Orient" };   // rotate from the driver
	inline constexpr const char* Point{ "Point" };     // translate from the driver
	inline constexpr const char* Swing{ "Swing" };     // translate and the rotation without its twist about X from the driver
	inline constexpr const char* Twist{ "Twist" };     // translate and the twist about X from the driver, the swing from the parent

	// The rigConstraint node's constraintType enum, in this order. Scenes store the index, so append new types.
	inline constexpr std::array<const char*, 5> All{ Parent, Orient, Point, Swing, Twist };

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
	inline constexpr const char* ConstraintWeight{ "constraintWeight" };
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

// Keys and values of a selection set's setMarkup (D39). Public API: tools find sets through them.
namespace SelectionSetKey
{
	inline constexpr const char* Kind{ "kind" };
	inline constexpr const char* Role{ "role" };
	inline constexpr const char* Module{ "module" };
}

// What a set is within its kind: the top set, a side set, the set that holds the module sets, or one module's set.
namespace SelectionSetRole
{
	inline constexpr const char* All{ "all" };
	inline constexpr const char* Side{ "side" };
	inline constexpr const char* Modules{ "modules" };
	inline constexpr const char* Module{ "module" };
}

namespace SelectionSetKind
{
	inline constexpr const char* Controls{ "controls" };
	inline constexpr const char* Joints{ "joints" };   // the skin joints
}

namespace SideNames
{
	inline constexpr const char* Center{ "Center" };
	inline constexpr const char* Left{ "Left" };
	inline constexpr const char* Right{ "Right" };
}

namespace RigNaming
{
	// The layout's base name: its controls' usages are "layout_<n>" and its controls are layout_<nn>_M_CTRL (D58).
	inline constexpr const char* LayoutBase{ "layout" };
	// Reserved module name for the implicit layout module (D58). Templates address it as "layout_00_M_MOD.end".
	inline constexpr const char* LayoutModule{ "layout_00_M_MOD" };
	// The lookRoot node of a rig (D54), created in the rig namespace.
	inline constexpr const char* LookRoot{ "lookRoot" };
	// The display layers of a rig (D38), created in the rig namespace.
	inline constexpr const char* JointLayer{ "jointsLayer" };
	inline constexpr const char* MeshLayer{ "meshesLayer" };
	inline constexpr const char* ControlLayer{ "controlsLayer" };
	// The rig's top group and the group of its imported meshes (D43), in the rig namespace.
	inline constexpr const char* AllGroup{ "rig_all" };
	inline constexpr const char* MeshGroup{ "mesh_grp" };
}
