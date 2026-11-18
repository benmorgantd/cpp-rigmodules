#include "RibbonMath.h"

#include <algorithm>
#include <cmath>

namespace RibbonMath
{
	namespace
	{
		constexpr double kPi{ 3.14159265358979323846 };
		// A chord this short has no direction: the rest frame stands in for it (Q-e).
		constexpr double kMinChord{ 1.0e-4 };
		constexpr double kTiny{ 1.0e-12 };
		// The volume exponent clamps the length ratio so an extreme pull can't zero or explode the scale (Q-e).
		constexpr double kMinLengthRatio{ 0.25 };
		constexpr double kMaxLengthRatio{ 4.0 };
		// The step of the central difference that turns the displaced curve into a tangent.
		constexpr double kTangentStep{ 1.0e-3 };
		// How much of an interior control's own X axis sets the tangent of the curve through it (the rest is the
		// finite difference of its neighbours).
		constexpr double kControlTangentBlend{ 0.5 };

		Vec3 add(const Vec3& a, const Vec3& b)
		{
			return { a.x + b.x, a.y + b.y, a.z + b.z };
		}
		Vec3 sub(const Vec3& a, const Vec3& b)
		{
			return { a.x - b.x, a.y - b.y, a.z - b.z };
		}
		Vec3 scale(const Vec3& a, double s)
		{
			return { a.x * s, a.y * s, a.z * s };
		}
		double dot(const Vec3& a, const Vec3& b)
		{
			return a.x * b.x + a.y * b.y + a.z * b.z;
		}
		Vec3 cross(const Vec3& a, const Vec3& b)
		{
			return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
		}
		double length(const Vec3& a)
		{
			return std::sqrt(dot(a, a));
		}
		Vec3 normalized(const Vec3& a, const Vec3& fallback)
		{
			const double len = length(a);
			return len > kTiny ? scale(a, 1.0 / len) : fallback;
		}
		Vec3 lerp(const Vec3& a, const Vec3& b, double t)
		{
			return add(scale(a, 1.0 - t), scale(b, t));
		}
		Vec3 row(const Mat4& a, int r)
		{
			return { a.m[r][0], a.m[r][1], a.m[r][2] };
		}
		Vec3 origin(const Mat4& a)
		{
			return row(a, 3);
		}
		double clamp01(double v)
		{
			return std::min(1.0, std::max(0.0, v));
		}
		double smoothstep(double t)
		{
			const double s = clamp01(t);
			return s * s * (3.0 - 2.0 * s);
		}

		Mat4 makeFrame(const Vec3& x, const Vec3& y, const Vec3& z, const Vec3& o)
		{
			Mat4 result;
			const Vec3* axes[4]{ &x, &y, &z, &o };
			for (int r = 0; r < 4; ++r)
			{
				result.m[r][0] = axes[r]->x;
				result.m[r][1] = axes[r]->y;
				result.m[r][2] = axes[r]->z;
			}
			return result;
		}

		// A vector perpendicular to v, for the cases with no better choice.
		Vec3 anyPerpendicular(const Vec3& v)
		{
			const Vec3 axis = std::fabs(v.x) < 0.9 ? Vec3{ 1.0, 0.0, 0.0 } : Vec3{ 0.0, 1.0, 0.0 };
			return normalized(cross(v, axis), { 0.0, 1.0, 0.0 });
		}

		// v turned by the shortest arc that takes unit vector `from` to unit vector `to` (Rodrigues). Antiparallel is
		// a half turn about v's own perpendicular, so v is kept as it is.
		Vec3 rotateByArc(const Vec3& v, const Vec3& from, const Vec3& to)
		{
			const Vec3 axis = cross(from, to);
			const double sine = length(axis);
			const double cosine = dot(from, to);
			if (sine < 1.0e-9)
			{
				return v;
			}
			const Vec3 k = scale(axis, 1.0 / sine);
			return add(add(scale(v, cosine), scale(cross(k, v), sine)), scale(k, dot(k, v) * (1.0 - cosine)));
		}

		// ---- the spline ----

		struct Spline
		{
			int count{ 0 };
			double u[MaxControls]{};
			Vec3 p[MaxControls];
			Vec3 m[MaxControls];   // dP/du at each knot
		};

		void evalSpline(const Spline& s, double u, Vec3& outPos, Vec3& outDerivative)
		{
			// Past either end the end segment's polynomial carries on, so a central difference at an end stays central.
			int k = 0;
			while (k < s.count - 2 && u > s.u[k + 1])
			{
				++k;
			}
			const double h = s.u[k + 1] - s.u[k];
			const double t = (u - s.u[k]) / h;
			const double t2 = t * t;
			const double t3 = t2 * t;
			const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
			const double h10 = t3 - 2.0 * t2 + t;
			const double h01 = -2.0 * t3 + 3.0 * t2;
			const double h11 = t3 - t2;
			outPos = add(add(scale(s.p[k], h00), scale(s.m[k], h10 * h)), add(scale(s.p[k + 1], h01), scale(s.m[k + 1], h11 * h)));
			const double d00 = (6.0 * t2 - 6.0 * t) / h;
			const double d10 = 3.0 * t2 - 4.0 * t + 1.0;
			const double d01 = (-6.0 * t2 + 6.0 * t) / h;
			const double d11 = 3.0 * t2 - 2.0 * t;
			outDerivative = add(add(scale(s.p[k], d00), scale(s.m[k], d10)), add(scale(s.p[k + 1], d01), scale(s.m[k + 1], d11)));
		}

