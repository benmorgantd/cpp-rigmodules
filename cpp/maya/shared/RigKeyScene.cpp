#include "maya/shared/RigKeyScene.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <maya/MAngle.h>
#include <maya/MDoubleArray.h>
#include <maya/MFnAnimCurve.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MObjectArray.h>
#include <maya/MPlug.h>
#include <maya/MTime.h>
#include <maya/MTimeArray.h>

namespace
{
	// One channel's keys, checked and converted to internal units, waiting for its curve.
	struct KeyJob
	{
		MObject oControl;
		MObject oCurve;   // null until the curve exists
		MPlug plug;
		MTimeArray times;
		MDoubleArray values;
		ChannelCurve curve;   // the keys in frame order, with their tangents and the curve's flags
		bool isStepped = false;
	};

	// The animCurve that animates the plug, or a null object.
	MObject findCurve(const MPlug& plug)
	{
		const MPlug pSource = plug.source();
		if (!pSource.isNull() && pSource.node().hasFn(MFn::kAnimCurve))
		{
			return pSource.node();
		}
		return MObject::kNullObj;
	}

	bool isStepChannel(RigPoseScene::ValueKind kind)
	{
		return kind == RigPoseScene::ValueKind::kEnum || kind == RigPoseScene::ValueKind::kBool
			|| kind == RigPoseScene::ValueKind::kInt || kind == RigPoseScene::ValueKind::kShort;
	}

	MFnAnimCurve::TangentType toMayaTangent(TangentKind kind)
	{
		switch (kind)
		{
		case TangentKind::Clamped:
			return MFnAnimCurve::kTangentClamped;
		case TangentKind::Linear:
			return MFnAnimCurve::kTangentLinear;
		case TangentKind::Flat:
			return MFnAnimCurve::kTangentFlat;
		case TangentKind::Step:
			return MFnAnimCurve::kTangentStep;
		case TangentKind::Spline:
			return MFnAnimCurve::kTangentSmooth;
		case TangentKind::Fixed:
			return MFnAnimCurve::kTangentFixed;
		default:
			return MFnAnimCurve::kTangentAuto;
		}
	}

	// The tangent of a key. A type with no name here (plateau, step next, the auto variants) is read as the fixed
	// tangent it currently is, so the shape survives even when the type name doesn't.
	Tangent readTangent(const MFnAnimCurve& fnCurve, unsigned int index, bool isIn)
	{
		const MFnAnimCurve::TangentType type = isIn ? fnCurve.inTangentType(index) : fnCurve.outTangentType(index);
		Tangent tangent;
		switch (type)
		{
		case MFnAnimCurve::kTangentAuto:
			tangent.kind = TangentKind::Auto;
			return tangent;
		case MFnAnimCurve::kTangentClamped:
			tangent.kind = TangentKind::Clamped;
			return tangent;
		case MFnAnimCurve::kTangentLinear:
			tangent.kind = TangentKind::Linear;
			return tangent;
		case MFnAnimCurve::kTangentFlat:
			tangent.kind = TangentKind::Flat;
			return tangent;
		case MFnAnimCurve::kTangentStep:
			tangent.kind = TangentKind::Step;
			return tangent;
		case MFnAnimCurve::kTangentSmooth:
			tangent.kind = TangentKind::Spline;
			return tangent;
		default:
			break;
		}

		MAngle angle;
		double weight = 1.0;
		if (fnCurve.getTangent(index, angle, weight, isIn) == MStatus::kSuccess)
		{
			tangent.kind = TangentKind::Fixed;
			tangent.angle = angle.asDegrees();
			tangent.weight = weight;
		}
		return tangent;
	}

	MStatus writeTangent(MFnAnimCurve& fnCurve, unsigned int index, const Tangent& tangent, bool isIn, MAnimCurveChange& change)
	{
		const MFnAnimCurve::TangentType type = toMayaTangent(tangent.kind);
		MStatus status = isIn ? fnCurve.setInTangentType(index, type, &change) : fnCurve.setOutTangentType(index, type, &change);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		if (tangent.kind == TangentKind::Fixed)
		{
			status = fnCurve.setTangent(index, MAngle(tangent.angle, MAngle::kDegrees), tangent.weight, isIn, &change);
		}
		return status;
	}

