#include "maya/shared/RigPoseScene.h"
#include "core/Constants.h"
#include "maya/shared/RigTraversal.h"
#include "maya/shared/Utilities.h"

#include <cmath>

#include <maya/MAngle.h>
#include <maya/MDistance.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnNumericData.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MObjectArray.h>
#include <maya/MPxTransform.h>

namespace
{
	// Two values closer than this are the same pose, so a control that is already there isn't reported as changed.
	constexpr double kValueTolerance{ 1e-9 };

	MString channelName(const MObject& oControl, const MObject& oAttr)
	{
		return DagUtils::getPartialPathName(oControl) + "." + MFnAttribute(oAttr).name();
	}

	// Fills the channel's kind, type name, rest and range from a numeric attribute. False for a type that can't be a
	// channel (a compound of numbers, a char).
	bool readNumericChannel(const MObject& oAttr, RigPoseScene::Channel& channel)
	{
		MFnNumericAttribute fnAttr(oAttr);
		switch (fnAttr.unitType())
		{
		case MFnNumericData::kBoolean:
		{
			bool rest = false;
			fnAttr.getDefault(rest);
			channel.kind = RigPoseScene::ValueKind::kBool;
			channel.info.type = "bool";
			channel.rest = rest ? 1.0 : 0.0;
			return true;   // a bool has no range
		}
		case MFnNumericData::kShort:
		case MFnNumericData::kInt:
		{
			int rest = 0;
			fnAttr.getDefault(rest);
			const bool isShort = fnAttr.unitType() == MFnNumericData::kShort;
			channel.kind = isShort ? RigPoseScene::ValueKind::kShort : RigPoseScene::ValueKind::kInt;
			channel.info.type = isShort ? "short" : "long";
			channel.rest = rest;
			break;
		}
		case MFnNumericData::kFloat:
		{
			float rest = 0.0f;
			fnAttr.getDefault(rest);
			channel.kind = RigPoseScene::ValueKind::kFloat;
			channel.info.type = "float";
			channel.rest = rest;
			break;
		}
		case MFnNumericData::kDouble:
		{
			double rest = 0.0;
			fnAttr.getDefault(rest);
			channel.kind = RigPoseScene::ValueKind::kDouble;
			channel.info.type = "double";
			channel.rest = rest;
			break;
		}
		default:
			return false;
		}

		channel.info.hasMin = fnAttr.hasMin();
		channel.info.hasMax = fnAttr.hasMax();
		if (channel.info.hasMin)
		{
			fnAttr.getMin(channel.info.min);
		}
		if (channel.info.hasMax)
		{
			fnAttr.getMax(channel.info.max);
		}
		return true;
	}

	// The same for an angle or a distance. info holds UI units, channel.rest internal units.
	bool readUnitChannel(const MObject& oAttr, RigPoseScene::Channel& channel)
	{
		MFnUnitAttribute fnAttr(oAttr);
		channel.info.hasMin = fnAttr.hasMin();
		channel.info.hasMax = fnAttr.hasMax();

		if (fnAttr.unitType() == MFnUnitAttribute::kAngle)
		{
			MAngle rest;
			fnAttr.getDefault(rest);
			channel.kind = RigPoseScene::ValueKind::kAngle;
			channel.info.type = "doubleAngle";
			channel.rest = rest.asRadians();
			channel.info.rest = rest.as(MAngle::uiUnit());
			MAngle limit;
			if (channel.info.hasMin)
			{
				fnAttr.getMin(limit);
				channel.info.min = limit.as(MAngle::uiUnit());
			}
			if (channel.info.hasMax)
			{
				fnAttr.getMax(limit);
				channel.info.max = limit.as(MAngle::uiUnit());
			}
			return true;
		}
		if (fnAttr.unitType() == MFnUnitAttribute::kDistance)
		{
			MDistance rest;
			fnAttr.getDefault(rest);
			channel.kind = RigPoseScene::ValueKind::kDistance;
			channel.info.type = "doubleLinear";
			channel.rest = rest.asCentimeters();
			channel.info.rest = rest.as(MDistance::uiUnit());
			MDistance limit;
			if (channel.info.hasMin)
			{
				fnAttr.getMin(limit);
				channel.info.min = limit.as(MDistance::uiUnit());
			}
			if (channel.info.hasMax)
			{
				fnAttr.getMax(limit);
				channel.info.max = limit.as(MDistance::uiUnit());
			}
			return true;
		}
		return false;   // time
	}

