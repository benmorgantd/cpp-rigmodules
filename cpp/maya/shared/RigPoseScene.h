#pragma once

#include "core/ControlChannels.h"

#include <string>
#include <vector>

#include <maya/MDGModifier.h>
#include <maya/MFnAttribute.h>
#include <maya/MObject.h>
#include <maya/MPlug.h>
#include <maya/MStatus.h>
#include <maya/MStringArray.h>

// Scene side of the animation interface (D34): a control's channels, and the pose tools that iterate them.
// A channel is a keyable attribute a tool may write (DagUtils::isChannelWritable). Its rest is the attribute's
// default. Every function displays its own errors. The pose functions queue their writes: the caller runs the modifier.
class RigPoseScene
{
public:
	// How a channel's value is read and written.
	enum class ValueKind
	{
		kDouble,
		kFloat,
		kInt,
		kShort,
		kBool,
		kEnum,
		kAngle,
		kDistance
	};

	struct Channel
	{
		MPlug plug;
		ControlChannel info;        // rest, min and max in UI units
		ValueKind kind = ValueKind::kDouble;
		double rest = 0.0;          // in internal units (radians, centimeters)
		int transformIndex = -1;    // index into kDefaultTransformValues (translate, rotate, scale), or -1
	};

	// Declares how a custom channel mirrors, as an attribute category (ChannelMirror). Every keyable attribute a
	// module or constraint adds to a control must call this before the attribute is added: getControlChannels fails
	// on one that didn't. Built-in channels (transforms, visibility) need nothing and copy.
	static MStatus setMirrorMode(MFnAttribute& fnAttr, bool negate);

	// The control's channels in channel box order.
	static MStatus getControlChannels(const MObject& oControl, std::vector<Channel>& outChannels);

	// Every channel of every control in the rig goes to its rest. outChangedNames lists the controls that moved.
	static MStatus zeroRigControls(const MObject& oRigRoot, MDGModifier& dgMod, MStringArray& outChangedNames);

	// Every sourceSide control's values go onto its mirror control: copied, or negated for a channel that says so.
	// Center controls and the source side keep their values. outChangedNames lists the mirrors that changed.
	static MStatus mirrorRigPose(const MObject& oRigRoot, const std::string& sourceSide, MDGModifier& dgMod, MStringArray& outChangedNames);

	// Left and Right controls swap values, and every Center control is reflected across the world YZ plane through
	// its channels (findMirrorAxis). outChangedNames lists the controls that changed.
	static MStatus flipRigPose(const MObject& oRigRoot, MDGModifier& dgMod, MStringArray& outChangedNames);
};
