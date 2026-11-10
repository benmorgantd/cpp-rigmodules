#include "maya/commands/SkinWeightCmds.h"
#include "core/SkinWeightFile.h"
#include "maya/shared/Utilities.h"

#include <string>

#include <maya/MArgDatabase.h>
#include <maya/MDagPath.h>
#include <maya/MGlobal.h>
#include <maya/MStringArray.h>

namespace
{
	// Code reads flags by long name only. Short names appear only in newSyntax() (D14).
	constexpr const char* kFileFlag{ "-file" };
	// Not "-new": Maya rejects a three-letter long name, and the flag silently never registers.
	constexpr const char* kNewIdFlag{ "-newId" };
	constexpr const char* kMaxInfluencesFlag{ "-maxInfluences" };

	// The one mesh the save and load commands take, plus their file.
	MStatus readMeshAndFile(const MArgList& args, const MSyntax& syntax, const char* commandName, MDagPath& outMesh, MString& outPath)
	{
		MStatus status;
		MArgDatabase argData(syntax, args, &status);
		if (!status)
		{
			MGlobal::displayError(MString("Error parsing ") + commandName + " arguments.");
			return status;
		}

		MString meshName;
		status = CommandUtils::readSingleObject(argData, commandName, "mesh", meshName);
		if (!status)
		{
			return status;
		}
		status = DagUtils::getDagPathFromName(meshName, outMesh, DagUtils::NodeKind::kMesh);
		if (!status)
		{
			return status;
		}
		return CommandUtils::readFilePath(argData, kFileFlag, commandName, outPath);
	}

	MSyntax meshAndFileSyntax()
	{
		MSyntax syntax;
		CHECK_MSTATUS(syntax.addFlag("-f", kFileFlag, MSyntax::kString));
		syntax.setObjectType(MSyntax::kStringObjects, 1, 1);
		syntax.useSelectionAsDefault(false);
		return syntax;
	}
}

// setRigMeshId -----------------------------------------------------------------------------------
const char* SetRigMeshIdCmd::aCommandString = "setRigMeshId";

void* SetRigMeshIdCmd::creator()
{
	return new SetRigMeshIdCmd();
}

bool SetRigMeshIdCmd::isUndoable() const
{
	return true;
}

MSyntax SetRigMeshIdCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-ni", kNewIdFlag, MSyntax::kBoolean));
	syntax.setObjectType(MSyntax::kStringObjects, 0);
	// Read by hand in doIt, filtered to meshes, so a selection that includes joints or controls still works.
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus SetRigMeshIdCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError(MString("Error parsing ") + aCommandString + " arguments.");
		return status;
	}

	bool isNew = false;
	if (argData.isFlagSet(kNewIdFlag))
	{
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kNewIdFlag, 0, isNew));
	}

	MDagPathArray meshes;
	status = CommandUtils::readMeshesOrSelection(argData, aCommandString, meshes);
	if (!status)
	{
		return status;
	}

	MStringArray ids;
	for (unsigned int i = 0; i < meshes.length(); ++i)
	{
		std::string id;
		status = SkinWeightScene::getMeshId(meshes[i], id);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		if (id.empty() || isNew)
		{
			status = SkinWeightScene::queueNewMeshId(meshes[i], fDGMod, id);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		ids.append(id.c_str());
	}

	setResult(ids);
	return redoIt();
}

MStatus SetRigMeshIdCmd::redoIt()
{
	return fDGMod.doIt();
}

MStatus SetRigMeshIdCmd::undoIt()
{
	return fDGMod.undoIt();
}

// saveRigSkinWeights -----------------------------------------------------------------------------
const char* SaveRigSkinWeightsCmd::aCommandString = "saveRigSkinWeights";

void* SaveRigSkinWeightsCmd::creator()
{
	return new SaveRigSkinWeightsCmd();
}

bool SaveRigSkinWeightsCmd::isUndoable() const
{
	return false;
}

MSyntax SaveRigSkinWeightsCmd::newSyntax()
{
	return meshAndFileSyntax();
}

MStatus SaveRigSkinWeightsCmd::doIt(const MArgList& args)
{
	MDagPath mesh;
	MString path;
	MStatus status = readMeshAndFile(args, syntax(), aCommandString, mesh, path);
	if (!status)
	{
		return status;
	}

	SkinWeights weights;
	status = SkinWeightScene::readSkinWeights(mesh, weights);
	if (!status)
	{
		return status;
	}

	status = CommandUtils::writeTextFile(path, "weight", serializeSkinWeights(weights));
	if (!status)
	{
		return status;
	}

	setResult(static_cast<int>(weights.influences.size()));
	return MStatus::kSuccess;
}