	// The channel's value in internal units.
	double getValue(const RigPoseScene::Channel& channel)
	{
		switch (channel.kind)
		{
		case RigPoseScene::ValueKind::kFloat:
			return channel.plug.asFloat();
		case RigPoseScene::ValueKind::kInt:
			return channel.plug.asInt();
		case RigPoseScene::ValueKind::kShort:
		case RigPoseScene::ValueKind::kEnum:
			return channel.plug.asShort();
		case RigPoseScene::ValueKind::kBool:
			return channel.plug.asBool() ? 1.0 : 0.0;
		case RigPoseScene::ValueKind::kAngle:
			return channel.plug.asMAngle().asRadians();
		case RigPoseScene::ValueKind::kDistance:
			return channel.plug.asMDistance().asCentimeters();
		default:
			return channel.plug.asDouble();
		}
	}

	// Queues the write when value differs from the channel's current one. outChanged is set, never cleared.
	MStatus setValue(const RigPoseScene::Channel& channel, double value, MDGModifier& dgMod, bool& outChanged)
	{
		if (std::abs(getValue(channel) - value) < kValueTolerance)
		{
			return MStatus::kSuccess;
		}
		outChanged = true;

		switch (channel.kind)
		{
		case RigPoseScene::ValueKind::kFloat:
			return dgMod.newPlugValueFloat(channel.plug, static_cast<float>(value));
		case RigPoseScene::ValueKind::kInt:
			return dgMod.newPlugValueInt(channel.plug, static_cast<int>(std::lround(value)));
		case RigPoseScene::ValueKind::kShort:
		case RigPoseScene::ValueKind::kEnum:
			return dgMod.newPlugValueShort(channel.plug, static_cast<short>(std::lround(value)));
		case RigPoseScene::ValueKind::kBool:
			return dgMod.newPlugValueBool(channel.plug, value != 0.0);
		case RigPoseScene::ValueKind::kAngle:
			return dgMod.newPlugValueMAngle(channel.plug, MAngle(value, MAngle::kRadians));
		case RigPoseScene::ValueKind::kDistance:
			return dgMod.newPlugValueMDistance(channel.plug, MDistance(value, MDistance::kCentimeters));
		default:
			return dgMod.newPlugValueDouble(channel.plug, value);
		}
	}

	// Queues every source channel's value onto the target channel of the same name, negated where the source says
	// so. Warns about a channel the target doesn't have. Values are read now, so two calls with the controls
	// exchanged queue a swap.
	MStatus copyChannels(const MObject& oSource, const MObject& oTarget, MDGModifier& dgMod, MStringArray& outChangedNames)
	{
		std::vector<RigPoseScene::Channel> sourceChannels;
		std::vector<RigPoseScene::Channel> targetChannels;
		MStatus status = RigPoseScene::getControlChannels(oSource, sourceChannels);
		if (status)
		{
			status = RigPoseScene::getControlChannels(oTarget, targetChannels);
		}
		if (!status)
		{
			return status;
		}

		bool changed = false;
		for (const RigPoseScene::Channel& source : sourceChannels)
		{
			const RigPoseScene::Channel* pTarget = nullptr;
			for (const RigPoseScene::Channel& target : targetChannels)
			{
				if (target.info.name == source.info.name)
				{
					pTarget = &target;
					break;
				}
			}
			if (pTarget == nullptr)
			{
				MGlobal::displayWarning(DagUtils::getPartialPathName(oTarget) + " has no writable channel " + source.info.name.c_str()
					+ ", so it keeps its value.");
				continue;
			}

			const double value = getValue(source);
			status = setValue(*pTarget, source.info.negate ? -value : value, dgMod, changed);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}

		if (changed)
		{
			outChangedNames.append(DagUtils::getPartialPathName(oTarget));
		}
		return MStatus::kSuccess;
	}

