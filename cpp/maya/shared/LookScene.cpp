#include "maya/shared/LookScene.h"
#include "core/AssetId.h"
#include "core/Constants.h"
#include "core/Hash64.h"
#include "core/RigTemplateValidation.h"
#include "core/SkeletonFile.h"
#include "maya/nodes/LookRoot.h"
#include "maya/nodes/RigRoot.h"
#include "maya/shared/RigTraversal.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <set>
#include <vector>

#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MObjectHandle.h>
#include <maya/MPlug.h>

namespace
{
	bool parseSkeleton(const std::string& text, const char* what, Skeleton& outSkeleton)
	{
		ValidationReport report;
		deserializeSkeleton(text, outSkeleton, report);
		if (report.hasErrors())
		{
			ReportUtils::displayReport(report);
			MGlobal::displayError(MString("The ") + what + " skeleton file failed validation.");
			return false;
		}
		return true;
	}

	bool containsNode(const std::vector<MObjectHandle>& handles, const MObject& oNode)
	{
		return std::find(handles.begin(), handles.end(), MObjectHandle(oNode)) != handles.end();
	}

	// ":rig" for the namespace "rig" or ":rig", ":" for the root.
	MString absoluteNamespace(const MString& rigNamespace)
	{
		if (rigNamespace.length() == 0 || rigNamespace == ":")
		{
			return ":";
		}
		return rigNamespace.asChar()[0] == ':' ? rigNamespace : MString(":") + rigNamespace;
	}
}

MStatus LookScene::checkSkeleton(const MObject& oRigRoot, const std::string& lookSkeletonText, const std::string& rigSkeletonText)
{
	Skeleton lookSkeleton;
	if (!parseSkeleton(lookSkeletonText, "look's", lookSkeleton))
	{
		return MStatus::kFailure;
	}
	const std::string lookHash = Hash64::format(skeletonHash(lookSkeleton));
	const std::string rigHash = MPlug(oRigRoot, RigRootNode::aSkeletonHash).asString().asChar();
	if (lookHash == rigHash)
	{
		return MStatus::kSuccess;
	}

	MGlobal::displayError(MString("The look's skeleton (") + lookHash.c_str() + ") is not the rig's (" + rigHash.c_str() + "), so the look was not applied.");
	if (!rigSkeletonText.empty())
	{
		Skeleton rigSkeleton;
		if (parseSkeleton(rigSkeletonText, "rig's", rigSkeleton))
		{
			for (const std::string& line : describeSkeletonDifferences(rigSkeleton, lookSkeleton))
			{
				MGlobal::displayError(line.c_str());
			}
		}
	}
	return MStatus::kFailure;
}

