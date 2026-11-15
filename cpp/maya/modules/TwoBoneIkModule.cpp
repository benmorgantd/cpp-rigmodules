#include "maya/modules/TwoBoneIkModule.h"
#include "core/Constants.h"
#include "core/RigJsonStructs.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/Side.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
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
#include <maya/MFnUnitAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MObject.h>
#include <maya/MPlug.h>
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

namespace
{
	// Rests closer than these have no bone, no end-to-handle match, or no pole plane.
	constexpr double kMinBoneLength{ 1.0e-4 };
	constexpr double kMaxIkEndOffset{ 1.0e-3 };
	constexpr double kMinPoleSine{ 1.0e-4 };
	// A rest mid this near the root-to-end line is a straight limb: it has no bend direction of its own.
	constexpr double kMinRestBend{ 1.0e-4 };

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

	// The three limb joints' frames from a solve: root and mid by position and the bend normal, the end by the solved
	// end and the IK handle's rotation (scale dropped), so turning the handle turns the hand. The plan step builds the
	// rest offsets with this, compute() builds the live joints, and the inverse will too, so they can't drift (D41).
	void computeLimbFrames(const TwoBoneIkResult& solve, const MVector& vRoot, const MMatrix& mIkWorld, MMatrix outFrames[TwoBoneIkSlot::LimbCount])
	{
		outFrames[TwoBoneIkSlot::Upper] = makeBoneFrame(vRoot, solve.mid, solve.normal);
		outFrames[TwoBoneIkSlot::Lower] = makeBoneFrame(solve.mid, solve.end, solve.normal);

		const MVector vX = MVector(mIkWorld[0][0], mIkWorld[0][1], mIkWorld[0][2]).normal();
		const MVector vZ = (vX ^ MVector(mIkWorld[1][0], mIkWorld[1][1], mIkWorld[1][2])).normal();
		outFrames[TwoBoneIkSlot::End] = makeFrame(vX, vZ ^ vX, vZ, solve.end);
	}

