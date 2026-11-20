#include "maya/commands/CreateRigCmd.h"
#include "core/AssetId.h"
#include "core/StringUtils.h"
#include "maya/shared/RigBuild.h"
#include "maya/shared/Utilities.h"

#include <filesystem>
#include <string>

#include <maya/MArgDatabase.h>
#include <maya/MGlobal.h>
#include <maya/MString.h>
#include <maya/MSyntax.h>

namespace
{
	// Code reads flags by long name only. MSyntax::addFlag requires a short name of at most 3 letters
	// (a long name there is rejected and the flag silently never registers), so short names appear only in newSyntax().
	constexpr const char* kTemplateFlag{ "-template" };
	constexpr const char* kAssetFlag{ "-asset" };
	constexpr const char* kNamespaceFlag{ "-namespace" };
	constexpr const char* kCheckMirrorPositionsFlag{ "-checkMirrorPositions" };
	constexpr const char* kParentRigFlag{ "-parentRig" };

	// Resolves every joint name before createRig runs, so a bad name fails before the read phase.
	MStatus readBuildInputs(const MArgDatabase& argData, RigBuildInputs& inputs)
	{
		MStatus status;
		if (!argData.isFlagSet(kTemplateFlag))
		{
			MGlobal::displayError("createRigFromTemplate needs -template.");
			return MStatus::kFailure;
		}
		status = argData.getFlagArgument(kTemplateFlag, 0, inputs.templatePath);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		if (inputs.templatePath.length() == 0 || !std::filesystem::exists(inputs.templatePath.asChar()))
		{
			MGlobal::displayError("Rig template file does not exist: " + inputs.templatePath);
			return MStatus::kFailure;
		}

		// Templates carry no asset, so one template can build many assets (D29).
		if (!argData.isFlagSet(kAssetFlag))
		{
			MGlobal::displayError("createRigFromTemplate needs -asset.");
			return MStatus::kFailure;
		}
		MString assetId;
		status = argData.getFlagArgument(kAssetFlag, 0, assetId);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		inputs.assetId = assetId.asChar();
		if (!AssetId::isValid(inputs.assetId))
		{
			MGlobal::displayError("Asset id '" + assetId + "' must be '\\'-separated folder names of lowercase letters, digits and '_'.");
			return MStatus::kFailure;
		}

		// Unset, the namespace defaults to "{asset name}_{rigName}" (D54), which createRig resolves once it has read the template.
		if (argData.isFlagSet(kNamespaceFlag))
		{
			MString rigNamespace;
			status = argData.getFlagArgument(kNamespaceFlag, 0, rigNamespace);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			// Leading and trailing colons are dropped so ":rig", "rig:" and "rig" all mean the same namespace, and ":" is the root.
			inputs.rigNamespace = MString(StringUtils::trim(rigNamespace.asChar(), ':').c_str());
		}

		if (argData.isFlagSet(kCheckMirrorPositionsFlag))
		{
			status = argData.getFlagArgument(kCheckMirrorPositionsFlag, 0, inputs.checkMirrorPositions);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}

		// The rig this one attaches to (D55). The root's own type is checked in the read phase, with the sockets.
		if (argData.isFlagSet(kParentRigFlag))
		{
			MString parentRig;
			status = argData.getFlagArgument(kParentRigFlag, 0, parentRig);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			status = DagUtils::getMObjectFromName(parentRig, inputs.oParentRigRoot);
			if (!status)
			{
				MGlobal::displayError("parentRig not found: " + parentRig);
				return status;
			}
		}

		return CommandUtils::readJoints(argData, "createRigFromTemplate", inputs.joints);
	}
}

const char* CreateRigCmd::aCommandString = "createRigFromTemplate";

CreateRigCmd::CreateRigCmd()
{
}

CreateRigCmd::~CreateRigCmd()
{
}

void* CreateRigCmd::creator()
{
	return new CreateRigCmd();
}

bool CreateRigCmd::isUndoable() const
{
	return true;
}

MSyntax CreateRigCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-tp", kTemplateFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-as", kAssetFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-ns", kNamespaceFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-cmp", kCheckMirrorPositionsFlag, MSyntax::kBoolean));
	CHECK_MSTATUS(syntax.addFlag("-pr", kParentRigFlag, MSyntax::kString));
	syntax.setObjectType(MSyntax::kStringObjects, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus CreateRigCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError("Error parsing createRigFromTemplate arguments.");
		return status;
	}

	RigBuildInputs inputs;
	status = readBuildInputs(argData, inputs);
	if (!status)
	{
		return status;
	}

	fCreatedNamespaces.captureBefore();
	status = RigBuild::createRig(inputs, fDagMod, fRigNamespace);
	if (!status)
	{
		return status;
	}

	status = redoIt();
	fCreatedNamespaces.recordCreated();
	return status;
}

MStatus CreateRigCmd::redoIt()
{
	const ScopedRigNamespace rigNamespaceScope(fRigNamespace);
	MStatus status = rigNamespaceScope.status();
	if (!status)
	{
		return status;
	}

	status = fDagMod.doIt();
	if (!status)
	{
		MGlobal::displayError("Failed executing DAG modifier during rig creation: " + status.errorString());
		return status;
	}

	// The build leaves the new nodes selected. A rig starts with nothing selected, so the animator picks what to work on.
	status = MGlobal::clearSelectionList();
	if (!status)
	{
		MGlobal::displayError("Failed clearing the selection after rig creation: " + status.errorString());
		return status;
	}

	MGlobal::displayInfo("Rig successfully created.");
	return MStatus::kSuccess;
}

MStatus CreateRigCmd::undoIt()
{
	MGlobal::displayInfo("Undoing creating the rig.");
	const MStatus status = fDagMod.undoIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);
	fCreatedNamespaces.removeCreated();
	return status;
}
