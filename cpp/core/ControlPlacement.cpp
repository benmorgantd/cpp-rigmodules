#include "ControlPlacement.h"

#include <algorithm>
#include <cmath>

namespace ControlPlacement
{
	namespace
	{
		constexpr double kPi{ 3.14159265358979323846 };
		// Two joints closer than this have no direction between them.
		constexpr double kMinChord{ 1.0e-4 };
		// Each spline segment is sampled this many times to measure its arc length.
		constexpr int kSamplesPerSegment{ 64 };

		using VectorMath::add;
		using VectorMath::cross;
		using VectorMath::dot;
		using VectorMath::length;
		using VectorMath::makeFrame;
		using VectorMath::normalized;
		using VectorMath::origin;
		using VectorMath::row;
		using VectorMath::scale;
		using VectorMath::sub;

		// ---- the spline through the joints ----

		// A cubic Hermite spline through the joints' positions, parameterized by chord length so uneven joint spacing doesn't
		// bunch the curve. Each tangent is the difference of the neighbours over their parameter span (one-sided at the ends),
		// so a straight chain is exactly straight at constant speed.
		struct Path
		{
			std::vector<Vec3> p;
			std::vector<Vec3> m;       // dP/dt at each joint
			std::vector<double> t;     // each joint's parameter: the chord length up to it
			std::vector<double> arc;   // the measured arc length at the start of each sample, kSamplesPerSegment per segment, then the total

			Vec3 at(int segment, double tau) const
			{
				const double h = t[segment + 1] - t[segment];
				const double tau2 = tau * tau;
				const double tau3 = tau2 * tau;
				const double h00 = 2.0 * tau3 - 3.0 * tau2 + 1.0;
				const double h10 = tau3 - 2.0 * tau2 + tau;
				const double h01 = -2.0 * tau3 + 3.0 * tau2;
				const double h11 = tau3 - tau2;
				return add(add(scale(p[segment], h00), scale(m[segment], h10 * h)), add(scale(p[segment + 1], h01), scale(m[segment + 1], h11 * h)));
			}
		};

		Path makePath(const std::vector<Mat4>& joints)
		{
			Path path;
			const int count = static_cast<int>(joints.size());
			path.p.resize(count);
			path.m.resize(count);
			path.t.resize(count);
			for (int i = 0; i < count; ++i)
			{
				path.p[i] = origin(joints[i]);
				path.t[i] = i == 0 ? 0.0 : path.t[i - 1] + std::max(length(sub(path.p[i], path.p[i - 1])), kMinChord);
			}
			for (int i = 0; i < count; ++i)
			{
				const int a = std::max(i - 1, 0);
				const int b = std::min(i + 1, count - 1);
				path.m[i] = scale(sub(path.p[b], path.p[a]), 1.0 / (path.t[b] - path.t[a]));
			}

			path.arc.reserve((count - 1) * kSamplesPerSegment + 1);
			double total = 0.0;
			for (int segment = 0; segment + 1 < count; ++segment)
			{
				Vec3 previous = path.p[segment];
				for (int k = 0; k < kSamplesPerSegment; ++k)
				{
					path.arc.push_back(total);
					const Vec3 next = path.at(segment, static_cast<double>(k + 1) / kSamplesPerSegment);
					total += length(sub(next, previous));
					previous = next;
				}
			}
			path.arc.push_back(total);
			return path;
		}

		// ---- rotations ----

		struct Quat
		{
			double w{ 1.0 };
			double x{ 0.0 };
			double y{ 0.0 };
			double z{ 0.0 };
		};

