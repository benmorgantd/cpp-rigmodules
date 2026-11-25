#include "maya/shared/MarkerScene.h"
#include "core/Constants.h"
#include "core/NamingConvention.h"
#include "core/RigTemplateValidation.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/Utilities.h"

#include <map>
#include <set>
#include <utility>

#include <maya/MAngle.h>
#include <maya/MDagPath.h>
#include <maya/MDistance.h>
#include <maya/MEulerRotation.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MNamespace.h>
#include <maya/MPlug.h>
#include <maya/MPxTransform.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MVector.h>

namespace
{
	// The editing locator is a rigControlNode in the Axis shape (X red, Y green, Z blue), so its orientation reads
	// at a glance. It is not part of any rig, so nothing treats it as a control.
	constexpr ShapeType kEditingShape{ ShapeType::Axis };
}

MStatus MarkerScene::queueAddMarkupAttribute(const MObject& oNode, MDGModifier& dgMod, MObject& oOutAttr)
{
	MStatus status;
	// One attribute object per node: a dynamic attribute MObject belongs to the node it is added to.
	MFnTypedAttribute fnTypedAttr;
	oOutAttr = fnTypedAttr.create(RigAttr::MarkerMarkup, RigAttr::MarkerMarkup, MFnData::kString, MObject::kNullObj, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	fnTypedAttr.setStorable(true);
	fnTypedAttr.setKeyable(false);
	return dgMod.addAttribute(oNode, oOutAttr);
}

MStatus MarkerScene::readMarkup(const MObject& oNode, bool& outHasMarkup, Marker& outMarker, std::string& outError)
{
	MStatus status;
	const MFnDependencyNode fnNode(oNode, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	outHasMarkup = fnNode.hasAttribute(RigAttr::MarkerMarkup);
	if (!outHasMarkup)
	{
		return MStatus::kSuccess;
	}

	const MPlug pMarkup = fnNode.findPlug(RigAttr::MarkerMarkup, false, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	const MString text = pMarkup.asString(&status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	if (!parseMarkerMarkup(text.asChar(), outMarker, outError))
	{
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

void MarkerScene::readMarkers(const MDagPathArray& nodes, MarkerSet& outSet, ValidationReport& report)
{
	MarkerSet set;
	std::map<std::string, MarkedJointSet> jointsByNamespace;
	std::set<std::pair<std::string, std::string>> seen;
	const std::size_t errorCount = report.errors.size();

	for (unsigned int i = 0; i < nodes.length(); ++i)
	{
		const std::string where = nodes[i].fullPathName().asChar();
		Marker marker;
		bool hasMarkup = false;
		std::string error;
		const MStatus status = readMarkup(nodes[i].node(), hasMarkup, marker, error);
		if (!hasMarkup)
		{
			report.addError(where, "has no rigMarkerMarkup. Mark it with rigMarkerMarkup, or do not pass it");
			continue;
		}
		if (!status)
		{
			report.addError(where, "invalid rigMarkerMarkup: " + error);
			continue;
		}

		marker.name = MNamespace::stripNamespaceFromName(MFnDependencyNode(nodes[i].node()).name()).asChar();
		NamingConvention::Name parsedName;
		if (!NamingConvention::parse(marker.name, parsedName) || parsedName.kind != NamingConvention::Kind::Marker)
		{
			report.addError(where, "name '" + marker.name + "' must be " + NamingConvention::Form + " ending in "
				+ NamingConvention::kindToken(NamingConvention::Kind::Marker) + ". Rename it with cmds.renameRigNodes");
			continue;
		}
		if (!seen.insert({ marker.markup.side, marker.markup.usage }).second)
		{
			report.addError(where, formatJointSelector(marker.markup) + " is marked on another node too");
			continue;
		}

		// The joints are the marked joints of the marker's own namespace, where loadRigMarkers puts it.
		const std::string absoluteNamespace = DagUtils::getAbsoluteNamespace(nodes[i].node()).asChar();
		auto found = jointsByNamespace.find(absoluteNamespace);
		if (found == jointsByNamespace.end())
		{
			found = jointsByNamespace.emplace(absoluteNamespace, MarkedJointSet()).first;
			JointMarkupScene::collectNamespaceJoints(absoluteNamespace.c_str(), found->second, report);
		}
		std::size_t entry = 0;
		if (!found->second.index.find(marker.joint, entry))
		{
			report.addError(where, "follows joint " + formatJointSelector(marker.joint) + ", which is not a marked joint in namespace " + absoluteNamespace);
			continue;
		}
		const MMatrix mOffset = nodes[i].inclusiveMatrix() * found->second.dagPaths[static_cast<unsigned int>(entry)].inclusiveMatrixInverse();
		marker.offsetLocal = MathUtils::toArray(mOffset);
		set.markers.push_back(marker);
	}

	if (report.errors.size() == errorCount)
	{
		sortMarkers(set);
		outSet = std::move(set);
	}
}

bool MarkerScene::placeMarker(const Marker& marker, const MarkedJointSet& joints, std::array<double, 16>& outWorld, std::string& outError)
{
	std::size_t entry = 0;
	if (!joints.index.find(marker.joint, entry))
	{
		outError = "marker " + formatJointSelector(marker.markup) + " follows joint " + formatJointSelector(marker.joint) + ", which is not among the joints";
		return false;
	}
	outWorld = MathUtils::toArray(MathUtils::toMMatrix(marker.offsetLocal) * joints.dagPaths[static_cast<unsigned int>(entry)].inclusiveMatrix());
	return true;
}

void MarkerScene::placeMarkers(const MarkerSet& set, const MarkedJointSet& joints, std::vector<ResolvedMarker>& outMarkers, ValidationReport& report)
{
	outMarkers.clear();
	for (const Marker& marker : set.markers)
	{
		ResolvedMarker resolved;
		std::string error;
		if (!placeMarker(marker, joints, resolved.world, error))
		{
			report.addWarning("markers", error + ", so it is left out");
			continue;
		}
		resolved.marker = marker;
		outMarkers.push_back(resolved);
	}
}

MStatus MarkerScene::queueSetWorld(const MObject& oNode, const MMatrix& mWorld, MDagModifier& dagMod)
{
	MStatus status;
	const MTransformationMatrix transform(mWorld);
	const MVector translation = transform.getTranslation(MSpace::kTransform, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	const MEulerRotation rotation = transform.eulerRotation();

	const MObject* translateAttrs[3]{ &MPxTransform::translateX, &MPxTransform::translateY, &MPxTransform::translateZ };
	const MObject* rotateAttrs[3]{ &MPxTransform::rotateX, &MPxTransform::rotateY, &MPxTransform::rotateZ };
	const double rotations[3]{ rotation.x, rotation.y, rotation.z };
	for (unsigned int axis = 0; axis < 3; ++axis)
	{
		status = dagMod.newPlugValueMDistance(MPlug(oNode, *translateAttrs[axis]), MDistance(translation[axis], MDistance::internalUnit()));
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = dagMod.newPlugValueMAngle(MPlug(oNode, *rotateAttrs[axis]), MAngle(rotations[axis], MAngle::kRadians));
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	return MStatus::kSuccess;
}

MStatus MarkerScene::createMarkerNodes(
	const MarkerSet& set,
	const MarkedJointSet& joints,
	double displaySize,
	MDagModifier& dagMod,
	MObjectArray& oOutNodes,
	ValidationReport& report)
{
	MStatus status;
	const std::size_t errorCount = report.errors.size();
	std::vector<std::array<double, 16>> worlds(set.markers.size());
	for (std::size_t i = 0; i < set.markers.size(); ++i)
	{
		std::string error;
		if (!placeMarker(set.markers[i], joints, worlds[i], error))
		{
			report.addError(set.markers[i].name, error);
		}
	}
	if (report.errors.size() != errorCount)
	{
		return MStatus::kFailure;
	}

	std::vector<MObject> oNodes(set.markers.size());
	for (std::size_t i = 0; i < set.markers.size(); ++i)
	{
		oNodes[i] = dagMod.createNode(RigControlNode::id, MObject::kNullObj, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = dagMod.renameNode(oNodes[i], set.markers[i].name.c_str());
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// renameNode makes a clashing name unique, and a renamed marker is another marker, so a clash undoes everything.
	for (std::size_t i = 0; i < set.markers.size(); ++i)
	{
		const MString name = MNamespace::stripNamespaceFromName(MFnDependencyNode(oNodes[i]).name());
		if (name != set.markers[i].name.c_str())
		{
			CHECK_MSTATUS(dagMod.undoIt());
			report.addError(set.markers[i].name, std::string("a node named '") + set.markers[i].name + "' already exists in the namespace '"
				+ MNamespace::currentNamespace().asChar() + "'. Load the markers into another namespace");
			return MStatus::kFailure;
		}
	}

	MMatrix mDisplaySize;
	mDisplaySize(0, 0) = displaySize;
	mDisplaySize(1, 1) = displaySize;
	mDisplaySize(2, 2) = displaySize;
	std::vector<MObject> oMarkupAttrs(set.markers.size());
	for (std::size_t i = 0; i < set.markers.size(); ++i)
	{
		status = queueSetWorld(oNodes[i], MathUtils::toMMatrix(worlds[i]), dagMod);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = dagMod.newPlugValueShort(MPlug(oNodes[i], RigControlNode::aShapeType), static_cast<short>(kEditingShape));
		CHECK_MSTATUS_AND_RETURN_IT(status);
		MPlug pShapeTransform(oNodes[i], RigControlNode::aShapeTransform);
		status = DagUtils::setMatrixPlug(pShapeTransform, mDisplaySize, dagMod);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = queueAddMarkupAttribute(oNodes[i], dagMod, oMarkupAttrs[i]);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	for (std::size_t i = 0; i < set.markers.size(); ++i)
	{
		status = dagMod.newPlugValueString(MPlug(oNodes[i], oMarkupAttrs[i]), serializeMarkerMarkup(set.markers[i]).c_str());
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	oOutNodes.clear();
	for (const MObject& oNode : oNodes)
	{
		oOutNodes.append(oNode);
	}
	return MStatus::kSuccess;
}

MMatrix MarkerScene::mirrorWorld(const MMatrix& mWorld)
{
	// S = diag(-1, 1, 1). S * R * S keeps a rotation a rotation, and the translation row is just reflected.
	MMatrix mReflect;
	mReflect(0, 0) = -1.0;
	return mReflect * mWorld * mReflect;
}
