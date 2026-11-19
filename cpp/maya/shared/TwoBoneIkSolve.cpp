#include "maya/shared/TwoBoneIkSolve.h"
#include "core/Constants.h"
#include "core/ControlPlacement.h"
#include "core/NamingConvention.h"
#include "core/StringUtils.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include <maya/MTransformationMatrix.h>

namespace
{
	// A frame with +X toward vTarget and +Z along the bend normal, the way the solve's positions orient a bone (section 4.3).
	MMatrix makeBoneFrame(const MVector& vOrigin, const MVector& vTarget, const MVector& vNormal)
	{
		const MVector vX = (vTarget - vOrigin).normal();
		const MVector vZ = TwoBoneIkSolve::perpendicularPart(vNormal, vX).normal();
		return TwoBoneIkSolve::makeFrame(vX, vZ ^ vX, vZ, vOrigin);
	}
}

MVector TwoBoneIkSolve::perpendicularPart(const MVector& v, const MVector& vAxis)
{
	return v - vAxis * (v * vAxis);
}

MMatrix TwoBoneIkSolve::makeFrame(const MVector& vX, const MVector& vY, const MVector& vZ, const MVector& vOrigin)
{
	const double mData[4][4] =
	{
		{ vX.x, vX.y, vX.z, 0.0 },
		{ vY.x, vY.y, vY.z, 0.0 },
		{ vZ.x, vZ.y, vZ.z, 0.0 },
		{ vOrigin.x, vOrigin.y, vOrigin.z, 1.0 }
	};
	return MMatrix(mData);
}

MMatrix TwoBoneIkSolve::orthonormalFrame(const MMatrix& mMatrix, const MVector& vOrigin)
{
	const MVector vX = MVector(mMatrix[0][0], mMatrix[0][1], mMatrix[0][2]).normal();
	const MVector vZ = (vX ^ MVector(mMatrix[1][0], mMatrix[1][1], mMatrix[1][2])).normal();
	return makeFrame(vX, vZ ^ vX, vZ, vOrigin);
}

void TwoBoneIkSolve::computeLimbFrames(const TwoBoneIkResult& solve, const MVector& vRoot, const MMatrix& mIkWorld, MMatrix outFrames[TwoBoneIkSlot::LimbCount])
{
	outFrames[TwoBoneIkSlot::Upper] = makeBoneFrame(vRoot, solve.mid, solve.normal);
	outFrames[TwoBoneIkSlot::Lower] = makeBoneFrame(solve.mid, solve.end, solve.normal);
	outFrames[TwoBoneIkSlot::End] = orthonormalFrame(mIkWorld, solve.end);
}

