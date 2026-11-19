#include "RibbonMath.h"
#include "SwingTwist.h"

#include <algorithm>
#include <cmath>

namespace RibbonMath
{
	namespace
	{
		constexpr double kPi{ 3.14159265358979323846 };
		// A chord this short has no direction: the rest frame stands in for it (Q-e).
		constexpr double kMinChord{ 1.0e-4 };
		// The volume exponent clamps the length ratio so an extreme pull can't zero or explode the scale (Q-e).
		constexpr double kMinLengthRatio{ 0.25 };
		constexpr double kMaxLengthRatio{ 4.0 };
		// The step of the central difference that turns the displaced curve into a tangent.
		constexpr double kTangentStep{ 1.0e-3 };
		// How much of an interior control's own X axis sets the tangent of the curve through it (the rest is the
		// finite difference of its neighbours).
		constexpr double kControlTangentBlend{ 0.5 };
		// Two control parameters closer than this are the same place on the curve.
		constexpr double kKnotTolerance{ 1.0e-6 };

		using VectorMath::add;
		using VectorMath::anyPerpendicular;
		using VectorMath::cross;
		using VectorMath::dot;
		using VectorMath::length;
		using VectorMath::lerp;
		using VectorMath::makeFrame;
		using VectorMath::normalized;
		using VectorMath::origin;
		using VectorMath::rotateByArc;
		using VectorMath::row;
		using VectorMath::scale;
		using VectorMath::sub;

		double clamp01(double v)
		{
			return std::min(1.0, std::max(0.0, v));
		}

		double smoothstep(double t)
		{
			const double s = clamp01(t);
			return s * s * (3.0 - 2.0 * s);
		}

		// ---- the spline ----

		struct Spline
		{
			int count{ 0 };
			double u[MaxControls]{};
			Vec3 p[MaxControls];
			Vec3 m[MaxControls];   // dP/du at each knot, leaving it
			// With `split` the tangent arriving at a knot is mIn, not m, so a knot can be a corner (a Limb's elbow, D62). Without it a
			// knot has one tangent, and mIn is not read.
			bool split{ false };
			Vec3 mIn[MaxControls];
		};

		// Segment k at t (0 to 1 across it, and past either end the polynomial carries on).
		void evalSegment(const Spline& s, int k, double t, Vec3& outPos, Vec3& outDerivative)
		{
			const double h = s.u[k + 1] - s.u[k];
			const double t2 = t * t;
			const double t3 = t2 * t;
			const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
			const double h10 = t3 - 2.0 * t2 + t;
			const double h01 = -2.0 * t3 + 3.0 * t2;
			const double h11 = t3 - t2;
			const Vec3& mEnd = s.split ? s.mIn[k + 1] : s.m[k + 1];
			outPos = add(add(scale(s.p[k], h00), scale(s.m[k], h10 * h)), add(scale(s.p[k + 1], h01), scale(mEnd, h11 * h)));
			const double d00 = (6.0 * t2 - 6.0 * t) / h;
			const double d10 = 3.0 * t2 - 4.0 * t + 1.0;
			const double d01 = (-6.0 * t2 + 6.0 * t) / h;
			const double d11 = 3.0 * t2 - 2.0 * t;
			outDerivative = add(add(scale(s.p[k], d00), scale(s.m[k], d10)), add(scale(s.p[k + 1], d01), scale(mEnd, d11)));
		}

		// The segment u falls in. At a knot it is the one arriving there.
		int segmentAt(const Spline& s, double u)
		{
			int k = 0;
			while (k < s.count - 2 && u > s.u[k + 1])
			{
				++k;
			}
			return k;
		}

		void evalSpline(const Spline& s, double u, Vec3& outPos, Vec3& outDerivative)
		{
			// Past either end the end segment's polynomial carries on, so a central difference at an end stays central.
			const int k = segmentAt(s, u);
			evalSegment(s, k, (u - s.u[k]) / (s.u[k + 1] - s.u[k]), outPos, outDerivative);
		}

		// ---- parallel transport ----

		// The normals carried along a spline at FrameSamples steps. The normal at a u is read back by interpolation and made
		// perpendicular to the exact tangent, so the frame is smooth and no state outlives the evaluation.
		//
		// A spline with corners (split, D62) is sampled segment by segment instead, SegmentSamples steps each, because the tangent jumps
		// at a corner and projecting the normal onto the new tangent's plane flips it when the corner turns more than a quarter turn. The
		// normal goes through a corner by the shortest rotation that takes the arriving tangent to the leaving one.
		constexpr int SegmentSamples{ 16 };
		struct FrameTable
		{
			Vec3 n[(MaxControls - 1) * (SegmentSamples + 1)];
		};

