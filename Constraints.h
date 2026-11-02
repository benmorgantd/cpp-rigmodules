#pragma once

#include "RigModule.h"
#include "RigJsonStructs.h"
#include "Constants.h"

#include <string>
#include <vector>
#include <unordered_map>

#include <maya/MPxNode.h>
#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>
#include <maya/MArgList.h>
#include <maya/MObject.h>
#include <maya/MMatrix.h>
#include <maya/MPlug.h>
#include <maya/MTypeId.h>
#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>

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
	MObject oControl;                // rig control that hosts the space enum
	MObject oModule;                 // module that owns the control
	unsigned int plugIndex = 0;      // child plug (and control) index on that module
	std::vector<ConstraintDriverInfo> drivers;
	unsigned int initialDriver = 0;
	MObject oRigRoot;                // may be null
};

// Space switch for one child plug. Outputs the parent motion the module reads as a child plug delta:
//   out = offset[active] * (rest[active]^-1 * world[active])
// The offset is captured when the space changes so the control never jumps (see matchSwitchSpace).
// One node per child plug. Attribute naming follows the module nodes (aMyAttr).
class ParentConstraintNode : public MPxNode
{
public:
	ParentConstraintNode();
	virtual ~ParentConstraintNode() override;

	MStatus compute(const MPlug& plug, MDataBlock& data) override;

	static void* creator();
	static MStatus initialize();

public:
	static MTypeId id;

	static MObject aDrivers;           // compound array
	static MObject aDriverMatrix;      //   live world matrix of the driver socket
	static MObject aDriverRest;        //   socket world matrix at rest
	static MObject aDriverOffset;      //   switch compensation, written by matchSwitchSpace
	static MObject aDriverLabel;       //   enum label
	static MObject aActiveDriver;      // int, driven by the control's enum
	static MObject aOutParentMatrix;   // delta for module.childPlugs[k].parentMatrix
	static MObject aRigRoot;           // message

public:
	static MStatus resolveSocket(const std::string& socketId, ConstraintDriverInfo& outDriver);

	// Two-stage build. Runs dagMod.doIt() twice, so it must run after the controls exist in the scene.
	static MStatus createConstraint(const ConstraintBuildArgs& args, MDagModifier& dagMod, MObject& oOutConstraint);

	// Template path. Resolves names through the build results of the earlier passes.
	static MStatus buildFromTemplate(
		const std::vector<RigConstraintData>& constraints,
		const std::unordered_map<std::string, RigModuleBuildResult>& rigModuleMap,
		const MObject& oRigRoot,
		MDagModifier& dagMod);

	// Switch space without a jump: captures the offset for the new driver, then sets the enum.
	static MStatus matchSwitchSpace(const MObject& oConstraint, unsigned int newDriver, MDagModifier& dagMod);
};

// createRigParentConstraint -control <rigControl> -driver "module.socket" [-driver ...] [-label ...] [-initial n] [-name n]
class CreateParentConstraintCmd : public MPxCommand
{
public:
	CreateParentConstraintCmd();
	virtual ~CreateParentConstraintCmd() override;

	MStatus doIt(const MArgList& args) override;
	MStatus redoIt() override;
	MStatus undoIt() override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;

private:
	MDagModifier fDagMod;
};

// setRigConstraintSpace <constraintNode> (-space n | -label name)
class SetConstraintSpaceCmd : public MPxCommand
{
public:
	SetConstraintSpaceCmd();
	virtual ~SetConstraintSpaceCmd() override;

	MStatus doIt(const MArgList& args) override;
	MStatus redoIt() override;
	MStatus undoIt() override;
	bool isUndoable() const override;

	static void* creator();
	static MSyntax newSyntax();
	static const char* aCommandString;

private:
	MDagModifier fDagMod;
};