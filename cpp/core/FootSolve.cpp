#include "FootSolve.h"
#include "RibbonMath.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace FootSolve
{
	namespace
	{
		struct RoleName
		{
			Role role;
			const char* name;
		};

		// The one table of roles. The order is only the order they are listed in messages.
		constexpr RoleName kRoles[]
		{
			{ Role::Heel, "heel" },
			{ Role::Ball, "ball" },
			{ Role::BankOut, "bankOut" },
			{ Role::BankIn, "bankIn" },
			{ Role::ToeTip, "toeTip" },
			{ Role::Toe, "toe" },
		};

		// The turns of one pivot, innermost first: roll about forward, then pitch about lateral, then swivel about up, the same
		// order decomposeRotation reads them back in.
		Mat4 pivotMotion(const Pivot& pivot, bool isBankPivot, const Frame& frame, const Channels& channels, const Peel& peel)
		{
			double pitch = 0.0;
			double swivel = 0.0;
			double roll = 0.0;
			switch (pivot.role)
			{
				case Role::Heel:
					pitch = -(peel.heel + channels.heelRaise);
					swivel = channels.heelSwivel;
					break;
				case Role::Ball:
					pitch = peel.ball + channels.ballRaise;
					swivel = channels.ballSwivel;
					roll = -channels.ballRoll;
					break;
				case Role::BankOut:
				case Role::BankIn:
					roll = isBankPivot ? -channels.bank : 0.0;
					break;
				case Role::ToeTip:
					pitch = peel.toeTip + channels.toeRaise;
					swivel = channels.toeSwivel;
					break;
				case Role::Toe:
					break;
			}
			Mat4 motion = rotationAbout(pivot.position, frame.forward, roll);
			motion = RibbonMath::multiply(motion, rotationAbout(pivot.position, frame.lateral, pitch));
			return RibbonMath::multiply(motion, rotationAbout(pivot.position, frame.up, swivel));
		}
	}

	const char* roleName(Role role)
	{
		for (const RoleName& entry : kRoles)
		{
			if (entry.role == role)
			{
				return entry.name;
			}
		}
		return "";
	}

	bool findRole(const std::string& usage, Role& outRole)
	{
		for (const RoleName& entry : kRoles)
		{
			if (usage == entry.name)
			{
				outRole = entry.role;
				return true;
			}
		}
		return false;
	}

	std::string allowedRoles()
	{
		std::string text;
		for (const RoleName& entry : kRoles)
		{
			text += (text.empty() ? "" : ", ") + std::string(entry.name);
		}
		return text;
	}

	std::vector<std::string> checkRoles(const std::vector<Role>& roles)
	{
		std::vector<std::string> problems;
		int counts[std::size(kRoles)]{};
		for (const Role role : roles)
		{
			if (++counts[static_cast<std::size_t>(role)] == 2)
			{
				problems.push_back(std::string("the role ") + roleName(role) + " is listed twice");
			}
		}
		if (counts[static_cast<std::size_t>(Role::BankOut)] > 0 && counts[static_cast<std::size_t>(Role::BankIn)] == 0)
		{
			problems.push_back("bankOut needs a bankIn: a bank turns about the pivot on the side it tips toward");
		}
		if (counts[static_cast<std::size_t>(Role::BankIn)] > 0 && counts[static_cast<std::size_t>(Role::BankOut)] == 0)
		{
			problems.push_back("bankIn needs a bankOut: a bank turns about the pivot on the side it tips toward");
		}
		return problems;
	}

	int findPivot(const std::vector<Pivot>& chain, Role role)
	{
		for (std::size_t i = 0; i < chain.size(); ++i)
		{
			if (chain[i].role == role)
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	Frame footFrame(const std::vector<Pivot>& chain)
	{
		Frame frame;
		if (chain.size() < 2)
		{
			return frame;
		}
		const int heel = findPivot(chain, Role::Heel);
		const int tip = findPivot(chain, Role::ToeTip);
		const Vec3 from = chain[heel >= 0 ? static_cast<std::size_t>(heel) : 0].position;
		const Vec3 to = chain[tip >= 0 ? static_cast<std::size_t>(tip) : chain.size() - 1].position;
		Vec3 along = VectorMath::sub(to, from);
		along.y = 0.0;
		frame.forward = VectorMath::normalized(along, frame.forward);
		frame.lateral = VectorMath::cross(frame.up, frame.forward);
		return frame;
	}

	Peel splitPeel(double footRoll, double breakAngle)
	{
		const double breakAt = std::max(breakAngle, 0.0);
		Peel peel;
		peel.heel = std::max(-footRoll, 0.0);
		peel.ball = std::clamp(footRoll, 0.0, breakAt);
		peel.toeTip = std::max(footRoll - breakAt, 0.0);
		return peel;
	}

	int bankPivotIndex(const std::vector<Pivot>& chain, const Frame& frame, double bank)
	{
		if (bank == 0.0)
		{
			return -1;
		}
		int best = -1;
		double bestSide = 0.0;
		for (std::size_t i = 0; i < chain.size(); ++i)
		{
			if (chain[i].role != Role::BankOut && chain[i].role != Role::BankIn)
			{
				continue;
			}
			// A positive bank turns about the pivot with the most lateral position, a negative one about the least.
			const double side = VectorMath::dot(chain[i].position, frame.lateral) * (bank > 0.0 ? 1.0 : -1.0);
			if (best < 0 || side > bestSide)
			{
				best = static_cast<int>(i);
				bestSide = side;
			}
		}
		return best;
	}

	Mat4 rotationAbout(const Vec3& point, const Vec3& axis, double angleDegrees)
	{
		const Vec3 unitAxis = VectorMath::normalized(axis, { 0.0, 1.0, 0.0 });
		if (angleDegrees == 0.0)
		{
			return Mat4();
		}
		const double angle = VectorMath::toRadians(angleDegrees);
		const Vec3 x = VectorMath::rotateAboutAxis({ 1.0, 0.0, 0.0 }, unitAxis, angle);
		const Vec3 y = VectorMath::rotateAboutAxis({ 0.0, 1.0, 0.0 }, unitAxis, angle);
		const Vec3 z = VectorMath::rotateAboutAxis({ 0.0, 0.0, 1.0 }, unitAxis, angle);
		// p' = point + R(p - point), so the translation row is point - R(point).
		const Vec3 turned = VectorMath::rotateAboutAxis(point, unitAxis, angle);
		return VectorMath::makeFrame(x, y, z, VectorMath::sub(point, turned));
	}

	Vec3 transformPoint(const Mat4& motion, const Vec3& point)
	{
		Vec3 result;
		result.x = point.x * motion.m[0][0] + point.y * motion.m[1][0] + point.z * motion.m[2][0] + motion.m[3][0];
		result.y = point.x * motion.m[0][1] + point.y * motion.m[1][1] + point.z * motion.m[2][1] + motion.m[3][1];
		result.z = point.x * motion.m[0][2] + point.y * motion.m[1][2] + point.z * motion.m[2][2] + motion.m[3][2];
		return result;
	}

	Mat4 foldChain(const std::vector<Pivot>& chain, std::size_t count, const Frame& frame, const Channels& channels)
	{
		const Peel peel = splitPeel(channels.footRoll, channels.breakAngle);
		const int bankIndex = bankPivotIndex(chain, frame, channels.bank);
		Mat4 motion;
		// Ankle' = Rot_0(Rot_1(... Rot_n(ankle))): the last pivot moves a point first, so each earlier pivot goes on the left.
		for (std::size_t i = 0; i < count && i < chain.size(); ++i)
		{
			motion = RibbonMath::multiply(pivotMotion(chain[i], static_cast<int>(i) == bankIndex, frame, channels, peel), motion);
		}
		return motion;
	}

	Mat4 ankleMotion(const std::vector<Pivot>& chain, const Frame& frame, const Channels& channels)
	{
		return foldChain(chain, chain.size(), frame, channels);
	}

	Mat4 toeMotion(const std::vector<Pivot>& chain, std::size_t toeIndex, const Frame& frame, const Channels& channels)
	{
		if (toeIndex >= chain.size())
		{
			return Mat4();
		}
		// The toe bone is the toe pivot's child: it turns by toeTap about the toe pivot first, and the pivots before the toe carry it.
		const Mat4 tap = rotationAbout(chain[toeIndex].position, frame.lateral, channels.toeTap);
		return RibbonMath::multiply(tap, foldChain(chain, toeIndex, frame, channels));
	}

	Euler decomposeRotation(const Mat4& motion, const Frame& frame)
	{
		// The rotation in the foot's own axes: Ml = B * R * B^T with B's rows lateral, up, forward.
		const Vec3 axes[3]{ frame.lateral, frame.up, frame.forward };
		double basis[3][3]{};
		for (int a = 0; a < 3; ++a)
		{
			basis[a][0] = axes[a].x;
			basis[a][1] = axes[a].y;
			basis[a][2] = axes[a].z;
		}
		double local[3][3]{};
		for (int r = 0; r < 3; ++r)
		{
			for (int c = 0; c < 3; ++c)
			{
				double sum = 0.0;
				for (int i = 0; i < 3; ++i)
				{
					for (int j = 0; j < 3; ++j)
					{
						sum += basis[r][i] * motion.m[i][j] * basis[c][j];
					}
				}
				local[r][c] = sum;
			}
		}
		// Rot_up(yaw) * Rot_lateral(pitch) * Rot_forward(roll) read off as a Y X Z Euler decomposition.
		Euler euler;
		euler.pitch = VectorMath::toDegrees(std::asin(std::clamp(-local[2][1], -1.0, 1.0)));
		euler.yaw = VectorMath::toDegrees(std::atan2(local[2][0], local[2][2]));
		euler.roll = VectorMath::toDegrees(std::atan2(local[0][1], local[1][1]));
		return euler;
	}
}