void TwoBoneIkSolve::evaluate(const TwoBoneIkEval& eval, TwoBoneIkSolveResult& outResult)
{
	// 1. FK chain. Always solved, even at blend 1: the controls' offsets stay current, and the IK root follows the start control.
	MMatrix mFkWorld[TwoBoneIkSlot::LimbCount];
	MMatrix mParentDelta = eval.mUpperDelta;
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::LimbCount; ++slot)
	{
		const ControlSolveStep step = RigModuleNodeBase::solveControlStep(eval.mLocal[slot], eval.mRest[slot], mParentDelta);
		outResult.mControlOPM[slot] = step.mOPM;
		mFkWorld[slot] = step.mWorld;
		if (slot + 1 < TwoBoneIkSlot::LimbCount)
		{
			mParentDelta = MathUtils::computeDeltaFromRest(eval.mRest[slot], step.mWorld);
		}
	}

	// 2. The IK handle, then the pole, which reads the handle's solved world in Ik space (slot order matters, section 4.4).
	const ControlSolveStep ikStep = RigModuleNodeBase::solveControlStep(eval.mLocal[TwoBoneIkSlot::Ik], eval.mRest[TwoBoneIkSlot::Ik], eval.mIkDelta);
	const MMatrix mPoleDelta = eval.poleFollowsIk
		? MathUtils::computeDeltaFromRest(eval.mRest[TwoBoneIkSlot::Ik], ikStep.mWorld)
		: eval.mPoleParentDelta;
	const ControlSolveStep poleStep = RigModuleNodeBase::solveControlStep(eval.mLocal[TwoBoneIkSlot::Pole], eval.mRest[TwoBoneIkSlot::Pole], mPoleDelta);
	const ControlSolveStep settingsStep = RigModuleNodeBase::solveControlStep(eval.mLocal[TwoBoneIkSlot::Settings], eval.mRest[TwoBoneIkSlot::Settings], eval.mSettingsDelta);

	outResult.mControlOPM[TwoBoneIkSlot::Ik] = ikStep.mOPM;
	outResult.mControlOPM[TwoBoneIkSlot::Pole] = poleStep.mOPM;
	outResult.mControlOPM[TwoBoneIkSlot::Settings] = settingsStep.mOPM;
	outResult.mJointWorld[TwoBoneIkSlot::Ik] = ikStep.mWorld;
	outResult.mJointWorld[TwoBoneIkSlot::Pole] = poleStep.mWorld;
	outResult.mJointWorld[TwoBoneIkSlot::Settings] = settingsStep.mWorld;

	// 3. The limb joints: FK at blend 0 and IK at blend 1 exactly, the solve skipped at 0, a blend between.
	if (eval.blend <= 0.0)
	{
		for (unsigned int slot = 0; slot < TwoBoneIkSlot::LimbCount; ++slot)
		{
			outResult.mJointWorld[slot] = mFkWorld[slot];
		}
		return;
	}

	const MVector vRoot = MathUtils::translationOf(mFkWorld[TwoBoneIkSlot::Upper]);   // the start control's rotation does nothing in IK
	TwoBoneIkInput input;
	input.root = vRoot;
	input.handle = MathUtils::translationOf(ikStep.mWorld);
	input.pole = MathUtils::translationOf(poleStep.mWorld);
	input.length1 = eval.rest.length1;
	input.length2 = eval.rest.length2;
	input.stretch = eval.stretch;
	input.soft = eval.soft;
	input.pin = eval.pin;
	input.rollRadians = eval.rollRadians;
	input.restPlaneAngle = eval.rest.restPlaneAngle;
	input.restBendNormal = eval.rest.restBendNormal;
	input.restAxis = MathUtils::translationOf(eval.mRest[TwoBoneIkSlot::Ik]) - MathUtils::translationOf(eval.mRest[TwoBoneIkSlot::Upper]);

	MMatrix mFrames[TwoBoneIkSlot::LimbCount];
	computeLimbFrames(MathUtils::computeTwoBoneIk(input), vRoot, ikStep.mWorld, mFrames);
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::LimbCount; ++slot)
	{
		outResult.mJointWorld[slot] = MathUtils::blendMatrices(mFkWorld[slot], eval.rest.ikRestOffset[slot] * mFrames[slot], eval.blend);
	}
}

namespace
{
	// Rests closer than these have no bone, no end-to-handle match, or no pole plane.
	constexpr double kMinBoneLength{ 1.0e-4 };
	constexpr double kMinPoleSine{ 1.0e-4 };
	// A rest mid this near the root-to-end line is a straight limb: it has no bend direction of its own.
	constexpr double kMinRestBend{ 1.0e-4 };
}

