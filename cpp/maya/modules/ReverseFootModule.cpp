#include "maya/modules/ReverseFootModule.h"
#include "core/Constants.h"
#include "core/FootSolve.h"
#include "core/NamingConvention.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/Side.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

#include <maya/MAngle.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MDagModifier.h>
#include <maya/MDataBlock.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MMatrix.h>
#include <maya/MPlug.h>
#include <maya/MVector.h>

MTypeId ReverseFootModuleNode::id(0x002194);
std::array<MObject, 11> ReverseFootModuleNode::aChannels;
MObject ReverseFootModuleNode::aPivotRoles;
MObject ReverseFootModuleNode::aPivotPositions;
MObject ReverseFootModuleNode::aAnkleRestWorld;
MObject ReverseFootModuleNode::aAnkleSocketMatrix;
MObject ReverseFootModuleNode::aAnklePlugMatrix;
MObject ReverseFootModuleNode::aAnklePlugRestMatrix;
MObject ReverseFootModuleNode::aAnklePlugConnected;
MObject ReverseFootModuleNode::aIkFkDriver;
MObject ReverseFootModuleNode::aIkFkBlend;
MObject ReverseFootModuleNode::aFkVisible;
MObject ReverseFootModuleNode::aIkVisible;

namespace
{
	// The toe FK control hides at blend 1 and the foot control at blend 0, the rule TwoBoneIk uses for its FK and IK controls. The foot's
	// channels have nothing to move at blend 0: the Limb ignores ikTarget, and the toe is the FK toe.
	bool isVisible(bool fk, double blend)
	{
		return fk ? blend < 1.0 : blend > 0.0;
	}

	// A pivot marker whose rotation differs from the world's by more than this (per matrix element, about 0.06 degrees)
	// is reported as not world aligned.
	constexpr double kWorldAlignedTolerance{ 1e-3 };

	static_assert(ReverseFootSlot::PivotCap == FootSolve::kRoleCount, "the module stores one pivot per role, the whole chainOrder");

	// The 11 channels, in the order of ReverseFootModuleNode::aChannels: name, the FootSolve field it is, its rest in degrees, and whether
	// a mirror negates it (D74 section 8). The member pointer is what ties a channel to its field, so reordering this table cannot swap
	// two channels in the solve. Names are the animator's, public API.
	struct ChannelSpec
	{
		const char* name;
		double FootSolve::Channels::* field;
		double restDegrees;
		bool negate;
	};
	constexpr std::array<ChannelSpec, 11> kChannels
	{
		ChannelSpec{ RigAttr::FootRoll, &FootSolve::Channels::footRoll, 0.0, false },
		ChannelSpec{ RigAttr::BreakAngle, &FootSolve::Channels::breakAngle, 30.0, false },
		ChannelSpec{ RigAttr::HeelRaise, &FootSolve::Channels::heelRaise, 0.0, false },
		ChannelSpec{ RigAttr::HeelSwivel, &FootSolve::Channels::heelSwivel, 0.0, true },
		ChannelSpec{ RigAttr::BallRaise, &FootSolve::Channels::ballRaise, 0.0, false },
		ChannelSpec{ RigAttr::BallSwivel, &FootSolve::Channels::ballSwivel, 0.0, true },
		ChannelSpec{ RigAttr::BallRoll, &FootSolve::Channels::ballRoll, 0.0, true },
		ChannelSpec{ RigAttr::FootBank, &FootSolve::Channels::bank, 0.0, true },
		ChannelSpec{ RigAttr::ToeRaise, &FootSolve::Channels::toeRaise, 0.0, false },
		ChannelSpec{ RigAttr::ToeSwivel, &FootSolve::Channels::toeSwivel, 0.0, true },
		ChannelSpec{ RigAttr::ToeTap, &FootSolve::Channels::toeTap, 0.0, false },
	};
}

ReverseFootModuleNode::ReverseFootModuleNode()
{
}
ReverseFootModuleNode::~ReverseFootModuleNode()
{
}

void* ReverseFootModuleNode::creator()
{
	return new ReverseFootModuleNode();
}

