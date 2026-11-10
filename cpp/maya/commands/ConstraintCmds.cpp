#include "maya/commands/ConstraintCmds.h"
#include "core/Constants.h"
#include "core/StringUtils.h"
#include "maya/nodes/Constraints.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/RigTraversal.h"
#include "maya/shared/Utilities.h"

#include <cstddef>
#include <string>
#include <vector>

#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MNamespace.h>
#include <maya/MPlug.h>

namespace
{
	// Code reads flags by long name only. MSyntax::addFlag requires a short name of at most 3 letters
	// (a long name there is rejected and the flag silently never registers), so short names appear only in newSyntax().
	// -label means one driver's label in createRigConstraint and the space to switch to in setRigConstraintSpace.
	constexpr const char* kNameFlag{ "-name" };
	constexpr const char* kDriverFlag{ "-driver" };
	constexpr const char* kLabelFlag{ "-label" };
	constexpr const char* kInitialFlag{ "-initial" };
	constexpr const char* kSpaceFlag{ "-space" };
	constexpr const char* kTypeFlag{ "-type" };

	constexpr const char* kDefaultConstraintSuffix{ "_space" };

	// Fills everything createConstraint needs from the control and the flags. Resolves every driver socket
	// before anything is queued, so a bad name leaves the scene untouched.
	MStatus readConstraintBuildArgs(const MArgDatabase& argData, const MString& controlName, ConstraintBuildArgs& outArgs)
	{
		MStatus status;
		if (!DagUtils::getMObjectFromName(controlName, outArgs.oControl))
		{
			MGlobal::displayError(MString("Control not found: ") + controlName);
			return MStatus::kFailure;
		}

		status = RigModuleNodeBase::resolveControlChildPlug(outArgs.oControl, outArgs.oModule, outArgs.plugIndex);
		if (!status)
		{
			return status;   // helper already reported why the control has no child plug
		}

		// Drivers are socket ids within the control's rig. From an absolute name this is ":rig", or "" for the root,
		// so "<ns>:<module>" stays absolute and does not depend on the current namespace.
		const MString rigNamespace = MNamespace::getNamespaceFromName(MFnDependencyNode(outArgs.oControl).absoluteName());

		MString type(ConstraintTypes::Parent);
		if (argData.isFlagSet(kTypeFlag))
		{
			status = argData.getFlagArgument(kTypeFlag, 0, type);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		outArgs.type = ConstraintTypes::indexOf(type.asChar());
		if (outArgs.type < 0)
		{
			MGlobal::displayError(MString("Unknown constraint -type '") + type + "' (has: "
				+ StringUtils::joinNames(ConstraintTypes::All).c_str() + ").");
			return MStatus::kFailure;
		}

		std::vector<std::string> driverIds;
		std::vector<std::string> labels;
		CHECK_MSTATUS_AND_RETURN_IT(CommandUtils::readStringList(argData, kDriverFlag, driverIds));
		CHECK_MSTATUS_AND_RETURN_IT(CommandUtils::readStringList(argData, kLabelFlag, labels));
		if (driverIds.empty() || (!labels.empty() && labels.size() != driverIds.size()))
		{
			MGlobal::displayError("Give at least one -driver, and either no -label or one -label per -driver.");
			return MStatus::kFailure;
		}

		for (std::size_t i = 0; i < driverIds.size(); ++i)
		{
			ConstraintDriverInfo driver;
			if (!labels.empty())
			{
				driver.label = labels[i];
			}

			status = RigConstraintNode::resolveSocket(driverIds[i], rigNamespace, driver);
			if (!status)
			{
				return status;   // helper already reported which driver failed
			}
			outArgs.drivers.push_back(driver);
		}

		// createConstraint rejects an index past the last driver. A negative one is rejected here, before the unsigned cast.
		int initialDriver = 0;
		if (argData.isFlagSet(kInitialFlag))
		{
			status = argData.getFlagArgument(kInitialFlag, 0, initialDriver);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		if (initialDriver < 0)
		{
			MGlobal::displayError("-initial must be a driver index of 0 or more.");
			return MStatus::kFailure;
		}
		outArgs.initialDriver = static_cast<unsigned int>(initialDriver);

		MString name = MNamespace::stripNamespaceFromName(MFnDependencyNode(outArgs.oControl).name()) + kDefaultConstraintSuffix;
		if (argData.isFlagSet(kNameFlag))
		{
			status = argData.getFlagArgument(kNameFlag, 0, name);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		// An unqualified name goes in the control's rig namespace. Absolute, so redo does not depend on the current namespace.
		if (name.index(':') < 0)
		{
			name = rigNamespace + ":" + name;
		}
		outArgs.name = name.asChar();

		// Null when the module is connected to no rig root.
		outArgs.oRigRoot = RigTraversal::findModuleRigRoot(outArgs.oModule);

		return MStatus::kSuccess;
	}

	// The logical driver index for -space or -label. Exactly one of the two must be given.
	MStatus readTargetDriver(const MArgDatabase& argData, const MObject& oConstraint, unsigned int& outDriver)
	{
		MStatus status;
		const bool hasSpace = argData.isFlagSet(kSpaceFlag);
		const bool hasLabel = argData.isFlagSet(kLabelFlag);
		if (hasSpace == hasLabel)
		{
			MGlobal::displayError("setRigConstraintSpace needs exactly one of -space or -label.");
			return MStatus::kFailure;
		}

		if (hasSpace)
		{
			int space = -1;
			status = argData.getFlagArgument(kSpaceFlag, 0, space);
			CHECK_MSTATUS_AND_RETURN_IT(status);
			if (space < 0)
			{
				MGlobal::displayError("-space must be a driver index of 0 or more.");
				return MStatus::kFailure;
			}
			outDriver = static_cast<unsigned int>(space);   // matchSwitchSpace rejects an index with no driver
			return MStatus::kSuccess;
		}

		MString label;
		status = argData.getFlagArgument(kLabelFlag, 0, label);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		MPlug pDrivers(oConstraint, RigConstraintNode::aDrivers);
		for (unsigned int e = 0; e < pDrivers.numElements(); ++e)
		{
			MPlug pElement = pDrivers.elementByPhysicalIndex(e);
			if (pElement.child(RigConstraintNode::aDriverLabel).asString() == label)
			{
				outDriver = pElement.logicalIndex();
				return MStatus::kSuccess;
			}
		}

		MGlobal::displayError(MString("Constraint ") + MFnDependencyNode(oConstraint).name() + " has no driver labelled '" + label + "'.");
		return MStatus::kFailure;
	}
}

// ------------------------------------------------------------------
// createRigConstraint
// ------------------------------------------------------------------
const char* CreateRigConstraintCmd::aCommandString = "createRigConstraint";

CreateRigConstraintCmd::CreateRigConstraintCmd()
{
}

CreateRigConstraintCmd::~CreateRigConstraintCmd()
{
}

void* CreateRigConstraintCmd::creator()
{
	return new CreateRigConstraintCmd();
}

bool CreateRigConstraintCmd::isUndoable() const
{
	return true;
}

MSyntax CreateRigConstraintCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-n", kNameFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.addFlag("-d", kDriverFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.makeFlagMultiUse(kDriverFlag));
	CHECK_MSTATUS(syntax.addFlag("-l", kLabelFlag, MSyntax::kString));
	CHECK_MSTATUS(syntax.makeFlagMultiUse(kLabelFlag));
	CHECK_MSTATUS(syntax.addFlag("-i", kInitialFlag, MSyntax::kLong));
	CHECK_MSTATUS(syntax.addFlag("-t", kTypeFlag, MSyntax::kString));
	syntax.setObjectType(MSyntax::kStringObjects, 1, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus CreateRigConstraintCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError("Error parsing createRigConstraint arguments.");
		return status;
	}

	MString controlName;
	status = CommandUtils::readSingleObject(argData, aCommandString, "node name", controlName);
	if (!status)
	{
		return status;
	}

	ConstraintBuildArgs buildArgs;
	status = readConstraintBuildArgs(argData, controlName, buildArgs);
	if (!status)
	{
		return status;
	}

	MObject oConstraint;
	status = RigConstraintNode::createConstraint(buildArgs, fDagMod, oConstraint);
	if (!status)
	{
		return status;   // createConstraint reports its own errors
	}

	setResult(MFnDependencyNode(oConstraint).name());
	return MStatus::kSuccess;
}

// createConstraint already ran both doIt stages. After an undo, doIt on the same modifier performs the operations again.
MStatus CreateRigConstraintCmd::redoIt()
{
	return fDagMod.doIt();
}

MStatus CreateRigConstraintCmd::undoIt()
{
	return fDagMod.undoIt();
}

// ------------------------------------------------------------------
// setRigConstraintSpace
// ------------------------------------------------------------------
const char* SetConstraintSpaceCmd::aCommandString = "setRigConstraintSpace";

SetConstraintSpaceCmd::SetConstraintSpaceCmd()
{
}

SetConstraintSpaceCmd::~SetConstraintSpaceCmd()
{
}

void* SetConstraintSpaceCmd::creator()
{
	return new SetConstraintSpaceCmd();
}

bool SetConstraintSpaceCmd::isUndoable() const
{
	return true;
}

MSyntax SetConstraintSpaceCmd::newSyntax()
{
	MSyntax syntax;
	CHECK_MSTATUS(syntax.addFlag("-s", kSpaceFlag, MSyntax::kLong));
	CHECK_MSTATUS(syntax.addFlag("-l", kLabelFlag, MSyntax::kString));
	syntax.setObjectType(MSyntax::kStringObjects, 1, 1);
	syntax.useSelectionAsDefault(false);
	return syntax;
}

MStatus SetConstraintSpaceCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	if (!status)
	{
		MGlobal::displayError("Error parsing setRigConstraintSpace arguments.");
		return status;
	}

	MString constraintName;
	status = CommandUtils::readSingleObject(argData, aCommandString, "node name", constraintName);
	if (!status)
	{
		return status;
	}

	MObject oConstraint;
	if (!DagUtils::getMObjectFromName(constraintName, oConstraint)
		|| MFnDependencyNode(oConstraint).typeId() != RigConstraintNode::id)
	{
		MGlobal::displayError(constraintName + " is not a rigConstraint node.");
		return MStatus::kFailure;
	}

	unsigned int newDriver = 0;
	status = readTargetDriver(argData, oConstraint, newDriver);
	if (!status)
	{
		return status;
	}

	return RigConstraintNode::matchSwitchSpace(oConstraint, newDriver, fDagMod);
}

MStatus SetConstraintSpaceCmd::redoIt()
{
	return fDagMod.doIt();
}

MStatus SetConstraintSpaceCmd::undoIt()
{
	return fDagMod.undoIt();
}
