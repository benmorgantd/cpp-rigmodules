#include "maya/shared/LookScene.h"
#include "core/AssetId.h"
#include "core/Constants.h"
#include "core/Hash64.h"
#include "core/RigTemplateValidation.h"
#include "core/SkeletonFile.h"
#include "core/SkinWeightFold.h"
#include "maya/nodes/LookRoot.h"
#include "maya/nodes/RigRoot.h"
#include "maya/shared/RigTraversal.h"
#include "maya/shared/SkeletonScene.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <memory>
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

	// What one mesh with a weight file gets: a load ready to apply, or none when the mesh goes rigid, and the report
	// entry either way.
	struct LadderOutcome
	{
		std::unique_ptr<SkinWeightLoad> load;
		LookMeshReport report;
	};

	// The fallback ladder, in order: a changed topology goes rigid, then each influence the rig no longer has folds into
	// the next skin joint up the file's skeleton (rigid when one can't), then the weights load. The skeletonHash only
	// sets staleSkeleton. Every fallback names its reason.
	LadderOutcome runLadder(const MDagPath& mesh, const SkinWeights& file, const std::vector<CurrentJoint>& joints, const std::string& rigHash)
	{
		LadderOutcome outcome;
		LookMeshReport& report = outcome.report;
		report.meshId = file.meshId;
		report.meshName = file.meshName;
		report.result = LookResult::Rigid;
		report.staleSkeleton = !file.skeletonHash.empty() && file.skeletonHash != rigHash;

		if (!SkinWeightScene::topologyMatches(mesh, file))
		{
			report.reasons.push_back("the mesh's topology is not the one the weights were saved with");
			return outcome;
		}
		SkinWeights weights = file;
		const FoldResult fold = foldSkinWeights(weights, joints);
		if (!fold.unfoldable.empty())
		{
			for (const JointSelector& influence : fold.unfoldable)
			{
				report.reasons.push_back(formatJointSelector(influence) + (file.skeleton.empty()
					? " is not a joint of the rig, and the file records no skeleton to fold it with: save the weights again"
					: " is not a joint of the rig, and no skin joint is above it in the saved skeleton"));
			}
			return outcome;
		}
		std::unique_ptr<SkinWeightLoad> load = std::make_unique<SkinWeightLoad>();
		if (!load->prepare(mesh, weights))
		{
			report.reasons.push_back("the weights could not be loaded (the errors above say why)");
			return outcome;
		}
		outcome.load = std::move(load);
		report.result = fold.folded.empty() ? LookResult::Loaded : LookResult::Folded;
		for (const FoldedInfluence& folded : fold.folded)
		{
			report.reasons.push_back(formatJointSelector(folded.from) + " is not a joint of the rig, so its weight went to " + formatJointSelector(folded.to));
		}
		return outcome;
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

MStatus LookScene::checkSkeleton(const MObject& oRigRoot, const std::string& lookSkeletonText, const std::string& rigSkeletonText, bool& outStale)
{
	outStale = false;
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

	outStale = true;
	MGlobal::displayWarning(MString("The look's skeleton (") + lookHash.c_str() + ") is not the rig's (" + rigHash.c_str()
		+ "). The look is applied anyway and is stale until its weights are saved again.");
	if (!rigSkeletonText.empty())
	{
		Skeleton rigSkeleton;
		if (parseSkeleton(rigSkeletonText, "rig's", rigSkeleton))
		{
			for (const std::string& line : describeSkeletonDifferences(rigSkeleton, lookSkeleton))
			{
				MGlobal::displayWarning(line.c_str());
			}
		}
	}
	return MStatus::kSuccess;
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
	if (inputs.meshes.length() == 0 && inputs.rigidMeshes.length() == 0)
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
	fReport = LookReport();
	fReport.skeletonStale = inputs.skeletonStale;

	// The rig's joints as they are now, read once for every mesh.
	const std::string rigHash = MFnDependencyNode(fRigRoot).findPlug(RigAttr::SkeletonHash, false).asString().asChar();
	std::vector<CurrentJoint> joints;
	if (inputs.meshes.length() > 0)
	{
		Skeleton skeleton;
		ValidationReport jointReport;
		SkeletonScene::readNamespaceSkeleton(expectedNamespace, skeleton, jointReport);
		if (jointReport.hasErrors())
		{
			ReportUtils::displayReport(jointReport);
			MGlobal::displayError(MString("The joints in namespace ") + expectedNamespace + " could not be read, so the look was not applied.");
			return MStatus::kFailure;
		}
		joints = currentJoints(skeleton);
	}

	// Each mesh takes the file with its meshId. The look swap never guesses by order or by name.
	fMeshes.clear();
	fMeshNames.clear();
	fLoads.clear();
	std::set<std::size_t> usedFiles;
	bool hasErrors = false;
	MDagPathArray rigidMeshes = inputs.rigidMeshes;
	std::vector<LookMeshReport> rigidReports(inputs.rigidMeshes.length());   // parallel to rigidMeshes
	for (LookMeshReport& entry : rigidReports)
	{
		entry.result = LookResult::Rigid;
		entry.reasons.push_back("the mesh has no weight file yet");
	}
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
		LadderOutcome outcome = runLadder(inputs.meshes[i], inputs.weights[file], joints, rigHash);
		if (outcome.load)
		{
			fMeshes.append(inputs.meshes[i]);
			fMeshNames.push_back(inputs.weights[file].meshName);
			fLoads.push_back(std::move(outcome.load));
			fReport.meshes.push_back(std::move(outcome.report));
			continue;
		}
		// The file no longer fits. The mesh stays in the look, bound like a mesh with no file, to be skinned again and saved
		// over the old file.
		MString reasons;
		for (const std::string& reason : outcome.report.reasons)
		{
			reasons += MString(" ") + reason.c_str() + ".";
		}
		MGlobal::displayWarning(meshName + ": its weight file doesn't fit, so it is bound to the top skin joint instead." + reasons + " Skin it and save the weights.");
		rigidMeshes.append(inputs.meshes[i]);
		rigidReports.push_back(std::move(outcome.report));
	}

	// A rigid mesh has no weight file yet. It is bound whole to the topmost skin joint, so it is skinned and a member
	// like the others, and a weight save writes its first file. It keeps its own name.
	if (rigidMeshes.length() > 0 && !hasErrors)
	{
		JointSelector topJoint;
		MStatus status = SkeletonScene::findTopSkinJoint(expectedNamespace, topJoint);
		if (!status)
		{
			MGlobal::displayError("The look was not applied.");
			return status;
		}
		std::vector<MObjectHandle> fileMeshes;
		for (unsigned int i = 0; i < fMeshes.length(); ++i)
		{
			fileMeshes.emplace_back(fMeshes[i].node());
		}
		for (unsigned int i = 0; i < rigidMeshes.length(); ++i)
		{
			const MDagPath& mesh = rigidMeshes[i];
			const MString meshName = mesh.partialPathName();
			if (DagUtils::getAbsoluteNamespace(mesh.node()) != expectedNamespace)
			{
				MGlobal::displayError(meshName + " is not in the rig's namespace (" + expectedNamespace + "), so it would find no joint to bind to.");
				hasErrors = true;
				continue;
			}
			if (containsNode(fileMeshes, mesh.node()))
			{
				MGlobal::displayError(meshName + " is passed as a mesh with a weight file and as a rigid mesh.");
				hasErrors = true;
				continue;
			}
			SkinWeights weights;
			status = SkinWeightScene::makeRigidSkinWeights(mesh, topJoint, rigHash, weights);
			if (!status)
			{
				hasErrors = true;
				continue;
			}
			fileMeshes.emplace_back(mesh.node());
			fMeshes.append(mesh);
			fMeshNames.push_back(weights.meshName);
			fLoads.push_back(std::make_unique<SkinWeightLoad>());
			status = fLoads.back()->prepare(mesh, weights);
			if (!status)
			{
				hasErrors = true;
			}
			rigidReports[i].meshId = weights.meshId;
			rigidReports[i].meshName = weights.meshName;
			fReport.meshes.push_back(std::move(rigidReports[i]));
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
		MPlug pMessage(fMeshes[i].node(), MPxNode::message);
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

const LookReport& LookSwap::report() const
{
	return fReport;
}

MString LookSwap::lookRootName() const
{
	return fLookRoot.isNull() ? MString() : MFnDependencyNode(fLookRoot).name();
}