// loadRigSkinWeights -----------------------------------------------------------------------------
const char* LoadRigSkinWeightsCmd::aCommandString = "loadRigSkinWeights";

void* LoadRigSkinWeightsCmd::creator()
{
	return new LoadRigSkinWeightsCmd();
}

bool LoadRigSkinWeightsCmd::isUndoable() const
{
	return true;
}

MSyntax LoadRigSkinWeightsCmd::newSyntax()
{
	return meshAndFileSyntax();
}

MStatus LoadRigSkinWeightsCmd::doIt(const MArgList& args)
{
	MDagPath mesh;
	MString path;
	MStatus status = readMeshAndFile(args, syntax(), aCommandString, mesh, path);
	if (!status)
	{
		return status;
	}

	std::string text;
	status = CommandUtils::readTextFile(path, "weight", text);
	if (!status)
	{
		return status;
	}

	// Validate the whole file before queueing anything, so a bad file changes nothing.
	SkinWeights weights;
	ValidationReport report;
	deserializeSkinWeights(text, weights, report);
	if (report.hasErrors())
	{
		ReportUtils::displayReport(report);
		MGlobal::displayError(MString("The weight file failed validation, so nothing was loaded: ") + path);
		return MStatus::kFailure;
	}

	status = fLoad.prepare(mesh, weights);
	if (!status)
	{
		return status;
	}
	status = redoIt();
	if (status)
	{
		setResult(fLoad.skinClusterName());
	}
	return status;
}

MStatus LoadRigSkinWeightsCmd::redoIt()
{
	MStatus status = fLoad.apply();
	if (!status)
	{
		// A failed command never reaches the undo queue, so take back whatever part of it ran.
		fLoad.undo();
		MGlobal::displayError(MString("loadRigSkinWeights failed to apply: ") + status.errorString());
	}
	return status;
}

MStatus LoadRigSkinWeightsCmd::undoIt()
{
	return fLoad.undo();
}

// pruneToMaxInfluences ---------------------------------------------------------------------------
const char* PruneToMaxInfluencesCmd::aCommandString = "pruneToMaxInfluences";

void* PruneToMaxInfluencesCmd::creator()
{
	return new PruneToMaxInfluencesCmd();
}

bool PruneToMaxInfluencesCmd::isUndoable() const
{
	return true;
}

MSyntax PruneToMaxInfluencesCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-mi", kMaxInfluencesFlag, MSyntax::kLong));
	syntax.setObjectType(MSyntax::kStringObjects, 0);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus PruneToMaxInfluencesCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError(MString("Error parsing ") + aCommandString + " arguments.");
		return status;
	}

	int maxInfluences = 0;
	if (!argData.isFlagSet(kMaxInfluencesFlag) || !argData.getFlagArgument(kMaxInfluencesFlag, 0, maxInfluences) || maxInfluences < 1)
	{
		MGlobal::displayError(MString(aCommandString) + " needs -maxInfluences of at least 1.");
		return MStatus::kFailure;
	}

	MDagPathArray meshes;
	status = CommandUtils::readMeshesOrSelection(argData, aCommandString, meshes);
	if (!status)
	{
		return status;
	}

	// Every mesh is checked and pruned in memory before any is written, so one without a skinCluster changes nothing.
	fPrunes.assign(meshes.length(), SkinWeightPrune());
	int changedCount = 0;
	for (unsigned int i = 0; i < meshes.length(); ++i)
	{
		status = fPrunes[i].prepare(meshes[i], static_cast<unsigned int>(maxInfluences));
		if (!status)
		{
			return status;
		}
		changedCount += static_cast<int>(fPrunes[i].changedVertexCount());
	}

	setResult(changedCount);
	return redoIt();
}

MStatus PruneToMaxInfluencesCmd::redoIt()
{
	for (SkinWeightPrune& prune : fPrunes)
	{
		MStatus status = prune.apply();
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	return MStatus::kSuccess;
}

MStatus PruneToMaxInfluencesCmd::undoIt()
{
	for (auto it = fPrunes.rbegin(); it != fPrunes.rend(); ++it)
	{
		MStatus status = it->undo();
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	return MStatus::kSuccess;
}