	Infinity fromMayaInfinity(MFnAnimCurve::InfinityType type)
	{
		switch (type)
		{
		case MFnAnimCurve::kLinear:
			return Infinity::Linear;
		case MFnAnimCurve::kCycle:
			return Infinity::Cycle;
		case MFnAnimCurve::kCycleRelative:
			return Infinity::CycleRelative;
		case MFnAnimCurve::kOscillate:
			return Infinity::Oscillate;
		default:
			return Infinity::Constant;
		}
	}

	MFnAnimCurve::InfinityType toMayaInfinity(Infinity infinity)
	{
		switch (infinity)
		{
		case Infinity::Linear:
			return MFnAnimCurve::kLinear;
		case Infinity::Cycle:
			return MFnAnimCurve::kCycle;
		case Infinity::CycleRelative:
			return MFnAnimCurve::kCycleRelative;
		case Infinity::Oscillate:
			return MFnAnimCurve::kOscillate;
		default:
			return MFnAnimCurve::kConstant;
		}
	}

	// Gives every key the tangents it names, and the curve its flags. The keys are already on the curve (addKeys).
	// A stepped channel is all step already. A key whose tangents are both auto is what addKeys made, so it is left.
	MStatus applyCurveDetails(MFnAnimCurve& fnCurve, const KeyJob& job, MAnimCurveChange& change)
	{
		MStatus status = fnCurve.setIsWeighted(job.curve.isWeighted, &change);
		if (status)
		{
			status = fnCurve.setPreInfinityType(toMayaInfinity(job.curve.preInfinity), &change);
		}
		if (status)
		{
			status = fnCurve.setPostInfinityType(toMayaInfinity(job.curve.postInfinity), &change);
		}
		CHECK_MSTATUS_AND_RETURN_IT(status);
		if (job.isStepped)
		{
			return MStatus::kSuccess;
		}

		for (std::size_t k = 0; k < job.curve.keys.size(); ++k)
		{
			const Key& key = job.curve.keys[k];
			if (key.in.kind == TangentKind::Auto && key.out.kind == TangentKind::Auto)
			{
				continue;
			}

			unsigned int index = 0;
			if (!fnCurve.find(job.times[static_cast<unsigned int>(k)], index))
			{
				MGlobal::displayError(MString("Could not find the key at frame ") + job.curve.keys[k].frame + " on " + job.plug.name() + " to set its tangents.");
				return MStatus::kFailure;
			}
			// Two different tangents on one key need them unlocked.
			status = fnCurve.setTangentsLocked(index, false, &change);
			if (status)
			{
				status = writeTangent(fnCurve, index, key.in, true, change);
			}
			if (status)
			{
				status = writeTangent(fnCurve, index, key.out, false, change);
			}
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		return MStatus::kSuccess;
	}
}

MStatus RigKeyEdit::redo()
{
	MStatus status = dgMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);
	curveChange.redoIt();
	return MStatus::kSuccess;
}

MStatus RigKeyEdit::undo()
{
	curveChange.undoIt();
	return dgMod.undoIt();
}