		// The rotation of a matrix's axes (scale and the origin ignored). Shepperd's method on the matrix whose columns are the
		// axes, which is the transpose of the rows Maya stores.
		Quat quatFromAxes(const Mat4& matrix)
		{
			const Vec3 x = normalized(row(matrix, 0), { 1.0, 0.0, 0.0 });
			const Vec3 z0 = normalized(row(matrix, 2), { 0.0, 0.0, 1.0 });
			const Vec3 y = normalized(cross(z0, x), { 0.0, 1.0, 0.0 });
			const Vec3 z = cross(x, y);
			const double r[3][3]{ { x.x, y.x, z.x }, { x.y, y.y, z.y }, { x.z, y.z, z.z } };
			Quat q;
			const double trace = r[0][0] + r[1][1] + r[2][2];
			if (trace > 0.0)
			{
				const double s = 2.0 * std::sqrt(trace + 1.0);
				q = { 0.25 * s, (r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s };
			}
			else if (r[0][0] > r[1][1] && r[0][0] > r[2][2])
			{
				const double s = 2.0 * std::sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]);
				q = { (r[2][1] - r[1][2]) / s, 0.25 * s, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s };
			}
			else if (r[1][1] > r[2][2])
			{
				const double s = 2.0 * std::sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]);
				q = { (r[0][2] - r[2][0]) / s, (r[0][1] + r[1][0]) / s, 0.25 * s, (r[1][2] + r[2][1]) / s };
			}
			else
			{
				const double s = 2.0 * std::sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]);
				q = { (r[1][0] - r[0][1]) / s, (r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, 0.25 * s };
			}
			return q;
		}

		Quat slerp(const Quat& a, Quat b, double t)
		{
			double cosine = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
			if (cosine < 0.0)
			{
				// The long way round is the same rotation: take the short one.
				b = { -b.w, -b.x, -b.y, -b.z };
				cosine = -cosine;
			}
			double wa = 1.0 - t;
			double wb = t;
			if (cosine < 0.9995)
			{
				const double angle = std::acos(cosine);
				const double sine = std::sin(angle);
				wa = std::sin((1.0 - t) * angle) / sine;
				wb = std::sin(t * angle) / sine;
			}
			Quat q{ wa * a.w + wb * b.w, wa * a.x + wb * b.x, wa * a.y + wb * b.y, wa * a.z + wb * b.z };
			const double norm = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
			return { q.w / norm, q.x / norm, q.y / norm, q.z / norm };
		}

		// The axes of a rotation as a frame at `position`.
		Mat4 frameFromQuat(const Quat& q, const Vec3& position)
		{
			const Vec3 x{ 1.0 - 2.0 * (q.y * q.y + q.z * q.z), 2.0 * (q.x * q.y + q.w * q.z), 2.0 * (q.x * q.z - q.w * q.y) };
			const Vec3 y{ 2.0 * (q.x * q.y - q.w * q.z), 1.0 - 2.0 * (q.x * q.x + q.z * q.z), 2.0 * (q.y * q.z + q.w * q.x) };
			const Vec3 z{ 2.0 * (q.x * q.z + q.w * q.y), 2.0 * (q.y * q.z - q.w * q.x), 1.0 - 2.0 * (q.x * q.x + q.y * q.y) };
			return makeFrame(x, y, z, position);
		}
	}

	ChainCheck checkChain(const std::vector<Mat4>& joints)
	{
		ChainCheck check;
		const int count = static_cast<int>(joints.size());
		if (count < 2)
		{
			check.problem = ChainProblem::TooFewJoints;
			return check;
		}

		std::vector<double> cosines(count);
		for (int i = 0; i < count; ++i)
		{
			const int a = std::min(i, count - 2);
			const Vec3 chord = sub(origin(joints[a + 1]), origin(joints[a]));
			if (length(chord) < kMinChord)
			{
				check.problem = ChainProblem::JointsOnTopOfEachOther;
				check.jointIndex = a + 1;
				return check;
			}
			cosines[i] = dot(normalized(row(joints[i], 0), { 1.0, 0.0, 0.0 }), normalized(chord, { 1.0, 0.0, 0.0 }));
		}

		const double limit = std::cos(MaxAxisAngleDegrees * kPi / 180.0);
		for (int i = 0; i < count; ++i)
		{
			if (std::fabs(cosines[i]) < limit)
			{
				check.problem = ChainProblem::AxisAcrossChain;
				check.jointIndex = i;
				check.angleDegrees = std::acos(std::min(1.0, std::fabs(cosines[i]))) * 180.0 / kPi;
				return check;
			}
		}

		check.sign = cosines[0] < 0.0 ? -1.0 : 1.0;
		for (int i = 1; i < count; ++i)
		{
			if ((cosines[i] < 0.0 ? -1.0 : 1.0) != check.sign)
			{
				check.problem = ChainProblem::AxisDirectionDisagrees;
				check.jointIndex = i;
				return check;
			}
		}
		return check;
	}

	std::string describeChainProblem(const ChainCheck& check, const std::vector<std::string>& jointNames)
	{
		const auto name = [&jointNames](int index)
		{
			return index >= 0 && index < static_cast<int>(jointNames.size()) ? jointNames[index] : std::string("?");
		};
		switch (check.problem)
		{
		case ChainProblem::None:
			return "";
		case ChainProblem::TooFewJoints:
			return "a chain needs at least two joints";
		case ChainProblem::JointsOnTopOfEachOther:
			return "joint " + name(check.jointIndex) + " sits on the joint before it, so the chain has no direction there";
		case ChainProblem::AxisAcrossChain:
			return "joint " + name(check.jointIndex) + " has its X axis " + std::to_string(static_cast<int>(std::lround(check.angleDegrees)))
				+ " degrees off the chain: a joint's X axis must run along the chain, within "
				+ std::to_string(static_cast<int>(MaxAxisAngleDegrees)) + " degrees";
		case ChainProblem::AxisDirectionDisagrees:
			return "joint " + name(check.jointIndex) + " has its X axis running the other way along the chain from " + name(0)
				+ ": every joint's X axis must run toward the tip, or every one must run back";
		}
		return "";
	}

	std::vector<Mat4> placeAlongChain(const std::vector<Mat4>& joints, int count)
	{
		if (joints.size() < 2 || count < 2)
		{
			return {};
		}
		const Path path = makePath(joints);
		const int segments = static_cast<int>(joints.size()) - 1;
		const int samples = segments * kSamplesPerSegment;
		const double total = path.arc.back();

		std::vector<Quat> rotations;
		rotations.reserve(joints.size());
		for (const Mat4& joint : joints)
		{
			rotations.push_back(quatFromAxes(joint));
		}

		std::vector<Mat4> frames;
		frames.reserve(count);
		int sample = 0;
		for (int k = 0; k < count; ++k)
		{
			int segment = segments - 1;
			double tau = 1.0;
			if (k + 1 < count)
			{
				const double target = total * k / (count - 1);
				while (sample + 1 < samples && path.arc[sample + 1] <= target)
				{
					++sample;
				}
				const double span = path.arc[sample + 1] - path.arc[sample];
				const double along = span > 0.0 ? (target - path.arc[sample]) / span : 0.0;
				segment = sample / kSamplesPerSegment;
				tau = (static_cast<double>(sample % kSamplesPerSegment) + along) / kSamplesPerSegment;
			}
			frames.push_back(frameFromQuat(slerp(rotations[segment], rotations[segment + 1], tau), path.at(segment, tau)));
		}
		return frames;
	}

	LimbPlacement placeLimb(const Mat4& root, const Mat4& mid, const Mat4& end, const std::optional<Vec3>& poleOffset, const std::optional<Vec3>& settingsOffset)
	{
		const Vec3 pRoot = origin(root);
		const Vec3 pMid = origin(mid);
		const Vec3 pEnd = origin(end);
		const double limbLength = length(sub(pMid, pRoot)) + length(sub(pEnd, pMid));

		LimbPlacement placement;
		if (poleOffset)
		{
			placement.pole = add(pMid, *poleOffset);
		}
		else
		{
			const Vec3 axis = normalized(sub(pEnd, pRoot), { 1.0, 0.0, 0.0 });
			const Vec3 fromRoot = sub(pMid, pRoot);
			const Vec3 bend = sub(fromRoot, scale(axis, dot(fromRoot, axis)));
			const Vec3 direction = length(bend) > StraightLimbTolerance * limbLength
				? normalized(bend, { 0.0, 0.0, 1.0 })
				: scale(normalized(row(root, 1), { 0.0, 1.0, 0.0 }), -1.0);
			placement.pole = add(pMid, scale(direction, limbLength));
		}
		placement.settings = add(pEnd, settingsOffset ? *settingsOffset : Vec3{ 0.0, DefaultSettingsRise * limbLength, 0.0 });
		return placement;
	}
}
