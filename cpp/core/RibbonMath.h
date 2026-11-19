#pragma once

// The Ribbon module's math (D56, design/ribbon.md), with no Maya types so it can be checked against closed-form cases.
//
// COUPLED TO Limb (D62, design/limb.md): LimbNode runs this math for its one continuous ribbon. A change here changes Limb.
// Read design/limb.md and run test_limb_matches_parts before changing it.
//
// A virtual ribbon: a piecewise cubic Hermite spline through the control matrices, a frame at any parameter u by parallel
// transport, twist, a sine layer and a scale profile. Everything is a pure function of its inputs:
// no state survives an evaluation, so scrubbing, baking and parallel evaluation cannot change a result.
//
// Matrices are row-major with row vectors (Maya's convention): rows 0 to 2 are the X, Y and Z axes, row 3 the origin.
// Everything is in one space, the "ribbon space" the caller chooses (the rig's rest world, carried by the parent's motion).

#include "VectorMath.h"

namespace RibbonMath
{
	// The caps that keep evaluation allocation-free. The plan step rejects a template over them.
	inline constexpr int MaxControls{ 8 };
	inline constexpr int MaxBindJoints{ 64 };
	// Frames are carried along the curve at this many steps, and read back by interpolation.
	inline constexpr int FrameSamples{ 32 };

	using VectorMath::Mat4;
	using VectorMath::Vec3;

	Mat4 multiply(const Mat4& a, const Mat4& b);
	// The inverse of a rigid matrix (orthonormal axes), which every frame the ribbon builds at rest is.
	Mat4 inverseRigid(const Mat4& a);

	// The animator's channels, in internal units (angles in radians). The defaults are the rest: the ribbon at its rest
	// controls and these values reproduces the rest pose (D34).
	struct Params
	{
		double startTwist{ 0.0 };           // secondary twist angle of the start and end controls: picks the branch (4.1). The ends'
		                                    // total twist ramps linearly along the whole ribbon, and every control adds its own on top
		double endTwist{ 0.0 };
		double sineAmplitude{ 0.0 };        // a fraction of the rest length
		double sineFrequency{ 1.0 };        // cycles along the whole ribbon
		double sinePhase{ 0.0 };            // cycles
		double sineOrient{ 0.0 };           // angle around the tangent: 0 is the frame's Y, a quarter turn its Z
		double sineStartFalloff{ 0.2 };     // the fraction of the ribbon over which the sine fades in and out
		double sineEndFalloff{ 0.2 };
		double startAimBlend{ 0.0 };        // 0: the end's own X axis sets its tangent, 1: it aims at the other end
		double endAimBlend{ 0.0 };
		double bendRoundness{ 1.0 };        // Hermite handle scale at the ends: 0 is straight, about 0.65 to 1 is rounded
		double volumePreserve{ 0.0 };       // 0 to 1
		double scaleStart{ 1.0 };           // the volume profile: weights at the start, the middle and the end
		double scaleMid{ 1.0 };
		double scaleEnd{ 1.0 };
	};

	// What the rest pose fixes. buildSetup fills it, the module stores it on the node, and every evaluation reads it.
	struct Setup
	{
		int controlCount{ 0 };
		int bindCount{ 0 };
		double knotU[MaxControls]{};        // each control's parameter: its share of the rest chord length
		double bindU[MaxBindJoints]{};      // each bind joint's parameter: its rest position projected on the control polyline
		double restLength{ 0.0 };           // the sum of the rest chords between neighbouring controls
		Vec3 restUp;                        // the start's rest Y axis, made perpendicular to the rest tangent
		Vec3 restTangent;                   // the start's rest X axis, flipped by chainSign so it runs down the chain
		// +1 when the controls' X axes run down the chain, -1 when they point back up it, as in a mirrored limb whose joints keep
		// the other side's axes (D57). Every use of a control's X axis as a direction goes through it, and a twist about that
		// axis is turned about the chain's forward tangent with its sign flipped.
		double chainSign{ 1.0 };
		Mat4 ikOffset[MaxControls];         // an interior control's rest in the base curve's frame at its parameter (every control's,
		                                    // the ends too, when there is an fk chain)
		// The fk chain (D59), when fkCount is above 0: a real chain of controls the base curve passes through, which every ik control
		// then floats on. With no fk chain the base curve is built from the start and end alone.
		int fkCount{ 0 };
		double fkKnotU[MaxControls]{};      // each fk control's parameter: its share of the fk rest chord length (fkKnots)
		Mat4 fkRest[MaxControls];           // each fk control's rest world matrix
		// The fk chain is a Limb's anchors (D62): its start, mid and end joint frames, given to every evaluation as world matrices
		// (Input::fkWorld) and not composed from controls. The curve passes through them, the ik controls float on it as in D59, and
		// their roll about the curve also twists the bind joints, easing from one anchor's to the next (an fk chain's roll only turns
		// the ik controls).
		bool fkAnchors{ false };
		double fkRollRest[MaxControls]{};   // each fk control's rest roll about the base curve's tangent (what the ik controls' roll is measured from)
		Mat4 bindOffset[MaxBindJoints];     // a bind joint's rest in the detail curve's frame at its parameter
	};