MStatus RigKeyScene::getRigKeys(const RigControlScope& scope, RigKeys& outKeys, MStringArray& outControlNames)
{
	outKeys.clear();
	MObjectArray oControls;
	MStatus status = RigTraversal::getScopeControls(scope, oControls);
	if (!status)
	{
		return status;
	}

	for (unsigned int i = 0; i < oControls.length(); ++i)
	{
		ControlMarkup markup;
		std::vector<RigPoseScene::Channel> channels;
		status = RigTraversal::getControlMarkup(oControls[i], markup);
		if (status)
		{
			status = RigPoseScene::getControlChannels(oControls[i], channels);
		}
		if (!status)
		{
			return status;
		}

		outControlNames.append(DagUtils::getPartialPathName(oControls[i]));
		for (const RigPoseScene::Channel& channel : channels)
		{
			MObject oCurve = findCurve(channel.plug);
			if (oCurve.isNull())
			{
				continue;
			}

			MFnAnimCurve fnCurve(oCurve, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			ChannelCurve curve;
			curve.isWeighted = fnCurve.isWeighted();
			curve.preInfinity = fromMayaInfinity(fnCurve.preInfinityType());
			curve.postInfinity = fromMayaInfinity(fnCurve.postInfinityType());
			const bool isStepped = isStepChannel(channel.kind);
			for (unsigned int k = 0; k < fnCurve.numKeys(); ++k)
			{
				Key key;
				key.frame = fnCurve.time(k).as(MTime::uiUnit());
				key.value = RigPoseScene::toUiUnits(channel, fnCurve.value(k));
				// A stepped channel is stepped on every key, so it has nothing to say about tangents.
				if (!isStepped)
				{
					key.in = readTangent(fnCurve, k, true);
					key.out = readTangent(fnCurve, k, false);
				}
				curve.keys.push_back(key);
			}
			if (!curve.keys.empty())
			{
				outKeys[markup.side][markup.usage][channel.info.name] = curve;
			}
		}
	}
	return MStatus::kSuccess;
}

MStatus RigKeyScene::setRigKeys(const RigControlScope& scope, const RigKeys& keys, RigKeyEdit& edit, MStringArray& outChangedNames)
{
	MObjectArray oControls;
	MStatus status = RigTraversal::getScopeControls(scope, oControls);
	if (!status)
	{
		return status;
	}

	// 1. Check and convert everything. Nothing is queued yet.
	std::vector<KeyJob> jobs;
	std::set<std::pair<std::string, std::string>> usedEntries;
	for (unsigned int i = 0; i < oControls.length(); ++i)
	{
		ControlMarkup markup;
		status = RigTraversal::getControlMarkup(oControls[i], markup);
		if (!status)
		{
			return status;
		}

		const std::map<std::string, ChannelCurve>* pChannels = nullptr;
		const auto itSide = keys.find(markup.side);
		if (itSide != keys.end())
		{
			const auto itUsage = itSide->second.find(markup.usage);
			if (itUsage != itSide->second.end())
			{
				pChannels = &itUsage->second;
			}
		}
		if (pChannels == nullptr)
		{
			if (scope.isWholeRig)
			{
				MGlobal::displayWarning(DagUtils::getPartialPathName(oControls[i]) + " (" + markup.side.c_str() + ", " + markup.usage.c_str()
					+ ") has no keys, so it keeps its animation.");
			}
			continue;
		}
		usedEntries.emplace(markup.side, markup.usage);

		std::vector<RigPoseScene::Channel> channels;
		status = RigPoseScene::getControlChannels(oControls[i], channels);
		if (!status)
		{
			return status;
		}

		for (const auto& [name, curve] : *pChannels)
		{
			const auto itChannel = std::find_if(channels.begin(), channels.end(), [&name](const RigPoseScene::Channel& channel)
			{
				return channel.info.name == name;
			});
			if (itChannel == channels.end())
			{
				MGlobal::displayWarning(DagUtils::getPartialPathName(oControls[i]) + " has no writable channel " + name.c_str()
					+ ", so its keys are skipped.");
				continue;
			}
			if (curve.keys.empty())
			{
				continue;
			}

			KeyJob job;
			job.oControl = oControls[i];
			job.plug = itChannel->plug;
			job.isStepped = isStepChannel(itChannel->kind);

			// addKeys wants the keys in time order.
			job.curve = curve;
			std::sort(job.curve.keys.begin(), job.curve.keys.end(), [](const Key& a, const Key& b)
			{
				return a.frame < b.frame;
			});
			for (const Key& key : job.curve.keys)
			{
				job.times.append(MTime(key.frame, MTime::uiUnit()));
				job.values.append(RigPoseScene::fromUiUnits(*itChannel, key.value));
			}
			jobs.push_back(std::move(job));
		}
	}

	if (scope.isWholeRig)
	{
		for (const auto& [side, usages] : keys)
		{
			for (const auto& entry : usages)
			{
				if (usedEntries.count({ side, entry.first }) == 0)
				{
					MGlobal::displayWarning(MString("The keys have a control (") + side.c_str() + ", " + entry.first.c_str()
						+ ") that the rig lacks, so they are skipped.");
				}
			}
		}
	}

	// 2. One curve for each channel that has none, then every channel's keys in one call.
	for (KeyJob& job : jobs)
	{
		job.oCurve = findCurve(job.plug);
		if (job.oCurve.isNull())
		{
			MFnAnimCurve fnNew;
			job.oCurve = fnNew.create(job.plug, &edit.dgMod, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
	}
	status = edit.dgMod.doIt();
	if (!status)
	{
		MGlobal::displayError(MString("Could not create the animation curves: ") + status.errorString());
		return status;
	}

	std::set<std::string> changed;
	for (KeyJob& job : jobs)
	{
		MFnAnimCurve fnCurve(job.oCurve, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		// Weights only count on a weighted curve, so the flag goes first.
		const MFnAnimCurve::TangentType tangent = job.isStepped ? MFnAnimCurve::kTangentStep : MFnAnimCurve::kTangentAuto;
		status = fnCurve.setIsWeighted(job.curve.isWeighted, &edit.curveChange);
		if (status)
		{
			status = fnCurve.addKeys(&job.times, &job.values, tangent, tangent, false, &edit.curveChange);
		}
		if (status)
		{
			status = applyCurveDetails(fnCurve, job, edit.curveChange);
		}
		if (!status)
		{
			MGlobal::displayError(MString("Could not add keys to ") + job.plug.name() + ": " + status.errorString());
			return status;
		}
		changed.insert(DagUtils::getPartialPathName(job.oControl).asChar());
	}

	for (const std::string& name : changed)
	{
		outChangedNames.append(name.c_str());
	}
	return MStatus::kSuccess;
}

MStatus RigKeyScene::mirrorRigKeys(const RigControlScope& scope, const RigKeys& keys, RigKeys& outMirrored)
{
	outMirrored.clear();
	MObjectArray oControls;
	MStatus status = RigTraversal::getRigControls(scope.oRigRoot, RigControlFilter(), oControls);
	if (!status)
	{
		return status;
	}

	std::set<std::pair<std::string, std::string>> usedEntries;
	for (unsigned int i = 0; i < oControls.length(); ++i)
	{
		ControlMarkup markup;
		status = RigTraversal::getControlMarkup(oControls[i], markup);
		if (!status)
		{
			return status;
		}
		const auto itSide = keys.find(markup.side);
		if (itSide == keys.end() || itSide->second.find(markup.usage) == itSide->second.end())
		{
			continue;
		}
		usedEntries.emplace(markup.side, markup.usage);

		// A Center control is its own mirror and flips by its axis. The others move to their mirror control.
		ControlMarkup target = markup;
		int flipAxis = -1;
		if (markup.side == SideNames::Center)
		{
			status = RigPoseScene::getFlipAxis(oControls[i], flipAxis);
			if (!status)
			{
				return status;
			}
			if (flipAxis < 0)
			{
				MGlobal::displayWarning(DagUtils::getPartialPathName(oControls[i])
					+ " has no rest axis along world X, so its keys can't be mirrored. They keep their values.");
			}
		}
		else
		{
			MObject oMirror;
			status = RigTraversal::findMirrorControl(oControls[i], oMirror);
			if (!status)
			{
				return status;
			}
			if (oMirror.isNull())
			{
				MGlobal::displayWarning(DagUtils::getPartialPathName(oControls[i]) + " has no mirror control, so its keys are skipped.");
				continue;
			}
			status = RigTraversal::getControlMarkup(oMirror, target);
			if (!status)
			{
				return status;
			}
		}

		std::vector<RigPoseScene::Channel> channels;
		status = RigPoseScene::getControlChannels(oControls[i], channels);
		if (!status)
		{
			return status;
		}
		for (const auto& [name, curve] : itSide->second.at(markup.usage))
		{
			const auto itChannel = std::find_if(channels.begin(), channels.end(), [&name](const RigPoseScene::Channel& channel)
			{
				return channel.info.name == name;
			});
			// A channel the control lacks is copied, and the write warns about it.
			const bool isNegated = itChannel != channels.end() && (markup.side == SideNames::Center
				? flipAxis >= 0 && RigPoseScene::isNegatedByFlip(*itChannel, flipAxis)
				: itChannel->info.negate);
			outMirrored[target.side][target.usage][name] = isNegated ? negateCurve(curve) : curve;
		}
	}

	if (scope.isWholeRig)
	{
		for (const auto& [side, usages] : keys)
		{
			for (const auto& entry : usages)
			{
				if (usedEntries.count({ side, entry.first }) == 0)
				{
					MGlobal::displayWarning(MString("The keys have a control (") + side.c_str() + ", " + entry.first.c_str()
						+ ") that the rig lacks, so they are skipped.");
				}
			}
		}
	}
	return MStatus::kSuccess;
}
