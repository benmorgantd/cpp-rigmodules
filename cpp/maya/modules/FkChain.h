#pragma once

#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/Side.h"

#include <string>
#include <vector>

#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MObject.h>
#include <maya/MPlug.h>
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MTypeId.h>

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
	static RigModuleBuildResult createModule(
		const MString& moduleName,
		const Side& side,
		const ShapeType& shapeType,
		const MObjectArray& oJoints,
		const MMatrixArray& mJointMatrices,
		MObject& oRigRoot,
		MDagModifier& dagMod
	);
	static bool planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan);

	// Names of socket index in a chain of count controls: index_N, plus start on the first and end on the last.
	// The plan step and createModule both read this, so they cannot drift apart.
	static std::vector<std::string> socketNamesAt(unsigned int index, unsigned int count);
	static ModuleInterface makeInterface(unsigned int count);
};


// "joints": [["Left","upperArm"], ["Left","lowerArm"], ["Left","hand"]], root to tip.
struct FkModuleArgs
{
	static constexpr const char* JointsKey{ "joints" };   // Must match the field name the macro below maps.
	std::vector<JointSelector> joints;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FkModuleArgs, joints)