		void buildFrameTable(const Spline& spline, const Vec3& up0, FrameTable& table)
		{
			Vec3 pos;
			Vec3 d;
			if (spline.split)
			{
				Vec3 normal;
				Vec3 endTangent{ 1.0, 0.0, 0.0 };
				for (int k = 0; k + 1 < spline.count; ++k)
				{
					evalSegment(spline, k, 0.0, pos, d);
					Vec3 tangent = normalized(d, endTangent);
					normal = k == 0 ? normalized(sub(up0, scale(tangent, dot(up0, tangent))), anyPerpendicular(tangent))
						: normalized(rotateByArc(normal, endTangent, tangent), normal);
					table.n[k * (SegmentSamples + 1)] = normal;
					for (int i = 1; i <= SegmentSamples; ++i)
					{
						evalSegment(spline, k, static_cast<double>(i) / SegmentSamples, pos, d);
						tangent = normalized(d, tangent);
						normal = normalized(sub(normal, scale(tangent, dot(normal, tangent))), normal);
						table.n[k * (SegmentSamples + 1) + i] = normal;
					}
					endTangent = tangent;
				}
				return;
			}
			evalSpline(spline, spline.u[0], pos, d);
			Vec3 tangent = normalized(d, { 1.0, 0.0, 0.0 });
			Vec3 normal = normalized(sub(up0, scale(tangent, dot(up0, tangent))), anyPerpendicular(tangent));
			table.n[0] = normal;
			const double span = spline.u[spline.count - 1] - spline.u[0];
			for (int i = 1; i <= FrameSamples; ++i)
			{
				evalSpline(spline, spline.u[0] + span * i / FrameSamples, pos, d);
				tangent = normalized(d, tangent);
				normal = normalized(sub(normal, scale(tangent, dot(normal, tangent))), normal);
				table.n[i] = normal;
			}
		}

		// The frame at t across segment k of a split spline: its tangent and the carried normal.
		void splitFrameAt(const Spline& spline, const FrameTable& table, int k, double t, Vec3& outTangent, Vec3& outNormal)
		{
			Vec3 pos;
			Vec3 d;
			evalSegment(spline, k, t, pos, d);
			outTangent = normalized(d, { 1.0, 0.0, 0.0 });
			const double f = clamp01(t) * SegmentSamples;
			const int i = std::min(static_cast<int>(f), SegmentSamples - 1);
			const int base = k * (SegmentSamples + 1) + i;
			const Vec3 blended = lerp(table.n[base], table.n[base + 1], f - i);
			outNormal = normalized(sub(blended, scale(outTangent, dot(blended, outTangent))), anyPerpendicular(outTangent));
		}

		// The frame's tangent and normal at u (u in the spline's own range). At a corner it is the frame arriving there.
		void frameAt(const Spline& spline, const FrameTable& table, double u, Vec3& outTangent, Vec3& outNormal)
		{
			if (spline.split)
			{
				const int k = segmentAt(spline, u);
				splitFrameAt(spline, table, k, (u - spline.u[k]) / (spline.u[k + 1] - spline.u[k]), outTangent, outNormal);
				return;
			}
			Vec3 pos;
			Vec3 d;
			evalSpline(spline, u, pos, d);
			outTangent = normalized(d, { 1.0, 0.0, 0.0 });
			const double span = spline.u[spline.count - 1] - spline.u[0];
			const double f = clamp01((u - spline.u[0]) / span) * FrameSamples;
			const int k = std::min(static_cast<int>(f), FrameSamples - 1);
			const Vec3 blended = lerp(table.n[k], table.n[k + 1], f - k);
			outNormal = normalized(sub(blended, scale(outTangent, dot(blended, outTangent))), anyPerpendicular(outTangent));
		}

		// The carried frame at u, turned about its tangent by `angle`.
		Mat4 frameMatrix(const Spline& spline, const FrameTable& table, double u, double angle)
		{
			Vec3 tangent;
			Vec3 normal;
			frameAt(spline, table, u, tangent, normal);
			Vec3 pos;
			Vec3 d;
			evalSpline(spline, u, pos, d);
			const Vec3 binormal = cross(tangent, normal);
			const double c = std::cos(angle);
			const double s = std::sin(angle);
			return makeFrame(tangent, add(scale(normal, c), scale(binormal, s)), sub(scale(binormal, c), scale(normal, s)), pos);
		}

		// The total twist of the start and end controls (4.1). Every control between them carries the same ramp, so the ends'
		// twist runs along the whole chain and not only to the next control.
		struct EndTwists
		{
			double start{ 0.0 };
			double end{ 0.0 };

			double at(double u) const
			{
				return start + (end - start) * u;
			}
		};

		// The start's frame is its parent's, so its twist is its own control's alone. The end also follows the twist of its plug's
		// motion relative to the start's, so the end plug's parent can twist the ribbon. The secondary angle picks the turn of
		// the sum.
		EndTwists endTwists(const Params& params, const Input& input, int controlCount)
		{
			return { pickTwistBranch(extractTwist(input.local[0]), params.startTwist),
				pickTwistBranch(extractTwist(input.local[controlCount - 1]) + extractTwist(input.endDelta), params.endTwist) };
		}

		// ---- the base curve: start and end only ----

		struct Base
		{
			Spline spline;
			FrameTable table;
			// With an fk chain: each fk control's roll away from its rest about the curve (fkRollDeltas), leaving its knot and arriving at it.
			// They are one value except at a corner.
			double fkRollOut[MaxControls]{};
			double fkRollIn[MaxControls]{};
		};

		// A control's X axis as a direction down the chain.
		Vec3 forwardAxis(const Mat4& world, const Setup& setup, const Vec3& fallback)
		{
			return normalized(scale(row(world, 0), setup.chainSign), fallback);
		}

