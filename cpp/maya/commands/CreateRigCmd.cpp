#include "maya/commands/CreateRigCmd.h"
#include "maya/nodes/RigRoot.h"
#include "maya/shared/Utilities.h"

#include <filesystem>
#include <string>

#include <maya/MArgDatabase.h>
#include <maya/MDagPath.h>
#include <maya/MGlobal.h>
#include <maya/MString.h>
#include <maya/MStringArray.h>
#include <maya/MSyntax.h>

namespace
{
	// Code reads flags by long name only. MSyntax::addFlag requires a short name of at most 3 letters
	// (a long name there is rejected and the flag silently never registers), so short names appear only in newSyntax().
	constexpr const char* kTemplateFlag{ "-template" };
	constexpr const char* kNamespaceFlag{ "-namespace" };
	constexpr const char* kCheckMirrorPositionsFlag{ "-checkMirrorPositions" };

	constexpr const char* kDefaultRigNamespace{ "rig" };

	// Leading and trailing colons are dropped so ":rig", "rig:" and "rig" all mean the same namespace, and ":" is the root.
	MString normalizeNamespace(const MString& namespaceName)
	{
		std::string text = namespaceName.asChar();
		while (!text.empty() && text.front() == ':')
		{
			text.erase(0, 1);
		}
		while (!text.empty() && text.back() == ':')
		{
			text.pop_back();
		}
		return MString(text.c_str());
	}

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

		MString rigNamespace(kDefaultRigNamespace);
		if (argData.isFlagSet(kNamespaceFlag))
		{
			status = argData.getFlagArgument(kNamespaceFlag, 0, rigNamespace);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		inputs.rigNamespace = normalizeNamespace(rigNamespace);

		if (argData.isFlagSet(kCheckMirrorPositionsFlag))
		{
			status = argData.getFlagArgument(kCheckMirrorPositionsFlag, 0, inputs.checkMirrorPositions);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}

		MStringArray jointNames;
		status = argData.getObjects(jointNames);
		if (!status || jointNames.length() == 0)
		{
			MGlobal::displayError("createRigFromTemplate needs at least one joint name.");
			return MStatus::kFailure;
		}

		for (unsigned int i = 0; i < jointNames.length(); ++i)
		{
			MDagPath jointPath;
			status = DagUtils::getJointDagPathFromName(jointNames[i], jointPath);
			if (!status)
			{
				return status;   // helper already reported which joint failed
			}
			inputs.joints.append(jointPath);
		}

		return MStatus::kSuccess;
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
	CHECK_MSTATUS(syntax.addFlag("-ns", kNamespaceFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-cmp", kCheckMirrorPositionsFlag, MSyntax::kBoolean));
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

	status = RigRootNode::createRig(inputs, fDagMod);
	if (!status)
	{
		return status;
	}

	fRigNamespace = inputs.rigNamespace;
	return redoIt();
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

	MGlobal::displayInfo("Rig successfully created.");
	return MStatus::kSuccess;
}

MStatus CreateRigCmd::undoIt()
{
	MGlobal::displayInfo("Undoing creating the rig.");
	return fDagMod.undoIt();
}