TwoBoneIkRest TwoBoneIkSolve::computeRest(const std::array<MMatrix, TwoBoneIkSlot::Count>& restWorlds)
{
	const MVector vRoot = MathUtils::translationOf(restWorlds[TwoBoneIkSlot::Upper]);
	const MVector vMid = MathUtils::translationOf(restWorlds[TwoBoneIkSlot::Lower]);
	const MVector vEnd = MathUtils::translationOf(restWorlds[TwoBoneIkSlot::End]);
	const MVector vHandle = MathUtils::translationOf(restWorlds[TwoBoneIkSlot::Ik]);
	const MVector vPole = MathUtils::translationOf(restWorlds[TwoBoneIkSlot::Pole]);

	TwoBoneIkRest rest;
	rest.length1 = (vMid - vRoot).length();
	rest.length2 = (vEnd - vMid).length();

	const MVector vAxis = (vHandle - vRoot).normal();
	const MVector vPoleNormal = perpendicularPart(vPole - vRoot, vAxis).normal();
	const MVector vMidPart = perpendicularPart(vMid - vRoot, vAxis);
	if (vMidPart.length() > kMinRestBend)
	{
		const MVector vMidNormal = vMidPart.normal();
		rest.restBendNormal = vMidNormal;
		rest.restPlaneAngle = std::atan2(vAxis * (vPoleNormal ^ vMidNormal), vPoleNormal * vMidNormal);
	}
	else
	{
		rest.restBendNormal = vPoleNormal;
	}

	// The solve at rest, with every setting off. Its frames against the joints' rests give the offsets.
	TwoBoneIkInput input;
	input.root = vRoot;
	input.handle = vHandle;
	input.pole = vPole;
	input.length1 = rest.length1;
	input.length2 = rest.length2;
	input.restPlaneAngle = rest.restPlaneAngle;
	input.restBendNormal = rest.restBendNormal;
	input.restAxis = vAxis;
	MMatrix mFrames[TwoBoneIkSlot::LimbCount];
	computeLimbFrames(MathUtils::computeTwoBoneIk(input), vRoot, restWorlds[TwoBoneIkSlot::Ik], mFrames);
	for (unsigned int i = 0; i < TwoBoneIkSlot::LimbCount; ++i)
	{
		rest.ikRestOffset[i] = restWorlds[i] * mFrames[i].inverse();
	}
	return rest;
}

const char* TwoBoneIkSolve::childPlugNameAt(unsigned int slot)
{
	switch (slot)
	{
	case TwoBoneIkSlot::Upper:
		return PlugAndSocketNames::Start;
	case TwoBoneIkSlot::Ik:
		return PlugAndSocketNames::Ik;
	case TwoBoneIkSlot::Pole:
		return PlugAndSocketNames::Pole;
	case TwoBoneIkSlot::Settings:
		return PlugAndSocketNames::Settings;
	default:
		return nullptr;
	}
}

std::vector<std::string> TwoBoneIkSolve::socketNamesAt(unsigned int slot)
{
	switch (slot)
	{
	case TwoBoneIkSlot::Upper:
		return { std::string(PlugAndSocketNames::Index) + "0", PlugAndSocketNames::Start };
	case TwoBoneIkSlot::Lower:
		return { std::string(PlugAndSocketNames::Index) + "1" };
	case TwoBoneIkSlot::End:
		return { std::string(PlugAndSocketNames::Index) + "2", PlugAndSocketNames::End };
	case TwoBoneIkSlot::Ik:
		return { PlugAndSocketNames::Ik };
	case TwoBoneIkSlot::Pole:
		return { PlugAndSocketNames::Pole };
	default:
		return { PlugAndSocketNames::Settings };
	}
}

ModuleInterface TwoBoneIkSolve::makeInterface()
{
	ModuleInterface moduleInterface;
	moduleInterface.childPlugs.push_back(ModulePlugInfo{ PlugAndSocketNames::Start, true, true, "" });
	moduleInterface.childPlugs.push_back(ModulePlugInfo{ PlugAndSocketNames::Ik, true, true, "" });
	moduleInterface.childPlugs.push_back(ModulePlugInfo{ PlugAndSocketNames::Pole, true, false, "" });
	moduleInterface.childPlugs.push_back(ModulePlugInfo{ PlugAndSocketNames::Settings, true, false, PlugAndSocketNames::Start });
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
	{
		const std::vector<std::string> names = socketNamesAt(slot);
		moduleInterface.sockets.insert(moduleInterface.sockets.end(), names.begin(), names.end());
	}
	return moduleInterface;
}

