#pragma once

#include <cmath>

// Small vector and matrix math with no Maya types, shared by the code in core/ that works on rest matrices
// (RibbonMath, ControlPlacement). Matrices are row-major with row vectors (Maya's convention): rows 0 to 2 are the X, Y and Z
// axes, row 3 the origin. Header-only and inline, so each user keeps the math it needs and nothing else.

namespace VectorMath
{
	// Below this a vector has no direction and the caller's fallback stands in for it.
	inline constexpr double kTiny{ 1.0e-12 };

	struct Vec3
	{
		double x{ 0.0 };
		double y{ 0.0 };
		double z{ 0.0 };
	};

	struct Mat4
	{
		double m[4][4]{ { 1.0, 0.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0, 0.0 }, { 0.0, 0.0, 0.0, 1.0 } };
	};

	inline Vec3 add(const Vec3& a, const Vec3& b)
	{
		return { a.x + b.x, a.y + b.y, a.z + b.z };
	}

	inline Vec3 sub(const Vec3& a, const Vec3& b)
	{
		return { a.x - b.x, a.y - b.y, a.z - b.z };
	}

	inline Vec3 scale(const Vec3& a, double s)
	{
		return { a.x * s, a.y * s, a.z * s };
	}

	inline double dot(const Vec3& a, const Vec3& b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	inline Vec3 cross(const Vec3& a, const Vec3& b)
	{
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}

	inline double length(const Vec3& a)
	{
		return std::sqrt(dot(a, a));
	}

	inline Vec3 normalized(const Vec3& a, const Vec3& fallback)
	{
		const double len = length(a);
		return len > kTiny ? scale(a, 1.0 / len) : fallback;
	}

	inline Vec3 lerp(const Vec3& a, const Vec3& b, double t)
	{
		return add(scale(a, 1.0 - t), scale(b, t));
	}

	inline Vec3 row(const Mat4& a, int r)
	{
		return { a.m[r][0], a.m[r][1], a.m[r][2] };
	}

	inline Vec3 origin(const Mat4& a)
	{
		return row(a, 3);
	}

	inline Mat4 makeFrame(const Vec3& x, const Vec3& y, const Vec3& z, const Vec3& o)
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
	inline Vec3 anyPerpendicular(const Vec3& v)
	{
		const Vec3 axis = std::fabs(v.x) < 0.9 ? Vec3{ 1.0, 0.0, 0.0 } : Vec3{ 0.0, 1.0, 0.0 };
		return normalized(cross(v, axis), { 0.0, 1.0, 0.0 });
	}

	// v turned by the shortest arc that takes unit vector `from` to unit vector `to` (Rodrigues). Antiparallel is
	// a half turn about v's own perpendicular, so v is kept as it is.
	inline Vec3 rotateByArc(const Vec3& v, const Vec3& from, const Vec3& to)
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
}