		// A two-point spline from the start to the end, with the end handles the aim blends and the roundness set. Also the ends of an
		// fk chain's base curve.
		void makeEnds(const Setup& setup, const Params& params, const Mat4& startWorld, const Mat4& endWorld, Spline& spline)
		{
			const Vec3 p0 = origin(startWorld);
			Vec3 p1 = origin(endWorld);
			Vec3 chord = sub(p1, p0);
			double chordLength = length(chord);
			Vec3 dir0;
			Vec3 dir1;
			if (chordLength < kMinChord)
			{
				// The ends sit on each other: the rest frame stands in, so the node never builds a singular matrix.
				chordLength = setup.restLength;
				p1 = add(p0, scale(setup.restTangent, chordLength));
				dir0 = setup.restTangent;
				dir1 = setup.restTangent;
			}
			else
			{
				const Vec3 aim = scale(chord, 1.0 / chordLength);
				dir0 = normalized(lerp(forwardAxis(startWorld, setup, aim), aim, clamp01(params.startAimBlend)), aim);
				dir1 = normalized(lerp(forwardAxis(endWorld, setup, aim), aim, clamp01(params.endAimBlend)), aim);
			}

			const double handle = chordLength * std::max(0.0, params.bendRoundness);
			spline.count = 2;
			spline.u[0] = 0.0;
			spline.u[1] = 1.0;
			spline.p[0] = p0;
			spline.p[1] = p1;
			spline.m[0] = scale(dir0, handle);
			spline.m[1] = scale(dir1, handle);
		}

		// The frame table of a base curve, carrying the rest up vector from the start.
		void finishBase(const Setup& setup, Base& base)
		{
			Vec3 pos;
			Vec3 d;
			evalSpline(base.spline, 0.0, pos, d);
			const Vec3 up0 = rotateByArc(setup.restUp, setup.restTangent, normalized(d, setup.restTangent));
			buildFrameTable(base.spline, up0, base.table);
		}

		void makeBase(const Setup& setup, const Params& params, const Mat4& startWorld, const Mat4& endWorld, Base& base)
		{
			makeEnds(setup, params, startWorld, endWorld, base.spline);
			finishBase(setup, base);
		}

		// Makes knots corners (D62, a Limb's elbow): each side of a corner leaves it along its own chord, so a segment between two
		// corners is a straight line and the curve through them is the polyline. `softness` blends back to the smooth tangents the knots
		// already hold: 0 is the corner, 1 leaves them as they are. isCorner marks the interior knots that are corners (null: all of
		// them), and the two ends take the chord of their one segment too when `ends` is set.
		void softenCorners(Spline& spline, double softness, const bool* isCorner, bool ends)
		{
			const double s = clamp01(softness);
			spline.split = true;
			for (int k = 0; k < spline.count; ++k)
			{
				spline.mIn[k] = spline.m[k];
			}
			for (int k = 0; k < spline.count; ++k)
			{
				const bool isEnd = k == 0 || k == spline.count - 1;
				if (isEnd ? !ends : (isCorner != nullptr && !isCorner[k]))
				{
					continue;
				}
				if (k + 1 < spline.count)
				{
					const Vec3 chordOut = scale(sub(spline.p[k + 1], spline.p[k]), 1.0 / (spline.u[k + 1] - spline.u[k]));
					spline.m[k] = lerp(chordOut, spline.m[k], s);
				}
				if (k > 0)
				{
					const Vec3 chordIn = scale(sub(spline.p[k], spline.p[k - 1]), 1.0 / (spline.u[k] - spline.u[k - 1]));
					spline.mIn[k] = lerp(chordIn, spline.mIn[k], s);
				}
			}
		}

		// The base curve of an fk chain (D59): through the fk controls' positions, with the ends' handles from makeEnds and each
		// interior tangent blended from its neighbours and its own X axis, as the detail curve's are. Two fk controls are makeBase.
		void makeFkBase(const Setup& setup, const Params& params, const Mat4* fkWorld, Base& base)
		{
			const int last = setup.fkCount - 1;
			Spline ends;
			makeEnds(setup, params, fkWorld[0], fkWorld[last], ends);
			Spline& spline = base.spline;
			spline.count = setup.fkCount;
			for (int k = 0; k <= last; ++k)
			{
				spline.u[k] = setup.fkKnotU[k];
				spline.p[k] = origin(fkWorld[k]);
			}
			spline.p[last] = ends.p[1];
			spline.m[0] = ends.m[0];
			spline.m[last] = ends.m[1];
			for (int k = 1; k < last; ++k)
			{
				const Vec3 difference = scale(sub(spline.p[k + 1], spline.p[k - 1]), 1.0 / (spline.u[k + 1] - spline.u[k - 1]));
				const Vec3 differenceDirection = normalized(difference, { 1.0, 0.0, 0.0 });
				const Vec3 direction = normalized(lerp(differenceDirection, forwardAxis(fkWorld[k], setup, differenceDirection), kControlTangentBlend), differenceDirection);
				spline.m[k] = scale(direction, length(difference));
			}
			if (setup.fkAnchors)
			{
				softenCorners(spline, params.cornerSoftness, nullptr, true);
			}
			finishBase(setup, base);
		}

		// How far a Y axis is turned about a tangent from the carried normal, in (-pi, pi].
		double rollFrom(const Vec3& tangent, const Vec3& normal, const Vec3& y)
		{
			const Vec3 flat = sub(y, scale(tangent, dot(y, tangent)));
			return std::atan2(dot(flat, cross(tangent, normal)), dot(flat, normal));
		}

