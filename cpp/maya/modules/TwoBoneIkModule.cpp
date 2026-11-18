#include "maya/modules/TwoBoneIkModule.h"
#include "core/Constants.h"
#include "core/ControlPlacement.h"
#include "core/JointMarkup.h"
#include "core/JsonUtils.h"
#include "core/NamingConvention.h"
#include "core/RigJsonStructs.h"
#include "core/StringUtils.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/Side.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <maya/MAngle.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnStringData.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MPlug.h>
#include <maya/MPoint.h>
#include <maya/MProfiler.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MTypeId.h>
#include <maya/MVector.h>

MTypeId TwoBoneIkNode::id(0x00218F);
MObject TwoBoneIkNode::aIkFkBlend;
MObject TwoBoneIkNode::aStretch;
MObject TwoBoneIkNode::aSoft;
MObject TwoBoneIkNode::aPin;
MObject TwoBoneIkNode::aRoll;
MObject TwoBoneIkNode::aPoleSpace;
MObject TwoBoneIkNode::aIkRestOffset;
MObject TwoBoneIkNode::aRestBendNormal;
MObject TwoBoneIkNode::aRestPlaneAngle;
MObject TwoBoneIkNode::aLimbLengths;
MObject TwoBoneIkNode::aFkVisible;
MObject TwoBoneIkNode::aIkVisible;
MObject TwoBoneIkNode::aFkMatchLocal;
MObject TwoBoneIkNode::aIkMatchLocal;
MObject TwoBoneIkNode::aMatchWarnings;

namespace
{
	// Rests closer than these have no bone, no end-to-handle match, or no pole plane.
	constexpr double kMinBoneLength{ 1.0e-4 };
	constexpr double kMinPoleSine{ 1.0e-4 };
	// A rest mid this near the root-to-end line is a straight limb: it has no bend direction of its own.
	constexpr double kMinRestBend{ 1.0e-4 };
	// The IK solve of a restored pose may miss the joints by this fraction of the limb's reach before the restore warns.
	constexpr double kMaxIkMismatch{ 1.0e-3 };

	// Version 2 (D57): the IK handle, pole and settings controls come from the limb joints, so the three marker joints ("ik", "pole",
	// "settings") are gone. The step has no scene, so it cannot turn their positions into offsets: an old template builds with the
	// derived places. Anything else is left for the validation that follows.
	void dropMarkerJoints(nlohmann::json& args)
	{
		if (!args.is_object())
		{
			return;
		}
		for (const char* key : { "ik", "pole", "settings" })
		{
			args.erase(key);
		}
	}

	// The child plug each slot owns, or nullptr for the slots with none (the FK lower and end controls).
	const char* childPlugNameAt(unsigned int slot)
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

	// Every name of the socket on a slot. index_0 to index_2 (and start and end, which are the same sockets) carry the blended joint worlds.
	std::vector<std::string> socketNamesAt(unsigned int slot)
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

	// Mirrors createModule: every child plug has a control, the pole and settings plugs take no constraint, and the
	// settings plug follows start's parent (D52 decision 14).
	ModuleInterface makeTwoBoneIkInterface()
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

	// The component of v perpendicular to the unit vector axis.
	MVector perpendicularPart(const MVector& v, const MVector& vAxis)
	{
		return v - vAxis * (v * vAxis);
	}

	MMatrix makeFrame(const MVector& vX, const MVector& vY, const MVector& vZ, const MVector& vOrigin)
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

	// A frame with +X toward vTarget and +Z along the bend normal, the way the solve's positions orient a bone (section 4.3).
	MMatrix makeBoneFrame(const MVector& vOrigin, const MVector& vTarget, const MVector& vNormal)
	{
		const MVector vX = (vTarget - vOrigin).normal();
		const MVector vZ = perpendicularPart(vNormal, vX).normal();
		return makeFrame(vX, vZ ^ vX, vZ, vOrigin);
	}

	// A matrix's rotation as a frame at vOrigin: +X along its X row, +Z perpendicular to that and its Y row. Scale and
	// shear are dropped.
	MMatrix orthonormalFrame(const MMatrix& mMatrix, const MVector& vOrigin)
	{
		const MVector vX = MVector(mMatrix[0][0], mMatrix[0][1], mMatrix[0][2]).normal();
		const MVector vZ = (vX ^ MVector(mMatrix[1][0], mMatrix[1][1], mMatrix[1][2])).normal();
		return makeFrame(vX, vZ ^ vX, vZ, vOrigin);
	}

	// The three limb joints' frames from a solve: root and mid by position and the bend normal, the end by the solved
	// end and the IK handle's rotation (scale dropped), so turning the handle turns the hand. The plan step builds the
	// rest offsets with this, compute() builds the live joints, and the inverse reads the handle back through the same
	// frame, so they can't drift (D41).
	void computeLimbFrames(const TwoBoneIkResult& solve, const MVector& vRoot, const MMatrix& mIkWorld, MMatrix outFrames[TwoBoneIkSlot::LimbCount])
	{
		outFrames[TwoBoneIkSlot::Upper] = makeBoneFrame(vRoot, solve.mid, solve.normal);
		outFrames[TwoBoneIkSlot::Lower] = makeBoneFrame(solve.mid, solve.end, solve.normal);
		outFrames[TwoBoneIkSlot::End] = orthonormalFrame(mIkWorld, solve.end);
	}