	// Everything the rest pose fixes: bone lengths, the plane angle that makes IK at rest reproduce the rest bend, and
	// the offsets that give each IK joint its own rest orientation (bind pose safe). The slots' rests are valid
	// (planFromJson checked the geometry).
	TwoBoneIkRest computeRest(const std::array<TwoBoneIkSlotInput, TwoBoneIkSlot::Count>& slots)
	{
		const MVector vRoot = MathUtils::translationOf(slots[TwoBoneIkSlot::Upper].mRestWorld);
		const MVector vMid = MathUtils::translationOf(slots[TwoBoneIkSlot::Lower].mRestWorld);
		const MVector vEnd = MathUtils::translationOf(slots[TwoBoneIkSlot::End].mRestWorld);
		const MVector vHandle = MathUtils::translationOf(slots[TwoBoneIkSlot::Ik].mRestWorld);
		const MVector vPole = MathUtils::translationOf(slots[TwoBoneIkSlot::Pole].mRestWorld);

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
		computeLimbFrames(MathUtils::computeTwoBoneIk(input), vRoot, slots[TwoBoneIkSlot::Ik].mRestWorld, mFrames);
		for (unsigned int i = 0; i < TwoBoneIkSlot::LimbCount; ++i)
		{
			rest.ikRestOffset[i] = slots[i].mRestWorld * mFrames[i].inverse();
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

void* TwoBoneIkNode::creator()
{
	return new TwoBoneIkNode();
}

MStatus TwoBoneIkNode::initialize()
{
	MStatus status;
	MFnNumericAttribute nAttr;
	MFnMatrixAttribute mAttr;
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

	return MStatus::kSuccess;
}

// Solves the limb once. compute() writes every output array from this one solve.
MStatus TwoBoneIkNode::solve(MDataBlock& data, TwoBoneIkSolveResult& outResult)
{
	MStatus status;

	MArrayDataHandle hControlInputData = data.inputArrayValue(aControlInputData, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hChildPlugs = data.inputArrayValue(aChildPlugs, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MMatrix mLocal[TwoBoneIkSlot::Count];
	MMatrix mRest[TwoBoneIkSlot::Count];
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
	{
		status = readControlInput(hControlInputData, slot, mLocal[slot], mRest[slot]);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}

	// 1. FK chain. Always solved, even at blend 1: the controls' offsets stay current, and the IK root follows the start control.
	MMatrix mFkWorld[TwoBoneIkSlot::LimbCount];
	MMatrix mParentDelta = readChildPlugParentDelta(hChildPlugs, TwoBoneIkSlot::Upper);
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::LimbCount; ++slot)
	{
		const ControlSolveStep step = solveControlStep(mLocal[slot], mRest[slot], mParentDelta);
		outResult.mControlOPM[slot] = step.mOPM;
		mFkWorld[slot] = step.mWorld;
		if (slot + 1 < TwoBoneIkSlot::LimbCount)
		{
			mParentDelta = MathUtils::computeDeltaFromRest(mRest[slot], step.mWorld);
		}
	}

	// 2. The IK handle, then the pole, which reads the handle's solved world in Ik space (slot order matters, section 4.4).
	const ControlSolveStep ikStep = solveControlStep(mLocal[TwoBoneIkSlot::Ik], mRest[TwoBoneIkSlot::Ik],
		readChildPlugParentDelta(hChildPlugs, TwoBoneIkSlot::Ik));
	const bool poleFollowsIk = data.inputValue(aPoleSpace).asShort() == 1;
	const MMatrix mPoleDelta = poleFollowsIk
		? MathUtils::computeDeltaFromRest(mRest[TwoBoneIkSlot::Ik], ikStep.mWorld)
		: readChildPlugParentDelta(hChildPlugs, TwoBoneIkSlot::Pole);
	const ControlSolveStep poleStep = solveControlStep(mLocal[TwoBoneIkSlot::Pole], mRest[TwoBoneIkSlot::Pole], mPoleDelta);
	const ControlSolveStep settingsStep = solveControlStep(mLocal[TwoBoneIkSlot::Settings], mRest[TwoBoneIkSlot::Settings],
		readChildPlugParentDelta(hChildPlugs, TwoBoneIkSlot::Settings));

	outResult.mControlOPM[TwoBoneIkSlot::Ik] = ikStep.mOPM;
	outResult.mControlOPM[TwoBoneIkSlot::Pole] = poleStep.mOPM;
	outResult.mControlOPM[TwoBoneIkSlot::Settings] = settingsStep.mOPM;
	outResult.mJointWorld[TwoBoneIkSlot::Ik] = ikStep.mWorld;
	outResult.mJointWorld[TwoBoneIkSlot::Pole] = poleStep.mWorld;
	outResult.mJointWorld[TwoBoneIkSlot::Settings] = settingsStep.mWorld;

	// 3. The limb joints: FK at blend 0 and IK at blend 1 exactly, the solve skipped at 0, a blend between.
	const double blend = data.inputValue(aIkFkBlend).asDouble();
	if (blend <= 0.0)
	{
		for (unsigned int slot = 0; slot < TwoBoneIkSlot::LimbCount; ++slot)
		{
			outResult.mJointWorld[slot] = mFkWorld[slot];
		}
		return MStatus::kSuccess;
	}

	const MVector vRoot = MathUtils::translationOf(mFkWorld[TwoBoneIkSlot::Upper]);   // the start control's rotation does nothing in IK
	TwoBoneIkInput input;
	input.root = vRoot;
	input.handle = MathUtils::translationOf(ikStep.mWorld);
	input.pole = MathUtils::translationOf(poleStep.mWorld);
	const double2& lengths = data.inputValue(aLimbLengths).asDouble2();
	input.length1 = lengths[0];
	input.length2 = lengths[1];
	input.stretch = data.inputValue(aStretch).asDouble();
	input.soft = data.inputValue(aSoft).asDouble();
	input.pin = data.inputValue(aPin).asDouble();
	input.rollRadians = data.inputValue(aRoll).asAngle().asRadians();
	input.restPlaneAngle = data.inputValue(aRestPlaneAngle).asDouble();
	input.restBendNormal = data.inputValue(aRestBendNormal).asVector();
	input.restAxis = MathUtils::translationOf(mRest[TwoBoneIkSlot::Ik]) - MathUtils::translationOf(mRest[TwoBoneIkSlot::Upper]);

	MMatrix mFrames[TwoBoneIkSlot::LimbCount];
	computeLimbFrames(MathUtils::computeTwoBoneIk(input), vRoot, ikStep.mWorld, mFrames);

	MArrayDataHandle hRestOffset = data.inputArrayValue(aIkRestOffset, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::LimbCount; ++slot)
	{
		status = hRestOffset.jumpToElement(slot);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		const MMatrix mIkJoint = hRestOffset.inputValue().asMatrix() * mFrames[slot];
		outResult.mJointWorld[slot] = MathUtils::blendMatrices(mFkWorld[slot], mIkJoint, blend);
	}
	return MStatus::kSuccess;
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

	if (oOutputAttr != aControlOPM && oOutputAttr != aJointOPM && oOutputAttr != aOutputSocketMatrix)
	{
		return MStatus::kUnknownParameter;
	}

	MStatus status;
	TwoBoneIkSolveResult result;
	status = solve(data, result);
	CHECK_MSTATUS_AND_RETURN_IT(status);

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

ModuleInverseResult TwoBoneIkNode::inverse(const ModuleInverseContext& context, const ModuleInverseInput& input)
{
	ModuleInverseResult result;
	result.controlLocal.resize(TwoBoneIkSlot::Count);
	result.socketWorld.resize(TwoBoneIkSlot::Count);
	result.solved = false;
	result.reason = "the TwoBoneIk inverse is not built yet (phase 5), so its controls come from the sidecar or rest";
	for (unsigned int slot = 0; slot < TwoBoneIkSlot::Count; ++slot)
	{
		result.socketWorld[slot] = RigModuleNodeBase::solveControlStep(input.fallbackLocal[slot], context.controlRestWorld[slot], input.parentDelta[slot]).mWorld;
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
		MFnDependencyNode fnJoint(input.oJoint);

		status = TwoBoneIkNode::wireJoint(oModule, input.oJoint, slot, input.mRestWorld, dagMod);
		if (!status)
		{
			result.status = status;
			return result;
		}

		oControls[slot] = RigControlNode::createRigControl(dagMod, fnJoint.name(), shapeType, side, input.controlMarkup, MEulerRotation::kXYZ);
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
	if (!readModuleArgs(args, { "joints", "ik", "pole", "settings" }, ctx, ikArgs))
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

	// claimJoint rejects a joint picked twice, which also covers "six distinct joints". Claim every selector even after a
	// failure, so the report lists every bad one at once.
	std::vector<const JointSelector*> selectors;
	for (const JointSelector& selector : ikArgs.joints)
	{
		selectors.push_back(&selector);
	}
	selectors.insert(selectors.end(), { &ikArgs.ik, &ikArgs.pole, &ikArgs.settings });

	std::array<TwoBoneIkSlotInput, TwoBoneIkSlot::Count> slots;
	std::size_t slotIndex = 0;
	for (const JointSelector* pSelector : selectors)
	{
		// With the wrong joint count the first three aren't the limb, so they are claimed into a scratch input.
		TwoBoneIkSlotInput scratch;
		TwoBoneIkSlotInput& slot = valid ? slots[slotIndex] : scratch;
		valid = claimJoint(ctx, *pSelector, slot.oJoint, slot.mRestWorld) && valid;
		slot.controlMarkup = makeControlMarkup(pSelector->side, pSelector->usage);
		++slotIndex;
	}
	if (!valid)
	{
		return false;
	}

	// A degenerate rest pose gives the solve no bones, no axis or no plane.
	const MVector vRoot = MathUtils::translationOf(slots[TwoBoneIkSlot::Upper].mRestWorld);
	const MVector vMid = MathUtils::translationOf(slots[TwoBoneIkSlot::Lower].mRestWorld);
	const MVector vEnd = MathUtils::translationOf(slots[TwoBoneIkSlot::End].mRestWorld);
	const MVector vHandle = MathUtils::translationOf(slots[TwoBoneIkSlot::Ik].mRestWorld);
	const MVector vPole = MathUtils::translationOf(slots[TwoBoneIkSlot::Pole].mRestWorld);
	if ((vMid - vRoot).length() < kMinBoneLength || (vEnd - vMid).length() < kMinBoneLength)
	{
		ctx.report.addError(argsPath + ".joints", "the limb's joints must not sit on each other: a bone has no length");
		return false;
	}
	if ((vHandle - vEnd).length() > kMaxIkEndOffset)
	{
		ctx.report.addError(argsPath + ".ik", "the ik joint must sit at the end joint (an offset IK handle isn't supported)");
		return false;
	}
	const MVector vToPole = vPole - vRoot;
	const MVector vToHandle = vHandle - vRoot;
	if (vToHandle.length() < kMinBoneLength || vToPole.length() < kMinBoneLength || (vToPole.normal() ^ vToHandle.normal()).length() < kMinPoleSine)
	{
		ctx.report.addError(argsPath + ".pole", "the pole joint lies on the line from the root to the end, so the bend plane is undefined");
		return false;
	}

	const TwoBoneIkRest rest = computeRest(slots);

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
