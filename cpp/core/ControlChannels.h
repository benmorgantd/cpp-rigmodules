#pragma once

// Maya-free core: no Maya headers here.
// A control's animation interface (D34): its keyable, writable channels. Each channel's rest is its attribute
// default, its range is the attribute's min and max, and its mirror mode says how a value goes to the other side.
//
// getRigControlChannels returns, per control name:
//   [ { "name": "translateX", "type": "doubleLinear", "rest": 0.0, "min": null, "max": null, "mirror": "copy" }, ... ]
// in channel box order. Values are in the scene's UI units, like cmds.getAttr and cmds.setAttr.

#include <string>
#include <utility>
#include <vector>

struct ControlChannel
{
	std::string name;        // the attribute's long name. Hard API (D32).
	std::string type;        // Maya's attribute type name: "double", "doubleAngle", "enum", "bool", ...
	double rest = 0.0;
	bool hasMin = false;
	double min = 0.0;
	bool hasMax = false;
	double max = 0.0;
	bool negate = false;     // mirror mode: false copies the value, true negates it
};

// Control name -> its channels, in the order the caller asked for the controls.
using ControlChannelList = std::vector<std::pair<std::string, std::vector<ControlChannel>>>;

// One JSON object keyed by control name. Keys in ControlChannelKey, mirror names in ChannelMirror.
std::string serializeControlChannels(const ControlChannelList& controls);

// Flipping a Center control across the world YZ plane (D34). xOfLocalAxes holds the world X component of the
// control's three unit rest axes. When one local axis lies along world X, the flip negates the translate on that
// axis and the rotates on the other two, and this returns its index (0 to 2). Returns -1 when no axis does: the flip
// would then mix channels, so the caller leaves the control alone.
int findMirrorAxis(const double xOfLocalAxes[3]);
