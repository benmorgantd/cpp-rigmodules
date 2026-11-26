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

// The ReverseFoot (D74, design/reverse-foot.md): foot roll, pivots and bank as channels on one control, an FK toe, on top of a Limb.
//
// COUPLED TO Limb (D62, design/limb.md section 8): the foot hangs from a Limb's ik and end sockets, feeds its ikTarget and follows its
// ikFkBlend. A change to the Limb's blend, sockets, ikTarget or inverse changes the foot, and a change here changes where the Limb's end joint
// reaches.
//
// Two stages, in separate attribute groups (D62 phase 4): Maya orders the graph by attribute, and a socket and a plug on one attribute is a loop
// in the legacy DG.
//  Stage 1 reads the ik plug (childPlugs[0], the Limb's ik socket), the channels and the pivots, and writes the ankle socket, the IK ankle
//  target the Limb's ikTarget takes. It never reads the ankle plug.
//  Stage 2 reads the ankle plug (three inputs of its own, the Limb's end socket: the ankle as it really is), the ik plug, the channels, the
//  pivots, the toe FK control and the blend, and writes the toe and toe tip joints, their sockets, the pivot sockets and the foot control's and
//  toe FK control's offsets. The ankle socket is an output array of its own (ankleSocketMatrix), so it does not see the ankle plug.
// The module drives the toe and toe tip. The ankle joint is the Limb's: the foot only reads it.
class ReverseFootModuleNode : public RigModuleWithJointsNode<ReverseFootModuleNode>
{
public:
	ReverseFootModuleNode();
	virtual ~ReverseFootModuleNode() override;

	static void* creator();
	static MStatus initialize();
	virtual MStatus compute(const MPlug& plug, MDataBlock& data) override;

public:
	static MTypeId id;

	// The animator's channels, in the order of the module's channel table, each an angle on the foot control and an input of the module.
	static std::array<MObject, 11> aChannels;
	static MObject aPivotRoles;         // array of int: a FootSolve::Role per pivot, in chain order
	static MObject aPivotPositions;     // array of matrix: the translation row is the pivot's rest position
	static MObject aAnkleRestWorld;     // the ankle joint's rest world, which the ankle socket carries
	static MObject aAnkleSocketMatrix;  // output array, element 0 is the ankle socket
	static MObject aAnklePlugMatrix;    // the ankle child plug: the ankle as it is
	static MObject aAnklePlugRestMatrix;
	static MObject aAnklePlugConnected;
	static MObject aIkFkDriver;         // the module id the blend follows
	static MObject aIkFkBlend;          // 0 is FK and 1 is IK. Connected from the driver's ikFkBlend in pass 2, never keyed
	static MObject aFkVisible;          // 1 below blend 1: the toe FK control is visible
	static MObject aIkVisible;          // 1 above blend 0: the foot control is visible

public:
	// The foot's input for one build, from the plan step.
	struct Plan
	{
		std::vector<FootSolve::Pivot> chain;   // in template order, root first
		MMatrix mAnkleRestWorld;
		// The toe and toe tip joints the foot drives, and their rests.
		MObject oToeJoints[2];
		MMatrix mToeJointRests[2];
		MMatrix mFootControlRest;              // world aligned, at the ball pivot
		std::string controlUsage;              // also the control's base name
		ControlMarkup controlMarkup;
		std::string toeFkUsage;                // the toe's usage and FK, also the toe FK control's base name
		ControlMarkup toeFkMarkup;
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
	// The foot from the joints (design/reverse-foot.md section 10). From the ankle (ModuleInverseInput::ikTargetJointWorld, the Limb's end) it
	// solves footRoll and bank in the author's gauge (FootSolve::solveAnkleChannels: beyond what the sidecar's channels turn), and gives the
	// Limb the foot's motion F through ModuleInverseResult::ikTargetMotion; from the toe it solves toeTap and the toe FK control, exactly.
	// Every other channel comes back as given. RigInverse runs it twice per frame: in the pre-pass, before the Limb, for F, and in the sweep.
	static ModuleInverseResult inverse(const ModuleInverseContext& context, const ModuleInverseInput& input);
	// The 11 channels on the foot control, in kChannels order, for ModuleDescriptor::inverseChannels.
	static std::vector<ModuleInverseChannel> inverseChannels();
	// ModuleInverseContext::moduleRest: the ankle's rest, the ankle plug's rest, the toe's and toe tip's, then the six pivots in
	// FootSolve::chainOrder. Fails when the stored pivots are not the whole chain in that order.
	static MStatus readInverseRest(const MObject& oModule, std::vector<MMatrix>& outRest);

	// The interface for a chain of pivots: the ik and ankle plugs, and the sockets ankle, toe, toeTip and one per pivot. Names, not rests.
	static ModuleInterface makeInterface(const std::vector<FootSolve::Pivot>& chain);

	// The socket a pivot's world is on: its role, and for the roles toe and toeTip (which name the bind joints' sockets) the role and Pivot.
	static std::string pivotSocketName(FootSolve::Role role);

private:
	MStatus computeControl(const MPlug& plug, MDataBlock& data);
	MStatus computeAnkle(const MPlug& plug, MDataBlock& data);
	MStatus computeToe(const MPlug& plug, MDataBlock& data);
	// The pivots from their stored arrays. False when the roles and positions do not pair up or there are none (not built yet).
	static bool readPivots(MDataBlock& data, std::array<FootSolve::Pivot, ReverseFootSlot::PivotCap>& outPivots, std::size_t& outCount);
	static FootSolve::Channels readChannels(MDataBlock& data);
};

// The module's arguments (design/reverse-foot.md section 5). Field names are the template keys the macro maps.
struct ReverseFootArgs
{
	std::vector<JointSelector> joints;   // the ankle, the toe and the toe tip. The pivots are the six markers named by role on the foot's side
	std::string ikFkDriver;
	std::string controlName{ "foot" };
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ReverseFootArgs, joints, ikFkDriver, controlName)