	// Everything the rest pose fixes: bone lengths, the plane angle that makes IK at rest reproduce the rest bend, and
	// the offsets that give each IK joint its own rest orientation (bind pose safe). The rests are valid (planFromJson
	// checked the geometry). The inverse calls this again from the controls' stored rests.
	TwoBoneIkRest computeRest(const std::array<MMatrix, TwoBoneIkSlot::Count>& restWorlds)
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
}

TwoBoneIkNode::TwoBoneIkNode()
{
}
TwoBoneIkNode::~TwoBoneIkNode()
{
}

const UpdateTable& TwoBoneIkNode::argUpdateTable()
{
	// Add a step here, and nowhere else, when TwoBoneIk's moduleArgs change shape or meaning. Step 0 takes version 1 to 2.
	static const UpdateTable table{ 1, { &dropMarkerJoints } };
	return table;
}

void* TwoBoneIkNode::creator()
{
	return new TwoBoneIkNode();
}

MStatus TwoBoneIkNode::initialize()
{
	MStatus status;
	MFnNumericAttribute nAttr;
	MFnMatrixAttribute mAttr;
	MFnTypedAttribute tAttr;
	MFnUnitAttribute uAttr;

	status = initializeSharedAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// Channel inputs, connected from the owning controls at build. Blend, stretch, soft and pin are clamped to 0..1.
	struct ClampedInput
	{
		MObject& attr;
		const char* name;
		double defaultValue;
	};
	const ClampedInput clampedInputs[] =
	{
		{ aIkFkBlend, RigAttr::IkFkBlend, 1.0 },
		{ aStretch, RigAttr::Stretch, 0.0 },
		{ aSoft, RigAttr::Soft, 0.0 },
		{ aPin, RigAttr::Pin, 0.0 }
	};
	for (const ClampedInput& input : clampedInputs)
	{
		input.attr = nAttr.create(input.name, input.name, MFnNumericData::kDouble, input.defaultValue, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setMin(0.0));
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setMax(1.0));
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(addAttribute(input.attr));
	}

	aRoll = uAttr.create(RigAttr::Roll, RigAttr::Roll, MFnUnitAttribute::kAngle, 0.0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(uAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aRoll));

	aPoleSpace = nAttr.create(RigAttr::PoleSpace, RigAttr::PoleSpace, MFnNumericData::kShort, 0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setMin(0));
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setMax(1));
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aPoleSpace));

	// Stored at build, like Aim's aimRestOffset.
	aIkRestOffset = mAttr.create("ikRestOffset", "ikRestOffset", MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setArray(true));
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aIkRestOffset));

	aRestBendNormal = nAttr.create("restBendNormal", "restBendNormal", MFnNumericData::k3Double, 0.0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aRestBendNormal));

	aRestPlaneAngle = nAttr.create("restPlaneAngle", "restPlaneAngle", MFnNumericData::kDouble, 0.0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aRestPlaneAngle));

	aLimbLengths = nAttr.create("limbLengths", "limbLengths", MFnNumericData::k2Double, 0.0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aLimbLengths));

	// Outputs that drive the controls' visibility, a function of the blend alone.
	for (MObject* pAttr : { &aFkVisible, &aIkVisible })
	{
		const char* name = pAttr == &aFkVisible ? "fkVisible" : "ikVisible";
		*pAttr = nAttr.create(name, name, MFnNumericData::kDouble, 1.0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setWritable(false));
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(false));
		CHECK_MSTATUS_AND_RETURN_IT(addAttribute(*pAttr));
	}

	// Match outputs. Maya computes an output only when something pulls it, and nothing is connected to these.
	for (MObject* pAttr : { &aFkMatchLocal, &aIkMatchLocal })
	{
		const char* name = pAttr == &aFkMatchLocal ? RigAttr::FkMatchLocal : RigAttr::IkMatchLocal;
		*pAttr = mAttr.create(name, name, MFnMatrixAttribute::kDouble, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(mAttr.setArray(true));
		CHECK_MSTATUS_AND_RETURN_IT(mAttr.setWritable(false));
		CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(false));
		CHECK_MSTATUS_AND_RETURN_IT(mAttr.setUsesArrayDataBuilder(true));
		CHECK_MSTATUS_AND_RETURN_IT(addAttribute(*pAttr));
	}
	{
		MFnStringData fnEmptyList;
		const MObject oEmptyList = fnEmptyList.create("[]");
		aMatchWarnings = tAttr.create(RigAttr::MatchWarnings, RigAttr::MatchWarnings, MFnData::kString, oEmptyList, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(tAttr.setWritable(false));
		CHECK_MSTATUS_AND_RETURN_IT(tAttr.setStorable(false));
		CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aMatchWarnings));
	}

	// AFFECTS RELATIONSHIPS
	declareStandardAffects();
	// Every setting changes the solved joints and the sockets that carry them. The pole's space also moves the pole control.
	for (const MObject& oInput : { aIkFkBlend, aStretch, aSoft, aPin, aRoll, aPoleSpace, aIkRestOffset, aRestBendNormal, aRestPlaneAngle, aLimbLengths })
	{
		attributeAffects(oInput, aJointOPM);
		attributeAffects(oInput, aOutputSocketMatrix);
	}
	attributeAffects(aPoleSpace, aControlOPM);
	attributeAffects(aIkFkBlend, aFkVisible);
	attributeAffects(aIkFkBlend, aIkVisible);
	// The match outputs read everything the pose does: the controls, the child plugs' parents and every setting.
	for (const MObject& oInput : { aControlMatrix, aControlRestWorld, aChildPlugParentMatrix, aChildPlugParentRestMatrix,
		aIkFkBlend, aStretch, aSoft, aPin, aRoll, aPoleSpace, aIkRestOffset, aRestBendNormal, aRestPlaneAngle, aLimbLengths })
	{
		attributeAffects(oInput, aFkMatchLocal);
		attributeAffects(oInput, aIkMatchLocal);
		attributeAffects(oInput, aMatchWarnings);
	}

	return MStatus::kSuccess;
}