		// How far a control's Y axis is turned about the curve's tangent from the carried normal at u. The curve only carries a
		// direction, so this is how an fk control's roll reaches the ik controls.
		double rollAbout(const Base& base, double u, const Mat4& world)
		{
			Vec3 tangent;
			Vec3 normal;
			frameAt(base.spline, base.table, u, tangent, normal);
			return rollFrom(tangent, normal, row(world, 1));
		}

		// The roll at fk knot k, on the side the curve leaves it (leaving) or arrives at it. Only a corner has two sides. A joint at a corner
		// has its axes on the bone the curve leaves along, so for the side that arrives its Y axis is first turned back through the corner,
		// the shortest rotation that takes the leaving tangent to the arriving one: a joint that is only bent there has no roll on either
		// side, and one that is twisted has the twist it was given.
		double rollAtKnot(const Base& base, int k, bool leaving, const Mat4& world)
		{
			if (!base.spline.split)
			{
				return rollAbout(base, base.spline.u[k], world);
			}
			Vec3 tangent;
			Vec3 normal;
			splitFrameAt(base.spline, base.table, leaving ? k : k - 1, leaving ? 0.0 : 1.0, tangent, normal);
			Vec3 y = row(world, 1);
			if (!leaving && k + 1 < base.spline.count)
			{
				Vec3 leavingTangent;
				Vec3 leavingNormal;
				splitFrameAt(base.spline, base.table, k, 0.0, leavingTangent, leavingNormal);
				y = rotateByArc(y, leavingTangent, tangent);
			}
			return rollFrom(tangent, normal, y);
		}

		// Each fk control's roll away from its rest roll, unwrapped along the chain so a turn past pi doesn't flip the ik controls.
		void fkRollDeltas(const Setup& setup, const Base& base, const Mat4* fkWorld, double* outLeaving, double* outArriving)
		{
			const int last = setup.fkCount - 1;
			for (int k = 0; k <= last; ++k)
			{
				if (k == 0)
				{
					outLeaving[0] = wrapAngle(rollAtKnot(base, 0, true, fkWorld[0]) - setup.fkRollRest[0]);
					outArriving[0] = outLeaving[0];
					continue;
				}
				const double arriving = rollAtKnot(base, k, false, fkWorld[k]) - setup.fkRollRestIn[k];
				outArriving[k] = outLeaving[k - 1] + wrapAngle(arriving - outLeaving[k - 1]);
				if (k == last)
				{
					outLeaving[k] = outArriving[k];
					continue;
				}
				const double leaving = rollAtKnot(base, k, true, fkWorld[k]) - setup.fkRollRest[k];
				outLeaving[k] = outArriving[k] + wrapAngle(leaving - outArriving[k]);
			}
		}

		// The fk rolls interpolated linearly at u: from the roll leaving a knot to the one arriving at the next. At a knot itself the
		// side is asked for.
		double fkRollAt(const Setup& setup, const double* leaving, const double* arriving, double u, bool leavingSide)
		{
			int k = 0;
			if (leavingSide)
			{
				while (k < setup.fkCount - 2 && u >= setup.fkKnotU[k + 1] - kKnotTolerance)
				{
					++k;
				}
			}
			else
			{
				while (k < setup.fkCount - 2 && u > setup.fkKnotU[k + 1] + kKnotTolerance)
				{
					++k;
				}
			}
			const double span = setup.fkKnotU[k + 1] - setup.fkKnotU[k];
			const double s = clamp01((u - setup.fkKnotU[k]) / span);
			return leaving[k] + (arriving[k + 1] - leaving[k]) * s;
		}

		// Start and end as given, the interior controls on the base curve at their parameters. With an fk chain, the chain is composed
		// first, the base curve goes through it, and every ik control (the ends too) floats on that curve.
		void evaluateControlsInternal(const Setup& setup, const Params& params, const Input& input, const EndTwists& twists, Output& output, Base& base)
		{
			const int last = setup.controlCount - 1;
			if (setup.fkCount > 0)
			{
				// Each fk control rides its parent by the rest offset between them, so a rest chain gives the rest worlds.
				for (int k = 0; k < setup.fkCount; ++k)
				{
					if (setup.fkAnchors)
					{
						output.fkOPM[k] = input.fkWorld[k];
						output.fkWorld[k] = input.fkWorld[k];
						continue;
					}
					output.fkOPM[k] = k == 0 ? setup.fkRest[0] : multiply(multiply(setup.fkRest[k], inverseRigid(setup.fkRest[k - 1])), output.fkWorld[k - 1]);
					output.fkWorld[k] = multiply(input.fkLocal[k], output.fkOPM[k]);
				}
				makeFkBase(setup, params, output.fkWorld, base);
				fkRollDeltas(setup, base, output.fkWorld, base.fkRollOut, base.fkRollIn);
				for (int j = 0; j <= last; ++j)
				{
					// The ends' own twist is in their local matrix, so their frame carries none.
					const double ramp = j == 0 || j == last ? 0.0 : setup.chainSign * twists.at(setup.knotU[j]);
						// Every ik control also turns with the fk chain's roll at its parameter, so it follows the fk controls 1:1.
						const double angle = ramp + fkRollAt(setup, base.fkRollOut, base.fkRollIn, setup.knotU[j], false);
					output.controlOPM[j] = multiply(setup.ikOffset[j], frameMatrix(base.spline, base.table, setup.knotU[j], angle));
					output.controlWorld[j] = multiply(input.local[j], output.controlOPM[j]);
				}
				return;
			}
			output.controlOPM[0] = input.startOPM;
			output.controlOPM[last] = input.endOPM;
			output.controlWorld[0] = multiply(input.local[0], input.startOPM);
			output.controlWorld[last] = multiply(input.local[last], input.endOPM);
			makeBase(setup, params, output.controlWorld[0], output.controlWorld[last], base);
			for (int j = 1; j < last; ++j)
			{
				output.controlOPM[j] = multiply(setup.ikOffset[j], frameMatrix(base.spline, base.table, setup.knotU[j], setup.chainSign * twists.at(setup.knotU[j])));
				output.controlWorld[j] = multiply(input.local[j], output.controlOPM[j]);
			}
		}

