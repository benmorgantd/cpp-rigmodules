#pragma once

#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/Side.h"
#include "maya/shared/TwoBoneIkSolve.h"

#include <array>
#include <string>
#include <vector>

#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MString.h>
#include <maya/MTypeId.h>
#include <maya/MVector.h>

// COUPLED TO ReverseFoot (D74, design/reverse-foot.md): a foot hangs from the ik and end sockets, feeds ikTarget and follows ikFkBlend.
// A change to the blend, those sockets, ikTarget or the inverse changes the foot.
//
// A two-bone IK limb (arm, leg): an FK chain and an IK solve, blended joint by joint (D52). Child plugs: start, ik, pole,
// settings (internal: it follows start's parent) and the optional ikTarget, which the end joint reaches in place of the IK handle (a
// foot's ankle, D74). Sockets: index_0, index_1, index_2 (the blended joint worlds), start, end, ik, pole, settings.
class TwoBoneIkNode : public RigModuleWithJointsNode<TwoBoneIkNode>
{
public:
	TwoBoneIkNode();
	virtual ~TwoBoneIkNode() override;

	static void* creator();
	static MStatus initialize();
	virtual MStatus compute(const MPlug& plug, MDataBlock& data) override;
	// The moduleArgs version table. Step 0 takes version 1 (ik, pole and settings joints) to 2 (derived controls, D57).
	static const UpdateTable& argUpdateTable();

public:
	static MTypeId id;

	// The channels, read from the owning controls (RigAttr). Blend, stretch, soft and pin are clamped to 0..1.
	static MObject aIkFkBlend;
	static MObject aStretch;
	static MObject aSoft;
	static MObject aPin;
	static MObject aRoll;        // an angle
	static MObject aPoleSpace;   // PoleSpaceLabels: 0 Parent, 1 Ik

	// Stored at build (section 3.4).
	static MObject aIkRestOffset;      // matrix array, one per limb joint
	static MObject aRestBendNormal;
	static MObject aRestPlaneAngle;
	static MObject aLimbLengths;

	// 1 while the controls they drive are visible: FK lower and end below blend 1, the IK handle and pole above blend 0.
	static MObject aFkVisible;
	static MObject aIkVisible;

	// Match outputs (D52 section 4.1), pulled only by setRigIkFkBlend, never during playback: the control locals that
	// reproduce the current pose on each side, by slot (fkMatchLocal 0 to 2, ikMatchLocal 3 and 4), and what the match
	// can't do, a JSON list of messages (JsonUtils). Not writable, not storable, connected to nothing.
	static MObject aFkMatchLocal;
	static MObject aIkMatchLocal;
	static MObject aMatchWarnings;

	// The ikTarget child plug (D62 section 8, D80 phase 2) and the handle's own socket array: see TwoBoneIkPart.
	static MObject aIkTargetMatrix;
	static MObject aIkTargetRestMatrix;
	static MObject aIkTargetConnected;
	static MObject aHandleSocketMatrix;

public:
	static RigModuleBuildResult createModule(
		const MString& moduleName,
		const Side& side,
		const ShapeType& shapeType,
		const std::array<TwoBoneIkSlotInput, TwoBoneIkSlot::Count>& slots,
		const TwoBoneIkRest& rest,
		double ikFkBlend,
		MObject& oRigRoot,
		MDagModifier& dagMod
	);
	static bool planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan);
	// Control values from the limb joints (D52 section 6): the FK controls from the joint worlds, the IK handle and pole
	// from the observed positions through TwoBoneIkSolve::computeTwoBoneIkInverse, for the channels in inverseChannels().
	// The sockets are the forward evaluation of those controls. Not solved when a limb joint is missing.
	static ModuleInverseResult inverse(const ModuleInverseContext& context, const ModuleInverseInput& input);
	// The scalar channels the inverse reads, in TwoBoneIkInverseChannel order.
	static std::vector<ModuleInverseChannel> inverseChannels();
	// The pole's space deltas (ModuleSpaceDeltaFn): Parent is the pole child plug's parent motion, Ik is the IK handle's
	// motion since rest. Slot Pole only.
	static MStatus spaceDelta(const MObject& oModule, unsigned int slot, unsigned int label, MMatrix& outDelta);
};