MStatus ReverseFootModuleNode::initialize()
{
	MStatus status;
	MFnNumericAttribute nAttr;
	MFnUnitAttribute uAttr;
	MFnMatrixAttribute mAttr;
	MFnTypedAttribute tAttr;

	status = initializeSharedAttributes();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// The channels, connected from the foot control at build.
	for (std::size_t k = 0; k < kChannels.size(); ++k)
	{
		aChannels[k] = uAttr.create(kChannels[k].name, kChannels[k].name, MFnUnitAttribute::kAngle,
			MAngle(kChannels[k].restDegrees, MAngle::kDegrees).asRadians(), &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(uAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aChannels[k]));
	}

	// The pivots at rest: a role and a position per pivot, in chain order. Stored data the solve reads.
	aPivotRoles = nAttr.create(RigAttr::PivotRoles, RigAttr::PivotRoles, MFnNumericData::kInt, 0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setArray(true));
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aPivotRoles));
	aPivotPositions = mAttr.create(RigAttr::PivotPositions, RigAttr::PivotPositions, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setArray(true));
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aPivotPositions));
	aAnkleRestWorld = mAttr.create(RigAttr::AnkleRestWorld, RigAttr::AnkleRestWorld, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aAnkleRestWorld));

	// The ankle socket, on an output array of its own.
	aAnkleSocketMatrix = mAttr.create(RigAttr::AnkleSocketMatrix, RigAttr::AnkleSocketMatrix, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setArray(true));
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setUsesArrayDataBuilder(true));
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setWritable(false));
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(false));
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setWorldSpace(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aAnkleSocketMatrix));

	// The ankle child plug, on inputs of its own. Stage 1 never reads it.
	aAnklePlugMatrix = mAttr.create(RigAttr::AnklePlugMatrix, RigAttr::AnklePlugMatrix, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aAnklePlugMatrix));
	aAnklePlugRestMatrix = mAttr.create(RigAttr::AnklePlugRestMatrix, RigAttr::AnklePlugRestMatrix, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aAnklePlugRestMatrix));
	aAnklePlugConnected = nAttr.create(RigAttr::AnklePlugConnected, RigAttr::AnklePlugConnected, MFnNumericData::kBoolean, false, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aAnklePlugConnected));

	aIkFkDriver = tAttr.create(RigAttr::IkFkDriver, RigAttr::IkFkDriver, MFnData::kString, MObject::kNullObj, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(tAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aIkFkDriver));

	// The blend the foot follows: the driver's ikFkBlend is connected to it in pass 2. Not keyable and not in the channel box, and a connected
	// attribute can't be set, so an animator reaches the blend only through the limb (one switch for one limb, D59 step 3).
	aIkFkBlend = nAttr.create(RigAttr::IkFkBlend, RigAttr::IkFkBlend, MFnNumericData::kDouble, 1.0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setMin(0.0));
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setMax(1.0));
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setKeyable(false));
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setChannelBox(false));
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aIkFkBlend));

	// The outputs that drive the controls' visibility, a function of the blend alone.
	for (MObject* pAttr : { &aFkVisible, &aIkVisible })
	{
		const char* name = pAttr == &aFkVisible ? RigAttr::FkVisible : RigAttr::IkVisible;
		*pAttr = nAttr.create(name, name, MFnNumericData::kDouble, 1.0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setWritable(false));
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(false));
		CHECK_MSTATUS_AND_RETURN_IT(addAttribute(*pAttr));
	}

	// AFFECTS RELATIONSHIPS. Not declareStandardAffects: it wires the inputs to every output, and each output here is affected by exactly what
	// its compute reads. The ankle socket (stage 1) is affected by the ik plug, the channels and the pivots, and never by the ankle plug: that
	// is what lets the foot feed the Limb's ikTarget and take the Limb's end socket (blindSockets in makeInterface). The controls' offsets and
	// the toe (stage 2) read the ankle plug too.
	const MObject oAnklePlug[] = { aAnklePlugMatrix, aAnklePlugRestMatrix, aAnklePlugConnected };
	for (const MObject& oInput : { aControlMatrix, aControlRestWorld, aChildPlugParentMatrix, aChildPlugParentRestMatrix })
	{
		attributeAffects(oInput, aControlOPM);
	}
	for (const MObject& oInput : oAnklePlug)
	{
		attributeAffects(oInput, aControlOPM);
	}
	std::vector<MObject> oAnkleInputs(aChannels.begin(), aChannels.end());
	oAnkleInputs.insert(oAnkleInputs.end(), { aChildPlugParentMatrix, aChildPlugParentRestMatrix, aPivotRoles, aPivotPositions, aAnkleRestWorld });
	for (const MObject& oInput : oAnkleInputs)
	{
		attributeAffects(oInput, aAnkleSocketMatrix);
	}
	std::vector<MObject> oToeInputs(oAnkleInputs);
	oToeInputs.insert(oToeInputs.end(), { aControlMatrix, aControlRestWorld, aIkFkBlend, oAnklePlug[0], oAnklePlug[1], oAnklePlug[2] });
	for (const MObject& oInput : oToeInputs)
	{
		attributeAffects(oInput, aJointOPM);
		attributeAffects(oInput, aOutputSocketMatrix);
	}
	attributeAffects(aIkFkBlend, aFkVisible);
	attributeAffects(aIkFkBlend, aIkVisible);
	return MStatus::kSuccess;
}

MStatus ReverseFootModuleNode::compute(const MPlug& plug, MDataBlock& data)
{
	const MObject oOutputAttr = plug.attribute();
	if (oOutputAttr == aControlOPM)
	{
		return computeControl(plug, data);
	}
	if (oOutputAttr == aAnkleSocketMatrix)
	{
		return computeAnkle(plug, data);
	}
	if (oOutputAttr == aJointOPM || oOutputAttr == aOutputSocketMatrix)
	{
		return computeToe(plug, data);
	}
	if (oOutputAttr == aFkVisible || oOutputAttr == aIkVisible)
	{
		const bool isVisibleNow = isVisible(oOutputAttr == aFkVisible, data.inputValue(aIkFkBlend).asDouble());
		data.outputValue(oOutputAttr).setDouble(isVisibleNow ? 1.0 : 0.0);
		data.setClean(plug);
		return MStatus::kSuccess;
	}
	return MStatus::kUnknownParameter;
}

