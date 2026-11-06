#include "maya/shared/JointMarkupScene.h"
#include "core/Constants.h"
#include "core/RigTemplateValidation.h"

#include <maya/MDagPath.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MPlug.h>

namespace
{
	// Shared by readMarkup, which displays outError, and collectMarkedJoints, which reports it.
	// outError is empty when a Maya call failed rather than the markup text.
	MStatus readMarkupAttr(const MObject& oJoint, bool& outHasMarkup, JointMarkup& outMarkup, std::string& outError)
	{
		MStatus status;
		MFnDependencyNode fnJoint(oJoint, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		outHasMarkup = fnJoint.hasAttribute(RigAttr::JointMarkup);
		if (!outHasMarkup)
		{
			return MStatus::kSuccess;
		}

		MPlug pMarkup = fnJoint.findPlug(RigAttr::JointMarkup, false, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		const MString markupText = pMarkup.asString(&status);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		if (!parseJointMarkup(markupText.asChar(), outMarkup, outError))
		{
			return MStatus::kFailure;
		}

		return MStatus::kSuccess;
	}
}

void JointMarkupScene::collectMarkedJoints(const MDagPathArray& joints, MarkedJointSet& outSet, ValidationReport& report)
{
	for (unsigned int i = 0; i < joints.length(); ++i)
	{
		const MDagPath& jointPath = joints[i];
		const std::string jointName = jointPath.fullPathName().asChar();

		bool hasMarkup = false;
		JointMarkup markup;
		std::string error;
		MStatus status = readMarkupAttr(jointPath.node(), hasMarkup, markup, error);
		if (!status)
		{
			report.addError(jointName, error.empty() ? "could not read rigMarkup: " + std::string(status.errorString().asChar()) : "invalid rigMarkup: " + error);
			continue;
		}
		if (!hasMarkup)
		{
			report.addError(jointName, "has no rigMarkup. Mark it with rigJointMarkup, or do not pass it");
			continue;
		}

		if (!outSet.index.add(markup, jointName, error))
		{
			report.addError(jointName, error);
			continue;
		}
		outSet.dagPaths.append(jointPath);
	}
}

MStatus JointMarkupScene::readMarkup(const MObject& oJoint, bool& outHasMarkup, JointMarkup& outMarkup)
{
	std::string error;
	MStatus status = readMarkupAttr(oJoint, outHasMarkup, outMarkup, error);
	if (!status && !error.empty())
	{
		MGlobal::displayError(MString("Invalid joint markup on ") + MFnDependencyNode(oJoint).name() + ": " + error.c_str());
	}
	return status;
}

MStatus JointMarkupScene::claimJoint(
	MarkedJointSet& markedJoints,
	const JointSelector& selector,
	const std::string& moduleName,
	MObject& oOutJoint,
	MMatrix& mOutRestWorld,
	std::string& outError)
{
	std::size_t entry = 0;
	if (!markedJoints.index.claim(selector, moduleName, entry, outError))
	{
		return MStatus::kFailure;
	}

	MStatus status;
	const MDagPath& jointPath = markedJoints.dagPaths[static_cast<unsigned int>(entry)];
	oOutJoint = jointPath.node();

	MFnDependencyNode fnJoint(oOutJoint);
	MPlug pOffsetParentMatrix = fnJoint.findPlug("offsetParentMatrix", false, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	if (pOffsetParentMatrix.isDestination())
	{
		outError = "joint '" + std::string(fnJoint.name().asChar()) + "' already has an incoming offsetParentMatrix connection";
		return MStatus::kFailure;
	}

	mOutRestWorld = jointPath.inclusiveMatrix(&status);
	return status;
}
