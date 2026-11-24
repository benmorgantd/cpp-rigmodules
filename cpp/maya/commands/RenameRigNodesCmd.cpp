#include "maya/commands/RenameRigNodesCmd.h"
#include "core/Constants.h"
#include "core/NamingConvention.h"
#include "maya/shared/NamingScene.h"
#include "maya/shared/Utilities.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MNamespace.h>
#include <maya/MObjectArray.h>
#include <maya/MSelectionList.h>
#include <maya/MStringArray.h>

namespace
{
	// Code reads flags by long name only. MSyntax::addFlag requires a short name of at most 3 letters
	// (a long name there is rejected and the flag silently never registers), so short names appear only in newSyntax().
	constexpr const char* kBaseFlag{ "-base" };
	constexpr const char* kSideFlag{ "-side" };
	constexpr const char* kStartIndexFlag{ "-startIndex" };

	constexpr const char* kTempNamePrefix{ "rigRenameTemp_" };

	// The nodes in the order the names give them, else the selection in selection order.
	MStatus collectNodes(const MStringArray& names, MObjectArray& outNodes)
	{
		if (names.length() > 0)
		{
			for (unsigned int i = 0; i < names.length(); ++i)
			{
				MObject oNode;
				if (!DagUtils::getMObjectFromName(names[i], oNode))
				{
					MGlobal::displayError(MString("No node named ") + names[i] + ".");
					return MStatus::kFailure;
				}
				outNodes.append(oNode);
			}
			return MStatus::kSuccess;
		}

		MSelectionList selection;
		MStatus status = MGlobal::getActiveSelectionList(selection, true);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		if (selection.length() == 0)
		{
			MGlobal::displayError("renameRigNodes needs nodes: pass them or select them.");
			return MStatus::kFailure;
		}
		if (selection.length() > 1 && !MGlobal::trackSelectionOrderEnabled())
		{
			MGlobal::displayError("Maya is not tracking selection order, so the order of the selection is not known. "
				"Turn it on with cmds.selectPref(trackSelectionOrder=True) and select the nodes again, or pass the nodes in order.");
			return MStatus::kFailure;
		}
		for (unsigned int i = 0; i < selection.length(); ++i)
		{
			MObject oNode;
			status = selection.getDependNode(i, oNode);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			outNodes.append(oNode);
		}
		return MStatus::kSuccess;
	}

	MStatus queryNames(const MStringArray& names, MStringArray& outResults)
	{
		if (names.length() == 0)
		{
			MGlobal::displayError("renameRigNodes -query needs names.");
			return MStatus::kFailure;
		}

		for (unsigned int i = 0; i < names.length(); ++i)
		{
			NamingConvention::Name name;
			if (!NamingConvention::parse(MNamespace::stripNamespaceFromName(names[i]).asChar(), name))
			{
				outResults.append(MString());
				continue;
			}

			static const char* kKindNames[] = { "Joint", "Control", "Module", "Constraint", "Mesh", "Marker" };
			nlohmann::json parts;
			parts["base"] = name.base;
			parts["index"] = name.index;
			parts["side"] = name.side;
			parts["kind"] = kKindNames[static_cast<int>(name.kind)];
			outResults.append(parts.dump().c_str());
		}
		return MStatus::kSuccess;
	}

	struct Rename
	{
		MObject oNode;
		MString target;   // absolute: ":rig:upperArm_00_L_JNT"
	};

	// Works out every target before anything is queued. Reports every problem, not only the first.
	bool planRenames(
		const MObjectArray& oNodes,
		const std::string& flagBase,
		const std::string& flagSide,
		int startIndex,
		std::vector<Rename>& outRenames)
	{
		bool isValid = true;
		std::unordered_map<std::string, std::string> takenBy;   // target -> the node that claimed it

		for (unsigned int i = 0; i < oNodes.length(); ++i)
		{
			const MFnDependencyNode fnNode(oNodes[i]);
			const std::string currentName = MNamespace::stripNamespaceFromName(fnNode.name()).asChar();

			NamingConvention::Kind kind;
			if (!NamingScene::getKind(oNodes[i], kind))
			{
				MGlobal::displayError(fnNode.name() + " is not a joint, rig control, rig module, rig constraint or mesh.");
				isValid = false;
				continue;
			}

			NamingConvention::Name current;
			const bool isNamed = NamingConvention::parse(currentName, current);

			NamingConvention::Name name;
			name.kind = kind;
			name.index = startIndex + static_cast<int>(i);
			name.base = flagBase.empty() ? current.base : flagBase;
			name.side = !flagSide.empty() ? flagSide : (isNamed ? current.side : std::string(SideNames::Center));
			if (!NamingConvention::isValidBase(name.base))
			{
				MGlobal::displayError(fnNode.name() + ": " + (flagBase.empty() ? "it is not in the naming convention, so pass -base" : "-base must be camelCase")
					+ " (a lowercase letter, then letters and digits, no underscore).");
				isValid = false;
				continue;
			}

			const MString target = DagUtils::getAbsoluteName(fnNode.parentNamespace(), NamingConvention::format(name).c_str());
			auto [it, isNew] = takenBy.emplace(target.asChar(), fnNode.name().asChar());
			if (!isNew)
			{
				MGlobal::displayError(fnNode.name() + " and " + it->second.c_str() + " would both be named " + target + ".");
				isValid = false;
				continue;
			}
			outRenames.push_back({ oNodes[i], target });
		}

		// A node outside the ones being renamed that holds a target name would make Maya pick another name.
		for (const Rename& rename : outRenames)
		{
			MObject oExisting;
			if (DagUtils::getMObjectFromName(rename.target, oExisting) && oExisting != rename.oNode)
			{
				bool isBeingRenamed = false;
				for (unsigned int i = 0; i < oNodes.length(); ++i)
				{
					isBeingRenamed = isBeingRenamed || oNodes[i] == oExisting;
				}
				if (!isBeingRenamed)
				{
					MGlobal::displayError(MString("A node named ") + rename.target + " already exists.");
					isValid = false;
				}
			}
		}
		return isValid;
	}
}

