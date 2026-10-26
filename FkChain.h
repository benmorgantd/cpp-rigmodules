#pragma once

#include "RigModule.h"
#include "Side.h"
#include "RigControlNode.h"

class FkChainNode : public RigModuleWithJointsNode<FkChainNode>
{
public:
	FkChainNode();
	virtual ~FkChainNode() override;

	static void* creator();
	static MStatus initialize();
	virtual MStatus compute(const MPlug& plug, MDataBlock& data) override;

public:
	static MTypeId id;

	// Compound attribute for child plug relationships to their parent.
	static MObject aChildPlugs;
	static MObject aChildPlugName;
	static MObject aChildPlugParentMatrix;

	// Command String
	static const MString aCommandString;
public:
	static MStatus reparentChildPlugs(       // Function used when parenting child transforms to other module parents
		RigModuleDagReparentArgs
	);

public:
	static RigModuleBuildResult createModule(
		const MString& moduleName,
		const Side& side,
		const ShapeType& shapeType,
		const MDagPathArray& jointDags,
		MObject oRigRoot,
		MDagModifier& dagMod
	);
	static RigModuleBuildResult createModule(
		const RigModuleData& moduleData,
		MObject rigRoot,
		MDagModifier& dagMod
	);
};