MStatus LookSwap::prepare(const LookSwapInputs& inputs)
{
	if (!AssetId::isValid(inputs.assetId))
	{
		MGlobal::displayError(MString("Not a valid asset id: ") + inputs.assetId.c_str());
		return MStatus::kFailure;
	}
	if (!AssetId::isValidName(inputs.lookName))
	{
		MGlobal::displayError(MString("Not a valid look name: ") + inputs.lookName.c_str());
		return MStatus::kFailure;
	}
	if (inputs.meshes.length() == 0)
	{
		MGlobal::displayError("A look needs at least one mesh.");
		return MStatus::kFailure;
	}
	if (inputs.meshes.length() != inputs.weights.size())
	{
		MGlobal::displayError(MString("A look needs one weight file per mesh: ") + inputs.meshes.length() + " meshes and "
			+ static_cast<unsigned int>(inputs.weights.size()) + " files.");
		return MStatus::kFailure;
	}

	fRigRoot = inputs.oRigRoot;
	fNamespace = MFnDependencyNode(fRigRoot).parentNamespace();
	const MString expectedNamespace = absoluteNamespace(fNamespace);
	fAssetId = inputs.assetId;
	fLookName = inputs.lookName;
	fPublishVersion = inputs.publishVersion;

	// Each mesh takes the file with its meshId. The look swap never guesses by order or by name.
	fMeshes.clear();
	fMeshNames.clear();
	fLoads.clear();
	std::set<std::size_t> usedFiles;
	bool hasErrors = false;
	for (unsigned int i = 0; i < inputs.meshes.length(); ++i)
	{
		const MString meshName = inputs.meshes[i].partialPathName();
		if (DagUtils::getAbsoluteNamespace(inputs.meshes[i].node()) != expectedNamespace)
		{
			MGlobal::displayError(meshName + " is not in the rig's namespace (" + expectedNamespace + "), so its weights would find no joints.");
			hasErrors = true;
			continue;
		}
		std::string meshId;
		MStatus status = SkinWeightScene::getMeshId(inputs.meshes[i], meshId);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		std::size_t file = inputs.weights.size();
		for (std::size_t f = 0; f < inputs.weights.size() && file == inputs.weights.size(); ++f)
		{
			if (!meshId.empty() && inputs.weights[f].meshId == meshId)
			{
				file = f;
			}
		}
		if (file == inputs.weights.size() || !usedFiles.insert(file).second)
		{
			MGlobal::displayError(meshName + " has no weight file of its own: its meshId '" + meshId.c_str()
				+ "' matches no file, or matches one that another mesh took.");
			hasErrors = true;
			continue;
		}
		fMeshes.append(inputs.meshes[i]);
		fMeshNames.push_back(inputs.weights[file].meshName);
		fLoads.push_back(std::make_unique<SkinWeightLoad>());
		status = fLoads.back()->prepare(inputs.meshes[i], inputs.weights[file], inputs.ignoreSkeletonHash);
		if (!status)
		{
			hasErrors = true;
		}
	}
	if (hasErrors)
	{
		MGlobal::displayError("The look was not applied.");
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus LookSwap::apply()
{
	fLoadsApplied = 0;
	fSceneMod.reset();
	fLookRoot = MObject::kNullObj;
	for (const std::unique_ptr<SkinWeightLoad>& load : fLoads)
	{
		const MStatus loadStatus = load->apply();
		++fLoadsApplied;   // a load that failed partway still has to be undone
		if (!loadStatus)
		{
			undo();
			return loadStatus;
		}
	}

	MStatus status;
	fSceneMod = std::make_unique<MDagModifier>();
	MObject oLookRoot = RigTraversal::findLookRoot(fRigRoot);
	const MObjectArray oOldMembers = oLookRoot.isNull() ? MObjectArray() : RigTraversal::getLookMembers(oLookRoot);

	std::vector<MObjectHandle> newMeshes;
	for (unsigned int i = 0; i < fMeshes.length(); ++i)
	{
		newMeshes.emplace_back(fMeshes[i].node());
	}

	// The previous look's meshes go with their history, which MEL's delete clears and a bare node delete would not.
	for (unsigned int i = 0; i < oOldMembers.length(); ++i)
	{
		if (containsNode(newMeshes, oOldMembers[i]))
		{
			continue;
		}
		MFnDagNode fnMember(oOldMembers[i], &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = fSceneMod->commandToExecute(MString("delete ") + MelUtils::quoted(fnMember.fullPathName()));
		if (!status)
		{
			undo();
			return status;
		}
	}

	// Named after the file, now the previous meshes are gone, so a look's meshes keep the names its weight files have.
	for (unsigned int i = 0; i < fMeshes.length(); ++i)
	{
		status = fSceneMod->renameNode(fMeshes[i].node(), DagUtils::getAbsoluteName(fNamespace, fMeshNames[i].c_str()));
		if (!status)
		{
			undo();
			return status;
		}
	}

	if (oLookRoot.isNull())
	{
		oLookRoot = fSceneMod->MDGModifier::createNode(LookRootNode::id, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = fSceneMod->renameNode(oLookRoot, DagUtils::getAbsoluteName(fNamespace, RigNaming::LookRoot));
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = fSceneMod->connect(MPlug(fRigRoot, RigRootNode::aLook), MPlug(oLookRoot, LookRootNode::aRigRoot));
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	status = fSceneMod->newPlugValueString(MPlug(oLookRoot, LookRootNode::aAssetId), fAssetId.c_str());
	CHECK_MSTATUS_AND_RETURN_IT(status);
	status = fSceneMod->newPlugValueString(MPlug(oLookRoot, LookRootNode::aLookName), fLookName.c_str());
	CHECK_MSTATUS_AND_RETURN_IT(status);
	status = fSceneMod->newPlugValueString(MPlug(oLookRoot, LookRootNode::aPublishVersion), fPublishVersion.c_str());
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// Index from 0: the deletes above run first, so the old members' elements are free by the time these connect.
	// A mesh that is already a member stays connected where it is and takes no new element.
	std::vector<MObjectHandle> oldMembers;
	for (unsigned int i = 0; i < oOldMembers.length(); ++i)
	{
		oldMembers.emplace_back(oOldMembers[i]);
	}
	MPlug pMembers(oLookRoot, LookRootNode::aMembers);
	unsigned int nextIndex = 0;
	for (unsigned int i = 0; i < fMeshes.length(); ++i)
	{
		if (containsNode(oldMembers, fMeshes[i].node()))
		{
			continue;
		}
		MPlug pMessage = MFnDependencyNode(fMeshes[i].node()).findPlug("message", false, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		MPlug pElement = pMembers.elementByLogicalIndex(nextIndex++, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = fSceneMod->connect(pMessage, pElement);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}

	status = fSceneMod->doIt();
	if (!status)
	{
		undo();
		return status;
	}
	fLookRoot = oLookRoot;
	return MStatus::kSuccess;
}

MStatus LookSwap::undo()
{
	MStatus result = MStatus::kSuccess;
	if (fSceneMod)
	{
		result = fSceneMod->undoIt();
		fSceneMod.reset();
	}
	for (std::size_t i = fLoadsApplied; i > 0; --i)
	{
		const MStatus status = fLoads[i - 1]->undo();
		if (!status)
		{
			result = status;
		}
	}
	fLoadsApplied = 0;
	return result;
}

MString LookSwap::lookRootName() const
{
	return fLookRoot.isNull() ? MString() : MFnDependencyNode(fLookRoot).name();
}
