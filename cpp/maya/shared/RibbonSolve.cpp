#include "maya/shared/RibbonSolve.h"
#include "core/Constants.h"
#include "maya/nodes/Constraints.h"
#include "maya/shared/Utilities.h"

#include <utility>

#include <maya/MAngle.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MPlug.h>
#include <maya/MPxNode.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MVector.h>

void RibbonSolve::evaluate(
	const RibbonMath::Setup& setup,
	const RibbonMath::Params& params,
	const MMatrix* mRest,
	const MMatrix* mLocal,
	const MMatrix& mStartDelta,
	const MMatrix& mEndDelta,
	Result& outSolve,
	const MMatrix* pAnchorWorld)
{
	const int controlCount = setup.controlCount;

	// The curve is solved in the rest world, where the start and end sit at their rests, and the start's parent motion carries
	// the result. The end follows its own parent's motion, so a socket from another module can drive it (section 3.4).
	RibbonMath::Input input;
	for (int j = 0; j < controlCount; ++j)
	{
		input.local[j] = MathUtils::toMat4(mLocal[j]);
	}
	input.startOPM = MathUtils::toMat4(mRest[0]);
	// With an fk chain the whole ribbon rides the start's parent and the end rides the chain, so the end has no motion of its own.
	const int fkFirst = controlCount + setup.bindCount;
	for (int k = 0; k < setup.fkCount; ++k)
	{
		if (pAnchorWorld != nullptr)
		{
			input.fkWorld[k] = MathUtils::toMat4(pAnchorWorld[k]);
		}
		else
		{
			input.fkLocal[k] = MathUtils::toMat4(mLocal[fkFirst + k]);
		}
	}
	// The plug deltas are motions in world axes (endOPM = endRest * motion), so the end's twist about its own X is read from the
	// motion in the end control's frame: rest rotation * delta * rest rotation^-1.
	const MMatrix mEndMotion = setup.fkCount > 0 ? MMatrix::identity : mEndDelta * mStartDelta.inverse();
	const MMatrix mEndRestRotation = MTransformationMatrix(mRest[controlCount - 1]).asRotateMatrix();
	input.endDelta = MathUtils::toMat4(mEndRestRotation * mEndMotion * mEndRestRotation.inverse());
	input.endOPM = MathUtils::toMat4(mRest[controlCount - 1] * mEndMotion);

	RibbonMath::Output output;
	RibbonMath::evaluate(setup, params, input, output);

	for (int j = 0; j < controlCount; ++j)
	{
		outSolve.mControlOPM[j] = MathUtils::toMMatrix(output.controlOPM[j]) * mStartDelta;
		outSolve.mWorld[j] = mLocal[j] * outSolve.mControlOPM[j];
	}
	for (int i = 0; i < setup.bindCount; ++i)
	{
		const unsigned int slot = static_cast<unsigned int>(controlCount + i);
		outSolve.mControlOPM[slot] = MathUtils::toMMatrix(output.bindOPM[i]) * mStartDelta;
		outSolve.mWorld[slot] = mLocal[slot] * outSolve.mControlOPM[slot];
	}
	if (pAnchorWorld != nullptr)
	{
		return;
	}
	for (int k = 0; k < setup.fkCount; ++k)
	{
		const unsigned int slot = static_cast<unsigned int>(fkFirst + k);
		outSolve.mControlOPM[slot] = MathUtils::toMMatrix(output.fkOPM[k]) * mStartDelta;
		outSolve.mWorld[slot] = mLocal[slot] * outSolve.mControlOPM[slot];
	}
}

