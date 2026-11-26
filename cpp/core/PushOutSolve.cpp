#include "PushOutSolve.h"
#include "ControlShapes.h"

#include <algorithm>
#include <cmath>

namespace PushOutSolve
{
	using VectorMath::Mat4;
	using VectorMath::Vec3;

	bool validate(const Capsule& capsule, const Settings& settings, std::string& outError)
	{
		if (!std::isfinite(capsule.radius) || !(capsule.radius > 0.0))
		{
			outError = "radius must be above 0";
			return false;
		}
		if (!std::isfinite(capsule.halfLength) || capsule.halfLength < 0.0)
		{
			outError = "halfLength must be 0 or more";
			return false;
		}
		if (!std::isfinite(settings.margin) || settings.margin < 0.0)
		{
			outError = "margin must be 0 or more";
			return false;
		}
		if (!std::isfinite(settings.maxPush) || !(settings.maxPush > 0.0))
		{
			outError = "maxPush must be above 0";
			return false;
		}
		if (!std::isfinite(settings.weight) || settings.weight < 0.0 || settings.weight > 1.0)
		{
			outError = "weight must be between 0 and 1";
			return false;
		}
		return true;
	}

	Result solve(const Capsule& capsule, const Mat4& capsuleWorld, const Vec3& pointWorld, const Settings& settings)
	{
		// Unit axes, so a joint's scale does not resize the capsule.
		const Vec3 axisX = VectorMath::normalized(VectorMath::row(capsuleWorld, 0), { 1.0, 0.0, 0.0 });
		const Vec3 axisZ = VectorMath::normalized(VectorMath::row(capsuleWorld, 2), { 0.0, 0.0, 1.0 });
		const Vec3 origin = VectorMath::origin(capsuleWorld);

		const double along = std::clamp(VectorMath::dot(VectorMath::sub(pointWorld, origin), axisX), -capsule.halfLength, capsule.halfLength);
		const Vec3 nearest = VectorMath::add(origin, VectorMath::scale(axisX, along));
		const Vec3 away = VectorMath::sub(pointWorld, nearest);
		const double distance = VectorMath::length(away);

		const double reach = capsule.radius + settings.margin;
		Result result;
		if (!(distance < reach))
		{
			return result;
		}

		const Vec3 direction = VectorMath::normalized(away, axisZ);
		const double depth = std::min(reach - distance, settings.maxPush);
		result.push = VectorMath::scale(direction, depth * settings.weight);
		result.isPushed = true;
		return result;
	}

	Mat4 offsetMatrix(const Vec3& translate, const Vec3& rotateDegrees)
	{
		const double ax = VectorMath::toRadians(rotateDegrees.x);
		const double ay = VectorMath::toRadians(rotateDegrees.y);
		const double az = VectorMath::toRadians(rotateDegrees.z);
		const double cx = std::cos(ax);
		const double sx = std::sin(ax);
		const double cy = std::cos(ay);
		const double sy = std::sin(ay);
		const double cz = std::cos(az);
		const double sz = std::sin(az);

		// Row vectors: each row is where that axis goes. Rx * Ry * Rz turns about X first.
		const Mat4 rx = VectorMath::makeFrame({ 1.0, 0.0, 0.0 }, { 0.0, cx, sx }, { 0.0, -sx, cx }, {});
		const Mat4 ry = VectorMath::makeFrame({ cy, 0.0, -sy }, { 0.0, 1.0, 0.0 }, { sy, 0.0, cy }, {});
		const Mat4 rz = VectorMath::makeFrame({ cz, sz, 0.0 }, { -sz, cz, 0.0 }, { 0.0, 0.0, 1.0 }, {});

		const auto multiply = [](const Mat4& a, const Mat4& b)
		{
			Mat4 out;
			for (int r = 0; r < 4; ++r)
			{
				for (int c = 0; c < 4; ++c)
				{
					out.m[r][c] = a.m[r][0] * b.m[0][c] + a.m[r][1] * b.m[1][c] + a.m[r][2] * b.m[2][c] + a.m[r][3] * b.m[3][c];
				}
			}
			return out;
		};
		Mat4 result = multiply(multiply(rx, ry), rz);
		result.m[3][0] = translate.x;
		result.m[3][1] = translate.y;
		result.m[3][2] = translate.z;
		return result;
	}

	Mat4 reflectAcrossX(const Mat4& world)
	{
		// S * M * S with S = diag(-1, 1, 1, 1): an entry changes sign when exactly one of its row and column is X.
		Mat4 out = world;
		for (int r = 0; r < 4; ++r)
		{
			for (int c = 0; c < 4; ++c)
			{
				const bool isRowX = (r == 0);
				const bool isColX = (c == 0);
				if (isRowX != isColX)
				{
					out.m[r][c] = -world.m[r][c];
				}
			}
		}
		return out;
	}

	Vec3 drawPoint(const ShapePoint& meshPoint, double radius, double halfLength)
	{
		const double sign = meshPoint.x < 0.0 ? -1.0 : 1.0;
		const double beyondSeam = std::max(std::fabs(meshPoint.x) - 0.5, 0.0);
		const double along = std::min(std::fabs(meshPoint.x), 0.5) * 2.0 * halfLength;
		return { sign * (along + beyondSeam * 2.0 * radius), meshPoint.y * radius, meshPoint.z * radius };
	}
}
