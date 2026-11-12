#include "maya/shared/RigKeyScene.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

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
			ChannelKeys keys;
			for (unsigned int k = 0; k < fnCurve.numKeys(); ++k)
			{
				keys.emplace_back(fnCurve.time(k).as(MTime::uiUnit()), RigPoseScene::toUiUnits(channel, fnCurve.value(k)));
			}
			if (!keys.empty())
			{
				outKeys[markup.side][markup.usage][channel.info.name] = keys;
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

		const std::map<std::string, ChannelKeys>* pChannels = nullptr;
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

		for (const auto& [name, channelKeys] : *pChannels)
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
			if (channelKeys.empty())
			{
				continue;
			}

			KeyJob job;
			job.oControl = oControls[i];
			job.plug = itChannel->plug;
			job.isStepped = isStepChannel(itChannel->kind);

			// addKeys wants the keys in time order.
			ChannelKeys sorted = channelKeys;
			std::sort(sorted.begin(), sorted.end());
			for (const auto& [frame, value] : sorted)
			{
				job.times.append(MTime(frame, MTime::uiUnit()));
				job.values.append(RigPoseScene::fromUiUnits(*itChannel, value));
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
		const MFnAnimCurve::TangentType tangent = job.isStepped ? MFnAnimCurve::kTangentStep : MFnAnimCurve::kTangentGlobal;
		status = fnCurve.addKeys(&job.times, &job.values, tangent, tangent, false, &edit.curveChange);
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
