#pragma once

#include "core/Constants.h"
#include "core/PushOutSolve.h"

#include <string>

#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MPlug.h>
#include <maya/MPxNode.h>
#include <maya/MTypeId.h>

// Everything needed to build one push out.
struct PushOutBuildArgs
{
	std::string name;
	MObject oModule;                 // module that owns the target child plug
	unsigned int plugIndex = 0;      // child plug (and control) index on that module
	MObject oControl;                // the pushed control, for its rest world
	MObject oFollowJoint;            // the joint the capsule follows
	MMatrix mOffsetLocal;            // the capsule's matrix relative to that joint
	PushOutSolve::Capsule capsule;
	PushOutSolve::Settings settings;
	MObject oRigRoot;                // may be null
};

// A push out on one child plug (D79, design/push-out.md). Sits on the plug's parentMatrix feed like a constraint, after the
// constraint when the plug has one: it reads the plug's delta (parentMatrix against parentRest, or a constraint's output
// with an identity rest), puts the control's origin at its would-be place against one capsule and outputs a new delta,
// moved out of the capsule by PushOutSolve::solve. Translation only, stateless: the same inputs give the same result on any
// frame. Not invertible, so RigInverse runs it forward.
class RigPushOutNode : public MPxNode
{
public:
	RigPushOutNode();
	virtual ~RigPushOutNode() override;

	MStatus compute(const MPlug& plug, MDataBlock& data) override;

	static void* creator();
	static MStatus initialize();

	// The pure part of compute, so the inverse runs the same thing. mCapsuleWorld is the capsule's offset times its joint's
	// world, mChildRest the control's rest world and mParentDelta what feeds the plug. Returns the delta after the push.
	static MMatrix computeDelta(
		const PushOutSolve::Capsule& capsule,
		const MMatrix& mCapsuleWorld,
		const PushOutSolve::Settings& settings,
		const MMatrix& mChildRest,
		const MMatrix& mParentDelta);

	// Makes the node, wires it onto the target plug after whatever feeds it, and connects it to the rig root. The node is
	// named args.name. Needs the module, its control and the joint.
	static MStatus createPushOut(const PushOutBuildArgs& args, MDagModifier& dagMod, MObject& oOutPushOut);

public:
	static MTypeId id;

	static MObject aCapsuleMatrix;     // live world matrix of the followed joint
	static MObject aCapsuleOffset;     // the capsule's matrix relative to the joint, baked
	static MObject aRadius;
	static MObject aHalfLength;
	static MObject aMargin;
	static MObject aMaxPush;
	static MObject aWeight;            // 0..1: how much of the push is applied
	static MObject aParentMatrix;      // live world matrix of the plug's source, or the constraint's delta
	static MObject aParentRest;        // its rest (identity for a constraint's delta)
	static MObject aChildRest;         // rest world matrix of the pushed control
	static MObject aOutParentMatrix;   // delta for module.childPlugs[k].parentMatrix
	static MObject aRigRoot;           // message
};
