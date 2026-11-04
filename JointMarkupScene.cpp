#include "JointMarkupScene.h"
#include "Constants.h"

#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MItDag.h>
#include <maya/MNamespace.h>
#include <maya/MPlug.h>

namespace
{
	// Maya reports the root namespace as "" from parentNamespace() but users write ":".
	// Leading and trailing colons are dropped so ":char", "char:" and "char" all compare equal.
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
}

MStatus JointMarkupScene::collectMarkedJoints(const MString& namespaceName, MarkedJointSet& outSet)
{
	MStatus status;
	const MString targetNamespace = normalizeNamespace(namespaceName);

	if (targetNamespace.length() > 0 && !MNamespace::namespaceExists(MString(":") + targetNamespace))
	{
		MGlobal::displayError(MString("Joint markup namespace does not exist: ") + namespaceName);
		return MStatus::kFailure;
	}

	MItDag itJoints(MItDag::kDepthFirst, MFn::kJoint, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	for (; !itJoints.isDone(); itJoints.next())
	{
		MDagPath jointPath;
		status = itJoints.getPath(jointPath);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		MObject oJoint = jointPath.node();
		MFnDependencyNode fnJoint(oJoint);
		if (normalizeNamespace(fnJoint.parentNamespace()) != targetNamespace)
		{
			continue;
		}

		bool hasMarkup = false;
		JointMarkup markup;
		status = readMarkup(oJoint, hasMarkup, markup);
		if (!status)
		{
			return status;
		}
		if (!hasMarkup)
		{
			continue;
		}

		std::string error;
		if (!outSet.index.add(markup, jointPath.fullPathName().asChar(), error))
		{
			MGlobal::displayError(MString("Joint markup: ") + error.c_str());
			return MStatus::kFailure;
		}
		outSet.dagPaths.push_back(jointPath);
	}

	return MStatus::kSuccess;
}

MStatus JointMarkupScene::readMarkup(const MObject& oJoint, bool& outHasMarkup, JointMarkup& outMarkup)
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

	std::string error;
	if (!parseJointMarkup(markupText.asChar(), outMarkup, error))
	{
		MGlobal::displayError(MString("Invalid joint markup on ") + fnJoint.name() + ": " + error.c_str());
		return MStatus::kFailure;
	}

	return MStatus::kSuccess;
}

MStatus JointMarkupScene::claimJoint(
	MarkedJointSet& markedJoints,
	const JointSelector& selector,
	const std::string& moduleName,
	MObject& oOutJoint,
	MMatrix& mOutRestWorld)
{
	std::size_t entry = 0;
	std::string error;
	if (!markedJoints.index.claim(selector, moduleName, entry, error))
	{
		MGlobal::displayError(MString("Joint markup: ") + error.c_str());
		return MStatus::kFailure;
	}

	MStatus status;
	const MDagPath& jointPath = markedJoints.dagPaths[entry];
	oOutJoint = jointPath.node();
	mOutRestWorld = jointPath.inclusiveMatrix(&status);
	return status;
}
