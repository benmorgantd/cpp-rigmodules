#include "maya/commands/MarkerCmds.h"
#include "core/Constants.h"
#include "core/Hash64.h"
#include "core/MarkerFile.h"
#include "core/NamingConvention.h"
#include "core/RigTemplateValidation.h"
#include "core/StringUtils.h"
#include "maya/nodes/RigRoot.h"
#include "maya/shared/JointMarkupScene.h"
#include "maya/shared/MarkerScene.h"
#include "maya/shared/RigTraversal.h"

#include <array>
#include <map>
#include <string>
#include <vector>

#include <maya/MArgDatabase.h>
#include <maya/MDagPath.h>
#include <maya/MFn.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MNamespace.h>
#include <maya/MObjectArray.h>
#include <maya/MPlug.h>
#include <maya/MStringArray.h>

namespace
{
	// Code reads flags by long name only. Short names appear only in newSyntax().
	constexpr const char* kUsageFlag{ "-usage" };
	constexpr const char* kSideFlag{ "-side" };
	constexpr const char* kJointFlag{ "-joint" };
	constexpr const char* kExportFlag{ "-export" };
	constexpr const char* kRemoveFlag{ "-remove" };
	constexpr const char* kFileFlag{ "-file" };
	constexpr const char* kNamespaceFlag{ "-namespace" };
	constexpr const char* kDisplaySizeFlag{ "-displaySize" };

	constexpr std::array kValueFlags{ kUsageFlag, kSideFlag, kJointFlag, kExportFlag };

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

	std::string queryValue(const MArgDatabase& argData, const Marker& marker)
	{
		if (argData.isFlagSet(kUsageFlag))
		{
			return marker.markup.usage;
		}
		if (argData.isFlagSet(kSideFlag))
		{
			return marker.markup.side;
		}
		if (argData.isFlagSet(kJointFlag))
		{
			return formatJointSelector(marker.joint);
		}
		if (argData.isFlagSet(kExportFlag))
		{
			return marker.isExported ? "true" : "false";
		}
		return serializeMarkerMarkup(marker);
	}
}

// rigMarkerMarkup --------------------------------------------------------------------------------
const char* MarkerMarkupCmd::aCommandString = "rigMarkerMarkup";

void* MarkerMarkupCmd::creator()
{
	return new MarkerMarkupCmd();
}

bool MarkerMarkupCmd::isUndoable() const
{
	return fIsUndoable;
}

