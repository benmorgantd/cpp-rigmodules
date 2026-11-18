#include "SwingTwist.h"

#include <cmath>

namespace SwingTwist
{
	namespace
	{
		constexpr double kTiny{ 1.0e-12 };

		struct Vec3
		{
			double x;
			double y;
			double z;
		};

		Vec3 rowOf(const double (&rows)[3][3], int r)
		{
			return { rows[r][0], rows[r][1], rows[r][2] };
		}

		double dot(const Vec3& a, const Vec3& b)
		{
			return a.x * b.x + a.y * b.y + a.z * b.z;
		}

		Vec3 cross(const Vec3& a, const Vec3& b)
		{
			return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
		}

		// The unit vector along a, or `fallback` for a vector too short to have a direction.
		Vec3 normalized(const Vec3& a, const Vec3& fallback)
		{
			const double length = std::sqrt(dot(a, a));
			return length > kTiny ? Vec3{ a.x / length, a.y / length, a.z / length } : fallback;
		}

		// A unit vector perpendicular to v, for when the Y axis has nothing left after the X axis is taken out of it.
		Vec3 anyPerpendicular(const Vec3& v)
		{
			const Vec3 axis = std::fabs(v.x) < 0.9 ? Vec3{ 1.0, 0.0, 0.0 } : Vec3{ 0.0, 1.0, 0.0 };
			return normalized(cross(v, axis), { 0.0, 1.0, 0.0 });
		}
	}

	double twistAboutX(const double (&rows)[3][3])
	{
		// Scale off the axes, then the swing-twist split about X: for a rotation q with x and w, the twist is 2 atan2(x, w).
		// In Maya's row layout x * 4w = m12 - m21 and 4w^2 = 1 + trace, so both come from the matrix without a quaternion.
		const Vec3 x = normalized(rowOf(rows, 0), { 1.0, 0.0, 0.0 });
		Vec3 y = rowOf(rows, 1);
		const double alongX = dot(y, x);
		y = normalized({ y.x - x.x * alongX, y.y - x.y * alongX, y.z - x.z * alongX }, anyPerpendicular(x));
		const Vec3 z = cross(x, y);
		return 2.0 * std::atan2(y.z - z.y, 1.0 + x.x + y.y + z.z);
	}

	void rotationAboutX(double angle, double (&outRows)[3][3])
	{
		const double c = std::cos(angle);
		const double s = std::sin(angle);
		const double rows[3][3]{ { 1.0, 0.0, 0.0 }, { 0.0, c, s }, { 0.0, -s, c } };
		for (int r = 0; r < 3; ++r)
		{
			for (int k = 0; k < 3; ++k)
			{
				outRows[r][k] = rows[r][k];
			}
		}
	}
}
