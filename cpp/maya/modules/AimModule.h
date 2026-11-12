#pragma once

#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/Side.h"

#include <array>
#include <string>

#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MString.h>
#include <maya/MTypeId.h>

// Index shared by controlData, controlOPM, jointOPM, outputSocketMatrix, childPlugs and socketNames.
// Aim (0) has a joint, a socket and a child plug but no control. Its controlData element only stores the joint's rest.
namespace AimPart
{
	inline constexpr unsigned int Aim{ 0 };
	inline constexpr unsigned int Target{ 1 };
	inline constexpr unsigned int Up{ 2 };
	inline constexpr unsigned int Count{ 3 };
}

struct AimPartInput
{
	MObject oJoint;
	MMatrix mRestWorld;
	ControlMarkup controlMarkup;   // The joint's (side, usage). Unused for the aim part, which has no control.
};

// Field names are the JSON keys: "aim": ["Left","eye"], "target": [...], "up": [...]
struct AimModuleArgs
{
	JointSelector aim;
	JointSelector target;
	JointSelector up;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AimModuleArgs, aim, target, up)

// Points the aim joint's +X at the target control, with +Z toward the up control.
// Child plugs ("aim", "target", "up") take their parent sockets independently.
// Parent sockets ("aim", "target", "up") expose the solved aim joint and the two controls' world matrices.
class AimModuleNode : public RigModuleWithJointsNode<AimModuleNode>
{
public:
	AimModuleNode();
	virtual ~AimModuleNode() override;

	static void* creator();
	static MStatus initialize();
	virtual MStatus compute(const MPlug& plug, MDataBlock& data) override;

public:
	static MTypeId id;

	// World-space offset that keeps the aim joint's original rest orientation: jointRest * aimSolvedRest^-1.
	static MObject aAimRestOffset;

public:
	static RigModuleBuildResult createModule(
		const MString& moduleName,
		const Side& side,
		const ShapeType& shapeType,
		const std::array<AimPartInput, AimPart::Count>& parts,
		MObject& oRigRoot,
		MDagModifier& dagMod
	);
	static bool planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan);
	// Derives the target and up controls' transforms from their joints. The aim joint isn't inverted (D41, inverse-solve.md
	// section 8). Skeleton: not built.
	static ModuleInverseResult inverse(const ModuleInverseContext& context, const ModuleInverseInput& input);

private:
	struct AimSolveResult
	{
		MMatrix mControlOPM[AimPart::Count];   // Control rest carried by parent motion (no local)
		MMatrix mPartWorld[AimPart::Count];    // Control world. Aim slot holds the solved joint world.
	};

	static MStatus solveAim(MDataBlock& data, AimSolveResult& outResult);
};