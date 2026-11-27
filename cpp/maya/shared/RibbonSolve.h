#pragma once

#include "core/RibbonMath.h"

#include <array>
#include <vector>

#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>

// The order of the channels on the first control, which is the order of RibbonModuleNode::aChannels, of RibbonSolve::channelSpecs()
// and of ModuleInverseInput::channels. Append only: the order is not a name, but the node indexes by it.
namespace RibbonChannel
{
	inline constexpr unsigned int StartTwist{ 0 };
	inline constexpr unsigned int EndTwist{ 1 };
	inline constexpr unsigned int SineAmplitude{ 2 };
	inline constexpr unsigned int SineFrequency{ 3 };
	inline constexpr unsigned int SinePhase{ 4 };
	inline constexpr unsigned int SineOrient{ 5 };
	inline constexpr unsigned int SineStartFalloff{ 6 };
	inline constexpr unsigned int SineEndFalloff{ 7 };
	inline constexpr unsigned int StartAimBlend{ 8 };
	inline constexpr unsigned int EndAimBlend{ 9 };
	inline constexpr unsigned int BendRoundness{ 10 };
	inline constexpr unsigned int VolumePreserve{ 11 };
	inline constexpr unsigned int ScaleStart{ 12 };
	inline constexpr unsigned int ScaleMid{ 13 };
	inline constexpr unsigned int ScaleEnd{ 14 };
	// The anchored form's only (D80): how round the corner at the mid anchor is. A plain ribbon has no corner and no such channel.
	inline constexpr unsigned int BendSoftness{ 15 };
	inline constexpr unsigned int Count{ 16 };
}

// What an anchored ribbon's curve takes from its start and end anchors (D80, design/limb-split.md section 3.3), stored on the node as a short.
// The template names them "full", "swing" and "twist".
namespace AnchorRotation
{
	inline constexpr short Full{ 0 };    // the anchor's whole frame, so its twist reaches the curve (a tail's first control)
	inline constexpr short Swing{ 1 };   // the start: its swing without its twist about the bone (a shoulder or hip)
	inline constexpr short Twist{ 2 };   // the end: its twist on the mid anchor's swing, so its bend never reaches the curve (a wrist or ankle)
}

// The Ribbon module's forward evaluation (D56), channels and stored rest data, as plain values and shared functions with no node of
// their own. RibbonModuleNode calls it from compute() and its inverse, in the plain and the anchored form (D80).
namespace RibbonSolve
{
	inline constexpr unsigned int MaxSlots{ RibbonMath::MaxControls + RibbonMath::MaxBindJoints };
	// An anchored ribbon passes through this many frames: start, mid and end (author, 2026-11-27: three only, for now).
	inline constexpr unsigned int AnchorCount{ 3 };

	// What one evaluation produces, by slot, in world space.
	struct Result
	{
		MMatrix mControlOPM[MaxSlots];   // rest carried by the parent's motion (the ik controls on the curve), no local
		MMatrix mWorld[MaxSlots];        // local * controlOPM
	};

	// mRest and mLocal are by slot; the start's parent motion carries the whole ribbon and the end's is its own. With pAnchorWorld (a
	// an anchored ribbon's start, mid and end frames, as world matrices) the curve passes through them in world space (RibbonMath::Setup::fkAnchors):
	// pass identity for both deltas, and nothing reads the slots past the ik controls and the bind joints.
	void evaluate(
		const RibbonMath::Setup& setup,
		const RibbonMath::Params& params,
		const MMatrix* mRest,
		const MMatrix* mLocal,
		const MMatrix& mStartDelta,
		const MMatrix& mEndDelta,
		Result& outSolve,
		const MMatrix* pAnchorWorld = nullptr);

	// A channel on a control (D56 section 5): its name, rest (the default, D34), range, whether it is an angle and whether a mirror
	// negates it. The ranges keep the evaluation finite: a fraction of the rest length, a handle scale.
	struct ChannelSpec
	{
		const char* name;
		double defaultValue;
		double min;
		double max;
		bool isAngle;
		bool negate;
	};