	// Queues a Center control's reflection across the world YZ plane (see findMirrorAxis).
	MStatus flipCenterControl(const MObject& oControl, MDGModifier& dgMod, MStringArray& outChangedNames)
	{
		MMatrix mRest;
		MStatus status = RigTraversal::getControlRestWorld(oControl, mRest);
		if (!status)
		{
			return status;
		}

		// Row i of the rest matrix is local axis i in world space.
		double xOfLocalAxes[3];
		for (unsigned int axis = 0; axis < 3; ++axis)
		{
			const double length = std::sqrt(mRest[axis][0] * mRest[axis][0] + mRest[axis][1] * mRest[axis][1] + mRest[axis][2] * mRest[axis][2]);
			xOfLocalAxes[axis] = length > 0.0 ? mRest[axis][0] / length : 0.0;
		}
		const int mirrorAxis = findMirrorAxis(xOfLocalAxes);
		if (mirrorAxis < 0)
		{
			MGlobal::displayWarning(DagUtils::getPartialPathName(oControl)
				+ " has no rest axis along world X, so its channels can't hold a flipped pose. It keeps its values.");
			return MStatus::kSuccess;
		}

		std::vector<RigPoseScene::Channel> channels;
		status = RigPoseScene::getControlChannels(oControl, channels);
		if (!status)
		{
			return status;
		}

		bool changed = false;
		for (const RigPoseScene::Channel& channel : channels)
		{
			// kDefaultTransformValues order: translate 0-2, rotate 3-5, scale 6-8.
			const int index = channel.transformIndex;
			const bool negate = index < 0 ? channel.info.negate
				: index < 3 ? index == mirrorAxis
				: index < 6 ? index - 3 != mirrorAxis
				: false;
			if (negate)
			{
				status = setValue(channel, -getValue(channel), dgMod, changed);
				CHECK_MSTATUS_AND_RETURN_IT(status);
			}
		}

		if (changed)
		{
			outChangedNames.append(DagUtils::getPartialPathName(oControl));
		}
		return MStatus::kSuccess;
	}
}

MStatus RigPoseScene::setMirrorMode(MFnAttribute& fnAttr, bool negate)
{
	return fnAttr.addToCategory(negate ? ChannelMirror::NegateCategory : ChannelMirror::CopyCategory);
}

