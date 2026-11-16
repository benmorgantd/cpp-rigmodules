#include "maya/shared/SkeletonScene.h"
#include "core/RigTemplateValidation.h"
#include "maya/shared/JointMarkupScene.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <maya/MDagPath.h>
#include <maya/MFnTransform.h>
#include <maya/MMatrix.h>
#include <maya/MNamespace.h>
#include <maya/MTransformationMatrix.h>

void SkeletonScene::readSkeleton(const MDagPathArray& joints, Skeleton& outSkeleton, ValidationReport& report)
{
	MarkedJointSet markedJoints;
	JointMarkupScene::collectMarkedJoints(joints, markedJoints, report);
	if (report.hasErrors())
	{
		return;
	}

	const MDagPathArray& paths = markedJoints.dagPaths;
	std::map<std::string, unsigned int> indexByPath;
	for (unsigned int i = 0; i < paths.length(); ++i)
	{
		indexByPath[paths[i].fullPathName().asChar()] = i;
	}

	Skeleton skeleton;
	skeleton.joints.resize(paths.length());
	std::vector<int> parentIndex(paths.length(), -1);
	for (unsigned int i = 0; i < paths.length(); ++i)
	{
		const std::string where = paths[i].fullPathName().asChar();
		SkeletonJoint& joint = skeleton.joints[i];

		bool hasMarkup = false;
		MStatus status = JointMarkupScene::readMarkup(paths[i].node(), hasMarkup, joint.markup);
		if (!status || !hasMarkup)
		{
			report.addError(where, "could not read rigMarkup");
			continue;
		}

		MFnTransform fnJoint(paths[i], &status);
		if (!status)
		{
			report.addError(where, "is not a transform");
			continue;
		}
		joint.name = MNamespace::stripNamespaceFromName(fnJoint.name()).asChar();

		// MTransformationMatrix's orders start at kXYZ = 1, one past the rotateOrder attribute's enum.
		const MTransformationMatrix::RotationOrder order = fnJoint.rotationOrder(&status);
		if (!status || order < MTransformationMatrix::kXYZ || order > MTransformationMatrix::kZYX)
		{
			report.addError(where, "has no valid rotate order");
			continue;
		}
		joint.rotateOrder = static_cast<int>(order) - static_cast<int>(MTransformationMatrix::kXYZ);

		MDagPath ancestor(paths[i]);
		while (ancestor.pop() && ancestor.length() > 0)
		{
			const auto found = indexByPath.find(ancestor.fullPathName().asChar());
			if (found != indexByPath.end())
			{
				parentIndex[i] = static_cast<int>(found->second);
				break;
			}
		}
	}
	if (report.hasErrors())
	{
		return;
	}

	for (unsigned int i = 0; i < paths.length(); ++i)
	{
		SkeletonJoint& joint = skeleton.joints[i];
		MMatrix mRestLocal = paths[i].inclusiveMatrix();
		if (parentIndex[i] >= 0)
		{
			const SkeletonJoint& parent = skeleton.joints[static_cast<std::size_t>(parentIndex[i])];
			joint.hasParent = true;
			joint.parent = JointSelector{ parent.markup.side, parent.markup.usage };
			mRestLocal = mRestLocal * paths[static_cast<unsigned int>(parentIndex[i])].inclusiveMatrixInverse();
		}
		for (unsigned int row = 0; row < 4; ++row)
		{
			for (unsigned int column = 0; column < 4; ++column)
			{
				joint.restLocal[row * 4 + column] = mRestLocal(row, column);
			}
		}
	}

	sortSkeletonJoints(skeleton);
	outSkeleton = std::move(skeleton);
}
