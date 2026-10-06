#pragma once

#include "RigModule.h"

class AimModuleNode : public RigModuleNodeBase
{
public:
	AimModuleNode();
	virtual ~AimModuleNode() override;

	MStatus evaluateModuleSolver(const MPlug& plug, MDataBlock& data) override;

	static void* creator();
	static MStatus initialize();

public:
	static MTypeId id;

	// Top-Level Input Attributes
	static MObject aInputRestMatrix;

	// Control Array Attributes
	static MObject aControlMatrix;
	static MObject aOutputControlOPM;
	static MObject aOutputJointOPM;

	static const MString aCommandString;
public:
	static MObject createModule(
		const MString& moduleName,
		const MDagPath& aimJoint,
		const MDagPath& targetJoint,
		const MDagPath& upJoint,
		MObject oParentModule,
		unsigned int parentModuleSocketIndex,
		MObject oRigRoot,
		MDGModifier& dgMod,
		MDagModifier& dagMod,
		MStatus* status = nullptr
	);
	static MObject createModule(
		const RigModuleData& moduleData,
		MObject rigRoot,
		MObject parentModule,
		MDGModifier& dgMod,
		MDagModifier& dagMod
	);
};