MStatus RigPoseScene::getControlChannels(const MObject& oControl, std::vector<Channel>& outChannels)
{
	outChannels.clear();

	MStatus status;
	MFnDependencyNode fnControl(oControl);
	for (unsigned int i = 0; i < fnControl.attributeCount(); ++i)
	{
		MObject oAttr = fnControl.attribute(i, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		MPlug plug(oControl, oAttr);
		if (plug.isArray() || plug.isCompound() || !plug.isKeyable() || !DagUtils::isChannelWritable(plug))
		{
			continue;
		}

		Channel channel;
		channel.plug = plug;
		channel.info.name = MFnAttribute(oAttr).name().asChar();

		bool isSupported = false;
		if (oAttr.hasFn(MFn::kEnumAttribute))
		{
			short rest = 0;
			MFnEnumAttribute(oAttr).getDefault(rest);
			channel.kind = ValueKind::kEnum;
			channel.info.type = "enum";
			channel.rest = rest;
			isSupported = true;
		}
		else if (oAttr.hasFn(MFn::kUnitAttribute))
		{
			isSupported = readUnitChannel(oAttr, channel);
		}
		else if (oAttr.hasFn(MFn::kNumericAttribute))
		{
			isSupported = readNumericChannel(oAttr, channel);
			channel.info.rest = channel.rest;
		}
		if (!isSupported)
		{
			MGlobal::displayError(channelName(oControl, oAttr) + " is keyable, but its type can't be a control channel.");
			return MStatus::kFailure;
		}
		if (channel.kind == ValueKind::kEnum)
		{
			channel.info.rest = channel.rest;
		}

		for (int t = 0; t < 9; ++t)
		{
			if (kDefaultTransformValues[t].attr == oAttr)
			{
				channel.transformIndex = t;
				break;
			}
		}

		// Built-in channels copy. A custom one must say how it mirrors (D34).
		MFnAttribute fnAttr(oAttr);
		if (fnAttr.isDynamic())
		{
			const bool copies = fnAttr.hasCategory(ChannelMirror::CopyCategory);
			const bool negates = fnAttr.hasCategory(ChannelMirror::NegateCategory);
			if (copies == negates)
			{
				MGlobal::displayError(channelName(oControl, oAttr) + " must declare exactly one mirror mode, "
					+ ChannelMirror::CopyCategory + " or " + ChannelMirror::NegateCategory + ", as an attribute category.");
				return MStatus::kFailure;
			}
			if (negates && (channel.kind == ValueKind::kEnum || channel.kind == ValueKind::kBool))
			{
				MGlobal::displayError(channelName(oControl, oAttr) + " is " + channel.info.type.c_str() + ", which can't be negated in a mirror.");
				return MStatus::kFailure;
			}
			channel.info.negate = negates;
		}

		outChannels.push_back(channel);
	}
	return MStatus::kSuccess;
}

MStatus RigPoseScene::zeroRigControls(const MObject& oRigRoot, MDGModifier& dgMod, MStringArray& outChangedNames)
{
	MObjectArray oControls;
	MStatus status = RigTraversal::getRigControls(oRigRoot, RigControlFilter{}, oControls);
	if (!status)
	{
		return status;
	}

	for (unsigned int i = 0; i < oControls.length(); ++i)
	{
		std::vector<Channel> channels;
		status = getControlChannels(oControls[i], channels);
		if (!status)
		{
			return status;
		}

		bool changed = false;
		for (const Channel& channel : channels)
		{
			status = setValue(channel, channel.rest, dgMod, changed);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		if (changed)
		{
			outChangedNames.append(DagUtils::getPartialPathName(oControls[i]));
		}
	}
	return MStatus::kSuccess;
}

MStatus RigPoseScene::mirrorRigPose(const MObject& oRigRoot, const std::string& sourceSide, MDGModifier& dgMod, MStringArray& outChangedNames)
{
	MObjectArray oSources;
	MObjectArray oMirrors;
	MStatus status = RigTraversal::getMirrorControlPairs(oRigRoot, sourceSide, oSources, oMirrors);
	if (!status)
	{
		return status;
	}

	for (unsigned int i = 0; i < oSources.length(); ++i)
	{
		status = copyChannels(oSources[i], oMirrors[i], dgMod, outChangedNames);
		if (!status)
		{
			return status;
		}
	}
	return MStatus::kSuccess;
}

MStatus RigPoseScene::flipRigPose(const MObject& oRigRoot, MDGModifier& dgMod, MStringArray& outChangedNames)
{
	MObjectArray oLefts;
	MObjectArray oRights;
	MStatus status = RigTraversal::getMirrorControlPairs(oRigRoot, SideNames::Left, oLefts, oRights);
	if (!status)
	{
		return status;
	}

	// Both directions are queued before anything is written, so the two sides exchange values.
	for (unsigned int i = 0; i < oLefts.length(); ++i)
	{
		status = copyChannels(oLefts[i], oRights[i], dgMod, outChangedNames);
		if (status)
		{
			status = copyChannels(oRights[i], oLefts[i], dgMod, outChangedNames);
		}
		if (!status)
		{
			return status;
		}
	}

	RigControlFilter centerFilter;
	centerFilter.sides.push_back(SideNames::Center);
	MObjectArray oCenters;
	status = RigTraversal::getRigControls(oRigRoot, centerFilter, oCenters);
	if (!status)
	{
		return status;
	}
	for (unsigned int i = 0; i < oCenters.length(); ++i)
	{
		status = flipCenterControl(oCenters[i], dgMod, outChangedNames);
		if (!status)
		{
			return status;
		}
	}
	return MStatus::kSuccess;
}