const std::array<RibbonSolve::ChannelSpec, RibbonChannel::Count>& RibbonSolve::channelSpecs()
{
	static const std::array<ChannelSpec, RibbonChannel::Count> specs =
	{
		ChannelSpec{ RigAttr::StartTwist, 0.0, 0.0, 0.0, true, true },
		ChannelSpec{ RigAttr::EndTwist, 0.0, 0.0, 0.0, true, true },
		ChannelSpec{ RigAttr::SineAmplitude, 0.0, -5.0, 5.0, false, false },
		ChannelSpec{ RigAttr::SineFrequency, 1.0, 0.0, 20.0, false, false },
		ChannelSpec{ RigAttr::SinePhase, 0.0, -20.0, 20.0, false, false },
		ChannelSpec{ RigAttr::SineOrient, 0.0, 0.0, 0.0, true, true },
		ChannelSpec{ RigAttr::SineStartFalloff, 0.2, 0.0, 1.0, false, false },
		ChannelSpec{ RigAttr::SineEndFalloff, 0.2, 0.0, 1.0, false, false },
		ChannelSpec{ RigAttr::StartAimBlend, 0.0, 0.0, 1.0, false, false },
		ChannelSpec{ RigAttr::EndAimBlend, 0.0, 0.0, 1.0, false, false },
		ChannelSpec{ RigAttr::BendRoundness, 1.0, 0.0, 2.0, false, false },
		ChannelSpec{ RigAttr::VolumePreserve, 0.0, 0.0, 1.0, false, false },
		ChannelSpec{ RigAttr::ScaleStart, 1.0, 0.0, 1.0, false, false },
		ChannelSpec{ RigAttr::ScaleMid, 1.0, 0.0, 1.0, false, false },
		ChannelSpec{ RigAttr::ScaleEnd, 1.0, 0.0, 1.0, false, false },
		ChannelSpec{ RigAttr::BendSoftness, 0.0, 0.0, 1.0, false, false }
	};
	return specs;
}

bool RibbonSolve::hasChannel(unsigned int k, bool anchored)
{
	if (anchored)
	{
		return k != RibbonChannel::StartAimBlend && k != RibbonChannel::EndAimBlend && k != RibbonChannel::BendRoundness;
	}
	return k != RibbonChannel::BendSoftness;
}

void RibbonSolve::anchorFrames(const MMatrix* mRest, const MMatrix* mWorld, short startRotation, short endRotation, MMatrix* outFrames)
{
	constexpr int SwingType{ ConstraintTypes::indexOf(ConstraintTypes::Swing) };
	constexpr int TwistType{ ConstraintTypes::indexOf(ConstraintTypes::Twist) };
	if (startRotation == AnchorRotation::Swing)
	{
		const MMatrix mDelta = MathUtils::computeDeltaFromRest(mRest[0], mWorld[0]);
		outFrames[0] = mRest[0] * RigConstraintNode::computeDelta(SwingType, mRest[0], mDelta, mDelta);
	}
	else
	{
		outFrames[0] = mWorld[0];
	}
	outFrames[1] = mWorld[1];
	if (endRotation == AnchorRotation::Twist)
	{
		outFrames[2] = mRest[2] * RigConstraintNode::computeDelta(
			TwistType, mRest[2], MathUtils::computeDeltaFromRest(mRest[1], mWorld[1]), MathUtils::computeDeltaFromRest(mRest[2], mWorld[2]));
	}
	else
	{
		outFrames[2] = mWorld[2];
	}
}

bool RibbonSolve::setAnchors(RibbonMath::Setup& setup, const MMatrix* mAnchorRest)
{
	setup.fkAnchors = true;
	for (unsigned int k = 0; k < AnchorCount; ++k)
	{
		setup.fkRest[k] = MathUtils::toMat4(mAnchorRest[k]);
	}
	if (!RibbonMath::fkKnots(setup.fkRest, static_cast<int>(AnchorCount), setup.fkKnotU))
	{
		return false;
	}
	RibbonMath::markAnchorCorners(setup);
	return true;
}

void RibbonSolve::readChannels(MDataBlock& data, const MObject* attrs, const ChannelSpec* specs, unsigned int count, double* outValues)
{
	for (unsigned int k = 0; k < count; ++k)
	{
		MDataHandle hChannel = data.inputValue(attrs[k]);
		outValues[k] = specs[k].isAngle ? hChannel.asAngle().asRadians() : hChannel.asDouble();
	}
}

RibbonMath::Params RibbonSolve::paramsFromChannels(const double* values)
{
	RibbonMath::Params params;
	params.startTwist = values[RibbonChannel::StartTwist];
	params.endTwist = values[RibbonChannel::EndTwist];
	params.sineAmplitude = values[RibbonChannel::SineAmplitude];
	params.sineFrequency = values[RibbonChannel::SineFrequency];
	params.sinePhase = values[RibbonChannel::SinePhase];
	params.sineOrient = values[RibbonChannel::SineOrient];
	params.sineStartFalloff = values[RibbonChannel::SineStartFalloff];
	params.sineEndFalloff = values[RibbonChannel::SineEndFalloff];
	params.startAimBlend = values[RibbonChannel::StartAimBlend];
	params.endAimBlend = values[RibbonChannel::EndAimBlend];
	params.bendRoundness = values[RibbonChannel::BendRoundness];
	params.volumePreserve = values[RibbonChannel::VolumePreserve];
	params.scaleStart = values[RibbonChannel::ScaleStart];
	params.scaleMid = values[RibbonChannel::ScaleMid];
	params.scaleEnd = values[RibbonChannel::ScaleEnd];
	params.cornerSoftness = values[RibbonChannel::BendSoftness];
	return params;
}

