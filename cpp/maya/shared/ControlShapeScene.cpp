#include "maya/shared/ControlShapeScene.h"
#include "core/ControlShapes.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/RigTraversal.h"
#include "maya/shared/Utilities.h"

#include <set>
#include <string>
#include <utility>

#include <maya/MFnMatrixData.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MObjectArray.h>
#include <maya/MPlug.h>

namespace
{
	const ControlShape* findShape(const ControlShapeMap& shapes, const ControlMarkup& markup)
	{
		auto sideIt = shapes.find(markup.side);
		if (sideIt == shapes.end())
		{
			return nullptr;
		}
		auto usageIt = sideIt->second.find(markup.usage);
		return usageIt != sideIt->second.end() ? &usageIt->second : nullptr;
	}
}

ControlShape ControlShapeScene::getControlShape(const MObject& oControl)
{
	ControlShape shape;
	shape.shapeType = getShapeTypeString(RigControlNode::getShapeType(oControl));

	const MMatrix mShapeTransform = RigControlNode::getShapeTransform(oControl);
	for (unsigned int row = 0; row < 4; ++row)
	{
		for (unsigned int column = 0; column < 4; ++column)
		{
			shape.shapeTransform[row * 4 + column] = mShapeTransform[row][column];
		}
	}

	shape.filled = MPlug(oControl, RigControlNode::aFilled).asBool();
	return shape;
}

MStatus ControlShapeScene::setControlShape(const MObject& oControl, const ControlShape& shape, MDGModifier& dgMod)
{
	MStatus status;
	ShapeType shapeType = ShapeType::Box;
	if (!findShapeType(shape.shapeType, shapeType))
	{
		MGlobal::displayError(MString("Unknown shape type '") + shape.shapeType.c_str() + "' for " + DagUtils::getPartialPathName(oControl));
		return MStatus::kFailure;
	}

	MMatrix mShapeTransform;
	for (unsigned int row = 0; row < 4; ++row)
	{
		for (unsigned int column = 0; column < 4; ++column)
		{
			mShapeTransform[row][column] = shape.shapeTransform[row * 4 + column];
		}
	}

	MFnMatrixData fnMatrixData;
	MObject oShapeTransform = fnMatrixData.create(mShapeTransform, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MPlug pShapeType(oControl, RigControlNode::aShapeType);
	MPlug pShapeTransform(oControl, RigControlNode::aShapeTransform);
	MPlug pFilled(oControl, RigControlNode::aFilled);
	CHECK_MSTATUS_AND_RETURN_IT(dgMod.newPlugValueShort(pShapeType, static_cast<short>(shapeType)));
	CHECK_MSTATUS_AND_RETURN_IT(dgMod.newPlugValue(pShapeTransform, oShapeTransform));
	CHECK_MSTATUS_AND_RETURN_IT(dgMod.newPlugValueBool(pFilled, shape.filled));
	return MStatus::kSuccess;
}

MStatus ControlShapeScene::getRigControlShapes(const MObject& oRigRoot, ControlShapeMap& outShapes, MStringArray& outControlNames)
{
	MObjectArray oControls;
	MStatus status = RigTraversal::getRigControls(oRigRoot, RigControlFilter{}, oControls);
	if (!status)
	{
		return status;
	}

	for (unsigned int i = 0; i < oControls.length(); ++i)
	{
		ControlMarkup markup;
		status = RigTraversal::getControlMarkup(oControls[i], markup);
		if (!status)
		{
			return status;
		}
		outShapes[markup.side][markup.usage] = getControlShape(oControls[i]);
		outControlNames.append(DagUtils::getPartialPathName(oControls[i]));
	}
	return MStatus::kSuccess;
}

MStatus ControlShapeScene::setRigControlShapes(const MObject& oRigRoot, const ControlShapeMap& shapes, MDGModifier& dgMod, MStringArray& outAppliedNames)
{
	MObjectArray oControls;
	MStatus status = RigTraversal::getRigControls(oRigRoot, RigControlFilter{}, oControls);
	if (!status)
	{
		return status;
	}

	std::set<std::pair<std::string, std::string>> usedEntries;
	for (unsigned int i = 0; i < oControls.length(); ++i)
	{
		ControlMarkup markup;
		status = RigTraversal::getControlMarkup(oControls[i], markup);
		if (!status)
		{
			return status;
		}

		const ControlShape* pShape = findShape(shapes, markup);
		if (pShape == nullptr)
		{
			MGlobal::displayWarning(DagUtils::getPartialPathName(oControls[i]) + " (" + markup.side.c_str() + ", " + markup.usage.c_str()
				+ ") has no entry in the shape file, so it keeps its shape.");
			continue;
		}

		status = setControlShape(oControls[i], *pShape, dgMod);
		if (!status)
		{
			return status;
		}
		usedEntries.insert({ markup.side, markup.usage });
		outAppliedNames.append(DagUtils::getPartialPathName(oControls[i]));
	}

	for (const auto& [side, usages] : shapes)
	{
		for (const auto& [usage, shape] : usages)
		{
			if (usedEntries.count({ side, usage }) == 0)
			{
				MGlobal::displayWarning(MString("The shape file has an entry for (") + side.c_str() + ", " + usage.c_str()
					+ "), but the rig has no control with that markup.");
			}
		}
	}
	return MStatus::kSuccess;
}

MStatus ControlShapeScene::mirrorRigControlShapes(const MObject& oRigRoot, const std::string& sourceSide, MDGModifier& dgMod, MStringArray& outChangedNames)
{
	MObjectArray oSources;
	MObjectArray oMirrors;
	MStatus status = RigTraversal::getMirrorControlPairs(oRigRoot, sourceSide, oSources, oMirrors);
	if (!status)
	{
		return status;
	}

	// Reflection across world X = 0.
	MMatrix mReflect;
	mReflect[0][0] = -1.0;

	for (unsigned int i = 0; i < oSources.length(); ++i)
	{
		const MObject& oMirror = oMirrors[i];

		MMatrix mSourceRest;
		MMatrix mMirrorRest;
		status = RigTraversal::getControlRestWorld(oSources[i], mSourceRest);
		if (status)
		{
			status = RigTraversal::getControlRestWorld(oMirror, mMirrorRest);
		}
		if (!status)
		{
			return status;
		}

		// A shape point p sits at p * S * W in world. Solve S_mirror so the mirror's point lands on the source's, reflected:
		//   p * S_mirror * W_mirror == p * S_source * W_source * R
		const MMatrix mMirrorShape = RigControlNode::getShapeTransform(oSources[i]) * mSourceRest * mReflect * mMirrorRest.inverse();

		ControlShape shape = getControlShape(oSources[i]);
		for (unsigned int row = 0; row < 4; ++row)
		{
			for (unsigned int column = 0; column < 4; ++column)
			{
				shape.shapeTransform[row * 4 + column] = mMirrorShape[row][column];
			}
		}

		status = setControlShape(oMirror, shape, dgMod);
		if (!status)
		{
			return status;
		}
		outChangedNames.append(DagUtils::getPartialPathName(oMirror));
	}
	return MStatus::kSuccess;
}
