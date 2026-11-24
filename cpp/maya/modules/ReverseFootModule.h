#pragma once

#include "core/FootSolve.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/Side.h"

#include <array>
#include <string>
#include <vector>

#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MObject.h>
#include <maya/MPlug.h>
#include <maya/MStatus.h>
#include <maya/MString.h>
#include <maya/MTypeId.h>

// The ReverseFoot (D74, design/reverse-foot.md): foot roll, pivots and bank as channels on one control, on top of a Limb.
//
// COUPLED TO Limb (D62, design/limb.md section 8): the foot hangs from a Limb's ik and end sockets and feeds its ikTarget. A change to
// the Limb's blend, sockets, ikTarget or inverse changes the foot, and a change here changes where the Limb's end joint reaches.
//
// Stage 1 (built): the foot control and its channels, the ik child plug, and the ankle socket, the IK ankle target the Limb's ikTarget
// takes. Stage 2 (the toe and toe tip joints, the toe FK control, the blend, the visibility output) is not built yet, so the module
// claims the toe and toe tip joints and does not drive them.
//
// Two stages, two attribute groups (D62 phase 4): Maya orders the graph by attribute, and a socket and a plug on one attribute is a loop
// in the legacy DG. The ik plug is childPlugs[0], like every plug. The ankle plug is three inputs of its own and the ankle socket an
// output array of its own, the way a Limb's ikTarget and handle sockets are, so the ankle socket does not see the ankle plug.
class ReverseFootModuleNode : public RigModuleWithControlsNode<ReverseFootModuleNode>
{
public:
	ReverseFootModuleNode();
	virtual ~ReverseFootModuleNode() override;

	static void* creator();
	static MStatus initialize();
	virtual MStatus compute(const MPlug& plug, MDataBlock& data) override;

public:
	static MTypeId id;

	// The animator's channels, in FootChannel order, each an angle on the foot control and an input of the module.
	static std::array<MObject, 11> aChannels;
	static MObject aPivotRoles;         // array of int: a FootSolve::Role per pivot, in chain order
	static MObject aPivotPositions;     // array of matrix: the translation row is the pivot's rest position
	static MObject aAnkleRestWorld;     // the ankle joint's rest world, which the ankle socket carries
	static MObject aAnkleSocketMatrix;  // output array, element 0 is the ankle socket
	static MObject aAnklePlugMatrix;    // the ankle child plug: the ankle as it is (phase 4 reads it)
	static MObject aAnklePlugRestMatrix;
	static MObject aAnklePlugConnected;
	static MObject aIkFkDriver;         // the module id the blend follows (phase 4 connects it)

public:
	// The foot's input for one build, from the plan step.
	struct Plan
	{
		std::vector<FootSolve::Pivot> chain;   // in template order, root first
		MMatrix mAnkleRestWorld;
		// The toe and toe tip joints the foot claims. Stage 1 does not drive them: createModule only freezes each at its rest, so it keeps
		// its place with nothing inherited, and stage 2 wires them to the module's joint outputs.
		MObject oToeJoints[2];
		MMatrix mToeJointRests[2];
		MMatrix mFootControlRest;              // world aligned, at the ball pivot
		std::string controlUsage;              // also the control's base name
		ControlMarkup controlMarkup;
		std::string ikFkDriver;
	};

	static RigModuleBuildResult createModule(
		const MString& moduleName,
		const Side& side,
		const ShapeType& shapeType,
		const Plan& plan,
		MObject& oRigRoot,
		MDagModifier& dagMod);
	static bool planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan);
	// The foot's control is derived and locked, so the joints give it nothing: it takes the sidecar's channels and the rest. The match with
	// a raised foot is phase 5 (design/reverse-foot.md section 10).
	static ModuleInverseResult inverse(const ModuleInverseContext& context, const ModuleInverseInput& input);

	static ModuleInterface makeInterface();

private:
	MStatus computeStage1(const MPlug& plug, MDataBlock& data);
};

// The module's arguments (design/reverse-foot.md section 5). Field names are the template keys the macro maps.
struct ReverseFootArgs
{
	std::vector<JointSelector> joints;             // the ankle, the toe and the toe tip
	std::vector<JointSelector> reverseHierarchy;   // marker selectors in chain order, root first. The usage names the role
	std::string ikFkDriver;
	std::string controlName{ "foot" };
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ReverseFootArgs, joints, reverseHierarchy, ikFkDriver, controlName)