		// ---- the detail curve and its layers ----

		struct Detail
		{
			const Setup* pSetup{ nullptr };
			const Params* pParams{ nullptr };
			Spline spline;
			FrameTable table;
			// each control's total twist: the ends' ramp at its parameter plus its own, leaving it and arriving at it (one value except at a
			// corner, where the anchor's roll is measured on each side)
			double twistOut[MaxControls]{};
			double twistIn[MaxControls]{};
			Vec3 axisScale[MaxControls];
			double lengthRatio{ 1.0 };

			// The index of the control segment u falls in, and where in it (0 to 1).
			void segmentOf(double u, int& outK, double& outS) const
			{
				const double* knots = pSetup->knotU;
				int k = 0;
				while (k < pSetup->controlCount - 2 && u > knots[k + 1])
				{
					++k;
				}
				outK = k;
				outS = clamp01((u - knots[k]) / (knots[k + 1] - knots[k]));
			}

			double twistAt(double u) const
			{
				int k;
				double s;
				segmentOf(u, k, s);
				return twistOut[k] + (twistIn[k + 1] - twistOut[k]) * s;
			}

			Vec3 scaleAt(double u) const
			{
				int k;
				double s;
				segmentOf(u, k, s);
				return lerp(axisScale[k], axisScale[k + 1], smoothstep(s));
			}

			// The curve's tangent, the carried normal turned by the twist, and the third axis.
			void twistedFrame(double u, Vec3& outTangent, Vec3& outY, Vec3& outZ) const
			{
				Vec3 normal;
				frameAt(spline, table, u, outTangent, normal);
				const Vec3 binormal = cross(outTangent, normal);
				const double angle = twistAt(u);
				const double c = std::cos(angle);
				const double s = std::sin(angle);
				outY = add(scale(normal, c), scale(binormal, s));
				outZ = sub(scale(binormal, c), scale(normal, s));
			}

			// A sine's envelope: fades in over `falloff` of the ribbon from an end. A falloff of 0 is no fade.
			static double fade(double distanceFromEnd, double falloff)
			{
				return falloff <= 0.0 ? 1.0 : smoothstep(distanceFromEnd / falloff);
			}

			// The curve with the sine laid on, after the twist (Q-d).
			Vec3 displaced(double u) const
			{
				Vec3 pos;
				Vec3 d;
				evalSpline(spline, u, pos, d);
				Vec3 tangent;
				Vec3 y;
				Vec3 z;
				twistedFrame(u, tangent, y, z);
				const double restLength = pSetup->restLength;
				const double envelope = fade(u, pParams->sineStartFalloff) * fade(1.0 - u, pParams->sineEndFalloff);
				const double wave = pParams->sineAmplitude * restLength * envelope * std::sin(2.0 * kPi * (pParams->sineFrequency * u + pParams->sinePhase));
				const Vec3 direction = add(scale(y, std::cos(pParams->sineOrient)), scale(z, std::sin(pParams->sineOrient)));
				return add(pos, scale(direction, wave));
			}

			// The cross-section scale from the volume profile: a quadratic through the three weights.
			double crossScaleAt(double u) const
			{
				const double w = pParams->scaleStart * 2.0 * (u - 0.5) * (u - 1.0)
					+ pParams->scaleMid * -4.0 * u * (u - 1.0)
					+ pParams->scaleEnd * 2.0 * u * (u - 0.5);
				return std::pow(lengthRatio, -0.5 * clamp01(pParams->volumePreserve) * w);
			}
		};