MSyntax MarkerMarkupCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-us", kUsageFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-sd", kSideFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-jt", kJointFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-ex", kExportFlag, MSyntax::kBoolean));
	CHECK_MSTATUS(syntax.addFlag("-rm", kRemoveFlag));
	syntax.enableQuery(true);
	syntax.enableEdit(true);
	syntax.setObjectType(MSyntax::kStringObjects, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus MarkerMarkupCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError(MString("Error parsing ") + aCommandString + " arguments.");
		return status;
	}

	MDagPathArray nodes;
	status = CommandUtils::readTransforms(argData, aCommandString, nodes);
	if (!status)
	{
		return status;
	}

	// A bad value flag is found before the first write, so nothing is half changed.
	Marker flagged;
	if (argData.isFlagSet(kUsageFlag))
	{
		MString usage;
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kUsageFlag, 0, usage));
		flagged.markup.usage = usage.asChar();
	}
	if (argData.isFlagSet(kSideFlag))
	{
		MString side;
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kSideFlag, 0, side));
		flagged.markup.side = side.asChar();
	}
	if (argData.isFlagSet(kExportFlag))
	{
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kExportFlag, 0, flagged.isExported));
	}
	JointSelector flaggedJoint;
	// A query passes True where a value would go, so the joint is only looked up for a write.
	if (argData.isFlagSet(kJointFlag) && !argData.isQuery())
	{
		MString jointName;
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kJointFlag, 0, jointName));
		MObject oJoint;
		status = DagUtils::getMObjectFromName(jointName, oJoint);
		if (!status)
		{
			MGlobal::displayError(MString(aCommandString) + ": joint not found: " + jointName);
			return status;
		}
		status = JointMarkupScene::selectorOfJoint(oJoint, flaggedJoint);
		if (!status)
		{
			return status;
		}
	}

	if (argData.isQuery())
	{
		fIsUndoable = false;
		if (countValueFlags(argData) > 1 || argData.isFlagSet(kRemoveFlag))
		{
			MGlobal::displayError("rigMarkerMarkup -query takes at most one of -usage, -side, -joint, -export.");
			return MStatus::kFailure;
		}
		MStringArray results;
		for (unsigned int i = 0; i < nodes.length(); ++i)
		{
			bool hasMarkup = false;
			Marker marker;
			std::string error;
			status = MarkerScene::readMarkup(nodes[i].node(), hasMarkup, marker, error);
			if (!status)
			{
				MGlobal::displayError(nodes[i].partialPathName() + " has invalid marker markup: " + error.c_str());
				return status;
			}
			results.append(hasMarkup ? MString(queryValue(argData, marker).c_str()) : MString());
		}
		setResult(results);
		return MStatus::kSuccess;
	}

	if (argData.isFlagSet(kRemoveFlag))
	{
		if (argData.isEdit() || countValueFlags(argData) > 0)
		{
			MGlobal::displayError("rigMarkerMarkup -remove cannot be combined with -edit or value flags.");
			return MStatus::kFailure;
		}
		for (unsigned int i = 0; i < nodes.length(); ++i)
		{
			MFnDependencyNode fnNode(nodes[i].node());
			if (!fnNode.hasAttribute(RigAttr::MarkerMarkup))
			{
				continue;   // removing markup that is not there is a no-op
			}
			MObject oAttr = fnNode.attribute(RigAttr::MarkerMarkup, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			CHECK_MSTATUS_AND_RETURN_IT(fDGMod.removeAttribute(nodes[i].node(), oAttr));
		}
		return redoIt();
	}

	const bool isEdit = argData.isEdit();
	if (isEdit && countValueFlags(argData) == 0)
	{
		MGlobal::displayError("rigMarkerMarkup -edit needs at least one of -usage, -side, -joint, -export.");
		return MStatus::kFailure;
	}
	if (!isEdit && (!argData.isFlagSet(kUsageFlag) || !argData.isFlagSet(kSideFlag)))
	{
		MGlobal::displayError("rigMarkerMarkup create needs -usage and -side. Use -edit to change existing markup.");
		return MStatus::kFailure;
	}

	// Build every new markup first, so one bad node leaves the scene untouched.
	std::vector<Marker> markers(nodes.length());
	for (unsigned int i = 0; i < nodes.length(); ++i)
	{
		bool hasMarkup = false;
		std::string error;
		status = MarkerScene::readMarkup(nodes[i].node(), hasMarkup, markers[i], error);
		if (!status)
		{
			MGlobal::displayError(nodes[i].partialPathName() + " has invalid marker markup: " + error.c_str());
			return status;
		}
		if (isEdit && !hasMarkup)
		{
			MGlobal::displayError(nodes[i].partialPathName() + " has no marker markup to edit. Create it first.");
			return MStatus::kFailure;
		}
		if (!isEdit && hasMarkup)
		{
			MGlobal::displayError(nodes[i].partialPathName() + " already has marker markup. Use -edit to change it.");
			return MStatus::kFailure;
		}

		if (argData.isFlagSet(kUsageFlag))
		{
			markers[i].markup.usage = flagged.markup.usage;
		}
		if (argData.isFlagSet(kSideFlag))
		{
			markers[i].markup.side = flagged.markup.side;
		}
		if (argData.isFlagSet(kExportFlag))
		{
			markers[i].isExported = flagged.isExported;
		}
		if (argData.isFlagSet(kJointFlag))
		{
			markers[i].joint = flaggedJoint;
		}
		else if (!isEdit && !JointMarkupScene::findMarkedAncestor(nodes[i], markers[i].joint))
		{
			MGlobal::displayError(nodes[i].partialPathName() + " has no marked joint above it. Pass -joint to say which joint it follows.");
			return MStatus::kFailure;
		}

		// serializeMarkerMarkup writes whatever it is given, so the check is the parser's.
		Marker checked;
		if (!parseMarkerMarkup(serializeMarkerMarkup(markers[i]), checked, error))
		{
			MGlobal::displayError(MString("Invalid marker markup for ") + nodes[i].partialPathName() + ": " + error.c_str());
			return MStatus::kFailure;
		}
	}

	if (!isEdit)
	{
		std::vector<MObject> oAttrs(nodes.length());
		for (unsigned int i = 0; i < nodes.length(); ++i)
		{
			status = MarkerScene::queueAddMarkupAttribute(nodes[i].node(), fDGMod, oAttrs[i]);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		// The attributes must exist before their plugs can take values. undoIt reverts both batches.
		status = fDGMod.doIt();
		CHECK_MSTATUS_AND_RETURN_IT(status);
		for (unsigned int i = 0; i < nodes.length(); ++i)
		{
			CHECK_MSTATUS_AND_RETURN_IT(fDGMod.newPlugValueString(MPlug(nodes[i].node(), oAttrs[i]), serializeMarkerMarkup(markers[i]).c_str()));
		}
	}
	else
	{
		for (unsigned int i = 0; i < nodes.length(); ++i)
		{
			const MPlug pMarkup = MFnDependencyNode(nodes[i].node()).findPlug(RigAttr::MarkerMarkup, false, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			CHECK_MSTATUS_AND_RETURN_IT(fDGMod.newPlugValueString(pMarkup, serializeMarkerMarkup(markers[i]).c_str()));
		}
	}
	return redoIt();
}

MStatus MarkerMarkupCmd::redoIt()
{
	const MStatus status = fDGMod.doIt();
	if (!status)
	{
		MGlobal::displayError(MString(aCommandString) + " failed to apply: " + status.errorString());
	}
	return status;
}

MStatus MarkerMarkupCmd::undoIt()
{
	return fDGMod.undoIt();
}

// saveRigMarkers ---------------------------------------------------------------------------------
const char* SaveRigMarkersCmd::aCommandString = "saveRigMarkers";

void* SaveRigMarkersCmd::creator()
{
	return new SaveRigMarkersCmd();
}

bool SaveRigMarkersCmd::isUndoable() const
{
	return false;
}

MSyntax SaveRigMarkersCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-f", kFileFlag, MSyntax::kString));
	syntax.setObjectType(MSyntax::kStringObjects, 0);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus SaveRigMarkersCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError(MString("Error parsing ") + aCommandString + " arguments.");
		return status;
	}

	MDagPathArray nodes;
	status = CommandUtils::readTransforms(argData, aCommandString, nodes);
	if (!status)
	{
		return status;
	}
	MString path;
	status = CommandUtils::readFilePath(argData, kFileFlag, aCommandString, path);
	if (!status)
	{
		return status;
	}

	MarkerSet set;
	ValidationReport report;
	MarkerScene::readMarkers(nodes, set, report);
	if (report.hasErrors())
	{
		ReportUtils::displayReport(report);
		MGlobal::displayError(MString(aCommandString) + ": nothing was written.");
		return MStatus::kFailure;
	}

	status = CommandUtils::writeTextFile(path, "markers", serializeMarkers(set));
	if (!status)
	{
		return status;
	}
	setResult(MString(Hash64::format(markersHash(set)).c_str()));
	return MStatus::kSuccess;
}

