#pragma once

#include "RigModule.h"
#include "Side.h"
#include "RigControlNode.h"

#include <vector>
#include <string>

#include <maya/MString.h>
#include <maya/MObject.h>
#include <maya/MDagModifier.h>
#include <maya/MTypeId.h>
#include <maya/MDagPathArray.h>
#include <maya/MPlug.h>
#include <maya/MDataBlock.h>
#include <maya/MStatus.h>

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
	static RigModuleBuildResult buildFromJson(const nlohmann::json& args, BuildContext& ctx);
};


struct FkModuleArgs
{
	std::vector<std::string> joints;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FkModuleArgs, joints)