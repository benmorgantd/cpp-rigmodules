#pragma once

#include <string>

#include <maya/MDagModifier.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MObjectArray.h>
#include <maya/MString.h>

// Scene side of the temporary push-out display (design/push-out.md section 5). A display is a rigControlNode drawn as
// the capsule of one rigPushOut node. It follows the capsule's joint live through its offsetParentMatrix, so it moves with
// the rig, and sits at the node's stored offset. It is not part of the rig: nothing refers to it, and the template holds
// the numbers. The user moves and rotates it, and changes its `radius` and `halfLength` attributes, then reads the result
// with readCapsule. The only name the rig knows it by is RigAttr::PushOutDisplay.
class PushOutScene
{
public:
	// Every rigPushOut node in the namespace (absolute, without colons at its ends, "" for the root), and every display.
	static void findPushOuts(const MString& absNamespace, MObjectArray& oOutPushOuts);
	static void findDisplays(const MString& absNamespace, MObjectArray& oOutDisplays);

	// Queues one display for each push out, named `<push out name>_display`, in the current namespace. Maya's matrixNodes
	// plugin supplies the two utility nodes each display uses and is loaded if it is not. Runs the modifier's doIt itself,
	// because attributes must exist before they are connected. False, with the reason in outError, when a push out has no
	// joint wired or a display node of the same name is already there.
	static MStatus queueCreateDisplays(const MObjectArray& oPushOuts, MDagModifier& dagMod, MObjectArray& oOutDisplays, std::string& outError);

	// Queues the deletion of each display and the two utility nodes it uses. The caller runs doIt.
	static MStatus queueDeleteDisplays(const MObjectArray& oDisplays, MDagModifier& dagMod);

	// The template's `capsule` object for a display, as JSON text: the followed joint's `follow` selector, `radius`,
	// `halfLength` and the `offset` of the display's world from that joint's, rounded to 6 places. False, with the reason in
	// outError, for a display with no joint, with a scale (resize with radius and halfLength) or with no markup on its joint.
	static bool readCapsule(const MObject& oDisplay, std::string& outCapsuleJson, std::string& outError);

private:
	// The joint a display follows: the source of its multMatrix's second input. A null object when it has none.
	static MObject followedJoint(const MObject& oDisplay);
};