MStatus RibbonSolve::createRestAttributes(const RestAttrs& attrs)
{
	MStatus status;
	MFnNumericAttribute nAttr;
	MFnMatrixAttribute mAttr;

	attrs.ikControlCount = nAttr.create(RigAttr::IkControlCount, RigAttr::IkControlCount, MFnNumericData::kInt, 0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(attrs.ikControlCount));

	for (const auto& [pAttr, name] : { std::pair<MObject*, const char*>{ &attrs.knotU, RigAttr::KnotU }, { &attrs.bindU, RigAttr::BindU },
		{ &attrs.fkRollRest, RigAttr::FkRollRest }, { &attrs.fkRollRestIn, RigAttr::FkRollRestIn } })
	{
		*pAttr = nAttr.create(name, name, MFnNumericData::kDouble, 0.0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setArray(true));
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(*pAttr));
	}

	// Its default is 1 (all of the roll), so a scene file keeps only the shares a twistRamp changed.
	attrs.fkRollShare = nAttr.create(RigAttr::FkRollShare, RigAttr::FkRollShare, MFnNumericData::kDouble, 1.0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setArray(true));
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(attrs.fkRollShare));

	attrs.restLength = nAttr.create(RigAttr::RestLength, RigAttr::RestLength, MFnNumericData::kDouble, 1.0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(attrs.restLength));

	attrs.chainSign = nAttr.create(RigAttr::ChainSign, RigAttr::ChainSign, MFnNumericData::kDouble, 1.0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
	CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(attrs.chainSign));

	for (const auto& [pAttr, name] : { std::pair<MObject*, const char*>{ &attrs.restUp, RigAttr::RestUp }, { &attrs.restTangent, RigAttr::RestTangent } })
	{
		*pAttr = nAttr.create(name, name, MFnNumericData::k3Double, 0.0, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(nAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(*pAttr));
	}

	for (const auto& [pAttr, name] : { std::pair<MObject*, const char*>{ &attrs.ikRestOffset, RigAttr::IkRestOffset }, { &attrs.bindRestOffset, RigAttr::BindRestOffset } })
	{
		*pAttr = mAttr.create(name, name, MFnMatrixAttribute::kDouble, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		CHECK_MSTATUS_AND_RETURN_IT(mAttr.setArray(true));
		CHECK_MSTATUS_AND_RETURN_IT(mAttr.setStorable(true));
		CHECK_MSTATUS_AND_RETURN_IT(MPxNode::addAttribute(*pAttr));
	}
	return MStatus::kSuccess;
}

void RibbonSolve::readSetup(MDataBlock& data, const RestAttrs& attrs, int controlCount, int bindCount, int fkCount, RibbonMath::Setup& setup)
{
	setup.controlCount = controlCount;
	setup.bindCount = bindCount;
	setup.fkCount = fkCount;
	setup.restLength = data.inputValue(attrs.restLength).asDouble();
	setup.chainSign = data.inputValue(attrs.chainSign).asDouble();
	{
		const MVector vUp = data.inputValue(attrs.restUp).asVector();
		const MVector vTangent = data.inputValue(attrs.restTangent).asVector();
		setup.restUp = { vUp.x, vUp.y, vUp.z };
		setup.restTangent = { vTangent.x, vTangent.y, vTangent.z };
	}
	MStatus knotStatus;
	MStatus bindUStatus;
	MStatus ikOffsetStatus;
	MStatus bindOffsetStatus;
	MArrayDataHandle hKnotU = data.inputArrayValue(attrs.knotU, &knotStatus);
	MArrayDataHandle hBindU = data.inputArrayValue(attrs.bindU, &bindUStatus);
	MArrayDataHandle hIkOffset = data.inputArrayValue(attrs.ikRestOffset, &ikOffsetStatus);
	MArrayDataHandle hBindOffset = data.inputArrayValue(attrs.bindRestOffset, &bindOffsetStatus);
	for (int j = 0; j < controlCount; ++j)
	{
		if (knotStatus && hKnotU.jumpToElement(j))
		{
			setup.knotU[j] = hKnotU.inputValue().asDouble();
		}
		if (ikOffsetStatus && hIkOffset.jumpToElement(j))
		{
			setup.ikOffset[j] = MathUtils::toMat4(hIkOffset.inputValue().asMatrix());
		}
	}
	for (int i = 0; i < bindCount; ++i)
	{
		if (bindUStatus && hBindU.jumpToElement(i))
		{
			setup.bindU[i] = hBindU.inputValue().asDouble();
		}
		if (bindOffsetStatus && hBindOffset.jumpToElement(i))
		{
			setup.bindOffset[i] = MathUtils::toMat4(hBindOffset.inputValue().asMatrix());
		}
	}
	if (fkCount > 0)
	{
		MStatus shareStatus;
		MArrayDataHandle hShare = data.inputArrayValue(attrs.fkRollShare, &shareStatus);
		for (int j = 0; j < controlCount; ++j)
		{
			if (shareStatus && hShare.jumpToElement(j))
			{
				setup.fkRollShare[j] = hShare.inputValue().asDouble();
			}
		}
		MStatus rollStatus;
		MStatus rollInStatus;
		MArrayDataHandle hRoll = data.inputArrayValue(attrs.fkRollRest, &rollStatus);
		MArrayDataHandle hRollIn = data.inputArrayValue(attrs.fkRollRestIn, &rollInStatus);
		for (int k = 0; k < fkCount; ++k)
		{
			if (rollStatus && hRoll.jumpToElement(k))
			{
				setup.fkRollRest[k] = hRoll.inputValue().asDouble();
			}
			if (rollInStatus && hRollIn.jumpToElement(k))
			{
				setup.fkRollRestIn[k] = hRollIn.inputValue().asDouble();
			}
		}
	}
}

void RibbonSolve::applyStoredShares(const std::vector<double>& shares, RibbonMath::Setup& setup)
{
	const std::size_t count = std::min<std::size_t>(shares.size(), RibbonMath::MaxControls);
	for (std::size_t j = 0; j < count; ++j)
	{
		setup.fkRollShare[j] = shares[j];
	}
}

void RibbonSolve::writeRestData(const MObject& oModule, const RestAttrs& attrs, const RibbonMath::Setup& setup, MDagModifier& dagMod)
{
	dagMod.newPlugValueInt(MPlug(oModule, attrs.ikControlCount), setup.controlCount);
	for (int j = 0; j < setup.controlCount; ++j)
	{
		dagMod.newPlugValueDouble(MPlug(oModule, attrs.knotU).elementByLogicalIndex(j), setup.knotU[j]);
		MPlug pOffset = MPlug(oModule, attrs.ikRestOffset).elementByLogicalIndex(j);
		DagUtils::setMatrixPlug(pOffset, MathUtils::toMMatrix(setup.ikOffset[j]), dagMod);
	}
	for (int i = 0; i < setup.bindCount; ++i)
	{
		dagMod.newPlugValueDouble(MPlug(oModule, attrs.bindU).elementByLogicalIndex(i), setup.bindU[i]);
		MPlug pOffset = MPlug(oModule, attrs.bindRestOffset).elementByLogicalIndex(i);
		DagUtils::setMatrixPlug(pOffset, MathUtils::toMMatrix(setup.bindOffset[i]), dagMod);
	}
	if (setup.fkCount > 0)
	{
		for (int j = 0; j < setup.controlCount; ++j)
		{
			dagMod.newPlugValueDouble(MPlug(oModule, attrs.fkRollShare).elementByLogicalIndex(j), setup.fkRollShare[j]);
		}
	}
	for (int k = 0; k < setup.fkCount; ++k)
	{
		dagMod.newPlugValueDouble(MPlug(oModule, attrs.fkRollRest).elementByLogicalIndex(k), setup.fkRollRest[k]);
		dagMod.newPlugValueDouble(MPlug(oModule, attrs.fkRollRestIn).elementByLogicalIndex(k), setup.fkRollRestIn[k]);
	}
	dagMod.newPlugValueDouble(MPlug(oModule, attrs.restLength), setup.restLength);
	dagMod.newPlugValueDouble(MPlug(oModule, attrs.chainSign), setup.chainSign);
	for (const auto& [oAttr, vValue] : { std::pair<const MObject&, RibbonMath::Vec3>{ attrs.restUp, setup.restUp }, { attrs.restTangent, setup.restTangent } })
	{
		MPlug pVector(oModule, oAttr);
		dagMod.newPlugValueDouble(pVector.child(0), vValue.x);
		dagMod.newPlugValueDouble(pVector.child(1), vValue.y);
		dagMod.newPlugValueDouble(pVector.child(2), vValue.z);
	}
}
