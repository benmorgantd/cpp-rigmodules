#pragma once

#include "core/ControlMarkup.h"
#include "core/RigControlFilter.h"

#include <string>
#include <vector>

#include <maya/MDagPath.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MObjectArray.h>
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MStringArray.h>

// What a command that works on a rig or on some of its controls was given (D34). Either the rig root, which means every
// control, or the named controls, which all belong to oRigRoot.
struct RigControlScope
{
	MObject oRigRoot;
	bool isWholeRig = false;
	MObjectArray oControls;   // the named controls. Empty when isWholeRig.
};

// Walks a built rig through its connections, never through names (D24):
// rig root .rigModules -> each module's .rigRoot, then each module's .controlMessage[i] -> control .rigModule.
// Every function displays its own errors.
class RigTraversal
{
public:
	// The rig root node with this name. Fails if it is missing or not a rigRootModule.
	static MStatus getRigRoot(const MString& name, MObject& oOutRigRoot);

	// The rig control node with this name. Fails if it is missing or not a rigControlNode.
	static MStatus getRigControl(const MString& name, MObject& oOutControl);

	// Reads a command's objects as a scope: exactly one rig root, or one or more controls of one rig. Fails for
	// anything else, for a rig root mixed with controls, and for controls of two rigs.
	static MStatus getControlScope(const MStringArray& names, RigControlScope& outScope);

	// Every control the scope names, or every control of the rig for a whole-rig scope, in getRigControls order.
	static MStatus getScopeControls(const RigControlScope& scope, MObjectArray& oOutControls);

	// The rig root a rig root, rig module, rig control or joint belongs to. A rig root is its own. A joint belongs to
	// the rig whose module drives its offsetParentMatrix. Fails for any other node.
	static MStatus getOwningRigRoot(const MObject& oNode, MObject& oOutRigRoot);

	// True for a node getOwningRigRoot accepts. Reports nothing, so tools can test every selected node quietly.
	static bool isRigNode(const MObject& oNode);

	// The rig root a module is connected to, through module.rigRoot <- rigRoot.rigModules, or a null object for a
	// module that has none. Reports nothing.
	static MObject findModuleRigRoot(const MObject& oModule);

	// The asset root a rig root belongs to, through rigRoot.assetRoot <- assetRoot.children (D29). Fails if there is none.
	static MStatus getAssetRoot(const MObject& oRigRoot, MObject& oOutAssetRoot);

	// Every module connected to the rig root, layout included, sorted by node name so results are stable.
	static MStatus getRigModules(const MObject& oRigRoot, MObjectArray& oOutModules);

	// A module's controls in controlMessage index order.
	static MStatus getModuleControls(const MObject& oModule, MObjectArray& oOutControls);

	// The module's template moduleType ("FkChain"), from its node type. Empty for a node the registry doesn't know.
	static std::string getModuleType(const MObject& oModule);

	// The control's markup. Fails if the control has none or it doesn't parse.
	static MStatus getControlMarkup(const MObject& oControl, ControlMarkup& outMarkup);

	// The moduleType names a filter may use.
	static std::vector<std::string> getKnownModuleTypes();

	// Every control in the rig that the filter matches, grouped by module in getRigModules order.
	// Fails on an invalid filter or a control without valid markup.
	static MStatus getRigControls(const MObject& oRigRoot, const RigControlFilter& filter, MObjectArray& oOutControls);

	// The control's rest world matrix, stored on its module at build time (controlData[i].controlRestWorld).
	static MStatus getControlRestWorld(const MObject& oControl, MMatrix& mOutRestWorld);

	// The rig module that drives a joint through its offsetParentMatrix, or a null object if none does.
	static MObject getJointModule(const MObject& oJoint);

	// A joint's rest world matrix. For a joint a rig module drives, the build pose stored on the module
	// (jointRestWorld, by the jointOPM index that drives it), whatever the rig's current pose. For any other joint,
	// its current world matrix: with no rig, the skeleton's pose is its rest.
	static MStatus getJointRestWorld(const MDagPath& jointPath, MMatrix& mOutRestWorld);

	// The control in the same rig with the same usage and the other side (D28). A Center control is its own mirror.
	// Fails if there is none.
	static MStatus getMirrorControl(const MObject& oControl, MObject& oOutMirror);

	// Like getMirrorControl, but a control with no mirror succeeds with a null object and reports nothing.
	static MStatus findMirrorControl(const MObject& oControl, MObject& oOutMirror);

	// Everything getRigControlInfo reports about a control. Fails for a control with no module or invalid markup.
	static MStatus getControlInfo(const MObject& oControl, ControlInfo& outInfo);

	// Every sourceSide control with the control of the same usage on the other side (D28), index for index, in
	// getRigControls order. Warns about a source control with no mirror and leaves it out of both arrays.
	static MStatus getMirrorControlPairs(const MObject& oRigRoot, const std::string& sourceSide, MObjectArray& oOutSources, MObjectArray& oOutMirrors);

	// The same for the controls in oCandidates only: those whose side is sourceSide are paired, the rest are ignored.
	static MStatus getMirrorControlPairs(const MObject& oRigRoot, const std::string& sourceSide, const MObjectArray& oCandidates,
		MObjectArray& oOutSources, MObjectArray& oOutMirrors);
};