// loadRigMarkers ---------------------------------------------------------------------------------
const char* LoadRigMarkersCmd::aCommandString = "loadRigMarkers";

void* LoadRigMarkersCmd::creator()
{
	return new LoadRigMarkersCmd();
}

bool LoadRigMarkersCmd::isUndoable() const
{
	return true;
}

MSyntax LoadRigMarkersCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-f", kFileFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-ns", kNamespaceFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-ds", kDisplaySizeFlag, MSyntax::kDouble));
	return syntax;
}

MStatus LoadRigMarkersCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError(MString("Error parsing ") + aCommandString + " arguments.");
		return status;
	}

	MString path;
	status = CommandUtils::readFilePath(argData, kFileFlag, aCommandString, path);
	if (!status)
	{
		return status;
	}

	// Colons at the ends are dropped, so ":rig", "rig:" and "rig" mean the same namespace, and ":" is the root.
	MString namespaceName = MNamespace::currentNamespace();
	if (argData.isFlagSet(kNamespaceFlag))
	{
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kNamespaceFlag, 0, namespaceName));
	}
	fNamespace = StringUtils::trim(namespaceName.asChar(), ':').c_str();

	double displaySize = MarkerScene::DefaultDisplaySize;
	if (argData.isFlagSet(kDisplaySizeFlag))
	{
		CHECK_MSTATUS_AND_RETURN_IT(argData.getFlagArgument(kDisplaySizeFlag, 0, displaySize));
		if (!(displaySize > 0.0))
		{
			MGlobal::displayError(MString(aCommandString) + ": displaySize must be more than 0.");
			return MStatus::kInvalidParameter;
		}
	}

	std::string text;
	status = CommandUtils::readTextFile(path, "markers", text);
	if (!status)
	{
		return status;
	}

	// Validate the whole file before creating anything, so a bad file changes nothing.
	MarkerSet set;
	ValidationReport report;
	deserializeMarkers(text, set, report);
	if (report.hasErrors())
	{
		ReportUtils::displayReport(report);
		MGlobal::displayError(MString("The markers file failed validation, so nothing was loaded: ") + path);
		return MStatus::kFailure;
	}

	MarkedJointSet joints;
	JointMarkupScene::collectNamespaceJoints(MString(":") + fNamespace, joints, report);
	MObjectArray oNodes;
	fCreatedNamespaces.captureBefore();
	if (!report.hasErrors())
	{
		ScopedRigNamespace scopedNamespace(fNamespace);
		status = scopedNamespace.status();
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = MarkerScene::createMarkerNodes(set, joints, displaySize, fDagMod, oNodes, report);
	}
	else
	{
		status = MStatus::kFailure;
	}
	ReportUtils::displayReport(report);
	if (!status)
	{
		MGlobal::displayError(MString("The markers could not be placed, so nothing was loaded: ") + path);
		return MStatus::kFailure;
	}
	fCreatedNamespaces.recordCreated();

	MStringArray names;
	for (unsigned int i = 0; i < oNodes.length(); ++i)
	{
		names.append(DagUtils::getPartialPathName(oNodes[i]));
	}
	setResult(names);
	return MStatus::kSuccess;
}

