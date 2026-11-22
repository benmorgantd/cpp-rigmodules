#include "maya/commands/RigLookCmd.h"
#include "core/RigTemplateValidation.h"
#include "core/SkinWeightFile.h"
#include "maya/shared/Utilities.h"

#include <string>
#include <utility>
#include <vector>

#include <maya/MArgDatabase.h>
#include <maya/MDagPath.h>
#include <maya/MGlobal.h>

namespace
{
	// Code reads flags by long name only. Short names appear only in newSyntax() (D14).
	constexpr const char* kMeshFlag{ "-mesh" };
	constexpr const char* kFileFlag{ "-file" };
	constexpr const char* kRigidMeshFlag{ "-rigidMesh" };
	constexpr const char* kAssetFlag{ "-asset" };
	constexpr const char* kLookFlag{ "-look" };
	constexpr const char* kPublishVersionFlag{ "-publishVersion" };
	constexpr const char* kSkeletonFlag{ "-skeleton" };
	constexpr const char* kRigSkeletonFlag{ "-rigSkeleton" };
	constexpr const char* kIgnoreSkeletonHashFlag{ "-ignoreSkeletonHash" };

	// The string argument of an optional flag, empty when it isn't set.
	MStatus readOptionalString(const MArgDatabase& argData, const char* flag, std::string& outValue)
	{
		outValue.clear();
		if (!argData.isFlagSet(flag))
		{
			return MStatus::kSuccess;
		}
		MString value;
		MStatus status = argData.getFlagArgument(flag, 0, value);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		outValue = value.asChar();
		return MStatus::kSuccess;
	}
}

const char* SetRigLookCmd::aCommandString = "setRigLook";

void* SetRigLookCmd::creator()
{
	return new SetRigLookCmd();
}

bool SetRigLookCmd::isUndoable() const
{
	return true;
}

MSyntax SetRigLookCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-m", kMeshFlag, MSyntax::kString));
	syntax.makeFlagMultiUse(kMeshFlag);
	CHECK_MSTATUS(syntax.addFlag("-f", kFileFlag, MSyntax::kString));
	syntax.makeFlagMultiUse(kFileFlag);
	CHECK_MSTATUS(syntax.addFlag("-rm", kRigidMeshFlag, MSyntax::kString));
	syntax.makeFlagMultiUse(kRigidMeshFlag);
	CHECK_MSTATUS(syntax.addFlag("-a", kAssetFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-lk", kLookFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-pv", kPublishVersionFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-sk", kSkeletonFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-rs", kRigSkeletonFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-ish", kIgnoreSkeletonHashFlag, MSyntax::kBoolean));
	syntax.setObjectType(MSyntax::kStringObjects, 1, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus SetRigLookCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	LookSwapInputs inputs;
	status = CommandUtils::readRigRoot(argData, status, aCommandString, inputs.oRigRoot);
	if (!status)
	{
		return status;
	}

	std::vector<std::string> meshNames;
	std::vector<std::string> files;
	std::vector<std::string> rigidMeshNames;
	CHECK_MSTATUS_AND_RETURN_IT(CommandUtils::readStringList(argData, kMeshFlag, meshNames));
	CHECK_MSTATUS_AND_RETURN_IT(CommandUtils::readStringList(argData, kFileFlag, files));
	CHECK_MSTATUS_AND_RETURN_IT(CommandUtils::readStringList(argData, kRigidMeshFlag, rigidMeshNames));
	if (meshNames.size() != files.size() || (meshNames.empty() && rigidMeshNames.empty()))
	{
		MGlobal::displayError(MString(aCommandString) + " needs one -file for each -mesh, and at least one -mesh or -rigidMesh.");
		return MStatus::kFailure;
	}
	CHECK_MSTATUS_AND_RETURN_IT(readOptionalString(argData, kAssetFlag, inputs.assetId));
	CHECK_MSTATUS_AND_RETURN_IT(readOptionalString(argData, kLookFlag, inputs.lookName));
	CHECK_MSTATUS_AND_RETURN_IT(readOptionalString(argData, kPublishVersionFlag, inputs.publishVersion));
	if (inputs.assetId.empty() || inputs.lookName.empty())
	{
		MGlobal::displayError(MString(aCommandString) + " needs -asset and -look.");
		return MStatus::kFailure;
	}
	if (argData.isFlagSet(kIgnoreSkeletonHashFlag))
	{
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kIgnoreSkeletonHashFlag, 0, inputs.ignoreSkeletonHash));
	}

	for (const std::string& meshName : meshNames)
	{
		MDagPath mesh;
		status = DagUtils::getDagPathFromName(meshName.c_str(), mesh, DagUtils::NodeKind::kMesh);
		if (!status)
		{
			return status;
		}
		inputs.meshes.append(mesh);
	}
	for (const std::string& meshName : rigidMeshNames)
	{
		MDagPath mesh;
		status = DagUtils::getDagPathFromName(meshName.c_str(), mesh, DagUtils::NodeKind::kMesh);
		if (!status)
		{
			return status;
		}
		inputs.rigidMeshes.append(mesh);
	}

	// Every file is read and validated before anything changes, so a bad file leaves the scene alone.
	for (const std::string& file : files)
	{
		std::string text;
		status = CommandUtils::readTextFile(file.c_str(), "weight", text);
		if (!status)
		{
			return status;
		}
		SkinWeights weights;
		ValidationReport report;
		deserializeSkinWeights(text, weights, report);
		if (report.hasErrors())
		{
			ReportUtils::displayReport(report);
			MGlobal::displayError(MString("The weight file failed validation, so no look was applied: ") + file.c_str());
			return MStatus::kFailure;
		}
		inputs.weights.push_back(std::move(weights));
	}

	// With the look's skeleton file, a different skeleton fails before any weight is read against the rig.
	std::string lookSkeletonPath;
	CHECK_MSTATUS_AND_RETURN_IT(readOptionalString(argData, kSkeletonFlag, lookSkeletonPath));
	if (!lookSkeletonPath.empty())
	{
		std::string rigSkeletonPath;
		CHECK_MSTATUS_AND_RETURN_IT(readOptionalString(argData, kRigSkeletonFlag, rigSkeletonPath));
		std::string lookText;
		status = CommandUtils::readTextFile(lookSkeletonPath.c_str(), "skeleton", lookText);
		if (!status)
		{
			return status;
		}
		std::string rigText;
		if (!rigSkeletonPath.empty())
		{
			status = CommandUtils::readTextFile(rigSkeletonPath.c_str(), "skeleton", rigText);
			if (!status)
			{
				return status;
			}
		}
		status = LookScene::checkSkeleton(inputs.oRigRoot, lookText, rigText);
		if (!status)
		{
			return status;
		}
	}

	status = fSwap.prepare(inputs);
	if (!status)
	{
		return status;
	}
	status = redoIt();
	if (status)
	{
		setResult(fSwap.lookRootName());
	}
	return status;
}

MStatus SetRigLookCmd::redoIt()
{
	MStatus status = fSwap.apply();
	if (!status)
	{
		// apply took back what it ran, so a failed command leaves the scene as it found it.
		MGlobal::displayError(MString("setRigLook failed to apply: ") + status.errorString());
	}
	return status;
}

MStatus SetRigLookCmd::undoIt()
{
	return fSwap.undo();
}