		void makeDetail(const Setup& setup, const Params& params, const Input& input, const EndTwists& twists, const Output& output, const Base& base, Detail& detail)
		{
			const int count = setup.controlCount;
			detail.pSetup = &setup;
			detail.pParams = &params;
			Spline& spline = detail.spline;
			spline.count = count;
			for (int j = 0; j < count; ++j)
			{
				spline.u[j] = setup.knotU[j];
				spline.p[j] = origin(output.controlWorld[j]);
			}
			// The ends take the base curve's handles (aim blend and roundness). A two-control ribbon is the base curve.
			spline.m[0] = base.spline.m[0];
			const int baseLast = base.spline.count - 1;
			spline.m[count - 1] = base.spline.split ? base.spline.mIn[baseLast] : base.spline.m[baseLast];
			for (int j = 1; j < count - 1; ++j)
			{
				const Vec3 difference = scale(sub(spline.p[j + 1], spline.p[j - 1]), 1.0 / (spline.u[j + 1] - spline.u[j - 1]));
				const Vec3 differenceDirection = normalized(difference, { 1.0, 0.0, 0.0 });
				const Vec3 direction = normalized(lerp(differenceDirection, forwardAxis(output.controlWorld[j], setup, differenceDirection), kControlTangentBlend), differenceDirection);
				spline.m[j] = scale(direction, length(difference));
			}
			if (setup.fkAnchors)
			{
				// The controls at the anchors' corners are corners of this curve too, at the same softness, so the curve through the bind
				// joints rounds nothing the base curve does not. The ends already took the base's.
				softenCorners(spline, params.cornerSoftness, setup.corner, false);
			}

			Vec3 pos;
			Vec3 d;
			evalSpline(spline, spline.u[0], pos, d);
			buildFrameTable(spline, rotateByArc(setup.restUp, setup.restTangent, normalized(d, setup.restTangent)), detail.table);

			double liveLength = 0.0;
			for (int j = 0; j < count; ++j)
			{
				const Mat4& local = input.local[j];
				detail.axisScale[j] = { length(row(local, 0)), length(row(local, 1)), length(row(local, 2)) };
				// Twists are angles about a control's own X. The frame turns about the forward tangent, which is -X when the chain runs back.
				detail.twistOut[j] = setup.chainSign * (twists.at(setup.knotU[j]) + extractTwist(local));
				detail.twistIn[j] = detail.twistOut[j];
				if (j + 1 < count)
				{
					liveLength += length(sub(spline.p[j + 1], spline.p[j]));
				}
			}
			detail.twistOut[0] = setup.chainSign * twists.start;
			detail.twistIn[0] = detail.twistOut[0];
			detail.twistOut[count - 1] = setup.chainSign * twists.end;
			detail.twistIn[count - 1] = detail.twistOut[count - 1];
			if (setup.fkAnchors)
			{
				// A Limb's anchors are real joints, so their roll about the curve is twist the skin must follow: it eases between them
				// like the ends' ramp. fkRoll is already about the forward tangent, so chainSign is not applied again.
				for (int j = 0; j < count; ++j)
				{
					detail.twistOut[j] += fkRollAt(setup, base.fkRollOut, base.fkRollIn, setup.knotU[j], true);
					detail.twistIn[j] += fkRollAt(setup, base.fkRollOut, base.fkRollIn, setup.knotU[j], false);
				}
			}
			detail.lengthRatio = std::min(kMaxLengthRatio, std::max(kMinLengthRatio, liveLength / setup.restLength));
		}

		Mat4 bindFrame(const Detail& detail, double u)
		{
			const double uA = u - kTangentStep;
			const double uB = u + kTangentStep;
			const Vec3 p = detail.displaced(u);

			Vec3 curveTangent;
			Vec3 normal;
			frameAt(detail.spline, detail.table, u, curveTangent, normal);
			const Vec3 tangent = normalized(sub(detail.displaced(uB), detail.displaced(uA)), curveTangent);
			normal = normalized(sub(normal, scale(tangent, dot(normal, tangent))), anyPerpendicular(tangent));
			const Vec3 binormal = cross(tangent, normal);
			const double angle = detail.twistAt(u);
			const double c = std::cos(angle);
			const double s = std::sin(angle);
			const Vec3 y = add(scale(normal, c), scale(binormal, s));
			const Vec3 z = sub(scale(binormal, c), scale(normal, s));

			const Vec3 axes = detail.scaleAt(u);
			const double crossScale = detail.crossScaleAt(u);
			return makeFrame(scale(tangent, axes.x), scale(y, axes.y * crossScale), scale(z, axes.z * crossScale), p);
		}
	}