MStatus LoadRigMarkersCmd::redoIt()
{
	// Nodes are created relative to the current namespace, so the redo needs it current again.
	ScopedRigNamespace scopedNamespace(fNamespace);
	MStatus status = scopedNamespace.status();
	CHECK_MSTATUS_AND_RETURN_IT(status);
	return fDagMod.doIt();
}

MStatus LoadRigMarkersCmd::undoIt()
{
	const MStatus status = fDagMod.undoIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);
	fCreatedNamespaces.removeCreated();
	return status;
}

// mirrorRigMarkers -------------------------------------------------------------------------------
const char* MirrorRigMarkersCmd::aCommandString = "mirrorRigMarkers";

void* MirrorRigMarkersCmd::creator()
{
	return new MirrorRigMarkersCmd();
}

bool MirrorRigMarkersCmd::isUndoable() const
{
	return true;
}

MSyntax MirrorRigMarkersCmd::newSyntax()
{
	MSyntax syntax;
	syntax.setObjectType(MSyntax::kStringObjects, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus MirrorRigMarkersCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError(MString("Error parsing ") + aCommandString + " arguments.");
		return status;
	}
	MDagPathArray nodes;
	status = CommandUtils::readTransforms(argData, aCommandString, nodes);
	if (!status)
	{
		return status;
	}

	MarkerSet sources;
	ValidationReport report;
	MarkerScene::readMarkers(nodes, sources, report);
	if (report.hasErrors())
	{
		ReportUtils::displayReport(report);
		MGlobal::displayError(MString(aCommandString) + ": nothing was changed.");
		return MStatus::kFailure;
	}

	// A redo makes the new nodes again in the current namespace, so they all go into one.
	for (unsigned int i = 1; i < nodes.length(); ++i)
	{
		if (DagUtils::getAbsoluteNamespace(nodes[i].node()) != DagUtils::getAbsoluteNamespace(nodes[0].node()))
		{
			MGlobal::displayError(MString(aCommandString) + ": the nodes are in more than one namespace. Mirror each namespace on its own.");
			return MStatus::kFailure;
		}
	}
	fNamespace = StringUtils::trim(DagUtils::getAbsoluteNamespace(nodes[0].node()).asChar(), ':').c_str();

	// Each source gets the other side's marker. Plan all of them before the first write.
	struct Mirrored
	{
		Marker marker;
		MMatrix mWorld;
		MObject oExisting;   // the node already there, null when one has to be made
		std::size_t sourceNode;
	};
	std::vector<Mirrored> mirrored;
	std::map<std::string, MarkedJointSet> jointsByNamespace;
	for (unsigned int i = 0; i < nodes.length(); ++i)
	{
		const std::string where = nodes[i].fullPathName().asChar();
		const MFnDependencyNode fnNode(nodes[i].node());
		const std::string sourceName = MNamespace::stripNamespaceFromName(fnNode.name()).asChar();
		// readMarkers passed, so every node has valid markup.
		Marker source;
		bool hasMarkup = false;
		std::string error;
		CHECK_MSTATUS_AND_RETURN_IT(MarkerScene::readMarkup(nodes[i].node(), hasMarkup, source, error));

		if (source.markup.side == SideNames::Center)
		{
			report.addError(where, "is a Center marker, so it has no other side to mirror to");
			continue;
		}
		const auto otherSide = [](const std::string& side)
		{
			return side == SideNames::Left ? std::string(SideNames::Right) : side == SideNames::Right ? std::string(SideNames::Left) : side;
		};

		Mirrored result;
		result.marker.markup = JointSelector{ otherSide(source.markup.side), source.markup.usage };
		result.marker.joint = JointSelector{ otherSide(source.joint.side), source.joint.usage };
		result.marker.isExported = source.isExported;
		result.marker.name = NamingConvention::swapSide(sourceName);
		if (result.marker.name.empty())
		{
			report.addError(where, "name '" + sourceName + "' must be " + NamingConvention::Form + " with a Left or Right side to mirror");
			continue;
		}
		result.mWorld = MarkerScene::mirrorWorld(nodes[i].inclusiveMatrix());
		result.sourceNode = i;

		const std::string absoluteNamespace = DagUtils::getAbsoluteNamespace(nodes[i].node()).asChar();
		auto found = jointsByNamespace.find(absoluteNamespace);
		if (found == jointsByNamespace.end())
		{
			found = jointsByNamespace.emplace(absoluteNamespace, MarkedJointSet()).first;
			JointMarkupScene::collectNamespaceJoints(absoluteNamespace.c_str(), found->second, report);
		}
		std::size_t entry = 0;
		if (!found->second.index.find(result.marker.joint, entry))
		{
			report.addError(where, "the other side's joint " + formatJointSelector(result.marker.joint) + " is not a marked joint in namespace " + absoluteNamespace);
			continue;
		}
		const MMatrix mOffset = result.mWorld * found->second.dagPaths[static_cast<unsigned int>(entry)].inclusiveMatrixInverse();
		for (unsigned int row = 0; row < 4; ++row)
		{
			for (unsigned int column = 0; column < 4; ++column)
			{
				result.marker.offsetLocal[row * 4 + column] = mOffset(row, column);
			}
		}

		const MString otherName = MString(absoluteNamespace.c_str()) + (absoluteNamespace == ":" ? "" : ":") + result.marker.name.c_str();
		// A miss is the ordinary case: no counterpart yet, so one is made, and oExisting stays null.
		if (!DagUtils::getMObjectFromName(otherName, result.oExisting))
		{
			result.oExisting = MObject::kNullObj;
		}
		mirrored.push_back(result);
	}
	if (report.hasErrors())
	{
		ReportUtils::displayReport(report);
		MGlobal::displayError(MString(aCommandString) + ": nothing was changed.");
		return MStatus::kFailure;
	}

	MStringArray names;
	for (const Mirrored& item : mirrored)
	{
		const std::string absoluteNamespace = DagUtils::getAbsoluteNamespace(nodes[static_cast<unsigned int>(item.sourceNode)].node()).asChar();
		MObject oNode = item.oExisting;
		if (oNode.isNull())
		{
			// New nodes are made in the source's namespace, drawn at the source's size.
			MarkerSet one;
			one.markers.push_back(item.marker);
			MObjectArray oCreated;
			ScopedRigNamespace scopedNamespace(StringUtils::trim(absoluteNamespace, ':').c_str());
			status = scopedNamespace.status();
			CHECK_MSTATUS_AND_RETURN_IT(status);
			status = MarkerScene::createMarkerNodes(one, jointsByNamespace[absoluteNamespace], MarkerScene::DefaultDisplaySize, fDagMod, oCreated, report);
			if (!status)
			{
				ReportUtils::displayReport(report);
				CHECK_MSTATUS(fDagMod.undoIt());
				return MStatus::kFailure;
			}
			oNode = oCreated[0];
		}
		else
		{
			status = MarkerScene::queueSetWorld(oNode, item.mWorld, fDagMod);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			MFnDependencyNode fnExisting(oNode);
			if (!fnExisting.hasAttribute(RigAttr::MarkerMarkup))
			{
				MGlobal::displayError(fnExisting.name() + " is in the way of the other side's marker and has no marker markup. Rename or delete it.");
				CHECK_MSTATUS(fDagMod.undoIt());
				return MStatus::kFailure;
			}
			const MPlug pMarkup = fnExisting.findPlug(RigAttr::MarkerMarkup, false, &status);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			status = fDagMod.newPlugValueString(pMarkup, serializeMarkerMarkup(item.marker).c_str());
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		names.append(DagUtils::getPartialPathName(oNode));
	}
	status = fDagMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);
	setResult(names);
	return MStatus::kSuccess;
}

