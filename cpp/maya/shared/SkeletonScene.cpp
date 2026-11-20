#include "maya/shared/SkeletonScene.h"
#include "core/Constants.h"
#include "core/RigTemplateValidation.h"
#include "maya/shared/JointMarkupScene.h"
#include "maya/shared/Utilities.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <maya/MAngle.h>
#include <maya/MDagPath.h>
#include <maya/MDistance.h>
#include <maya/MEulerRotation.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnTransform.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MNamespace.h>
#include <maya/MPlug.h>
#include <maya/MPxTransform.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MVector.h>

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

namespace
{
	// Queues one joint's rest as channel values: restLocal = scale * shear * jointOrient * translate, with rotate and
	// rotateAxis at their zero defaults.
	MStatus queueJointRest(MObject& oJoint, const SkeletonJoint& joint, MDagModifier& dagMod)
	{
		MStatus status;
		MMatrix mRestLocal;
		for (unsigned int row = 0; row < 4; ++row)
		{
			for (unsigned int column = 0; column < 4; ++column)
			{
				mRestLocal(row, column) = joint.restLocal[row * 4 + column];
			}
		}
		const MTransformationMatrix restTransform(mRestLocal);
		const MVector translation = restTransform.getTranslation(MSpace::kTransform, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		// jointOrient is always in xyz order, whatever the joint's rotate order.
		const MEulerRotation orient = restTransform.rotation().asEulerRotation();
		double scale[3]{ 1.0, 1.0, 1.0 };
		status = restTransform.getScale(scale, MSpace::kTransform);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		double shear[3]{ 0.0, 0.0, 0.0 };
		status = restTransform.getShear(shear, MSpace::kTransform);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		const MObject* translateAttrs[3]{ &MPxTransform::translateX, &MPxTransform::translateY, &MPxTransform::translateZ };
		const MObject* scaleAttrs[3]{ &MPxTransform::scaleX, &MPxTransform::scaleY, &MPxTransform::scaleZ };
		const MObject* shearAttrs[3]{ &MPxTransform::shearXY, &MPxTransform::shearXZ, &MPxTransform::shearYZ };
		const char* orientNames[3]{ MayaAttr::JointOrientX, MayaAttr::JointOrientY, MayaAttr::JointOrientZ };
		const double orientValues[3]{ orient.x, orient.y, orient.z };
		MFnDependencyNode fnJoint(oJoint, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		for (unsigned int axis = 0; axis < 3; ++axis)
		{
			status = dagMod.newPlugValueMDistance(MPlug(oJoint, *translateAttrs[axis]), MDistance(translation[axis], MDistance::internalUnit()));
			CHECK_MSTATUS_AND_RETURN_IT(status);
			status = dagMod.newPlugValueDouble(MPlug(oJoint, *scaleAttrs[axis]), scale[axis]);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			status = dagMod.newPlugValueDouble(MPlug(oJoint, *shearAttrs[axis]), shear[axis]);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			MPlug pOrient = fnJoint.findPlug(orientNames[axis], false, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			status = dagMod.newPlugValueMAngle(pOrient, MAngle(orientValues[axis], MAngle::kRadians));
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}

		// The rotateOrder attribute's enum is the file's index (RotateOrders::All).
		status = dagMod.newPlugValueShort(MPlug(oJoint, MPxTransform::rotateOrder), static_cast<short>(joint.rotateOrder));
		CHECK_MSTATUS_AND_RETURN_IT(status);
		return DagUtils::setSegmentScaleCompensate(oJoint, false, dagMod);
	}
}

MStatus SkeletonScene::createSkeleton(const Skeleton& skeleton, MDagModifier& dagMod, MObjectArray& oOutJoints)
{
	MStatus status;
	const std::size_t count = skeleton.joints.size();
	std::vector<MObject> oJoints(count);
	for (const std::size_t index : skeletonParentsFirst(skeleton))
	{
		const SkeletonJoint& joint = skeleton.joints[index];
		MObject oParent;
		if (joint.hasParent)
		{
			oParent = oJoints[static_cast<std::size_t>(findSkeletonJoint(skeleton, joint.parent))];
		}
		oJoints[index] = dagMod.createNode("joint", oParent, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = dagMod.renameNode(oJoints[index], joint.name.c_str());
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	for (std::size_t i = 0; i < count; ++i)
	{
		const MString name = MNamespace::stripNamespaceFromName(MFnDependencyNode(oJoints[i]).name());
		if (name != skeleton.joints[i].name.c_str())
		{
			dagMod.undoIt();
			MGlobal::displayError(MString("A node named '") + skeleton.joints[i].name.c_str() + "' already exists in the namespace '"
				+ MNamespace::currentNamespace() + "', so the joint can't have its name. Load the skeleton into another namespace.");
			return MStatus::kFailure;
		}
	}

	MObjectArray oMarkupAttrs;
	for (std::size_t i = 0; i < count; ++i)
	{
		status = queueJointRest(oJoints[i], skeleton.joints[i], dagMod);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		MObject oMarkupAttr;
		status = JointMarkupScene::queueAddMarkupAttribute(oJoints[i], dagMod, oMarkupAttr);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		oMarkupAttrs.append(oMarkupAttr);
	}
	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	for (std::size_t i = 0; i < count; ++i)
	{
		MPlug pMarkup(oJoints[i], oMarkupAttrs[static_cast<unsigned int>(i)]);
		status = dagMod.newPlugValueString(pMarkup, serializeJointMarkup(skeleton.joints[i].markup).c_str());
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	oOutJoints.clear();
	for (const MObject& oJoint : oJoints)
	{
		oOutJoints.append(oJoint);
	}
	return MStatus::kSuccess;
}