	// What one evaluation reads: each control's local matrix (its channels), and where the start and end sit with no
	// local on (their rest carried by their parents' motion). The interior controls have no OPM input: they ride the curve.
	struct Input
	{
		Mat4 local[MaxControls];
		Mat4 fkLocal[MaxControls];          // the fk controls' local matrices (the chain is composed here, never read from their worlds)
		// The fk chain given as world matrices when setup.fkAnchors is set, instead of composed from fkLocal (D62).
		Mat4 fkWorld[MaxControls];
		Mat4 startOPM;
		Mat4 endOPM;
		// The end plug's motion relative to the start plug's, as a motion of the end control's own channels: the world motion
		// (endOPM = endRest * motion) conjugated by the end's rest rotation, so X is the end's bone axis. Its twist about X joins
		// the end's total twist, so a parent that twists the end (an elbow, a chest) twists the ribbon
		// (design/ribbon-twist-follow.md). The identity at rest.
		Mat4 endDelta;
	};

	struct Output
	{
		Mat4 controlOPM[MaxControls];   // every control's offset parent matrix (no local): start and end as given, the interior on the base curve
		Mat4 controlWorld[MaxControls]; // local * controlOPM
		Mat4 bindOPM[MaxBindJoints];    // every bind joint's frame (the tweaker's offset parent matrix), scale and offset in
		Mat4 fkOPM[MaxControls];        // every fk control's offset parent matrix (no local): the first at its rest, the others on the one before
		Mat4 fkWorld[MaxControls];      // local * fkOPM
	};

	// Each fk control's parameter, its share of the fk rest chord length, for `fkCount` controls. False when two neighbours sit on each other.
	bool fkKnots(const Mat4* fkRest, int fkCount, double* outU);

	// Fills setup.fkRollRest from the fk rest matrices, once setup.fkKnotU, restTangent, restUp and chainSign are set.
	void fkRollRests(Setup& setup);

	// Fills the Setup from the controls' and the bind joints' rest world matrices, and the fk controls' when `fkCount` is above 0.
	// False, with a reason, when the rest is unusable: a control, fk control or bind count out of range, controls on top of each
	// other, a degenerate chord.
	bool buildSetup(const Mat4* controlRest, int controlCount, const Mat4* bindRest, int bindCount, const Mat4* fkRest, int fkCount, Setup& outSetup, const char*& outReason);

	// The Limb's setup (D62): the fk chain is `anchorCount` anchors (2 to 8) whose rest frames the curve passes through. The ik controls
	// are not given but placed on that curve at `controlU` (their parameters, in the anchors' chord-share space: strictly increasing,
	// the first 0 and the last 1, so a control at an anchor's parameter sits on the anchor exactly). Their rest frames come back in
	// outControlRest (controlCount matrices): the position on the curve, with the first anchor's orientation carried along it, so the
	// offsets from the curve are zero and rolling the anchors swings no control off it. A mirrored chain (X running back) keeps that.
	// False, with a reason, when the rest is unusable.
	bool buildAnchoredSetup(const Mat4* anchorRest, int anchorCount, const double* controlU, int controlCount, const Mat4* bindRest, int bindCount, Setup& outSetup, Mat4* outControlRest, const char*& outReason);

	// The whole evaluation.
	void evaluate(const Setup& setup, const Params& params, const Input& input, Output& output);

	// The twist of a local matrix about its own X axis (the swing-twist split), in (-pi, pi]. Scale is ignored.
	double extractTwist(const Mat4& local);
	// angle wrapped to [-pi, pi).
	double wrapAngle(double angle);
	// The total twist of an end control (4.1): the secondary picks which turn of the extracted angle the result lands on.
	// With the secondary at 0 it is the plain wrapped angle.
	double pickTwistBranch(double extracted, double secondary);
}