	Mat4 multiply(const Mat4& a, const Mat4& b)
	{
		Mat4 result;
		for (int r = 0; r < 4; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				result.m[r][c] = a.m[r][0] * b.m[0][c] + a.m[r][1] * b.m[1][c] + a.m[r][2] * b.m[2][c] + a.m[r][3] * b.m[3][c];
			}
		}
		return result;
	}

	Mat4 inverseRigid(const Mat4& a)
	{
		Mat4 result;
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 3; ++c)
			{
				result.m[r][c] = a.m[c][r];
			}
		}
		const Vec3 o = origin(a);
		for (int c = 0; c < 3; ++c)
		{
			result.m[3][c] = -(o.x * result.m[0][c] + o.y * result.m[1][c] + o.z * result.m[2][c]);
		}
		return result;
	}

	double wrapAngle(double angle)
	{
		return angle - 2.0 * kPi * std::floor((angle + kPi) / (2.0 * kPi));
	}

	double pickTwistBranch(double extracted, double secondary)
	{
		return secondary + wrapAngle(extracted - secondary);
	}

	double extractTwist(const Mat4& local)
	{
		const double rows[3][3]{ { local.m[0][0], local.m[0][1], local.m[0][2] }, { local.m[1][0], local.m[1][1], local.m[1][2] }, { local.m[2][0], local.m[2][1], local.m[2][2] } };
		return SwingTwist::twistAboutX(rows);
	}

	void markAnchorCorners(Setup& setup)
	{
		for (int j = 0; j < setup.controlCount; ++j)
		{
			setup.corner[j] = false;
			for (int k = 1; k + 1 < setup.fkCount; ++k)
			{
				setup.corner[j] = setup.corner[j] || std::fabs(setup.knotU[j] - setup.fkKnotU[k]) < kKnotTolerance;
			}
		}
	}

	bool fkKnots(const Mat4* fkRest, int fkCount, double* outU)
	{
		double chords[MaxControls]{};
		double total = 0.0;
		for (int k = 0; k + 1 < fkCount; ++k)
		{
			chords[k] = length(sub(origin(fkRest[k + 1]), origin(fkRest[k])));
			if (chords[k] < kMinChord)
			{
				return false;
			}
			total += chords[k];
		}
		double running = 0.0;
		for (int k = 0; k < fkCount; ++k)
		{
			outU[k] = k == fkCount - 1 ? 1.0 : running / total;
			running += chords[k];
		}
		return true;
	}

	void fkRollRests(Setup& setup)
	{
		Base base;
		makeFkBase(setup, Params{}, setup.fkRest, base);
		const int last = setup.fkCount - 1;
		for (int k = 0; k <= last; ++k)
		{
			setup.fkRollRest[k] = rollAtKnot(base, k, k < last, setup.fkRest[k]);
			setup.fkRollRestIn[k] = k > 0 ? rollAtKnot(base, k, false, setup.fkRest[k]) : setup.fkRollRest[0];
		}
	}

	namespace
	{
		// Which way the controls' X axes run against the chain: each one dotted with the chord that leaves it (the last with the one
		// that arrives). +1 when they run down it, -1 when they point back up it.
		double chainSignOf(const Mat4* rest, int count)
		{
			double alignment = 0.0;
			for (int j = 0; j < count; ++j)
			{
				const int chord = std::min(j, count - 2);
				alignment += dot(row(rest[j], 0), sub(origin(rest[chord + 1]), origin(rest[chord])));
			}
			return alignment < 0.0 ? -1.0 : 1.0;
		}

		// The one setup builder. forcedKnotU is null when the knots are the controls' chord shares, and fkAnchors says the fk chain is
		// given as world frames (Setup::fkAnchors).
		bool buildSetupImpl(const Mat4* controlRest, int controlCount, const Mat4* bindRest, int bindCount, const Mat4* fkRest, int fkCount, const double* forcedKnotU, bool fkAnchors, Setup& outSetup, const char*& outReason)
		{
			if (controlCount < 2 || controlCount > MaxControls)
			{
				outReason = "the ribbon needs between 2 and 8 control joints";
				return false;
			}
			if (bindCount < 2 || bindCount > MaxBindJoints)
			{
				outReason = "the ribbon needs between 2 and 64 bind joints";
				return false;
			}

			if (fkCount != 0 && (fkCount < 2 || fkCount > MaxControls))
			{
				outReason = "the fk chain needs between 2 and 8 controls";
				return false;
			}

			Setup setup;
			setup.controlCount = controlCount;
			setup.bindCount = bindCount;
			setup.fkCount = fkCount;
			setup.fkAnchors = fkAnchors;
			for (int k = 0; k < fkCount; ++k)
			{
				setup.fkRest[k] = fkRest[k];
			}
			if (fkCount > 0 && !fkKnots(fkRest, fkCount, setup.fkKnotU))
			{
				outReason = "two neighbouring fk controls sit on each other, so the fk chain has no length between them";
				return false;
			}

			double chords[MaxControls]{};
			double total = 0.0;
			for (int j = 0; j + 1 < controlCount; ++j)
			{
				chords[j] = length(sub(origin(controlRest[j + 1]), origin(controlRest[j])));
				if (chords[j] < kMinChord)
				{
					outReason = "two neighbouring control joints sit on each other, so the ribbon has no length between them";
					return false;
				}
				total += chords[j];
			}
			setup.restLength = total;
			double running = 0.0;
			for (int j = 0; j < controlCount; ++j)
			{
				setup.knotU[j] = forcedKnotU ? forcedKnotU[j] : (j == controlCount - 1 ? 1.0 : running / total);
				running += chords[j];
			}

			if (fkAnchors)
			{
				markAnchorCorners(setup);
			}

			// A bind joint's parameter: the closest point of the control polyline, by its share of the chord's u span.
			for (int i = 0; i < bindCount; ++i)
			{
				const Vec3 p = origin(bindRest[i]);
				double bestDistance = -1.0;
				for (int j = 0; j + 1 < controlCount; ++j)
				{
					const Vec3 a = origin(controlRest[j]);
					const Vec3 ab = sub(origin(controlRest[j + 1]), a);
					const double t = clamp01(dot(sub(p, a), ab) / dot(ab, ab));
					const double distance = length(sub(p, add(a, scale(ab, t))));
					if (bestDistance < 0.0 || distance < bestDistance)
					{
						bestDistance = distance;
						setup.bindU[i] = setup.knotU[j] + t * (setup.knotU[j + 1] - setup.knotU[j]);
					}
				}
			}

			setup.chainSign = chainSignOf(controlRest, controlCount);

			setup.restTangent = forwardAxis(controlRest[0], setup, { 1.0, 0.0, 0.0 });
			const Vec3 y = row(controlRest[0], 1);
			setup.restUp = normalized(sub(y, scale(setup.restTangent, dot(y, setup.restTangent))), anyPerpendicular(setup.restTangent));

			// The interior controls' offsets from the base curve at rest, then the bind joints' from the detail curve with the
			// interior controls where they were built.
			const Params defaults{};
			Input input;
			input.startOPM = controlRest[0];
			input.endOPM = controlRest[controlCount - 1];
			if (fkAnchors)
			{
				for (int k = 0; k < fkCount; ++k)
				{
					input.fkWorld[k] = fkRest[k];
				}
			}
			Base base;
			if (fkCount > 0)
			{
				makeFkBase(setup, defaults, setup.fkRest, base);
				fkRollRests(setup);
			}
			else
			{
				makeBase(setup, defaults, controlRest[0], controlRest[controlCount - 1], base);
			}
			for (int j = fkCount > 0 ? 0 : 1; j < (fkCount > 0 ? controlCount : controlCount - 1); ++j)
			{
				setup.ikOffset[j] = multiply(controlRest[j], inverseRigid(frameMatrix(base.spline, base.table, setup.knotU[j], 0.0)));
			}

			Output output;
			evaluate(setup, defaults, input, output);
			for (int i = 0; i < bindCount; ++i)
			{
				setup.bindOffset[i] = multiply(bindRest[i], inverseRigid(output.bindOPM[i]));
			}

			outSetup = setup;
			outReason = "";
			return true;
		}
	}

	bool buildSetup(const Mat4* controlRest, int controlCount, const Mat4* bindRest, int bindCount, const Mat4* fkRest, int fkCount, Setup& outSetup, const char*& outReason)
	{
		return buildSetupImpl(controlRest, controlCount, bindRest, bindCount, fkRest, fkCount, nullptr, false, outSetup, outReason);
	}

	bool buildAnchoredSetup(const Mat4* anchorRest, int anchorCount, const double* controlU, int controlCount, const Mat4* bindRest, int bindCount, Setup& outSetup, Mat4* outControlRest, const char*& outReason)
	{
		if (anchorCount < 2 || anchorCount > MaxControls || controlCount < 2 || controlCount > MaxControls)
		{
			outReason = "the ribbon needs between 2 and 8 anchors and between 2 and 8 control joints";
			return false;
		}
		if (std::fabs(controlU[0]) > kKnotTolerance || std::fabs(controlU[controlCount - 1] - 1.0) > kKnotTolerance)
		{
			outReason = "the first and last control must sit at the start and end of the curve";
			return false;
		}
		for (int j = 0; j + 1 < controlCount; ++j)
		{
			if (controlU[j + 1] - controlU[j] < kKnotTolerance)
			{
				outReason = "two neighbouring controls sit at the same place on the curve";
				return false;
			}
		}

		// The anchors' curve at rest. Its frame carries the first anchor's orientation, so the controls on it do too: the half-turn a
		// mirrored chain keeps (X running back) is the first anchor's own difference from the curve's frame, and every control takes it.
		Setup curveSetup;
		curveSetup.fkCount = anchorCount;
		curveSetup.fkAnchors = true;   // the curve at rest has the corners the live one has by default
		for (int k = 0; k < anchorCount; ++k)
		{
			curveSetup.fkRest[k] = anchorRest[k];
		}
		if (!fkKnots(anchorRest, anchorCount, curveSetup.fkKnotU))
		{
			outReason = "two neighbouring anchors sit on each other, so the curve has no length between them";
			return false;
		}
		curveSetup.chainSign = chainSignOf(anchorRest, anchorCount);
		curveSetup.restTangent = forwardAxis(anchorRest[0], curveSetup, { 1.0, 0.0, 0.0 });
		const Vec3 anchorY = row(anchorRest[0], 1);
		curveSetup.restUp = normalized(sub(anchorY, scale(curveSetup.restTangent, dot(anchorY, curveSetup.restTangent))), anyPerpendicular(curveSetup.restTangent));
		Base base;
		makeFkBase(curveSetup, Params{}, anchorRest, base);
		Mat4 startFrame = frameMatrix(base.spline, base.table, 0.0, 0.0);
		Mat4 anchorRotation = anchorRest[0];
		for (int c = 0; c < 3; ++c)
		{
			startFrame.m[3][c] = 0.0;
			anchorRotation.m[3][c] = 0.0;
		}
		const Mat4 turn = multiply(anchorRotation, inverseRigid(startFrame));
		for (int j = 0; j < controlCount; ++j)
		{
			Mat4 frame = frameMatrix(base.spline, base.table, controlU[j], 0.0);
			const Vec3 position = origin(frame);
			for (int c = 0; c < 3; ++c)
			{
				frame.m[3][c] = 0.0;
			}
			outControlRest[j] = multiply(turn, frame);
			outControlRest[j].m[3][0] = position.x;
			outControlRest[j].m[3][1] = position.y;
			outControlRest[j].m[3][2] = position.z;
		}

		return buildSetupImpl(outControlRest, controlCount, bindRest, bindCount, anchorRest, anchorCount, controlU, true, outSetup, outReason);
	}

	void evaluate(const Setup& setup, const Params& params, const Input& input, Output& output)
	{
		const EndTwists twists = endTwists(params, input, setup.controlCount);
		Base base;
		evaluateControlsInternal(setup, params, input, twists, output, base);

		Detail detail;
		makeDetail(setup, params, input, twists, output, base, detail);
		for (int i = 0; i < setup.bindCount; ++i)
		{
			output.bindOPM[i] = multiply(setup.bindOffset[i], bindFrame(detail, setup.bindU[i]));
		}
	}
}