bool ReverseFootModuleNode::readPivots(MDataBlock& data, std::array<FootSolve::Pivot, ReverseFootSlot::PivotCap>& outPivots, std::size_t& outCount)
{
	MStatus status;
	outCount = 0;
	MArrayDataHandle hRoles = data.inputArrayValue(aPivotRoles, &status);
	CHECK_MSTATUS_AND_RETURN(status, false);
	MArrayDataHandle hPositions = data.inputArrayValue(aPivotPositions, &status);
	CHECK_MSTATUS_AND_RETURN(status, false);
	bool isWhole = hRoles.elementCount() == hPositions.elementCount() && hRoles.elementCount() <= outPivots.size();
	for (unsigned int i = 0; isWhole && i < hRoles.elementCount(); ++i)
	{
		isWhole = hRoles.jumpToElement(i) == MStatus::kSuccess && hPositions.jumpToElement(i) == MStatus::kSuccess;
		if (isWhole)
		{
			const MMatrix mPosition = hPositions.inputValue().asMatrix();
			outPivots[outCount].role = static_cast<FootSolve::Role>(hRoles.inputValue().asInt());
			outPivots[outCount].position = { mPosition(3, 0), mPosition(3, 1), mPosition(3, 2) };
			++outCount;
		}
	}
	return isWhole && outCount > 0;
}

FootSolve::Channels ReverseFootModuleNode::readChannels(MDataBlock& data)
{
	FootSolve::Channels channels;
	for (std::size_t k = 0; k < kChannels.size(); ++k)
	{
		channels.*(kChannels[k].field) = VectorMath::toDegrees(data.inputValue(aChannels[k]).asDouble());
	}
	return channels;
}

