#pragma once

#include "core/Constants.h"
#include "core/ControlChannels.h"
#include "core/RigJsonStructs.h"
#include "maya/shared/RigModule.h"

#include <string>
#include <unordered_map>
#include <vector>

#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MPlug.h>
#include <maya/MPxNode.h>
#include <maya/MString.h>
#include <maya/MTypeId.h>

// One driver of a constraint: a socket on some module.
struct ConstraintDriverInfo
{
	MObject oModule;                 // module that owns the socket
	unsigned int socketIndex = 0;    // logical index into outputSocketMatrix
	MMatrix mRestWorld;              // socket world matrix at rest
	std::string label;               // enum label shown on the control
};

// Everything needed to build one constraint. Filled from build results (template path) or the scene (command path).
struct ConstraintBuildArgs
{
	std::string name;
	int type = 0;                    // index into ConstraintTypes::All
	MObject oControl;                // rig control that hosts the space enum
	MObject oModule;                 // module that owns the control
	unsigned int plugIndex = 0;      // child plug (and control) index on that module
	std::vector<ConstraintDriverInfo> drivers;
	unsigned int initialDriver = 0;
	double constraintWeight = 1.0;   // 0 follows the parent, 1 is the full constraint (D63)
	MObject oRigRoot;                // may be null
};

// Space switch for one child plug. Outputs the motion the module reads as a child plug delta. For Parent that is
// the active driver's motion since rest, rest[active]^-1 * world[active]. Orient, Point, Swing and Twist take only the
// driver's rotation, translation, translation and rotation without its twist about X, or translation and its twist about X on the
// parent's swing, and the rest from the plug's parentStructure parent (D31).
// Every driver's delta is identity at rest, so every space gives the rest pose (D27). Nothing is stored per
// switch: matching changes the control's own values instead (see matchSwitchSpace).
// One node per child plug. Attribute naming follows the module nodes (aMyAttr).
class RigConstraintNode : public MPxNode
{
public:
	RigConstraintNode();
	virtual ~RigConstraintNode() override;

	MStatus compute(const MPlug& plug, MDataBlock& data) override;

	static void* creator();
	static MStatus initialize();

public:
	static MTypeId id;

	static MObject aDrivers;           // compound array
	static MObject aDriverMatrix;      //   live world matrix of the driver socket
	static MObject aDriverRest;        //   socket world matrix at rest
	static MObject aDriverLabel;       //   enum label
	static MObject aActiveDriver;      // int, driven by the control's enum
	static MObject aConstraintType;    // enum, ConstraintTypes::All
	static MObject aConstraintWeight;  // double 0..1, baked into the rig: 0 follows the parent, 1 is the full constraint (D63)
	static MObject aParentMatrix;      // live world matrix of the plug's parentStructure socket
	static MObject aParentRest;        // that socket's world matrix at rest
	static MObject aChildRest;         // rest world matrix of the constrained control
	static MObject aOutParentMatrix;   // delta for module.childPlugs[k].parentMatrix
	static MObject aRigRoot;           // message

public:
	// The delta a constraint of this type outputs, from plain matrices: the child's rest, the motion of its
	// parentStructure parent and the motion of the active driver (each rest^-1 * world, identity at rest). compute()
	// and the inverse solve's sweep (D41) both call it, so the two can't drift.
	// weight (D63) blends from the parent's own motion (0, the control just follows its parent, whatever the type) to the
	// type's full result (1): translation lerps, rotation slerps. Below 1 even a Parent constraint needs mParentDelta.
	static MMatrix computeDelta(
		int type,
		const MMatrix& mChildRest,
		const MMatrix& mParentDelta,
		const MMatrix& mDriverDelta,
		double weight = 1.0);

	// An unqualified module name resolves in rigNamespace (the constrained control's namespace), the same ids templates use.
	static MStatus resolveSocket(const std::string& socketId, const MString& rigNamespace, ConstraintDriverInfo& outDriver);

	// Two-stage build. Runs dagMod.doIt() twice, so it must run after the controls exist in the scene.
	static MStatus createConstraint(const ConstraintBuildArgs& args, MDagModifier& dagMod, MObject& oOutConstraint);

	// Template path. Resolves names through the build results of the earlier passes. A "parent:module.socket"
	// driver resolves in parentNamespace, the rig this one is attached to (D55).
	static MStatus buildFromTemplate(
		const std::vector<RigConstraintData>& constraints,
		const std::unordered_map<std::string, RigModuleBuildResult>& rigModuleMap,
		const MString& parentNamespace,
		const MObject& oRigRoot,
		MDagModifier& dagMod);

	// The space switch of a rig control: its constraint, the drivers' labels and the driver its `space` enum is on.
	// Returns false for a control that has no space switch (no `space` enum, as with a single driver). Read only.
	// pOutConstraint, when given, receives the constraint node.
	static bool getControlSpace(const MObject& oControl, ControlSpace& outSpace, MObject* pOutConstraint = nullptr);

	// Switch space without a jump: sets the control's translate, rotate and scale so it keeps its world matrix
	// under the new driver, then sets the enum. Sets no keys.
	static MStatus matchSwitchSpace(const MObject& oConstraint, unsigned int newDriver, MDagModifier& dagMod);
};