MStatus MirrorRigMarkersCmd::redoIt()
{
	ScopedRigNamespace scopedNamespace(fNamespace);
	MStatus status = scopedNamespace.status();
	CHECK_MSTATUS_AND_RETURN_IT(status);
	return fDagMod.doIt();
}

MStatus MirrorRigMarkersCmd::undoIt()
{
	return fDagMod.undoIt();
}

// getRigMarkers ----------------------------------------------------------------------------------
const char* GetRigMarkersCmd::aCommandString = "getRigMarkers";

void* GetRigMarkersCmd::creator()
{
	return new GetRigMarkersCmd();
}

bool GetRigMarkersCmd::isUndoable() const
{
	return false;
}

MSyntax GetRigMarkersCmd::newSyntax()
{
	MSyntax syntax;
	syntax.setObjectType(MSyntax::kStringObjects, 1, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus GetRigMarkersCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError(MString("Error parsing ") + aCommandString + " arguments.");
		return status;
	}

	MString nodeName;
	status = CommandUtils::readSingleObject(argData, aCommandString, "rig node", nodeName);
	if (!status)
	{
		return status;
	}
	MObject oNode;
	status = DagUtils::getMObjectFromName(nodeName, oNode);
	if (!status || !RigTraversal::isRigNode(oNode))
	{
		MGlobal::displayError(MString(aCommandString) + ": " + nodeName + " is not a node of a rig.");
		return MStatus::kFailure;
	}
	MObject oRigRoot;
	status = RigTraversal::getOwningRigRoot(oNode, oRigRoot);
	if (!status)
	{
		return status;
	}

	const MString text = MPlug(oRigRoot, RigRootNode::aMarkers).asString();
	setResult(text.length() == 0 ? MString("[]") : text);
	return MStatus::kSuccess;
}