bool TwoBoneIkSolve::planSlots(const TwoBoneIkArgs& ikArgs, PlanContext& ctx, std::array<TwoBoneIkSlotInput, TwoBoneIkSlot::Count>& slots, TwoBoneIkRest& outRest)
{
	const std::string argsPath = ctx.where + "." + TemplateKey::ModuleArgs;
	bool valid = true;
	// The channel clamps to 0..1, so a value outside it would build a rest the channel can't hold.
	if (ikArgs.ikFkBlend < 0.0 || ikArgs.ikFkBlend > 1.0)
	{
		ctx.report.addError(argsPath + "." + RigAttr::IkFkBlend, "must be between 0 (FK) and 1 (IK), got " + std::to_string(ikArgs.ikFkBlend));
		valid = false;
	}
	if (ikArgs.ikAlignToPole && !ikArgs.ikOrientToWorld)
	{
		ctx.report.addError(argsPath + ".ikAlignToPole", "turns a world-aligned handle toward the pole, so it needs ikOrientToWorld set to true as well");
		valid = false;
	}
	if (ikArgs.joints.size() != TwoBoneIkSlot::LimbCount)
	{
		ctx.report.addError(argsPath + ".joints", "a two-bone IK limb has exactly three joints (root, mid, end), got "
			+ std::to_string(ikArgs.joints.size()) + ". One IK bone is the Aim module. A longer IK chain needs a different module");
		valid = false;
	}

	// An offset is empty (the default) or three numbers. The other side of a mirrored module is the reflection across the YZ plane
	// (D28), so a world offset's X is negated there.
	const auto readOffset = [&](const std::vector<double>& values, const char* key) -> std::optional<VectorMath::Vec3>
	{
		if (values.empty())
		{
			return std::nullopt;
		}
		if (values.size() != 3)
		{
			ctx.report.addError(argsPath + "." + key, "must be [x, y, z], got " + std::to_string(values.size()) + " numbers");
			valid = false;
			return std::nullopt;
		}
		return VectorMath::Vec3{ ctx.moduleData.isMirror() ? -values[0] : values[0], values[1], values[2] };
	};
	const std::optional<VectorMath::Vec3> poleOffset = readOffset(ikArgs.poleOffset, "poleOffset");
	const std::optional<VectorMath::Vec3> settingsOffset = readOffset(ikArgs.settingsOffset, "settingsOffset");

	// claimJoint rejects a joint picked twice. Claim every selector even after a failure, so the report lists every bad one at once.
	for (std::size_t i = 0; i < ikArgs.joints.size(); ++i)
	{
		// With the wrong joint count the selectors aren't the limb, so they are claimed into a scratch input.
		TwoBoneIkSlotInput scratch;
		TwoBoneIkSlotInput& slot = ikArgs.joints.size() == TwoBoneIkSlot::LimbCount ? slots[i] : scratch;
		valid = RigModuleNodeBase::claimJoint(ctx, ikArgs.joints[i], slot.oJoint, slot.mRestWorld) && valid;
		slot.controlMarkup = makeControlMarkup(ikArgs.joints[i].side, ikArgs.joints[i].usage);
	}
	if (!valid)
	{
		return false;
	}

	// A degenerate rest pose gives the solve no bones. A straight limb is fine (mocap skeletons have them): the pole falls back to
	// the root joint's -Y, which ControlPlacement::placeLimb does.
	const MVector vRoot = MathUtils::translationOf(slots[TwoBoneIkSlot::Upper].mRestWorld);
	const MVector vMid = MathUtils::translationOf(slots[TwoBoneIkSlot::Lower].mRestWorld);
	const MVector vEnd = MathUtils::translationOf(slots[TwoBoneIkSlot::End].mRestWorld);
	if ((vMid - vRoot).length() < kMinBoneLength || (vEnd - vMid).length() < kMinBoneLength)
	{
		ctx.report.addError(argsPath + ".joints", "the limb's joints must not sit on each other: a bone has no length");
		return false;
	}

	// The derived controls (D57). The handle is at the end joint, turned the way that joint is. The pole and settings controls take
	// world axes: their rotation is locked, so this only sets how their translate channels read.
	const ControlPlacement::LimbPlacement placement = ControlPlacement::placeLimb(
		MathUtils::toMat4(slots[TwoBoneIkSlot::Upper].mRestWorld), MathUtils::toMat4(slots[TwoBoneIkSlot::Lower].mRestWorld),
		MathUtils::toMat4(slots[TwoBoneIkSlot::End].mRestWorld), poleOffset, settingsOffset, ctx.moduleData.isMirror());
	const MVector vPole(placement.pole.x, placement.pole.y, placement.pole.z);
	const MVector vSettings(placement.settings.x, placement.settings.y, placement.settings.z);

	// A pole on the root-to-end line has no bend plane. A straight limb that runs along its root's Y (a leg) lands there by default,
	// so it needs an offset.
	const MVector vToPole = vPole - vRoot;
	const MVector vToEnd = vEnd - vRoot;
	if (vToPole.length() < kMinBoneLength || (vToPole.normal() ^ vToEnd.normal()).length() < kMinPoleSine)
	{
		ctx.report.addError(argsPath + ".poleOffset", "the pole lies on the line from the root to the end, so the bend plane is undefined. "
			"Give the limb a poleOffset that moves the pole off that line");
		return false;
	}

	const MMatrix mEndRotation = MTransformationMatrix(slots[TwoBoneIkSlot::End].mRestWorld).asRotateMatrix();
	const auto frameAt = [](const MMatrix& mRotation, const MVector& vOrigin)
	{
		MMatrix mFrame = mRotation;
		mFrame[3][0] = vOrigin.x;
		mFrame[3][1] = vOrigin.y;
		mFrame[3][2] = vOrigin.z;
		return mFrame;
	};
	if (ikArgs.ikOrientToWorld)
	{
		// World-aligned (D59): on the mirrored side the pole is reflected, so a turn toward it is reflected too. The IK joints' rest
		// offsets (computeRest) absorb whichever rotation the handle has, so the bind pose is unaffected.
		const std::optional<VectorMath::Mat4> worldHandle = ControlPlacement::placeWorldIkHandle(
			{ vEnd.x, vEnd.y, vEnd.z }, placement.pole, ikArgs.ikAlignToPole);
		if (!worldHandle)
		{
			ctx.report.addError(argsPath + ".ikAlignToPole", "the pole is straight above or below the end joint, so there is no direction to turn the handle "
				"toward. Give the limb a poleOffset that moves the pole off that vertical line");
			return false;
		}
		slots[TwoBoneIkSlot::Ik].mRestWorld = MathUtils::toMMatrix(*worldHandle);
	}
	else
	{
		slots[TwoBoneIkSlot::Ik].mRestWorld = frameAt(mEndRotation, vEnd);
	}
	slots[TwoBoneIkSlot::Pole].mRestWorld = frameAt(MMatrix::identity, vPole);
	slots[TwoBoneIkSlot::Settings].mRestWorld = frameAt(MMatrix::identity, vSettings);

	// Names (D57): <base>IK, <base>Pole and <base>Settings. The base is controlName, else the first joint's usage without its
	// trailing _NN. A usage another module already uses is reported by the template's one-usage-per-rig check (D25).
	const std::string base = ikArgs.controlName.empty() ? StringUtils::stripNumberSuffix(ikArgs.joints.front().usage) : ikArgs.controlName;
	if (!NamingConvention::isValidBase(base))
	{
		ctx.report.addError(argsPath + ".controlName", "the derived controls are named from '" + base + "', which is not camelCase (a lowercase letter, "
			"then letters and digits). Set controlName to a camelCase name");
		return false;
	}
	const std::string& firstSide = ikArgs.joints.front().side;
	const std::pair<unsigned int, const char*> derived[] =
	{
		{ TwoBoneIkSlot::Ik, "IK" }, { TwoBoneIkSlot::Pole, "Pole" }, { TwoBoneIkSlot::Settings, "Settings" }
	};
	for (const auto& [slotIndex, suffix] : derived)
	{
		slots[slotIndex].baseName = base + suffix;
		slots[slotIndex].controlMarkup = makeControlMarkup(firstSide, slots[slotIndex].baseName);
	}

	std::array<MMatrix, TwoBoneIkSlot::Count> restWorlds;
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
	{
		restWorlds[slot] = slots[slot].mRestWorld;
	}
	outRest = computeRest(restWorlds);
	return true;
}
