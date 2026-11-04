#include "JointMarkupCmd.h"
#include "JointMarkup.h"
#include "JointMarkupScene.h"
#include "Constants.h"
#include "Utilities.h"

#include <array>
#include <string>

#include <maya/MFnDependencyNode.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MPlug.h>
#include <maya/MStringArray.h>

namespace
{
	// Code reads flags by long name only. MSyntax::addFlag requires a short name of at most 3 letters
	// (a long name there is rejected and the flag silently never registers), so short names appear only in newSyntax().
	constexpr const char* kUsageFlag{ "-usage" };
	constexpr const char* kSideFlag{ "-side" };
	constexpr const char* kExportFlag{ "-export" };
	constexpr const char* kSkinFlag{ "-skin" };
	constexpr const char* kRemoveFlag{ "-remove" };

	constexpr std::array kValueFlags{ kUsageFlag, kSideFlag, kExportFlag, kSkinFlag };

	unsigned int countValueFlags(const MArgDatabase& argData)
	{
		unsigned int count = 0;
		for (const char* flag : kValueFlags)
		{
			if (argData.isFlagSet(flag))
			{
				++count;
			}
		}
		return count;
	}

	// Writes every value flag that was passed into the markup, leaving the other fields as they are.
	MStatus applyValueFlags(const MArgDatabase& argData, JointMarkup& markup)
	{
		MStatus status;
		if (argData.isFlagSet(kUsageFlag))
		{
			MString usage;
			status = argData.getFlagArgument(kUsageFlag, 0, usage);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			markup.usage = usage.asChar();
		}
		if (argData.isFlagSet(kSideFlag))
		{
			MString side;
			status = argData.getFlagArgument(kSideFlag, 0, side);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			markup.side = side.asChar();
		}
		if (argData.isFlagSet(kExportFlag))
		{
			status = argData.getFlagArgument(kExportFlag, 0, markup.isExported);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		if (argData.isFlagSet(kSkinFlag))
		{
			status = argData.getFlagArgument(kSkinFlag, 0, markup.isSkinned);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		return MStatus::kSuccess;
	}

	MStatus validateForJoint(const JointMarkup& markup, const MObject& oJoint)
	{
		std::string error;
		if (!validateJointMarkup(markup, error))
		{
			MGlobal::displayError(MString("Invalid joint markup for ") + MFnDependencyNode(oJoint).name() + ": " + error.c_str());
			return MStatus::kFailure;
		}
		return MStatus::kSuccess;
	}

	// The query result for one flag. Strings come back unquoted and bools as "true"/"false".
	MString queryValue(const MArgDatabase& argData, const JointMarkup& markup)
	{
		if (argData.isFlagSet(kUsageFlag))
		{
			return markup.usage.c_str();
		}
		if (argData.isFlagSet(kSideFlag))
		{
			return markup.side.c_str();
		}
		if (argData.isFlagSet(kExportFlag))
		{
			return markup.isExported ? "true" : "false";
		}
		if (argData.isFlagSet(kSkinFlag))
		{
			return markup.isSkinned ? "true" : "false";
		}
		return serializeJointMarkup(markup).c_str();
	}
}

const char* JointMarkupCmd::aCommandString = "rigJointMarkup";

JointMarkupCmd::JointMarkupCmd()
{
}

JointMarkupCmd::~JointMarkupCmd()
{
}

void* JointMarkupCmd::creator()
{
	return new JointMarkupCmd();
}

bool JointMarkupCmd::isUndoable() const
{
	return fIsUndoable;
}

MSyntax JointMarkupCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-us", kUsageFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-sd", kSideFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-ex", kExportFlag, MSyntax::kBoolean));
	CHECK_MSTATUS(syntax.addFlag("-sk", kSkinFlag, MSyntax::kBoolean));
	CHECK_MSTATUS(syntax.addFlag("-rm", kRemoveFlag));
	syntax.enableQuery(true);
	syntax.enableEdit(true);
	syntax.setObjectType(MSyntax::kStringObjects, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus JointMarkupCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError("Error parsing rigJointMarkup arguments.");
		return status;
	}

	MStringArray jointNames;
	status = argData.getObjects(jointNames);
	if (!status || jointNames.length() == 0)
	{
		MGlobal::displayError("rigJointMarkup needs at least one joint name.");
		return MStatus::kFailure;
	}

	// Resolve every joint before changing anything, so a bad name leaves the scene untouched.
	MObjectArray oJoints;
	for (unsigned int i = 0; i < jointNames.length(); ++i)
	{
		MObject oJoint;
		status = DagUtils::getJointFromName(jointNames[i], oJoint);
		if (!status)
		{
			return status;   // helper already reported which joint failed
		}
		oJoints.append(oJoint);
	}

	if (argData.isQuery())
	{
		fIsUndoable = false;
		return doQuery(argData, oJoints);
	}

	if (argData.isFlagSet(kRemoveFlag))
	{
		if (argData.isEdit() || countValueFlags(argData) > 0)
		{
			MGlobal::displayError("rigJointMarkup -remove cannot be combined with -edit or value flags.");
			return MStatus::kFailure;
		}
		status = doRemove(oJoints);
	}
	else if (argData.isEdit())
	{
		status = doEdit(argData, oJoints);
	}
	else
	{
		status = doCreate(argData, oJoints);
	}

	if (!status)
	{
		return status;
	}

	return redoIt();
}

MStatus JointMarkupCmd::doQuery(const MArgDatabase& argData, const MObjectArray& oJoints)
{
	if (countValueFlags(argData) > 1 || argData.isFlagSet(kRemoveFlag))
	{
		MGlobal::displayError("rigJointMarkup -query takes at most one of -usage, -side, -export, -skin.");
		return MStatus::kFailure;
	}

	MStringArray results;
	for (unsigned int i = 0; i < oJoints.length(); ++i)
	{
		bool hasMarkup = false;
		JointMarkup markup;
		MStatus status = JointMarkupScene::readMarkup(oJoints[i], hasMarkup, markup);
		if (!status)
		{
			return status;
		}

		results.append(hasMarkup ? queryValue(argData, markup) : MString());
	}

	setResult(results);
	return MStatus::kSuccess;
}

MStatus JointMarkupCmd::doCreate(const MArgDatabase& argData, const MObjectArray& oJoints)
{
	MStatus status;
	if (countValueFlags(argData) != kValueFlags.size())
	{
		MGlobal::displayError("rigJointMarkup create needs -usage, -side, -export and -skin. Use -edit to change existing markup.");
		return MStatus::kFailure;
	}

	JointMarkup markup;
	status = applyValueFlags(argData, markup);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	for (unsigned int i = 0; i < oJoints.length(); ++i)
	{
		MFnDependencyNode fnJoint(oJoints[i]);
		if (fnJoint.hasAttribute(RigAttr::JointMarkup))
		{
			MGlobal::displayError(fnJoint.name() + " already has joint markup. Use -edit to change it.");
			return MStatus::kFailure;
		}
	}

	status = validateForJoint(markup, oJoints[0]);
	if (!status)
	{
		return status;
	}

	// One attribute object per joint: a dynamic attribute MObject belongs to the node it is added to.
	MObjectArray oMarkupAttrs;
	for (unsigned int i = 0; i < oJoints.length(); ++i)
	{
		MFnTypedAttribute fnTypedAttr;
		MObject oMarkupAttr = fnTypedAttr.create(RigAttr::JointMarkup, RigAttr::JointMarkup, MFnData::kString, MObject::kNullObj, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		fnTypedAttr.setStorable(true);
		fnTypedAttr.setKeyable(false);

		status = fDGMod.addAttribute(oJoints[i], oMarkupAttr);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		oMarkupAttrs.append(oMarkupAttr);
	}

	// The attributes must exist before their plugs can take values.
	// Later doIt calls run only the newly queued ops, and undoIt reverts both batches.
	status = fDGMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	const MString markupText = serializeJointMarkup(markup).c_str();
	for (unsigned int i = 0; i < oJoints.length(); ++i)
	{
		MPlug pMarkup(oJoints[i], oMarkupAttrs[i]);
		status = fDGMod.newPlugValueString(pMarkup, markupText);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}

	return MStatus::kSuccess;
}

MStatus JointMarkupCmd::doEdit(const MArgDatabase& argData, const MObjectArray& oJoints)
{
	MStatus status;
	if (countValueFlags(argData) == 0)
	{
		MGlobal::displayError("rigJointMarkup -edit needs at least one of -usage, -side, -export, -skin.");
		return MStatus::kFailure;
	}

	// Build every new value first, so one bad joint leaves the scene untouched.
	MStringArray markupTexts;
	for (unsigned int i = 0; i < oJoints.length(); ++i)
	{
		bool hasMarkup = false;
		JointMarkup markup;
		status = JointMarkupScene::readMarkup(oJoints[i], hasMarkup, markup);
		if (!status)
		{
			return status;
		}
		if (!hasMarkup)
		{
			MGlobal::displayError(MFnDependencyNode(oJoints[i]).name() + " has no joint markup to edit. Create it first.");
			return MStatus::kFailure;
		}

		status = applyValueFlags(argData, markup);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		status = validateForJoint(markup, oJoints[i]);
		if (!status)
		{
			return status;
		}

		markupTexts.append(serializeJointMarkup(markup).c_str());
	}

	for (unsigned int i = 0; i < oJoints.length(); ++i)
	{
		MFnDependencyNode fnJoint(oJoints[i]);
		MPlug pMarkup = fnJoint.findPlug(RigAttr::JointMarkup, false, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		status = fDGMod.newPlugValueString(pMarkup, markupTexts[i]);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}

	return MStatus::kSuccess;
}

MStatus JointMarkupCmd::doRemove(const MObjectArray& oJoints)
{
	MStatus status;
	for (unsigned int i = 0; i < oJoints.length(); ++i)
	{
		MFnDependencyNode fnJoint(oJoints[i]);
		if (!fnJoint.hasAttribute(RigAttr::JointMarkup))
		{
			continue;   // removing markup that is not there is a no-op
		}

		MObject oMarkupAttr = fnJoint.attribute(RigAttr::JointMarkup, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		status = fDGMod.removeAttribute(oJoints[i], oMarkupAttr);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}

	return MStatus::kSuccess;
}

MStatus JointMarkupCmd::redoIt()
{
	MStatus status = fDGMod.doIt();
	if (!status)
	{
		MGlobal::displayError("rigJointMarkup failed to apply: " + status.errorString());
	}
	return status;
}

MStatus JointMarkupCmd::undoIt()
{
	return fDGMod.undoIt();
}