// Reads the limb's plugs. compute() writes every output array from one evaluation of this.
MStatus TwoBoneIkNode::readEval(MDataBlock& data, TwoBoneIkEval& eval)
{
	MStatus status;

	MArrayDataHandle hControlInputData = data.inputArrayValue(aControlInputData, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hChildPlugs = data.inputArrayValue(aChildPlugs, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
	{
		status = readControlInput(hControlInputData, slot, eval.mLocal[slot], eval.mRest[slot]);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	eval.mUpperDelta = readChildPlugParentDelta(hChildPlugs, TwoBoneIkSlot::Upper);
	eval.mIkDelta = readChildPlugParentDelta(hChildPlugs, TwoBoneIkSlot::Ik);
	eval.mSettingsDelta = readChildPlugParentDelta(hChildPlugs, TwoBoneIkSlot::Settings);
	eval.poleFollowsIk = data.inputValue(aPoleSpace).asShort() == 1;
	if (!eval.poleFollowsIk)
	{
		eval.mPoleParentDelta = readChildPlugParentDelta(hChildPlugs, TwoBoneIkSlot::Pole);
	}

	// The settings are read at every blend: the match outputs run the inverse at blend 0 too, with these values.
	eval.blend = data.inputValue(aIkFkBlend).asDouble();
	eval.stretch = data.inputValue(aStretch).asDouble();
	eval.soft = data.inputValue(aSoft).asDouble();
	eval.pin = data.inputValue(aPin).asDouble();
	eval.rollRadians = data.inputValue(aRoll).asAngle().asRadians();
	if (eval.blend > 0.0)
	{
		const double2& lengths = data.inputValue(aLimbLengths).asDouble2();
		eval.rest.length1 = lengths[0];
		eval.rest.length2 = lengths[1];
		eval.rest.restPlaneAngle = data.inputValue(aRestPlaneAngle).asDouble();
		eval.rest.restBendNormal = data.inputValue(aRestBendNormal).asVector();

		MArrayDataHandle hRestOffset = data.inputArrayValue(aIkRestOffset, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		for (unsigned int slot = 0; slot < TwoBoneIkSlot::LimbCount; ++slot)
		{
			status = hRestOffset.jumpToElement(slot);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			eval.rest.ikRestOffset[slot] = hRestOffset.inputValue().asMatrix();
		}
	}

	return MStatus::kSuccess;
}

void TwoBoneIkNode::evaluate(const TwoBoneIkEval& eval, TwoBoneIkSolveResult& outResult)
{
	// 1. FK chain. Always solved, even at blend 1: the controls' offsets stay current, and the IK root follows the start control.
	MMatrix mFkWorld[TwoBoneIkSlot::LimbCount];
	MMatrix mParentDelta = eval.mUpperDelta;
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::LimbCount; ++slot)
	{
		const ControlSolveStep step = solveControlStep(eval.mLocal[slot], eval.mRest[slot], mParentDelta);
		outResult.mControlOPM[slot] = step.mOPM;
		mFkWorld[slot] = step.mWorld;
		if (slot + 1 < TwoBoneIkSlot::LimbCount)
		{
			mParentDelta = MathUtils::computeDeltaFromRest(eval.mRest[slot], step.mWorld);
		}
	}

	// 2. The IK handle, then the pole, which reads the handle's solved world in Ik space (slot order matters, section 4.4).
	const ControlSolveStep ikStep = solveControlStep(eval.mLocal[TwoBoneIkSlot::Ik], eval.mRest[TwoBoneIkSlot::Ik], eval.mIkDelta);
	const MMatrix mPoleDelta = eval.poleFollowsIk
		? MathUtils::computeDeltaFromRest(eval.mRest[TwoBoneIkSlot::Ik], ikStep.mWorld)
		: eval.mPoleParentDelta;
	const ControlSolveStep poleStep = solveControlStep(eval.mLocal[TwoBoneIkSlot::Pole], eval.mRest[TwoBoneIkSlot::Pole], mPoleDelta);
	const ControlSolveStep settingsStep = solveControlStep(eval.mLocal[TwoBoneIkSlot::Settings], eval.mRest[TwoBoneIkSlot::Settings], eval.mSettingsDelta);

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

MStatus TwoBoneIkNode::compute(const MPlug& plug, MDataBlock& data)
{
	const MObject oOutputAttr = plug.attribute();

	if (oOutputAttr == aFkVisible || oOutputAttr == aIkVisible)
	{
		const double blend = data.inputValue(aIkFkBlend).asDouble();
		const bool isVisible = oOutputAttr == aFkVisible ? blend < 1.0 : blend > 0.0;
		data.outputValue(oOutputAttr).setDouble(isVisible ? 1.0 : 0.0);
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	if (oOutputAttr == aFkMatchLocal || oOutputAttr == aIkMatchLocal || oOutputAttr == aMatchWarnings)
	{
		return computeMatch(plug, data);
	}

	if (oOutputAttr != aControlOPM && oOutputAttr != aJointOPM && oOutputAttr != aOutputSocketMatrix)
	{
		return MStatus::kUnknownParameter;
	}

	MStatus status;
	TwoBoneIkEval eval;
	status = readEval(data, eval);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	TwoBoneIkSolveResult result;
	evaluate(eval, result);

	MArrayDataHandle hControlOPM = data.outputArrayValue(aControlOPM, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hJointOPM = data.outputArrayValue(aJointOPM, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hSocket = data.outputArrayValue(aOutputSocketMatrix, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// One solve writes all three outputs. Control OPM excludes the control's local matrix. Joint OPM and socket both want
	// the solved world matrix. Elements that were never created (a socket nothing is connected to) are skipped.
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
	{
		writeOutputMatrix(hControlOPM, slot, result.mControlOPM[slot]);
		writeOutputMatrix(hJointOPM, slot, result.mJointWorld[slot]);
		writeOutputMatrix(hSocket, slot, result.mJointWorld[slot]);
	}

	const MObject oNode = plug.node();
	commitOutput(data, oNode, aControlOPM, hControlOPM);
	commitOutput(data, oNode, aJointOPM, hJointOPM);
	commitOutput(data, oNode, aOutputSocketMatrix, hSocket);
	data.setClean(plug);
	return MStatus::kSuccess;
}

namespace
{
	// Profiler event name, in ProfilerUtils' category. A profile of playback with no such event shows the match branch never ran.
	constexpr const char* ProfileComputeMatch{ "twoBoneIk computeMatch" };
}

// The one branch of compute() that allocates (the inverse's vectors and warnings), against D36's rule. It runs only when a
// tool pulls a match output: playback never does, and bench (D52 phase 8) checks that. The inverse is the restore's, run
// on the joints this evaluation solved, so what a match command writes is what a restore of the same pose would.
MStatus TwoBoneIkNode::computeMatch(const MPlug& plug, MDataBlock& data)
{
	MProfilingScope profile(ProfilerUtils::category(), MProfiler::kColorC_L2, ProfileComputeMatch);
	MStatus status;
	TwoBoneIkEval eval;
	status = readEval(data, eval);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	TwoBoneIkSolveResult pose;
	evaluate(eval, pose);

	ModuleInverseContext context;
	ModuleInverseInput input;
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
	{
		context.controlRestWorld.push_back(eval.mRest[slot]);
		input.fallbackLocal.push_back(eval.mLocal[slot]);
		if (slot < TwoBoneIkSlot::LimbCount)
		{
			input.jointWorld.push_back(pose.mJointWorld[slot]);
		}
		else
		{
			input.jointWorld.emplace_back();
		}
	}
	// The inverse reads the start, IK handle, pole (in Parent space) and settings deltas, and no other.
	input.parentDelta.assign(TwoBoneIkSlot::Count, MMatrix::identity);
	input.parentDelta[TwoBoneIkSlot::Upper] = eval.mUpperDelta;
	input.parentDelta[TwoBoneIkSlot::Ik] = eval.mIkDelta;
	input.parentDelta[TwoBoneIkSlot::Pole] = eval.mPoleParentDelta;
	input.parentDelta[TwoBoneIkSlot::Settings] = eval.mSettingsDelta;
	input.channels =
	{
		eval.soft, eval.stretch, eval.pin, eval.rollRadians,
		eval.poleFollowsIk ? 1.0 : 0.0,
		eval.blend
	};
	const ModuleInverseResult result = inverse(context, input);

	MArrayDataHandle hFkMatch = data.outputArrayValue(aFkMatchLocal, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hIkMatch = data.outputArrayValue(aIkMatchLocal, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::Settings && slot < result.controlLocal.size(); ++slot)
	{
		if (!result.controlLocal[slot].has_value())
		{
			continue;
		}
		writeOutputMatrix(slot < TwoBoneIkSlot::LimbCount ? hFkMatch : hIkMatch, slot, *result.controlLocal[slot]);
	}

	data.outputValue(aMatchWarnings).setString(JsonUtils::writeJsonString(result.warnings).c_str());

	const MObject oNode = plug.node();
	commitOutput(data, oNode, aFkMatchLocal, hFkMatch);
	commitOutput(data, oNode, aIkMatchLocal, hIkMatch);
	data.setClean(MPlug(oNode, aMatchWarnings));
	data.setClean(plug);
	return MStatus::kSuccess;
}

std::vector<ModuleInverseChannel> TwoBoneIkNode::inverseChannels()
{
	// In TwoBoneIkInverseChannel order. The defaults are the channels' rests.
	return
	{
		{ RigAttr::Soft, TwoBoneIkSlot::Settings, 0.0 },
		{ RigAttr::Stretch, TwoBoneIkSlot::Settings, 0.0 },
		{ RigAttr::Pin, TwoBoneIkSlot::Pole, 0.0 },
		{ RigAttr::Roll, TwoBoneIkSlot::Ik, 0.0 },
		{ RigAttr::ControlSpace, TwoBoneIkSlot::Pole, 0.0 },
		{ RigAttr::IkFkBlend, TwoBoneIkSlot::Settings, 1.0 }
	};
}

// Reads the same two deltas evaluate() hands the pole (section 4.4), from the plugs: the child plug's parent motion for
// Parent, and the IK handle's world socket against its rest for Ik. The handle doesn't depend on the pole, so pulling its
// socket is no cycle.
MStatus TwoBoneIkNode::spaceDelta(const MObject& oModule, unsigned int slot, unsigned int label, MMatrix& outDelta)
{
	constexpr unsigned int kParentSpace{ 0 };
	constexpr unsigned int kIkSpace{ 1 };

	if (slot != TwoBoneIkSlot::Pole || label > kIkSpace)
	{
		MGlobal::displayError(MString("TwoBoneIk has no space ") + label + " on control slot " + slot + ".");
		return MStatus::kInvalidParameter;
	}

	MStatus status;
	if (label == kParentSpace)
	{
		MPlug pParentMatrix;
		MPlug pParentRest;
		status = getChildPlugMatrixPlugs(oModule, TwoBoneIkSlot::Pole, pParentMatrix, pParentRest);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		outDelta = MathUtils::computeDeltaFromRest(DagUtils::getMatrixFromPlug(pParentRest), DagUtils::getMatrixFromPlug(pParentMatrix));
		return MStatus::kSuccess;
	}

	const MPlug pIkWorld = getSocketMatrixPlug(oModule, TwoBoneIkSlot::Ik, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	outDelta = MathUtils::computeDeltaFromRest(getControlRestWorld(oModule, TwoBoneIkSlot::Ik), DagUtils::getMatrixFromPlug(pIkWorld));
	return MStatus::kSuccess;
}

// Allocates (the contract's vectors, the warnings), which is fine for a restore. compute() never calls it (D52 section 6).
ModuleInverseResult TwoBoneIkNode::inverse(const ModuleInverseContext& context, const ModuleInverseInput& input)
{
	ModuleInverseResult result;
	result.controlLocal.resize(TwoBoneIkSlot::Count);
	result.socketWorld.resize(TwoBoneIkSlot::Count);

	// The forward evaluation's inputs: the controls at their fallback values to start with, and the settings the forward
	// solve will read.
	const std::vector<ModuleInverseChannel> wanted = inverseChannels();
	const auto channel = [&input, &wanted](unsigned int which)
	{
		return which < input.channels.size() ? input.channels[which] : wanted[which].defaultValue;
	};
	TwoBoneIkEval eval;
	std::array<MMatrix, TwoBoneIkSlot::Count> restWorlds;
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
	{
		restWorlds[slot] = context.controlRestWorld[slot];
		eval.mRest[slot] = context.controlRestWorld[slot];
		eval.mLocal[slot] = input.fallbackLocal[slot];
	}
	eval.mUpperDelta = input.parentDelta[TwoBoneIkSlot::Upper];
	eval.mIkDelta = input.parentDelta[TwoBoneIkSlot::Ik];
	eval.mPoleParentDelta = input.parentDelta[TwoBoneIkSlot::Pole];
	eval.mSettingsDelta = input.parentDelta[TwoBoneIkSlot::Settings];
	eval.poleFollowsIk = std::lround(channel(TwoBoneIkInverseChannel::Space)) == 1;
	eval.blend = channel(TwoBoneIkInverseChannel::IkFkBlend);
	eval.stretch = channel(TwoBoneIkInverseChannel::Stretch);
	eval.soft = channel(TwoBoneIkInverseChannel::Soft);
	eval.pin = channel(TwoBoneIkInverseChannel::Pin);
	eval.rollRadians = channel(TwoBoneIkInverseChannel::Roll);
	eval.rest = computeRest(restWorlds);

	const auto fillSockets = [&result, &eval]()
	{
		TwoBoneIkSolveResult forward;
		evaluate(eval, forward);
		for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
		{
			result.socketWorld[slot] = forward.mJointWorld[slot];
		}
	};

	// Slots 3 to 5 have no joints (D57): the handle and pole come from the limb's three, which are read here.
	bool hasLimb = input.jointWorld.size() >= TwoBoneIkSlot::LimbCount;
	for (unsigned int slot = 0; hasLimb && slot < TwoBoneIkSlot::LimbCount; ++slot)
	{
		hasLimb = input.jointWorld[slot].has_value();
	}
	if (!hasLimb)
	{
		result.reason = "a limb joint is missing from the source, so the limb's controls come from the sidecar or rest";
		fillSockets();
		return result;
	}
	result.solved = true;

	// 1. FK: the chain walk, so a stretched or sheared pose is reproduced in the FK controls at any blend.
	ModuleInverseContext fkContext;
	ModuleInverseInput fkInput;
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::LimbCount; ++slot)
	{
		fkContext.controlRestWorld.push_back(context.controlRestWorld[slot]);
		fkInput.jointWorld.push_back(input.jointWorld[slot]);
		fkInput.fallbackLocal.push_back(input.fallbackLocal[slot]);
	}
	fkInput.parentDelta.push_back(input.parentDelta[TwoBoneIkSlot::Upper]);
	const ModuleInverseResult fk = ModuleInverse::solveChain(fkContext, fkInput, true);
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::LimbCount; ++slot)
	{
		result.controlLocal[slot] = fk.controlLocal[slot];
		eval.mLocal[slot] = *fk.controlLocal[slot];
	}

	// 2. The IK handle and pole from the joints' positions.
	const MVector vRoot = MathUtils::translationOf(*input.jointWorld[TwoBoneIkSlot::Upper]);
	const MVector vMid = MathUtils::translationOf(*input.jointWorld[TwoBoneIkSlot::Lower]);
	const MVector vEnd = MathUtils::translationOf(*input.jointWorld[TwoBoneIkSlot::End]);
	TwoBoneIkInverseInput ikInput;
	ikInput.root = vRoot;
	ikInput.mid = vMid;
	ikInput.end = vEnd;
	ikInput.length1 = eval.rest.length1;
	ikInput.length2 = eval.rest.length2;
	ikInput.stretch = eval.stretch;
	ikInput.soft = eval.soft;
	ikInput.pin = eval.pin;
	ikInput.rollRadians = eval.rollRadians;
	ikInput.restPlaneAngle = eval.rest.restPlaneAngle;
	ikInput.restAxis = MathUtils::translationOf(restWorlds[TwoBoneIkSlot::Ik]) - MathUtils::translationOf(restWorlds[TwoBoneIkSlot::Upper]);
	ikInput.restPoleOffset = MathUtils::translationOf(restWorlds[TwoBoneIkSlot::Pole]) - MathUtils::translationOf(restWorlds[TwoBoneIkSlot::Upper]);
	const TwoBoneIkInverseResult ik = MathUtils::computeTwoBoneIkInverse(ikInput);

	// The handle: at the solved position, turned the way the hand joint is (the frame the forward solve gives the end).
	const MMatrix mEndFrame = eval.rest.ikRestOffset[TwoBoneIkSlot::End].inverse() * (*input.jointWorld[TwoBoneIkSlot::End]);
	const MMatrix mHandleWorld = orthonormalFrame(mEndFrame, ik.handle);
	eval.mLocal[TwoBoneIkSlot::Ik] = ModuleInverse::localFromWorld(mHandleWorld, restWorlds[TwoBoneIkSlot::Ik], eval.mIkDelta);
	result.controlLocal[TwoBoneIkSlot::Ik] = eval.mLocal[TwoBoneIkSlot::Ik];

	// The pole: in Ik space it moves with the matched handle (the one being written), not the handle's current world.
	// Its rotation channels are locked, so only the local translation that puts it at the solved position is set.
	const MMatrix mPoleDelta = eval.poleFollowsIk ? MathUtils::computeDeltaFromRest(restWorlds[TwoBoneIkSlot::Ik], mHandleWorld) : eval.mPoleParentDelta;
	const MPoint pPoleLocal = MPoint(ik.pole) * (restWorlds[TwoBoneIkSlot::Pole] * mPoleDelta).inverse();
	eval.mLocal[TwoBoneIkSlot::Pole] = makeFrame(MVector(1.0, 0.0, 0.0), MVector(0.0, 1.0, 0.0), MVector(0.0, 0.0, 1.0), MVector(pPoleLocal.x, pPoleLocal.y, pPoleLocal.z));
	result.controlLocal[TwoBoneIkSlot::Pole] = eval.mLocal[TwoBoneIkSlot::Pole];

	// The settings control has no joint, so it keeps its fallback. The sockets are what the forward solve gives these controls.
	fillSockets();

	// 3. A pose IK can't make. FK always matches, so only the IK side warns.
	if (eval.blend > 0.0 && eval.blend < 1.0)
	{
		result.warnings.push_back("ikFkBlend is between 0 and 1, so the IK handle and pole match the joints only approximately. FK matches exactly. Match at blend 0 or 1.");
		return result;
	}
	TwoBoneIkInput check;
	check.root = vRoot;
	check.handle = ik.handle;
	check.pole = ik.pole;
	check.length1 = eval.rest.length1;
	check.length2 = eval.rest.length2;
	check.stretch = eval.stretch;
	check.soft = eval.soft;
	check.pin = eval.pin;
	check.rollRadians = eval.rollRadians;
	check.restPlaneAngle = eval.rest.restPlaneAngle;
	check.restBendNormal = eval.rest.restBendNormal;
	check.restAxis = ikInput.restAxis;
	const TwoBoneIkResult forward = MathUtils::computeTwoBoneIk(check);
	const double tolerance = kMaxIkMismatch * (eval.rest.length1 + eval.rest.length2);
	if ((forward.mid - vMid).length() > tolerance || (forward.end - vEnd).length() > tolerance)
	{
		result.warnings.push_back("IK can't make the joints' pose: their bone lengths differ from the rig's with stretch and pin off, or the limb is past its reach. "
			"The IK handle and pole are as near as the limb gets. FK matches exactly.");
	}
	return result;
}

RigModuleBuildResult TwoBoneIkNode::createModule(
	const MString& moduleName,
	const Side& side,
	const ShapeType& shapeType,
	const std::array<TwoBoneIkSlotInput, TwoBoneIkSlot::Count>& slots,
	const TwoBoneIkRest& rest,
	MObject& oRigRoot,
	MDagModifier& dagMod)
{
	RigModuleBuildResult result;
	MStatus status;

	// 1. Module node
	MObject oModule = dagMod.MDGModifier::createNode(TwoBoneIkNode::id);
	dagMod.renameNode(oModule, moduleName);
	dagMod.newPlugValueShort(MPlug(oModule, TwoBoneIkNode::aSide), getShortFromSide(side));
	result.moduleNode = oModule;

	// 2. Per slot: bake and wire the joint, create and wire the control, name the sockets and child plug.
	MObject oControls[TwoBoneIkSlot::Count];
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
	{
		const TwoBoneIkSlotInput& input = slots[slot];

		// The IK, pole and settings controls have no joint (D57): their jointOPM and jointRestWorld elements are never made, which
		// compute(), the inverse and the match outputs allow for. Their nodes are named from their usage.
		MString controlBaseName(input.baseName.c_str());
		if (!input.oJoint.isNull())
		{
			status = TwoBoneIkNode::wireJoint(oModule, input.oJoint, slot, input.mRestWorld, dagMod);
			if (!status)
			{
				result.status = status;
				return result;
			}
			controlBaseName = MFnDependencyNode(input.oJoint).name();
		}

		oControls[slot] = RigControlNode::createRigControl(dagMod, controlBaseName, shapeType, side, input.controlMarkup, MEulerRotation::kXYZ);
		// FK lower under FK upper and FK end under FK lower. The IK, pole and settings controls stay out of the FK
		// hierarchy: visibility inherits through the DAG, and the FK controls hide in IK (section 3.2).
		if (slot == TwoBoneIkSlot::Lower || slot == TwoBoneIkSlot::End)
		{
			dagMod.reparentNode(oControls[slot], oControls[slot - 1]);
		}
		status = wireControl(oModule, oControls[slot], slot, input.mRestWorld, dagMod);
		if (!status)
		{
			result.status = status;
			return result;
		}

		// index_0 to index_2 (and start and end) carry the blended joint, not the FK control, so they have no DAG transform:
		// a child module's controls would otherwise inherit an FK control's visibility.
		RigModuleSocketInfo socketInfo;
		socketInfo.oTransform = slot < TwoBoneIkSlot::LimbCount ? MObject::kNullObj : oControls[slot];
		socketInfo.mRestWorld = input.mRestWorld;
		socketInfo.outputIndex = slot;
		const std::vector<std::string> socketNames = socketNamesAt(slot);
		for (const std::string& socketName : socketNames)
		{
			result.parentSockets[socketName] = socketInfo;
		}
		DagPlugAndSocketUtils::setParentSocketNames(oModule, slot, socketNames, dagMod);

		if (const char* plugName = childPlugNameAt(slot))
		{
			RigModuleChildPlugInfo plugInfo{ oControls[slot], slot };
			if (slot == TwoBoneIkSlot::Settings)
			{
				plugInfo.followsPlug = PlugAndSocketNames::Start;
			}
			result.childPlugs[plugName] = plugInfo;
			DagPlugAndSocketUtils::setChildPlugName(oModule, slot, plugName, dagMod);
		}
	}

	// 3. Rest data the solve reads.
	for (unsigned int i = 0; i < TwoBoneIkSlot::LimbCount; ++i)
	{
		MPlug pOffset = MPlug(oModule, TwoBoneIkNode::aIkRestOffset).elementByLogicalIndex(i);
		DagUtils::setMatrixPlug(pOffset, rest.ikRestOffset[i], dagMod);
	}
	MPlug pBendNormal(oModule, TwoBoneIkNode::aRestBendNormal);
	dagMod.newPlugValueDouble(pBendNormal.child(0), rest.restBendNormal.x);
	dagMod.newPlugValueDouble(pBendNormal.child(1), rest.restBendNormal.y);
	dagMod.newPlugValueDouble(pBendNormal.child(2), rest.restBendNormal.z);
	dagMod.newPlugValueDouble(MPlug(oModule, TwoBoneIkNode::aRestPlaneAngle), rest.restPlaneAngle);
	MPlug pLengths(oModule, TwoBoneIkNode::aLimbLengths);
	dagMod.newPlugValueDouble(pLengths.child(0), rest.length1);
	dagMod.newPlugValueDouble(pLengths.child(1), rest.length2);

	// 4. The channels. The controls must exist before an attribute can be added to them, and the attributes before they
	// can be wired, as in RigConstraintNode::createConstraint, so the build runs the modifier here.
	status = dagMod.doIt();
	if (!status)
	{
		result.status = status;
		return result;
	}

	status = RigPoseScene::addDoubleChannel(oControls[TwoBoneIkSlot::Settings], RigAttr::IkFkBlend, 1.0, 0.0, 1.0, false, dagMod);
	CHECK_MSTATUS_AND_RETURN(status, result);
	status = RigPoseScene::addDoubleChannel(oControls[TwoBoneIkSlot::Settings], RigAttr::Stretch, 0.0, 0.0, 1.0, false, dagMod);
	CHECK_MSTATUS_AND_RETURN(status, result);
	status = RigPoseScene::addDoubleChannel(oControls[TwoBoneIkSlot::Settings], RigAttr::Soft, 0.0, 0.0, 1.0, false, dagMod);
	CHECK_MSTATUS_AND_RETURN(status, result);
	status = RigPoseScene::addAngleChannel(oControls[TwoBoneIkSlot::Ik], RigAttr::Roll, true, dagMod);   // a reflection reverses a roll's sense
	CHECK_MSTATUS_AND_RETURN(status, result);
	status = RigPoseScene::addDoubleChannel(oControls[TwoBoneIkSlot::Pole], RigAttr::Pin, 0.0, 0.0, 1.0, false, dagMod);
	CHECK_MSTATUS_AND_RETURN(status, result);
	// The pole's module-owned space (D52 section 4.4): the module reads it, so it is wired here and not through a rigConstraint.
	status = RigPoseScene::addEnumChannel(oControls[TwoBoneIkSlot::Pole], RigAttr::ControlSpace, { PoleSpaceLabels::Parent, PoleSpaceLabels::Ik }, 0, false, dagMod);
	CHECK_MSTATUS_AND_RETURN(status, result);
	status = dagMod.doIt();
	if (!status)
	{
		result.status = status;
		return result;
	}

	struct ChannelWire
	{
		unsigned int slot;
		const char* controlAttr;
		const MObject& moduleAttr;
	};
	const ChannelWire wires[] =
	{
		{ TwoBoneIkSlot::Settings, RigAttr::IkFkBlend, aIkFkBlend },
		{ TwoBoneIkSlot::Settings, RigAttr::Stretch, aStretch },
		{ TwoBoneIkSlot::Settings, RigAttr::Soft, aSoft },
		{ TwoBoneIkSlot::Ik, RigAttr::Roll, aRoll },
		{ TwoBoneIkSlot::Pole, RigAttr::Pin, aPin },
		{ TwoBoneIkSlot::Pole, RigAttr::ControlSpace, aPoleSpace }
	};
	for (const ChannelWire& wire : wires)
	{
		MPlug pControlAttr = MFnDependencyNode(oControls[wire.slot]).findPlug(wire.controlAttr, false, &status);
		CHECK_MSTATUS_AND_RETURN(status, result);
		status = dagMod.connect(pControlAttr, MPlug(oModule, wire.moduleAttr));
		CHECK_MSTATUS_AND_RETURN(status, result);
	}

	// FK lower and end are visible below blend 1, the IK handle and pole above blend 0. Start and settings are always visible.
	const std::pair<unsigned int, const MObject&> visibility[] =
	{
		{ TwoBoneIkSlot::Lower, aFkVisible }, { TwoBoneIkSlot::End, aFkVisible },
		{ TwoBoneIkSlot::Ik, aIkVisible }, { TwoBoneIkSlot::Pole, aIkVisible }
	};
	for (const auto& [slot, oVisibleAttr] : visibility)
	{
		MPlug pVisibility = MFnDependencyNode(oControls[slot]).findPlug("visibility", false, &status);
		CHECK_MSTATUS_AND_RETURN(status, result);
		status = dagMod.connect(MPlug(oModule, oVisibleAttr), pVisibility);
		CHECK_MSTATUS_AND_RETURN(status, result);
	}

	// Transform channels the animator doesn't use are locked and hidden. The IK controls keep what they move by (section 3.3).
	const std::vector<std::string> ikKeep{ "tx", "ty", "tz", "rx", "ry", "rz" };
	const std::vector<std::string> poleKeep{ "tx", "ty", "tz" };
	status = DagUtils::queueLockAndHideTransform(oControls[TwoBoneIkSlot::Ik], dagMod, ikKeep);
	CHECK_MSTATUS_AND_RETURN(status, result);
	status = DagUtils::queueLockAndHideTransform(oControls[TwoBoneIkSlot::Pole], dagMod, poleKeep);
	CHECK_MSTATUS_AND_RETURN(status, result);
	status = DagUtils::queueLockAndHideTransform(oControls[TwoBoneIkSlot::Settings], dagMod);
	CHECK_MSTATUS_AND_RETURN(status, result);

	// 5. Network
	if (!oRigRoot.isNull())
	{
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dagMod, oModule, aRigRoot);
	}

	// Shared callback: skips the DAG reparent for the sockets with no control, always connects matrices.
	result.dagParentCallback = TwoBoneIkNode::reparentChildPlugToSocket;
	result.status = MStatus::kSuccess;
	return result;
}

bool TwoBoneIkNode::planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan)
{
	TwoBoneIkArgs ikArgs;
	if (!readModuleArgs(args, { "joints" }, ctx, ikArgs, { "controlName", "poleOffset", "settingsOffset" }))
	{
		return false;
	}

	const std::string argsPath = ctx.where + "." + TemplateKey::ModuleArgs;
	bool valid = true;
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
	std::array<TwoBoneIkSlotInput, TwoBoneIkSlot::Count> slots;
	for (std::size_t i = 0; i < ikArgs.joints.size(); ++i)
	{
		// With the wrong joint count the selectors aren't the limb, so they are claimed into a scratch input.
		TwoBoneIkSlotInput scratch;
		TwoBoneIkSlotInput& slot = ikArgs.joints.size() == TwoBoneIkSlot::LimbCount ? slots[i] : scratch;
		valid = claimJoint(ctx, ikArgs.joints[i], slot.oJoint, slot.mRestWorld) && valid;
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
		MathUtils::toMat4(slots[TwoBoneIkSlot::End].mRestWorld), poleOffset, settingsOffset);
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
	slots[TwoBoneIkSlot::Ik].mRestWorld = frameAt(mEndRotation, vEnd);
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
	const TwoBoneIkRest rest = computeRest(restWorlds);

	outPlan.moduleInterface = makeTwoBoneIkInterface();
	for (const TwoBoneIkSlotInput& slot : slots)
	{
		outPlan.moduleInterface.controls.push_back(slot.controlMarkup);
	}

	const MString moduleName = ctx.moduleData.name.c_str();
	const Side side = getSideFromString(ctx.moduleData.side);
	const ShapeType shapeType = getShapeTypeFromString(ctx.moduleData.shapeType);
	outPlan.build = [moduleName, side, shapeType, slots, rest](MObject& oRigRoot, MDagModifier& dagMod)
	{
		return TwoBoneIkNode::createModule(moduleName, side, shapeType, slots, rest, oRigRoot, dagMod);
	};
	return true;
}