	// In RibbonChannel order.
	const std::array<ChannelSpec, RibbonChannel::Count>& channelSpecs();

	// Whether a ribbon has the channel at RibbonChannel index k on its channel control. A plain ribbon has all but bendSoftness. An anchored
	// one (D80) has all but the aim blends and bendRoundness, which its anchors replace. The node has every channel's attribute, and one
	// that is not on the control stays at its default.
	bool hasChannel(unsigned int k, bool anchored);

	// The anchored form's frames (D80): the three anchors' worlds as the curve takes them. The start is its whole world, or with
	// AnchorRotation::Swing its swing without its twist about its rest X (the Swing constraint, D31, with the anchor as driver and
	// parent). The mid is its world. The end is its whole world, or with AnchorRotation::Twist its twist on the mid's swing (the Twist
	// constraint with the end as driver and the mid as parent). mRest and mWorld are by anchor.
	void anchorFrames(const MMatrix* mRest, const MMatrix* mWorld, short startRotation, short endRotation, MMatrix* outFrames);

	// The anchors' part of an anchored setup: their rests are the fk chain the curve passes through, with a corner at the mid. compute()
	// sets it on the setup it reads back, and the inverse on the one it rebuilds, so both run the one rule. False when two anchors sit on
	// each other.
	bool setAnchors(RibbonMath::Setup& setup, const MMatrix* mAnchorRest);

	// Reads `count` channel plugs into `outValues`, in the order of `attrs` and `specs`, angles in radians.
	void readChannels(MDataBlock& data, const MObject* attrs, const ChannelSpec* specs, unsigned int count, double* outValues);

	// The animator's channels in internal units, in RibbonChannel order, as the evaluation's parameters.
	RibbonMath::Params paramsFromChannels(const double* values);

	// The rest data a ribbon stores on its node at build (D56 section 3.4), by the module's own attributes. The two modules use the
	// same names but for the ik controls' offsets, which the Limb kept apart from its two-bone limb's (D62).
	struct RestAttrs
	{
		MObject& ikControlCount;
		MObject& knotU;
		MObject& bindU;
		MObject& restLength;
		MObject& chainSign;
		MObject& restUp;
		MObject& restTangent;
		MObject& ikRestOffset;
		MObject& bindRestOffset;
		MObject& fkRollRest;     // double array, one per fk control: RibbonMath::Setup::fkRollRest
		MObject& fkRollRestIn;   // double array, one per fk control: RibbonMath::Setup::fkRollRestIn
		MObject& fkRollShare;    // double array, one per ik control, default 1: RibbonMath::Setup::fkRollShare (the template's twistRamp)
	};

	// Creates and adds the rest attributes to the node being initialized (MPxNode::addAttribute). The offsets of the ik controls are
	// named `ikRestOffsetName`.
	MStatus createRestAttributes(const RestAttrs& attrs, const char* ikRestOffsetName);

	// The stored rest into setup (knots, parameters, offsets, length, frame, chain sign, and the fk controls' rest roll for `fkCount`
	// of them). A saved scene keeps only the array elements that differ from their default, and an array with none has no handle, so
	// a missing element is its default, which the Setup already holds. The caller still sets the fk rest matrices and knots, which
	// are cheap and come from the controls' own rests.
	void readSetup(MDataBlock& data, const RestAttrs& attrs, int controlCount, int bindCount, int fkCount, RibbonMath::Setup& setup);

	// The stored shares of the fk roll (ModuleInverseContext::fkRollShare) into a setup the inverse rebuilt from the rests, so the
	// inverse runs the forward solve the template's twistRamp made. A short list leaves the rest at 1.
	void applyStoredShares(const std::vector<double>& shares, RibbonMath::Setup& setup);

	// Queues the same rest data from a built setup, for createModule.
	void writeRestData(const MObject& oModule, const RestAttrs& attrs, const RibbonMath::Setup& setup, MDagModifier& dagMod);
}
