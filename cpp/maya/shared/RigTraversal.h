#pragma once

#include "core/ControlMarkup.h"
#include "core/RigControlFilter.h"

#include <string>
#include <vector>

#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MObjectArray.h>
#include <maya/MStatus.h>
#include <maya/MString.h>

// Walks a built rig through its connections, never through names (D24):
// rig root .rigModules -> each module's .rigRoot, then each module's .controlMessage[i] -> control .rigModule.
// Every function displays its own errors.
class RigTraversal
{
public:
	// The rig root node with this name. Fails if it is missing or not a rigRootModule.
	static MStatus getRigRoot(const MString& name, MObject& oOutRigRoot);

	// The rig root a rig root, rig module or rig control belongs to. A rig root is its own. Fails for any other node.
	static MStatus getOwningRigRoot(const MObject& oNode, MObject& oOutRigRoot);

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

	// The control in the same rig with the same usage and the other side (D28). A Center control is its own mirror.
	// Fails if there is none.
	static MStatus getMirrorControl(const MObject& oControl, MObject& oOutMirror);
};
