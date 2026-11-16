#include "maya/shared/RigDataFileScene.h"
#include "core/ControlMarkup.h"
#include "core/RigControlFilter.h"
#include "maya/nodes/RigRoot.h"
#include "maya/shared/RigTraversal.h"
#include "maya/shared/Utilities.h"

#include <set>

#include <maya/MGlobal.h>
#include <maya/MObjectArray.h>
#include <maya/MPlug.h>

MStatus RigDataFileScene::fillRigMeta(const MObject& oRigRoot, LibraryMeta& meta)
{
	meta.assetId = MPlug(oRigRoot, RigRootNode::aAssetId).asString().asChar();
	meta.rigName = MPlug(oRigRoot, RigRootNode::aRigName).asString().asChar();
	meta.skeletonHash = MPlug(oRigRoot, RigRootNode::aSkeletonHash).asString().asChar();
	meta.pluginVersion = MPlug(oRigRoot, RigRootNode::aPluginVersion).asString().asChar();
	meta.created = utcTimestampNow();
	return MStatus::kSuccess;
}

MStatus RigDataFileScene::readSaveMeta(const MArgDatabase& argData, const char* metaFlag, const char* commandName, const MObject& oRigRoot, LibraryMeta& outMeta)
{
	if (argData.isFlagSet(metaFlag))
	{
		MString text;
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(metaFlag, 0, text));
		ValidationReport report;
		applyCallerMeta(text.asChar(), outMeta, report);
		if (report.hasErrors())
		{
			ReportUtils::displayReport(report);
			MGlobal::displayError(MString(commandName) + ": " + metaFlag + " is not valid, so nothing was saved.");
			return MStatus::kInvalidParameter;
		}
	}
	return fillRigMeta(oRigRoot, outMeta);
}

MStatus RigDataFileScene::findUnmatchedControls(
	const MObject& oRigRoot,
	const std::vector<std::pair<std::string, std::string>>& fileControls,
	std::vector<std::pair<std::string, std::string>>& outUnmatched)
{
	outUnmatched.clear();
	MObjectArray oControls;
	MStatus status = RigTraversal::getRigControls(oRigRoot, RigControlFilter(), oControls);
	if (!status)
	{
		return status;
	}

	std::set<std::pair<std::string, std::string>> rigEntries;
	for (unsigned int i = 0; i < oControls.length(); ++i)
	{
		ControlMarkup markup;
		status = RigTraversal::getControlMarkup(oControls[i], markup);
		if (!status)
		{
			return status;
		}
		rigEntries.emplace(markup.side, markup.usage);
	}

	for (const auto& entry : fileControls)
	{
		if (rigEntries.count(entry) == 0)
		{
			outUnmatched.push_back(entry);
		}
	}
	return MStatus::kSuccess;
}
