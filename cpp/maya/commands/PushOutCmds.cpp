#include "maya/commands/PushOutCmds.h"
#include "core/Constants.h"
#include "core/StringUtils.h"
#include "maya/shared/PushOutScene.h"
#include "maya/shared/Utilities.h"

#include <string>

#include <maya/MArgDatabase.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MNamespace.h>
#include <maya/MObjectArray.h>
#include <maya/MStringArray.h>

namespace
{
	constexpr const char* kNamespaceFlag{ "-namespace" };

	MSyntax namespaceSyntax()
	{
		MSyntax syntax;
		CHECK_MSTATUS(syntax.addFlag("-ns", kNamespaceFlag, MSyntax::kString));
		return syntax;
	}

	// Colons at the ends are dropped, so ":rig", "rig:" and "rig" mean the same namespace, and ":" is the root.
	MStatus readNamespace(const MArgDatabase& argData, MString& outNamespace)
	{
		MString namespaceName = MNamespace::currentNamespace();
		if (argData.isFlagSet(kNamespaceFlag))
		{
			CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kNamespaceFlag, 0, namespaceName));
		}
		outNamespace = StringUtils::trim(namespaceName.asChar(), ':').c_str();
		return MStatus::kSuccess;
	}

	MString absoluteNamespace(const MString& namespaceName)
	{
		return MString(":") + namespaceName;
	}

	MStringArray namesOf(const MObjectArray& oNodes)
	{
		MStringArray names;
		for (unsigned int i = 0; i < oNodes.length(); ++i)
		{
			names.append(DagUtils::getPartialPathName(oNodes[i]));
		}
		return names;
	}
}

// showRigPushOuts --------------------------------------------------------------------------------
const char* ShowRigPushOutsCmd::aCommandString = "showRigPushOuts";

void* ShowRigPushOutsCmd::creator()
{
	return new ShowRigPushOutsCmd();
}

bool ShowRigPushOutsCmd::isUndoable() const
{
	return true;
}

MSyntax ShowRigPushOutsCmd::newSyntax()
{
	return namespaceSyntax();
}

MStatus ShowRigPushOutsCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError(MString("Error parsing ") + aCommandString + " arguments.");
		return status;
	}
	CHECK_MSTATUS_AND_RETURN_IT(readNamespace(argData, fNamespace));

	MObjectArray oPushOuts;
	PushOutScene::findPushOuts(absoluteNamespace(fNamespace), oPushOuts);
	if (oPushOuts.length() == 0)
	{
		MGlobal::displayWarning(MString(aCommandString) + ": there are no push outs in the namespace '" + fNamespace + "'.");
		setResult(MStringArray());
		return MStatus::kSuccess;
	}

	// Nodes are created relative to the current namespace, so it is the rig's while they are made.
	ScopedRigNamespace scopedNamespace(fNamespace);
	status = scopedNamespace.status();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MObjectArray oDisplays;
	std::string error;
	status = PushOutScene::queueCreateDisplays(oPushOuts, fDagMod, oDisplays, error);
	if (!status)
	{
		MGlobal::displayError(MString(aCommandString) + ": " + (error.empty() ? "the displays could not be made" : error.c_str()));
		fDagMod.undoIt();
		return MStatus::kFailure;
	}
	setResult(namesOf(oDisplays));
	return MStatus::kSuccess;
}

MStatus ShowRigPushOutsCmd::redoIt()
{
	ScopedRigNamespace scopedNamespace(fNamespace);
	const MStatus status = scopedNamespace.status();
	CHECK_MSTATUS_AND_RETURN_IT(status);
	return fDagMod.doIt();
}

MStatus ShowRigPushOutsCmd::undoIt()
{
	return fDagMod.undoIt();
}

// hideRigPushOuts --------------------------------------------------------------------------------
const char* HideRigPushOutsCmd::aCommandString = "hideRigPushOuts";

void* HideRigPushOutsCmd::creator()
{
	return new HideRigPushOutsCmd();
}

bool HideRigPushOutsCmd::isUndoable() const
{
	return true;
}

MSyntax HideRigPushOutsCmd::newSyntax()
{
	return namespaceSyntax();
}

MStatus HideRigPushOutsCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError(MString("Error parsing ") + aCommandString + " arguments.");
		return status;
	}
	MString namespaceName;
	CHECK_MSTATUS_AND_RETURN_IT(readNamespace(argData, namespaceName));

	MObjectArray oDisplays;
	PushOutScene::findDisplays(absoluteNamespace(namespaceName), oDisplays);
	const MStringArray names = namesOf(oDisplays);
	status = PushOutScene::queueDeleteDisplays(oDisplays, fDagMod);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	status = fDagMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);
	setResult(names);
	return MStatus::kSuccess;
}

MStatus HideRigPushOutsCmd::redoIt()
{
	return fDagMod.doIt();
}

MStatus HideRigPushOutsCmd::undoIt()
{
	return fDagMod.undoIt();
}

// getRigPushOutCapsule ---------------------------------------------------------------------------
const char* GetRigPushOutCapsuleCmd::aCommandString = "getRigPushOutCapsule";

void* GetRigPushOutCapsuleCmd::creator()
{
	return new GetRigPushOutCapsuleCmd();
}

bool GetRigPushOutCapsuleCmd::isUndoable() const
{
	return false;
}

MSyntax GetRigPushOutCapsuleCmd::newSyntax()
{
	MSyntax syntax;
	syntax.setObjectType(MSyntax::kStringObjects, 1, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus GetRigPushOutCapsuleCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError(MString("Error parsing ") + aCommandString + " arguments.");
		return status;
	}

	MString nodeName;
	status = CommandUtils::readSingleObject(argData, aCommandString, "display node", nodeName);
	if (!status)
	{
		return status;
	}
	MObject oDisplay;
	status = DagUtils::getMObjectFromName(nodeName, oDisplay);
	if (!status || !MFnDependencyNode(oDisplay).hasAttribute(RigAttr::PushOutDisplay))
	{
		MGlobal::displayError(MString(aCommandString) + ": " + nodeName + " is not a push-out display (showRigPushOuts makes them).");
		return MStatus::kFailure;
	}

	std::string capsule;
	std::string error;
	if (!PushOutScene::readCapsule(oDisplay, capsule, error))
	{
		MGlobal::displayError(MString(aCommandString) + ": " + nodeName + ": " + error.c_str());
		return MStatus::kFailure;
	}
	setResult(MString(capsule.c_str()));
	return MStatus::kSuccess;
}
