#pragma once

#include "core/ControlChannels.h"
#include "core/PoseFile.h"
#include "maya/shared/RigTraversal.h"

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

	// Every channel of every control in the scope goes to its rest. outChangedNames lists the controls that moved.
	static MStatus zeroRigControls(const RigControlScope& scope, MDGModifier& dgMod, MStringArray& outChangedNames);

	// Every sourceSide control in the scope has its values go onto its mirror control: copied, or negated for a channel
	// that says so. Center controls and the source side keep their values. An empty sourceSide means: the whole rig
	// mirrors from Left, and a set of controls mirrors from their side when they all share one, and from Left otherwise.
	// outChangedNames lists the mirrors that changed.
	static MStatus mirrorRigPose(const RigControlScope& scope, const std::string& sourceSide, MDGModifier& dgMod, MStringArray& outChangedNames);

	// Each Left or Right control in the scope swaps values with its mirror (a pair named from both ends swaps once),
	// and each Center control is reflected across the world YZ plane through its channels (findMirrorAxis).
	// outChangedNames lists the controls that changed.
	static MStatus flipRigPose(const RigControlScope& scope, MDGModifier& dgMod, MStringArray& outChangedNames);

	// The channel values of every control in the scope, keyed by markup (PoseFile.h). outControlNames lists the controls
	// read, in scope order.
	static MStatus getRigPose(const RigControlScope& scope, PoseMap& outPose, MStringArray& outControlNames);

	// Writes a pose onto the controls in the scope, matched by markup. A control the pose lacks keeps its values, and
	// so does a channel it lacks. A control or channel the pose has and the rig lacks is skipped with a warning (a
	// control outside a selection scope is not one). outChangedNames lists the controls that changed.
	static MStatus setRigPose(const RigControlScope& scope, const PoseMap& pose, MDGModifier& dgMod, MStringArray& outChangedNames);
};