// Where the controls sit. The foot control is its rest carried by the handle's motion (the ik plug), the toe FK control its rest carried by
// the actual ankle's motion (the ankle plug), each with its own local on top. Reads the controls, the ik plug and the ankle plug only.
MStatus ReverseFootModuleNode::computeControl(const MPlug& plug, MDataBlock& data)
{
	MStatus status;
	MArrayDataHandle hControlInputData = data.inputArrayValue(aControlInputData, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MMatrix mFootLocal = MMatrix::identity;
	MMatrix mFootRest = MMatrix::identity;
	MMatrix mToeLocal = MMatrix::identity;
	MMatrix mToeRest = MMatrix::identity;
	if (readControlInput(hControlInputData, ReverseFootSlot::Foot, mFootLocal, mFootRest) != MStatus::kSuccess
		|| readControlInput(hControlInputData, ReverseFootSlot::ToeFk, mToeLocal, mToeRest) != MStatus::kSuccess)
	{
		// Not built yet: the build creates the controls after the node.
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	MArrayDataHandle hChildPlugs = data.inputArrayValue(aChildPlugs, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	const MMatrix mHandleDelta = readChildPlugParentDelta(hChildPlugs, ReverseFootSlot::IkPlug);
	const MMatrix mAnkleDelta = data.inputValue(aAnklePlugConnected).asBool()
		? MathUtils::computeDeltaFromRest(data.inputValue(aAnklePlugRestMatrix).asMatrix(), data.inputValue(aAnklePlugMatrix).asMatrix())
		: MMatrix::identity;

	MArrayDataHandle hControlOPM = data.outputArrayValue(aControlOPM, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	writeOutputMatrix(hControlOPM, ReverseFootSlot::Foot, solveControlStep(mFootLocal, mFootRest, mHandleDelta).mOPM);
	writeOutputMatrix(hControlOPM, ReverseFootSlot::ToeFk, solveControlStep(mToeLocal, mToeRest, mAnkleDelta).mOPM);
	commitOutput(data, plug.node(), aControlOPM, hControlOPM);
	data.setClean(plug);
	return MStatus::kSuccess;
}

// Where the Limb's end joint should reach (the ankle socket): the ankle at rest, turned by the pivots for the channels, carried by the
// handle's motion. Reads the ik plug, the channels and the pivots, never the ankle plug and never the control (its transform is locked).
// The pivots are read from their stored arrays on every evaluation: six elements are cheaper than a cache that the evaluation manager's
// parallel mode would not invalidate, because it does not call setDependentsDirty.
MStatus ReverseFootModuleNode::computeAnkle(const MPlug& plug, MDataBlock& data)
{
	MStatus status;

	std::array<FootSolve::Pivot, ReverseFootSlot::PivotCap> pivots;
	std::size_t pivotCount = 0;
	if (!readPivots(data, pivots, pivotCount))
	{
		// Not built yet, or the roles and the positions do not pair up: the foot is left unbuilt rather than solved from half a chain.
		data.setClean(plug);
		return MStatus::kSuccess;
	}

	const FootSolve::Channels channels = readChannels(data);
	const std::span<const FootSolve::Pivot> chain(pivots.data(), pivotCount);
	const FootSolve::Frame frame = FootSolve::footFrame(chain);
	const MMatrix mFoot = MathUtils::toMMatrix(FootSolve::ankleMotion(chain, frame, channels));

	// The foot hangs from the handle: its pivots turn the ankle first, and the handle's motion carries the result.
	MArrayDataHandle hChildPlugs = data.inputArrayValue(aChildPlugs, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	const MMatrix mHandleDelta = readChildPlugParentDelta(hChildPlugs, ReverseFootSlot::IkPlug);
	const MMatrix mAnkleWorld = data.inputValue(aAnkleRestWorld).asMatrix() * mFoot * mHandleDelta;

	MArrayDataHandle hAnkle = data.outputArrayValue(aAnkleSocketMatrix, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	writeOutputMatrix(hAnkle, ReverseFootSlot::AnkleSocket, mAnkleWorld);
	commitOutput(data, plug.node(), aAnkleSocketMatrix, hAnkle);
	data.setClean(plug);
	return MStatus::kSuccess;
}

// Stage 2: the toe and toe tip joints, their sockets and the pivot sockets (design/reverse-foot.md section 7). Reads the ankle plug, the ik
// plug, the channels, the pivots, the toe FK control and the blend. Two toes are made and blended by ikFkBlend with MathUtils::blendMatrices,
// the function TwoBoneIk blends its joints with:
//  IK: the toe's rest carried by the toe pivot's motion (the pivots before it, then toeTap) and the handle's motion, and then pulled toward
//      the ankle as it really is, so it keeps the ankle-to-toe distance of the rest. A limb that reaches puts the actual ankle exactly where the
//      foot wanted it, the toe is exactly where the motion put it and the pull changes nothing. A limb that can't reach keeps the bone length
//      and aims at where the toe pivot is.
//  FK: the toe's rest carried by the actual ankle's motion since rest, with the toe FK control's local on top.
// The toe tip rides the blended toe rigidly.
MStatus ReverseFootModuleNode::computeToe(const MPlug& plug, MDataBlock& data)
{
	MStatus status;

	std::array<FootSolve::Pivot, ReverseFootSlot::PivotCap> pivots;
	std::size_t pivotCount = 0;
	MArrayDataHandle hJointRest = data.inputArrayValue(aJointRestWorld, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hControlInputData = data.inputArrayValue(aControlInputData, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MMatrix mToeFkLocal = MMatrix::identity;
	MMatrix mToeFkRest = MMatrix::identity;
	if (!readPivots(data, pivots, pivotCount)
		|| hJointRest.jumpToElement(ReverseFootSlot::ToeJoint) != MStatus::kSuccess
		|| readControlInput(hControlInputData, ReverseFootSlot::ToeFk, mToeFkLocal, mToeFkRest) != MStatus::kSuccess)
	{
		// Not built yet: the build stores the pivots, the joints' rests and the controls after it creates the node.
		data.setClean(plug);
		return MStatus::kSuccess;
	}
	const MMatrix mToeRest = hJointRest.inputValue().asMatrix();
	if (hJointRest.jumpToElement(ReverseFootSlot::ToeTipJoint) != MStatus::kSuccess)
	{
		data.setClean(plug);
		return MStatus::kSuccess;
	}
	const MMatrix mToeTipRest = hJointRest.inputValue().asMatrix();

	const FootSolve::Channels channels = readChannels(data);
	const std::span<const FootSolve::Pivot> chain(pivots.data(), pivotCount);
	const FootSolve::Frame frame = FootSolve::footFrame(chain);

	MArrayDataHandle hChildPlugs = data.inputArrayValue(aChildPlugs, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	const MMatrix mHandleDelta = readChildPlugParentDelta(hChildPlugs, ReverseFootSlot::IkPlug);
	const MMatrix mAnkleRest = data.inputValue(aAnkleRestWorld).asMatrix();
	const MMatrix mAnkleDelta = data.inputValue(aAnklePlugConnected).asBool()
		? MathUtils::computeDeltaFromRest(data.inputValue(aAnklePlugRestMatrix).asMatrix(), data.inputValue(aAnklePlugMatrix).asMatrix())
		: MMatrix::identity;

	// IK. A foot with no toe pivot has the toe ride the ankle's motion, with no tap.
	const int toeIndex = FootSolve::findPivot(chain, FootSolve::Role::Toe);
	const MMatrix mToeMotion = MathUtils::toMMatrix(toeIndex >= 0
		? FootSolve::toeMotion(chain, static_cast<std::size_t>(toeIndex), frame, channels)
		: FootSolve::ankleMotion(chain, frame, channels));
	MMatrix mIkToe = mToeRest * mToeMotion * mHandleDelta;
	const MVector vRestBone(mToeRest(3, 0) - mAnkleRest(3, 0), mToeRest(3, 1) - mAnkleRest(3, 1), mToeRest(3, 2) - mAnkleRest(3, 2));
	const MMatrix mActualAnkle = mAnkleRest * mAnkleDelta;
	const MVector vActualAnkle(mActualAnkle(3, 0), mActualAnkle(3, 1), mActualAnkle(3, 2));
	const MVector vToAim = MVector(mIkToe(3, 0), mIkToe(3, 1), mIkToe(3, 2)) - vActualAnkle;
	if (vToAim.length() > VectorMath::kTiny)
	{
		const MVector vPulled = vActualAnkle + vToAim.normal() * vRestBone.length();
		mIkToe(3, 0) = vPulled.x;
		mIkToe(3, 1) = vPulled.y;
		mIkToe(3, 2) = vPulled.z;
	}

	// FK, then the blend and the toe tip.
	const MMatrix mFkToe = solveControlStep(mToeFkLocal, mToeRest, mAnkleDelta).mWorld;
	const double blend = std::clamp(data.inputValue(aIkFkBlend).asDouble(), 0.0, 1.0);
	const MMatrix mToe = MathUtils::blendMatrices(mFkToe, mIkToe, blend);
	const MMatrix mToeTip = mToeTipRest * mToeRest.inverse() * mToe;

	MArrayDataHandle hJointOPM = data.outputArrayValue(aJointOPM, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MArrayDataHandle hSocket = data.outputArrayValue(aOutputSocketMatrix, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	writeOutputMatrix(hJointOPM, ReverseFootSlot::ToeJoint, mToe);
	writeOutputMatrix(hJointOPM, ReverseFootSlot::ToeTipJoint, mToeTip);
	writeOutputMatrix(hSocket, ReverseFootSlot::ToeSocket, mToe);
	writeOutputMatrix(hSocket, ReverseFootSlot::ToeTipSocket, mToeTip);
	// A pivot's world: where its marker sits, turned by the pivots up to and including its own, and carried by the handle.
	for (std::size_t i = 0; i < pivotCount; ++i)
	{
		const MMatrix mPivot = MathUtils::translationMatrix(pivots[i].position) * MathUtils::toMMatrix(FootSolve::foldChain(chain, i + 1, frame, channels)) * mHandleDelta;
		writeOutputMatrix(hSocket, ReverseFootSlot::FirstPivotSocket + static_cast<unsigned int>(i), mPivot);
	}
	commitOutput(data, plug.node(), aJointOPM, hJointOPM);
	commitOutput(data, plug.node(), aOutputSocketMatrix, hSocket);
	data.setClean(plug);
	return MStatus::kSuccess;
}

ModuleInverseResult ReverseFootModuleNode::inverse(const ModuleInverseContext& context, const ModuleInverseInput&)
{
	// The foot control is derived and locked, and the toe FK control is rotate only. The joints give neither a value yet: they keep the
	// sidecar's channels or the rest, which the driver writes from fallbackLocal and the sidecar. The ankle socket feeds a Limb's ikTarget,
	// which the sweep does not follow. Solving footRoll, bank, toeTap and the toe FK control from the joints is phase 5.
	ModuleInverseResult result;
	result.controlLocal.resize(context.controlRestWorld.size());
	result.socketWorld.assign(context.controlRestWorld.size(), MMatrix::identity);
	result.solved = true;
	return result;
}

std::string ReverseFootModuleNode::pivotSocketName(FootSolve::Role role)
{
	const std::string name = FootSolve::roleName(role);
	return role == FootSolve::Role::Toe || role == FootSolve::Role::ToeTip ? name + PlugAndSocketNames::PivotSuffix : name;
}

ModuleInterface ReverseFootModuleNode::makeInterface(const std::vector<FootSolve::Pivot>& chain)
{
	ModuleInterface moduleInterface;
	// Neither plug has a control, so a template constraint on either is an error (author, 2026-11-24). The ankle plug takes the Limb's end
	// socket, which follows ikTarget, so it must not be seen by the ankle socket that feeds it.
	moduleInterface.childPlugs.push_back(ModulePlugInfo{ PlugAndSocketNames::Ik, false, false });
	moduleInterface.childPlugs.push_back(ModulePlugInfo{ PlugAndSocketNames::Ankle, false, false, "", "", false, { PlugAndSocketNames::Ankle } });
	moduleInterface.sockets.push_back(PlugAndSocketNames::Ankle);
	moduleInterface.sockets.push_back(PlugAndSocketNames::Toe);
	moduleInterface.sockets.push_back(PlugAndSocketNames::ToeTip);
	for (const FootSolve::Pivot& pivot : chain)
	{
		moduleInterface.sockets.push_back(pivotSocketName(pivot.role));
	}
	return moduleInterface;
}

RigModuleBuildResult ReverseFootModuleNode::createModule(
	const MString& moduleName,
	const Side& side,
	const ShapeType& shapeType,
	const Plan& plan,
	MObject& oRigRoot,
	MDagModifier& dagMod)
{
	RigModuleBuildResult result;
	MStatus status;

	// 1. Module node and its stored rest data.
	MObject oModule = dagMod.MDGModifier::createNode(ReverseFootModuleNode::id);
	dagMod.renameNode(oModule, moduleName);
	dagMod.newPlugValueShort(MPlug(oModule, aSide), getShortFromSide(side));
	result.moduleNode = oModule;

	for (std::size_t i = 0; i < plan.chain.size(); ++i)
	{
		const unsigned int index = static_cast<unsigned int>(i);
		dagMod.newPlugValueInt(MPlug(oModule, aPivotRoles).elementByLogicalIndex(index), static_cast<int>(plan.chain[i].role));
		MPlug pPosition = MPlug(oModule, aPivotPositions).elementByLogicalIndex(index);
		DagUtils::setMatrixPlug(pPosition, MathUtils::translationMatrix(plan.chain[i].position), dagMod);
	}
	MPlug pAnkleRest(oModule, aAnkleRestWorld);
	DagUtils::setMatrixPlug(pAnkleRest, plan.mAnkleRestWorld, dagMod);
	dagMod.newPlugValueString(MPlug(oModule, aIkFkDriver), plan.ikFkDriver.c_str());

	// 2. The foot control: derived from the ball marker, with no joint (D57), world aligned.
	const MObject oControl = RigControlNode::createRigControl(dagMod, MString(plan.controlUsage.c_str()), shapeType, side, plan.controlMarkup, MEulerRotation::kXYZ);
	status = wireControl(oModule, oControl, ReverseFootSlot::Foot, plan.mFootControlRest, dagMod);
	CHECK_MSTATUS_AND_RETURN(status, result);

	// The toe FK control (phase 4): rotate only, at the toe's rest, carried by the actual ankle. It is named from the toe's usage and FK, so it
	// does not take the toe joint's own name, and its joints are the module's: the control has no joint of its own.
	const MObject oToeFk = RigControlNode::createRigControl(dagMod, MString(plan.toeFkUsage.c_str()), shapeType, side, plan.toeFkMarkup, MEulerRotation::kXYZ);
	status = wireControl(oModule, oToeFk, ReverseFootSlot::ToeFk, plan.mToeJointRests[0], dagMod);
	CHECK_MSTATUS_AND_RETURN(status, result);

	// 3. The ankle socket (no DAG transform: nothing hangs from it by DAG) and the two plugs, neither with a control.
	RigModuleSocketInfo ankleSocket;
	ankleSocket.mRestWorld = plan.mAnkleRestWorld;
	ankleSocket.outputIndex = ReverseFootSlot::AnkleSocket;
	result.parentSockets[PlugAndSocketNames::Ankle] = ankleSocket;
	DagPlugAndSocketUtils::setParentSocketNames(oModule, ReverseFootSlot::AnkleSocket, { PlugAndSocketNames::Ankle }, dagMod);
	// The solved toe joints and the pivots, on the base outputSocketMatrix from index 1. They carry worlds and have no DAG transform either.
	const auto addSocket = [&](const std::string& name, unsigned int index, const MMatrix& mRestWorld)
	{
		RigModuleSocketInfo socket;
		socket.mRestWorld = mRestWorld;
		socket.outputIndex = index;
		result.parentSockets[name] = socket;
		DagPlugAndSocketUtils::setParentSocketNames(oModule, index, { name }, dagMod);
	};
	addSocket(PlugAndSocketNames::Toe, ReverseFootSlot::ToeSocket, plan.mToeJointRests[0]);
	addSocket(PlugAndSocketNames::ToeTip, ReverseFootSlot::ToeTipSocket, plan.mToeJointRests[1]);
	for (std::size_t i = 0; i < plan.chain.size(); ++i)
	{
		addSocket(pivotSocketName(plan.chain[i].role), ReverseFootSlot::FirstPivotSocket + static_cast<unsigned int>(i), MathUtils::translationMatrix(plan.chain[i].position));
	}
	result.childPlugs[PlugAndSocketNames::Ik] = RigModuleChildPlugInfo{ MObject::kNullObj, ReverseFootSlot::IkPlug };
	DagPlugAndSocketUtils::setChildPlugName(oModule, ReverseFootSlot::IkPlug, PlugAndSocketNames::Ik, dagMod);
	result.childPlugs[PlugAndSocketNames::Ankle] = RigModuleChildPlugInfo{ MObject::kNullObj, ReverseFootSlot::AnklePlug };

	// 4. The channels, all on the foot control. The control must exist before an attribute can be added to it, and the attribute before it
	// can be wired, so the build runs the modifier between the steps, as TwoBoneIk does.
	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN(status, result);
	for (const ChannelSpec& spec : kChannels)
	{
		status = RigPoseScene::addAngleChannel(oControl, spec.name, spec.negate, dagMod, spec.restDegrees);
		CHECK_MSTATUS_AND_RETURN(status, result);
	}
	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN(status, result);
	const MFnDependencyNode fnControl(oControl);
	for (std::size_t k = 0; k < kChannels.size(); ++k)
	{
		const MPlug pControlAttr = fnControl.findPlug(kChannels[k].name, false, &status);
		CHECK_MSTATUS_AND_RETURN(status, result);
		status = dagMod.connect(pControlAttr, MPlug(oModule, aChannels[k]));
		CHECK_MSTATUS_AND_RETURN(status, result);
	}
	// The foot control carries channels only: its transform is locked and hidden. The toe FK control turns and nothing else.
	status = DagUtils::queueLockAndHideTransform(oControl, dagMod);
	CHECK_MSTATUS_AND_RETURN(status, result);
	status = DagUtils::queueLockAndHideTransform(oToeFk, dagMod, { "rx", "ry", "rz" });
	CHECK_MSTATUS_AND_RETURN(status, result);

	// The blend decides which control is visible: the toe FK control below blend 1, the foot control above blend 0.
	status = dagMod.connect(MPlug(oModule, aFkVisible), MPlug(oToeFk, MPxTransform::visibility));
	CHECK_MSTATUS_AND_RETURN(status, result);
	status = dagMod.connect(MPlug(oModule, aIkVisible), MPlug(oControl, MPxTransform::visibility));
	CHECK_MSTATUS_AND_RETURN(status, result);

	// The toe and toe tip are the module's: it drives their offset parent matrices.
	for (unsigned int i = 0; i < 2; ++i)
	{
		status = wireJoint(oModule, plan.oToeJoints[i], i, plan.mToeJointRests[i], dagMod);
		CHECK_MSTATUS_AND_RETURN(status, result);
	}
	result.blendDriver = plan.ikFkDriver;

	// 5. Network
	if (!oRigRoot.isNull())
	{
		RigModuleNodeBase::connectModuleToRigRoot(oRigRoot, dagMod, oModule, aRigRoot);
	}

	result.dagParentCallback = [](const RigModuleDagReparentArgs& args)
	{
		// The ankle plug is a matrix connection only: it has no control to reparent, and nothing reads it in stage 1.
		if (args.childPlug.plugIndex == ReverseFootSlot::AnklePlug)
		{
			MStatus plugStatus;
			const MPlug pSocket = ReverseFootModuleNode::getSocketMatrixPlug(args.oParentTargetModuleNode, args.parentSocket.outputIndex, &plugStatus);
			CHECK_MSTATUS_AND_RETURN_IT(plugStatus);
			MPlug pRest(args.oChildModuleNode, ReverseFootModuleNode::aAnklePlugRestMatrix);
			DagUtils::setMatrixPlug(pRest, args.parentSocket.mRestWorld, args.dagMod);
			plugStatus = args.dagMod.connect(pSocket, MPlug(args.oChildModuleNode, ReverseFootModuleNode::aAnklePlugMatrix));
			CHECK_MSTATUS_AND_RETURN_IT(plugStatus);
			return args.dagMod.newPlugValueBool(MPlug(args.oChildModuleNode, ReverseFootModuleNode::aAnklePlugConnected), true);
		}
		return ReverseFootModuleNode::reparentChildPlugToSocket(args);
	};
	result.status = MStatus::kSuccess;
	return result;
}

bool ReverseFootModuleNode::planFromJson(const nlohmann::json& args, PlanContext& ctx, ModulePlan& outPlan)
{
	ReverseFootArgs footArgs;
	if (!readModuleArgs(args, { "joints", "ikFkDriver" }, ctx, footArgs, { "controlName" }))
	{
		return false;
	}

	const std::string argsPath = ctx.where + "." + TemplateKey::ModuleArgs;
	if (footArgs.joints.size() != 3)
	{
		ctx.report.addError(argsPath + ".joints", "needs three joints, the ankle, the toe and the toe tip, got " + std::to_string(footArgs.joints.size()));
		return false;
	}
	bool valid = true;

	// The six pivots are the markers named by role on the foot's own side ([Left, heel]), in the fixed chain order (author,
	// 2026-11-24: every foot has all six, and the order is the module's, not the template's). A mirrored foot's side is already
	// the other one. Every missing marker is reported, so one build lists them all.
	// Only positions are read (D74): the axes come from the foot frame. A rotated marker is a warning, because a rigger
	// who turned one (to match a slanted ball line, say) would otherwise expect the roll to follow it.
	Plan plan;
	std::string rotatedMarkers;
	for (const FootSolve::Role role : FootSolve::chainOrder())
	{
		const JointSelector selector{ ctx.moduleData.side, FootSolve::roleName(role) };
		ResolvedMarker marker;
		if (!resolveMarker(ctx, selector, marker))
		{
			valid = false;
			continue;
		}
		plan.chain.push_back(FootSolve::Pivot{ role, { marker.world[12], marker.world[13], marker.world[14] } });

		MMatrix mOrientation = MathUtils::toMMatrix(marker.world);
		mOrientation(3, 0) = 0.0;
		mOrientation(3, 1) = 0.0;
		mOrientation(3, 2) = 0.0;
		if (!mOrientation.isEquivalent(MMatrix::identity, kWorldAlignedTolerance))
		{
			rotatedMarkers += (rotatedMarkers.empty() ? "" : ", ") + formatJointSelector(selector);
		}
	}
	if (!rotatedMarkers.empty())
	{
		ctx.report.addWarning(argsPath, "the markers " + rotatedMarkers + " are not world aligned, and their rotation is not used: "
			"the foot reads marker positions only, and its roll, bank and swivel axes come from the heel to toe tip direction and world up");
	}

	// The foot drives the toe and the toe tip. The ankle is a reference only: the Limb drives it, and a joint two modules drive is an error.
	if (!findJointRest(ctx, footArgs.joints[0], plan.mAnkleRestWorld))
	{
		valid = false;
	}
	for (std::size_t i = 0; i < 2; ++i)
	{
		valid = claimJoint(ctx, footArgs.joints[i + 1], plan.oToeJoints[i], plan.mToeJointRests[i]) && valid;
	}

	if (footArgs.ikFkDriver.empty())
	{
		ctx.report.addError(argsPath + ".ikFkDriver", "must name the Limb or TwoBoneIk module whose ikFkBlend the foot follows");
		valid = false;
	}
	NamingConvention::Name controlName;
	if (!NamingConvention::fromUsage(footArgs.controlName, ctx.moduleData.side, NamingConvention::Kind::Control, controlName))
	{
		ctx.report.addError(argsPath + ".controlName", "'" + footArgs.controlName + "' must be camelCase, with an optional _NN: it names the foot control");
		valid = false;
	}
	// The toe FK control is named from the toe's usage and FK ("toe" gives "toeFK"), so it is a camelCase usage of its own.
	// FK goes before a trailing _NN, so a joint usage of "ball_00" gives "ballFK_00" and the control ballFK_00_L_CTRL.
	const std::string toeBase = StringUtils::stripNumberSuffix(footArgs.joints[1].usage);
	plan.toeFkUsage = toeBase + "FK" + footArgs.joints[1].usage.substr(toeBase.size());
	NamingConvention::Name toeFkName;
	if (!NamingConvention::fromUsage(plan.toeFkUsage, ctx.moduleData.side, NamingConvention::Kind::Control, toeFkName))
	{
		ctx.report.addError(argsPath + ".joints", "the toe's usage '" + footArgs.joints[1].usage + "' with FK ('" + plan.toeFkUsage + "') must be camelCase: it names the toe FK control");
		valid = false;
	}
	// A mirrored foot follows the other side's limb (D28). The selectors' sides were swapped when the template expanded.
	plan.ikFkDriver = footArgs.ikFkDriver;
	if (footArgs.ikFkDriver.find('.') != std::string::npos)
	{
		ctx.report.addError(argsPath + ".ikFkDriver", "'" + footArgs.ikFkDriver + "' is a socket id. ikFkDriver is the limb's module name alone, for example 'leg_00_L_MOD'");
		valid = false;
	}
	else if (ctx.moduleData.isMirror())
	{
		const std::string mirrored = NamingConvention::swapSide(footArgs.ikFkDriver);
		if (mirrored.empty())
		{
			ctx.report.addError(argsPath + ".ikFkDriver", "'" + footArgs.ikFkDriver + "' has no other side to follow on a mirrored foot: it must be a module name in the naming convention with a Left or Right side");
			valid = false;
		}
		plan.ikFkDriver = mirrored;
	}
	if (!valid)
	{
		return false;
	}

	const int ballIndex = FootSolve::findPivot(plan.chain, FootSolve::Role::Ball);
	plan.mFootControlRest = MathUtils::translationMatrix(plan.chain[static_cast<std::size_t>(ballIndex)].position);
	plan.controlUsage = footArgs.controlName;
	plan.controlMarkup = makeControlMarkup(ctx.moduleData.side, footArgs.controlName);

	plan.toeFkMarkup = makeControlMarkup(ctx.moduleData.side, plan.toeFkUsage);

	outPlan.moduleInterface = makeInterface(plan.chain);
	outPlan.moduleInterface.controls.push_back(plan.controlMarkup);
	outPlan.moduleInterface.controls.push_back(plan.toeFkMarkup);
	// The foot follows the blend of the module it hangs from, which validateBlendDrivers checks.
	outPlan.moduleInterface.blendDriver = plan.ikFkDriver;
	// The Limb must take the foot's ankle as its ikTarget, or the channels move nothing (the author's solomon build did exactly that).
	outPlan.moduleInterface.driverPlug = PlugAndSocketNames::IkTarget;
	outPlan.moduleInterface.driverPlugSocket = PlugAndSocketNames::Ankle;

	const MString moduleName = ctx.moduleData.name.c_str();
	const Side side = getSideFromString(ctx.moduleData.side);
	const ShapeType shapeType = getShapeTypeFromString(ctx.moduleData.shapeType);
	outPlan.build = [moduleName, side, shapeType, plan](MObject& oRigRoot, MDagModifier& dagMod)
	{
		return ReverseFootModuleNode::createModule(moduleName, side, shapeType, plan, oRigRoot, dagMod);
	};
	return true;
}