		// ---- parallel transport ----

		// The normals carried along a spline at FrameSamples steps. The normal at a u is read back by interpolation and made
		// perpendicular to the exact tangent, so the frame is smooth and no state outlives the evaluation.
		struct FrameTable
		{
			Vec3 n[FrameSamples + 1];
		};

		void buildFrameTable(const Spline& spline, const Vec3& up0, FrameTable& table)
		{
			Vec3 pos;
			Vec3 d;
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

		// The frame's tangent and normal at u (u in the spline's own range).
		void frameAt(const Spline& spline, const FrameTable& table, double u, Vec3& outTangent, Vec3& outNormal)
		{
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
		};

		void makeBase(const Setup& setup, const Params& params, const Mat4& startWorld, const Mat4& endWorld, Base& base)
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
				dir0 = normalized(lerp(normalized(row(startWorld, 0), aim), aim, clamp01(params.startAimBlend)), aim);
				dir1 = normalized(lerp(normalized(row(endWorld, 0), aim), aim, clamp01(params.endAimBlend)), aim);
			}

			const double handle = chordLength * std::max(0.0, params.bendRoundness);
			base.spline.count = 2;
			base.spline.u[0] = 0.0;
			base.spline.u[1] = 1.0;
			base.spline.p[0] = p0;
			base.spline.p[1] = p1;
			base.spline.m[0] = scale(dir0, handle);
			base.spline.m[1] = scale(dir1, handle);

			Vec3 pos;
			Vec3 d;
			evalSpline(base.spline, 0.0, pos, d);
			const Vec3 up0 = rotateByArc(setup.restUp, setup.restTangent, normalized(d, setup.restTangent));
			buildFrameTable(base.spline, up0, base.table);
		}

		// Start and end as given, the interior controls on the base curve at their parameters.
		void evaluateControlsInternal(const Setup& setup, const Params& params, const Input& input, const EndTwists& twists, Output& output, Base& base)
		{
			const int last = setup.controlCount - 1;
			output.controlOPM[0] = input.startOPM;
			output.controlOPM[last] = input.endOPM;
			output.controlWorld[0] = multiply(input.local[0], input.startOPM);
			output.controlWorld[last] = multiply(input.local[last], input.endOPM);
			makeBase(setup, params, output.controlWorld[0], output.controlWorld[last], base);
			for (int j = 1; j < last; ++j)
			{
				output.controlOPM[j] = multiply(setup.ikOffset[j], frameMatrix(base.spline, base.table, setup.knotU[j], twists.at(setup.knotU[j])));
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
			double twist[MaxControls]{};   // each control's total twist: the ends' ramp at its parameter plus its own
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
				return twist[k] + (twist[k + 1] - twist[k]) * s;
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
			spline.m[count - 1] = base.spline.m[1];
			for (int j = 1; j < count - 1; ++j)
			{
				const Vec3 difference = scale(sub(spline.p[j + 1], spline.p[j - 1]), 1.0 / (spline.u[j + 1] - spline.u[j - 1]));
				const Vec3 differenceDirection = normalized(difference, { 1.0, 0.0, 0.0 });
				const Vec3 direction = normalized(lerp(differenceDirection, normalized(row(output.controlWorld[j], 0), differenceDirection), kControlTangentBlend), differenceDirection);
				spline.m[j] = scale(direction, length(difference));
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
				detail.twist[j] = twists.at(setup.knotU[j]) + extractTwist(local);
				if (j + 1 < count)
				{
					liveLength += length(sub(spline.p[j + 1], spline.p[j]));
				}
			}
			detail.twist[0] = twists.start;
			detail.twist[count - 1] = twists.end;
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
		// Scale off the axes, then the swing-twist split about X: for a rotation q with x and w, the twist is 2 atan2(x, w).
		// In Maya's row layout x * 4w = m12 - m21 and 4w^2 = 1 + trace, so both come from the matrix without a quaternion.
		const Vec3 x = normalized(row(local, 0), { 1.0, 0.0, 0.0 });
		Vec3 y = row(local, 1);
		y = normalized(sub(y, scale(x, dot(y, x))), anyPerpendicular(x));
		const Vec3 z = cross(x, y);
		return 2.0 * std::atan2(y.z - z.y, 1.0 + x.x + y.y + z.z);
	}

	bool buildSetup(const Mat4* controlRest, int controlCount, const Mat4* bindRest, int bindCount, Setup& outSetup, const char*& outReason)
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

		Setup setup;
		setup.controlCount = controlCount;
		setup.bindCount = bindCount;

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
			setup.knotU[j] = j == controlCount - 1 ? 1.0 : running / total;
			running += chords[j];
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

		setup.restTangent = normalized(row(controlRest[0], 0), { 1.0, 0.0, 0.0 });
		const Vec3 y = row(controlRest[0], 1);
		setup.restUp = normalized(sub(y, scale(setup.restTangent, dot(y, setup.restTangent))), anyPerpendicular(setup.restTangent));

		// The interior controls' offsets from the base curve at rest, then the bind joints' from the detail curve with the
		// interior controls where they were built.
		const Params defaults{};
		Input input;
		input.startOPM = controlRest[0];
		input.endOPM = controlRest[controlCount - 1];
		Base base;
		makeBase(setup, defaults, controlRest[0], controlRest[controlCount - 1], base);
		for (int j = 1; j < controlCount - 1; ++j)
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
