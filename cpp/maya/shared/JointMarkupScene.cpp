#include "maya/shared/JointMarkupScene.h"
#include "core/Constants.h"
#include "core/RigTemplateValidation.h"
#include "maya/shared/Utilities.h"

#include <maya/MDagPath.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MItDag.h>
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

void JointMarkupScene::collectNamespaceJoints(const MString& absoluteNamespace, MarkedJointSet& outSet, ValidationReport& report)
{
	MStatus status;
	MDagPathArray joints;
	for (MItDag itDag(MItDag::kDepthFirst, MFn::kJoint, &status); status && !itDag.isDone(); itDag.next())
	{
		MDagPath jointPath;
		itDag.getPath(jointPath);
		// An instanced joint is visited once per path, but it is one joint.
		if (jointPath.instanceNumber() != 0 || DagUtils::getAbsoluteNamespace(jointPath.node()) != absoluteNamespace)
		{
			continue;
		}
		if (MFnDependencyNode(jointPath.node()).hasAttribute(RigAttr::JointMarkup))
		{
			joints.append(jointPath);
		}
	}
	collectMarkedJoints(joints, outSet, report);
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

MStatus JointMarkupScene::collectJointSubtree(const MDagPath& rootPath, std::vector<HierarchyJoint>& outJoints)
{
	MStatus status;
	outJoints.clear();

	// The index in outJoints of the last joint seen at each path length, or -1 when that joint was not collected. In a
	// depth-first walk the last joint seen one level up is the parent of a joint when the parent is a joint at all.
	std::vector<int> lastAtLength;
	MItDag itDag(MItDag::kDepthFirst, MFn::kJoint, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(itDag.reset(rootPath, MItDag::kDepthFirst, MFn::kJoint));

	for (; !itDag.isDone(); itDag.next())
	{
		MDagPath jointPath;
		CHECK_MSTATUS_AND_RETURN_IT(itDag.getPath(jointPath));

		const std::size_t length = jointPath.length();
		lastAtLength.resize(length + 1, -1);
		lastAtLength[length] = -1;   // Set below if this joint is collected

		// The first joint the walk returns is the root itself.
		int parent = -1;
		if (!outJoints.empty())
		{
			MDagPath parentPath(jointPath);
			parentPath.pop();
			parent = parentPath.hasFn(MFn::kJoint) ? lastAtLength[length - 1] : -1;
			if (parent < 0)
			{
				continue;
			}
		}

		HierarchyJoint joint;
		joint.label = jointPath.partialPathName().asChar();
		joint.parent = parent;

		JointMarkup markup;
		std::string error;
		const MStatus markupStatus = readMarkupAttr(jointPath.node(), joint.hasMarkup, markup, error);
		if (markupStatus && joint.hasMarkup)
		{
			joint.selector = JointSelector{ markup.side, markup.usage };
		}
		else
		{
			joint.hasMarkup = false;
		}

		lastAtLength[length] = static_cast<int>(outJoints.size());
		outJoints.push_back(joint);
	}
	return MStatus::kSuccess;
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
	MPlug pOffsetParentMatrix(oOutJoint, MPxTransform::offsetParentMatrix);
	if (pOffsetParentMatrix.isDestination())
	{
		outError = "joint '" + std::string(fnJoint.name().asChar()) + "' already has an incoming offsetParentMatrix connection";
		return MStatus::kFailure;
	}

	mOutRestWorld = jointPath.inclusiveMatrix(&status);
	return status;
}

MStatus JointMarkupScene::selectorOfJoint(const MObject& oJoint, JointSelector& outSelector)
{
	bool hasMarkup = false;
	JointMarkup markup;
	if (!oJoint.hasFn(MFn::kJoint) || !readMarkup(oJoint, hasMarkup, markup) || !hasMarkup)
	{
		MGlobal::displayError(MFnDependencyNode(oJoint).name() + " is not a marked joint.");
		return MStatus::kFailure;
	}
	outSelector = JointSelector{ markup.side, markup.usage };
	return MStatus::kSuccess;
}

bool JointMarkupScene::findMarkedAncestor(const MDagPath& node, JointSelector& outSelector)
{
	MDagPath ancestor(node);
	while (ancestor.pop() && ancestor.length() > 0)
	{
		bool hasMarkup = false;
		JointMarkup markup;
		if (ancestor.hasFn(MFn::kJoint) && readMarkup(ancestor.node(), hasMarkup, markup) && hasMarkup)
		{
			outSelector = JointSelector{ markup.side, markup.usage };
			return true;
		}
	}
	return false;
}

MStatus JointMarkupScene::queueAddMarkupAttribute(const MObject& oJoint, MDGModifier& dgMod, MObject& oOutMarkupAttr)
{
	MStatus status;
	// One attribute object per joint: a dynamic attribute MObject belongs to the node it is added to.
	MFnTypedAttribute fnTypedAttr;
	oOutMarkupAttr = fnTypedAttr.create(RigAttr::JointMarkup, RigAttr::JointMarkup, MFnData::kString, MObject::kNullObj, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	fnTypedAttr.setStorable(true);
	fnTypedAttr.setKeyable(false);

	return dgMod.addAttribute(oJoint, oOutMarkupAttr);
}