const char* RenameRigNodesCmd::aCommandString = "renameRigNodes";

RenameRigNodesCmd::RenameRigNodesCmd()
{
}

RenameRigNodesCmd::~RenameRigNodesCmd()
{
}

void* RenameRigNodesCmd::creator()
{
	return new RenameRigNodesCmd();
}

bool RenameRigNodesCmd::isUndoable() const
{
	return fIsUndoable;
}

MSyntax RenameRigNodesCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-bs", kBaseFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-sd", kSideFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-si", kStartIndexFlag, MSyntax::kLong));
	syntax.enableQuery(true);
	syntax.setObjectType(MSyntax::kStringObjects, 0);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus RenameRigNodesCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError("Error parsing renameRigNodes arguments.");
		return status;
	}

	MStringArray names;
	status = argData.getObjects(names);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	if (argData.isQuery())
	{
		fIsUndoable = false;
		MStringArray queryResults;
		status = queryNames(names, queryResults);
		if (status)
		{
			setResult(queryResults);
		}
		return status;
	}

	std::string flagBase;
	std::string flagSide;
	int startIndex = 0;
	if (argData.isFlagSet(kBaseFlag))
	{
		MString base;
		status = argData.getFlagArgument(kBaseFlag, 0, base);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		flagBase = base.asChar();
	}
	if (argData.isFlagSet(kSideFlag))
	{
		MString side;
		status = argData.getFlagArgument(kSideFlag, 0, side);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		flagSide = side.asChar();
		if (NamingConvention::sideToken(flagSide).empty())
		{
			MGlobal::displayError(MString("-side must be ") + SideNames::Left + ", " + SideNames::Right + " or " + SideNames::Center + ", got '" + side + "'.");
			return MStatus::kFailure;
		}
	}
	if (argData.isFlagSet(kStartIndexFlag))
	{
		status = argData.getFlagArgument(kStartIndexFlag, 0, startIndex);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		if (startIndex < 0)
		{
			MGlobal::displayError("-startIndex must be 0 or more.");
			return MStatus::kFailure;
		}
	}

	MObjectArray oNodes;
	status = collectNodes(names, oNodes);
	if (!status)
	{
		return status;
	}

	std::vector<Rename> renames;
	if (!planRenames(oNodes, flagBase, flagSide, startIndex, renames))
	{
		return MStatus::kFailure;
	}

	// Through temporary names first, so renumbering nodes among themselves (00 -> 01 while 01 -> 00) never meets a taken name.
	for (std::size_t i = 0; i < renames.size(); ++i)
	{
		status = fDagMod.renameNode(renames[i].oNode, (std::string(kTempNamePrefix) + std::to_string(i)).c_str());
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	for (const Rename& rename : renames)
	{
		status = fDagMod.renameNode(rename.oNode, rename.target);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}

	status = redoIt();
	if (!status)
	{
		return status;
	}

	MStringArray results;
	for (const Rename& rename : renames)
	{
		results.append(MFnDependencyNode(rename.oNode).name());
	}
	setResult(results);
	return MStatus::kSuccess;
}

MStatus RenameRigNodesCmd::redoIt()
{
	MStatus status = fDagMod.doIt();
	if (!status)
	{
		MGlobal::displayError("renameRigNodes failed to apply: " + status.errorString());
	}
	return status;
}

MStatus RenameRigNodesCmd::undoIt()
{
	return fDagMod.undoIt();
}
