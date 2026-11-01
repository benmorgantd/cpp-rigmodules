#include "Constraints.h"
#include "RigControlNode.h"
#include "Utilities.h"
#include "Constants.h"

#include <unordered_map>

#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnCompoundAttribute.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MArgDatabase.h>
#include <maya/MSelectionList.h>
#include <maya/MGlobal.h>

MTypeId ParentConstraintNode::id(0x00218E);

MObject ParentConstraintNode::aDrivers;
MObject ParentConstraintNode::aDriverMatrix;
MObject ParentConstraintNode::aDriverRest;
MObject ParentConstraintNode::aDriverOffset;
MObject ParentConstraintNode::aDriverLabel;
MObject ParentConstraintNode::aActiveDriver;
MObject ParentConstraintNode::aOutParentMatrix;
MObject ParentConstraintNode::aRigRoot;

namespace
{
	// "module.name" -> ("module", "name")
	bool splitIdentifier(const std::string& identifier, std::string& outModule, std::string& outName)
	{
		const size_t dot = identifier.find('.');
		if (dot == std::string::npos || dot == 0 || dot + 1 >= identifier.size())
		{
			return false;
		}
		outModule = identifier.substr(0, dot);
		outName = identifier.substr(dot + 1);
		return true;
	}

	MStatus getNodeByName(const MString& name, MObject& oOutNode)
	{
		MSelectionList sel;
		MStatus status = sel.add(name);
		if (!status)
		{
			return status;
		}
		return sel.getDependNode(0, oOutNode);
	}

	// Motion of a driver since rest.
	MMatrix driverDelta(const MMatrix& mRest, const MMatrix& mWorld)
	{
		return mRest.inverse() * mWorld;
	}
}

ParentConstraintNode::ParentConstraintNode() {}
ParentConstraintNode::~ParentConstraintNode() {}

void* ParentConstraintNode::creator()
{
	return new ParentConstraintNode();
}

MStatus ParentConstraintNode::initialize()
{
	MStatus status;
	MFnMatrixAttribute mAttr;
	MFnTypedAttribute tAttr;
	MFnNumericAttribute nAttr;
	MFnCompoundAttribute cAttr;
	MFnMessageAttribute msgAttr;

	aDriverMatrix = mAttr.create(ConstraintAttr::DriverMatrix, ConstraintAttr::DriverMatrix, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	aDriverRest = mAttr.create(ConstraintAttr::DriverRest, ConstraintAttr::DriverRest, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	mAttr.setStorable(true);

	aDriverOffset = mAttr.create(ConstraintAttr::DriverOffset, ConstraintAttr::DriverOffset, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	mAttr.setStorable(true);

	aDriverLabel = tAttr.create(ConstraintAttr::DriverLabel, ConstraintAttr::DriverLabel, MFnData::kString, MObject::kNullObj, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	tAttr.setStorable(true);

	aDrivers = cAttr.create(ConstraintAttr::Drivers, ConstraintAttr::Drivers, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	cAttr.addChild(aDriverMatrix);
	cAttr.addChild(aDriverRest);
	cAttr.addChild(aDriverOffset);
	cAttr.addChild(aDriverLabel);
	cAttr.setArray(true);
	cAttr.setUsesArrayDataBuilder(true);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aDrivers));

	// An int (not an enum attribute) so the number of drivers can grow after creation.
	aActiveDriver = nAttr.create(ConstraintAttr::ActiveDriver, ConstraintAttr::ActiveDriver, MFnNumericData::kInt, 0, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	nAttr.setStorable(true);
	nAttr.setKeyable(false);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aActiveDriver));

	aOutParentMatrix = mAttr.create(ConstraintAttr::OutParentMatrix, ConstraintAttr::OutParentMatrix, MFnMatrixAttribute::kDouble, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	mAttr.setWritable(false);
	mAttr.setStorable(false);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aOutParentMatrix));

	aRigRoot = msgAttr.create(ConstraintAttr::RigRoot, ConstraintAttr::RigRoot, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	CHECK_MSTATUS_AND_RETURN_IT(addAttribute(aRigRoot));

	// AFFECTS RELATIONSHIPS
	attributeAffects(aDriverMatrix, aOutParentMatrix);
	attributeAffects(aDriverRest, aOutParentMatrix);
	attributeAffects(aDriverOffset, aOutParentMatrix);
	attributeAffects(aActiveDriver, aOutParentMatrix);

	return MStatus::kSuccess;
}

// out = offset * (rest^-1 * world) for the active driver only. Inactive drivers are never read, so they are never evaluated.
MStatus ParentConstraintNode::compute(const MPlug& plug, MDataBlock& data)
{
	if (plug.attribute() != aOutParentMatrix)
	{
		return MStatus::kUnknownParameter;
	}

	MStatus status;
	const int activeDriver = data.inputValue(aActiveDriver, &status).asInt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MArrayDataHandle hDrivers = data.inputArrayValue(aDrivers, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MMatrix mOut = MMatrix::identity;
	if (activeDriver >= 0 && hDrivers.jumpToElement(static_cast<unsigned int>(activeDriver)) == MStatus::kSuccess)
	{
		MDataHandle hDriver = hDrivers.inputValue(&status);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		const MMatrix mWorld = hDriver.child(aDriverMatrix).asMatrix();
		const MMatrix mRest = hDriver.child(aDriverRest).asMatrix();
		const MMatrix mOffset = hDriver.child(aDriverOffset).asMatrix();
		mOut = mOffset * driverDelta(mRest, mWorld);
	}

	data.outputValue(aOutParentMatrix).setMMatrix(mOut);
	data.setClean(plug);
	return MStatus::kSuccess;
}

// ------------------------------------------------------------------
// Scene resolution
// ------------------------------------------------------------------

// A constrained control must be a rig control wired to a module, and its index must be a named child plug.
// Child plug index == control index on every current module.
MStatus ParentConstraintNode::resolveControl(const MObject& oControl, MObject& oOutModule, unsigned int& outPlugIndex)
{
	MStatus status;
	if (oControl.isNull() || !oControl.hasFn(MFn::kTransform))
	{
		MGlobal::displayError("Constraint control must be a rigControlNode transform.");
		return MStatus::kFailure;
	}

	MFnDependencyNode fnControl(oControl);
	if (fnControl.typeId() != RigControlNode::id)
	{
		MGlobal::displayError(MString("Not a rig control: ") + fnControl.name());
		return MStatus::kFailure;
	}

	MPlug pRigModule(oControl, RigControlNode::aRigModule);
	MPlug pModuleSource = pRigModule.source(&status);
	if (!status || pModuleSource.isNull() || !pModuleSource.isElement())
	{
		MGlobal::displayError(MString("Rig control does not belong to a module: ") + fnControl.name());
		return MStatus::kFailure;
	}

	oOutModule = pModuleSource.node();
	outPlugIndex = pModuleSource.logicalIndex();

	MFnDependencyNode fnModule(oOutModule);
	MPlug pChildPlugs = fnModule.findPlug(RigAttr::ChildPlugs, false, &status);
	if (!status || pChildPlugs.isNull())
	{
		MGlobal::displayError(MString("Node is not a rig module: ") + fnModule.name());
		return MStatus::kFailure;
	}

	// Walk physical elements so we don't create sparse elements by asking for a logical index.
	MObject oChildPlugName = fnModule.attribute(RigAttr::ChildPlugName);
	for (unsigned int e = 0; e < pChildPlugs.numElements(); ++e)
	{
		MPlug pElement = pChildPlugs.elementByPhysicalIndex(e);
		if (pElement.logicalIndex() == outPlugIndex)
		{
			if (pElement.child(oChildPlugName).asString().length() > 0)
			{
				return MStatus::kSuccess;
			}
			break;
		}
	}

	MGlobal::displayError(MString("Control is not a child plug of its module: ") + fnControl.name());
	return MStatus::kFailure;
}

// "module.socket" -> module node, socket index, rest matrix. Socket names live in socketNames as ';' separated tokens.
MStatus ParentConstraintNode::resolveSocket(const std::string& socketId, ConstraintDriverInfo& outDriver)
{
	MStatus status;
	std::string moduleName, socketName;
	if (!splitIdentifier(socketId, moduleName, socketName))
	{
		MGlobal::displayError(MString("Driver must be 'module.socket': ") + socketId.c_str());
		return MStatus::kFailure;
	}

	MObject oModule;
	if (!getNodeByName(moduleName.c_str(), oModule))
	{
		MGlobal::displayError(MString("Driver module not found: ") + moduleName.c_str());
		return MStatus::kFailure;
	}

	MFnDependencyNode fnModule(oModule);
	MPlug pSocketNames = fnModule.findPlug(RigAttr::SocketNames, false, &status);
	if (!status || pSocketNames.isNull())
	{
		MGlobal::displayError(MString("Node has no sockets: ") + moduleName.c_str());
		return MStatus::kFailure;
	}

	for (unsigned int e = 0; e < pSocketNames.numElements(); ++e)
	{
		MPlug pElement = pSocketNames.elementByPhysicalIndex(e);
		if (StringUtils::nameListContains(pElement.asString(), socketName.c_str()))
		{
			const unsigned int socketIndex = pElement.logicalIndex();

			// Socket i is always control i's element in controlData, so the rest lives there.
			MPlug pControlData = fnModule.findPlug(RigAttr::ControlInputData, false);
			MPlug pControlElement = pControlData.elementByLogicalIndex(socketIndex);
			MObject oControlRestWorld = fnModule.attribute(RigAttr::ControlRestWorld);
			MPlug pRest = pControlElement.child(oControlRestWorld);

			outDriver.oModule = oModule;
			outDriver.socketIndex = socketIndex;
			outDriver.mRestWorld = DagUtils::getMatrixFromPlug(pRest);
			if (outDriver.label.empty())
			{
				outDriver.label = socketId;
			}
			return MStatus::kSuccess;
		}
	}

	MGlobal::displayError(MString("Socket '") + socketName.c_str() + "' not found on module '" + moduleName.c_str() + "'.");
	return MStatus::kFailure;
}

MStatus ParentConstraintNode::getChildPlugMatrixPlugs(const MObject& oModule, unsigned int plugIndex, MPlug& pOutParentMatrix, MPlug& pOutParentRest)
{
	MStatus status;
	MFnDependencyNode fnModule(oModule);
	MPlug pChildPlugs = fnModule.findPlug(RigAttr::ChildPlugs, false, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MPlug pElement = pChildPlugs.elementByLogicalIndex(plugIndex, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MObject oChildPlugParentMatrix = fnModule.attribute(RigAttr::ChildPlugParentMatrix);
	pOutParentMatrix = pElement.child(oChildPlugParentMatrix, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MObject oChildPlugParentRestMatrix = fnModule.attribute(RigAttr::ChildPlugParentRestMatrix);
	pOutParentRest = pElement.child(oChildPlugParentRestMatrix, &status);
	return status;
}

MPlug ParentConstraintNode::getSocketPlug(const ConstraintDriverInfo& driver)
{
	MFnDependencyNode fnModule(driver.oModule);
	MPlug pSockets = fnModule.findPlug(RigAttr::OutputSocketMatrix, false);
	return pSockets.elementByLogicalIndex(driver.socketIndex);
}

// ------------------------------------------------------------------
// Creation
// ------------------------------------------------------------------
MStatus ParentConstraintNode::createConstraint(const ConstraintBuildArgs& args, MDagModifier& dagMod, MObject& oOutConstraint)
{
	MStatus status;

	if (args.oControl.isNull() || args.oModule.isNull() || args.drivers.empty())
	{
		MGlobal::displayError("Constraint needs a control, its module and at least one driver.");
		return MStatus::kFailure;
	}
	if (args.initialDriver >= args.drivers.size())
	{
		MGlobal::displayError("Initial driver index is out of range.");
		return MStatus::kFailure;
	}

	MFnDependencyNode fnControl(args.oControl);
	if (fnControl.hasAttribute(RigAttr::ControlSpace))
	{
		MGlobal::displayError(MString("Control already has a space attribute: ") + fnControl.name());
		return MStatus::kFailure;
	}

	for (const ConstraintDriverInfo& driver : args.drivers)
	{
		// A module driving its own child plug is a DG cycle.
		if (driver.oModule == args.oModule)
		{
			MGlobal::displayError(MString("A module cannot drive its own child plug: ") + driver.label.c_str());
			return MStatus::kFailure;
		}
	}

	MPlug pParentMatrix;
	MPlug pParentRest;
	status = getChildPlugMatrixPlugs(args.oModule, args.plugIndex, pParentMatrix, pParentRest);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// Capture the parent motion the control has right now, before anything is rewired. This is what the initial driver must reproduce.
	const MMatrix mCurrentDelta = driverDelta(DagUtils::getMatrixFromPlug(pParentRest), DagUtils::getMatrixFromPlug(pParentMatrix));

	const ConstraintDriverInfo& initialDriver = args.drivers[args.initialDriver];
	const MMatrix mInitialDriverDelta = driverDelta(initialDriver.mRestWorld, DagUtils::getMatrixFromPlug(getSocketPlug(initialDriver)));

	// STAGE 1: the constraint node and the control's enum. The enum must exist in the scene before it can be wired.
	MObject oConstraint = dagMod.MDGModifier::createNode(ParentConstraintNode::id);
	dagMod.renameNode(oConstraint, args.name.c_str());

	if (args.exposeSpace)
	{
		MFnEnumAttribute eAttr;
		MObject oSpaceAttr = eAttr.create(RigAttr::ControlSpace, RigAttr::ControlSpace, static_cast<short>(args.initialDriver), &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		for (unsigned int i = 0; i < args.drivers.size(); ++i)
		{
			eAttr.addField(args.drivers[i].label.c_str(), static_cast<short>(i));
		}
		eAttr.setKeyable(true);
		eAttr.setStorable(true);

		status = dagMod.addAttribute(args.oControl, oSpaceAttr);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}

	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// STAGE 2: wiring and values
	MPlug pDrivers(oConstraint, aDrivers);
	for (unsigned int i = 0; i < args.drivers.size(); ++i)
	{
		const ConstraintDriverInfo& driver = args.drivers[i];
		MPlug pDriver = pDrivers.elementByLogicalIndex(i, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		MPlug pDriverMatrix = pDriver.child(aDriverMatrix);
		MPlug pDriverRest = pDriver.child(aDriverRest);
		MPlug pDriverOffset = pDriver.child(aDriverOffset);
		MPlug pDriverLabel = pDriver.child(aDriverLabel);

		status = dagMod.connect(getSocketPlug(driver), pDriverMatrix);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		DagUtils::setMatrixPlug(pDriverRest, driver.mRestWorld, dagMod);
		dagMod.newPlugValueString(pDriverLabel, MString(driver.label.c_str()));

		// Only the initial driver gets a compensation offset now. The others are matched when they are switched to.
		const MMatrix mOffset = (i == args.initialDriver) ? mCurrentDelta * mInitialDriverDelta.inverse() : MMatrix::identity;
		DagUtils::setMatrixPlug(pDriverOffset, mOffset, dagMod);
	}

	MPlug pActiveDriver(oConstraint, aActiveDriver);
	if (args.exposeSpace)
	{
		MPlug pControlSpace = fnControl.findPlug(RigAttr::ControlSpace, false, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = dagMod.connect(pControlSpace, pActiveDriver);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	else
	{
		dagMod.newPlugValueInt(pActiveDriver, static_cast<int>(args.initialDriver));
	}

	// Replace whatever the parenting pass connected to the child plug.
	MPlug pOldSource = pParentMatrix.source();
	if (!pOldSource.isNull())
	{
		dagMod.disconnect(pOldSource, pParentMatrix);
	}
	MPlug pOut(oConstraint, aOutParentMatrix);
	status = dagMod.connect(pOut, pParentMatrix);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// The constraint output is already a delta, so the plug's rest is identity.
	DagUtils::setMatrixPlug(pParentRest, MMatrix::identity, dagMod);

	// Network: rig root keeps a list of its constraints for traversal and baking.
	if (!args.oRigRoot.isNull())
	{
		MFnDependencyNode fnRigRoot(args.oRigRoot);
		MPlug pRigRootConstraints = fnRigRoot.findPlug(RigAttr::Constraints, false, &status);
		if (status && !pRigRootConstraints.isNull())
		{
			MPlug pSlot = pRigRootConstraints.elementByLogicalIndex(pRigRootConstraints.evaluateNumElements());
			MPlug pConstraintRigRoot(oConstraint, aRigRoot);
			dagMod.connect(pSlot, pConstraintRigRoot);
		}
	}

	status = dagMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	oOutConstraint = oConstraint;
	return MStatus::kSuccess;
}

MStatus ParentConstraintNode::buildFromTemplate(
	const std::vector<RigConstraintData>& constraints,
	const std::unordered_map<std::string, RigModuleBuildResult>& rigModuleMap,
	const MObject& oRigRoot,
	MDagModifier& dagMod)
{
	bool anyFailed = false;

	for (const RigConstraintData& constraintData : constraints)
	{
		if (constraintData.type != "Parent")
		{
			MGlobal::displayError(MString("Unknown constraint type '") + constraintData.type.c_str() + "' on " + constraintData.name.c_str());
			anyFailed = true;
			continue;
		}

		// Target "module.childPlug" -> the control and plug index from the build result.
		std::string targetModuleName, targetPlugName;
		if (!splitIdentifier(constraintData.target, targetModuleName, targetPlugName))
		{
			MGlobal::displayError(MString("Constraint target must be 'module.childPlug': ") + constraintData.name.c_str());
			anyFailed = true;
			continue;
		}

		auto moduleIt = rigModuleMap.find(targetModuleName);
		const RigModuleChildPlugInfo* pChildPlug =
			(moduleIt != rigModuleMap.end()) ? RigModuleNodeBase::findInfo(moduleIt->second.childPlugs, targetPlugName) : nullptr;
		if (pChildPlug == nullptr)
		{
			MGlobal::displayError(MString("Constraint target not found: ") + constraintData.target.c_str());
			anyFailed = true;
			continue;
		}
		if (pChildPlug->oTransform.isNull())
		{
			MGlobal::displayError(MString("Child plug has no control to host a space attribute: ") + constraintData.target.c_str());
			anyFailed = true;
			continue;
		}

		ConstraintBuildArgs args;
		args.name = constraintData.name;
		args.oControl = pChildPlug->oTransform;
		args.oModule = moduleIt->second.moduleNode;
		args.plugIndex = pChildPlug->plugIndex;
		args.exposeSpace = constraintData.exposeSpace;
		args.oRigRoot = oRigRoot;

		// Drivers resolve through the build results, the same way pass 2 resolves parents.
		double bestWeight = 0.0;
		bool driversValid = true;
		for (unsigned int i = 0; i < constraintData.drivers.size(); ++i)
		{
			const RigConstraintDriverData& driverData = constraintData.drivers[i];
			std::string driverModuleName, driverSocketName;
			if (!splitIdentifier(driverData.socket, driverModuleName, driverSocketName))
			{
				MGlobal::displayError(MString("Driver must be 'module.socket': ") + driverData.socket.c_str());
				driversValid = false;
				break;
			}

			auto driverModuleIt = rigModuleMap.find(driverModuleName);
			const RigModuleSocketInfo* pSocket =
				(driverModuleIt != rigModuleMap.end()) ? RigModuleNodeBase::findInfo(driverModuleIt->second.parentSockets, driverSocketName) : nullptr;
			if (pSocket == nullptr)
			{
				MGlobal::displayError(MString("Driver socket not found: ") + driverData.socket.c_str());
				driversValid = false;
				break;
			}

			ConstraintDriverInfo driver;
			driver.oModule = driverModuleIt->second.moduleNode;
			driver.socketIndex = pSocket->outputIndex;
			driver.mRestWorld = pSocket->mRestWorld;
			driver.label = driverData.label;
			args.drivers.push_back(driver);

			if (driverData.weight > bestWeight)
			{
				bestWeight = driverData.weight;
				args.initialDriver = i;
			}
		}

		MObject oConstraint;
		if (!driversValid || !createConstraint(args, dagMod, oConstraint))
		{
			MGlobal::displayError(MString("Failed to build constraint: ") + constraintData.name.c_str());
			anyFailed = true;
		}
	}

	return anyFailed ? MStatus::kFailure : MStatus::kSuccess;
}

// ------------------------------------------------------------------
// Switching
// ------------------------------------------------------------------
MStatus ParentConstraintNode::matchSwitchSpace(const MObject& oConstraint, unsigned int newDriver, MDagModifier& dagMod)
{
	MStatus status;
	MPlug pDrivers(oConstraint, aDrivers);

	MPlug pNewDriver;
	for (unsigned int e = 0; e < pDrivers.numElements(); ++e)
	{
		MPlug pElement = pDrivers.elementByPhysicalIndex(e);
		if (pElement.logicalIndex() == newDriver)
		{
			pNewDriver = pElement;
			break;
		}
	}
	if (pNewDriver.isNull())
	{
		MGlobal::displayError("Constraint has no driver at that index.");
		return MStatus::kFailure;
	}

	// D_old: what the module is receiving right now (evaluates with the current active driver).
	MPlug pOut(oConstraint, aOutParentMatrix);
	const MMatrix mOld = DagUtils::getMatrixFromPlug(pOut);

	// D_new: motion of the new driver since its rest.
	const MMatrix mNewDelta = driverDelta(
		DagUtils::getMatrixFromPlug(pNewDriver.child(aDriverRest)),
		DagUtils::getMatrixFromPlug(pNewDriver.child(aDriverMatrix)));

	// offset * D_new == D_old at this instant, so nothing moves.
	MPlug pOffset = pNewDriver.child(aDriverOffset);
	status = DagUtils::setMatrixPlug(pOffset, mOld * mNewDelta.inverse(), dagMod);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// Set the control's enum when the constraint is driven by one, otherwise the int directly.
	MPlug pActive(oConstraint, aActiveDriver);
	MPlug pSource = pActive.source();
	if (!pSource.isNull())
	{
		dagMod.newPlugValueShort(pSource, static_cast<short>(newDriver));
	}
	else
	{
		dagMod.newPlugValueInt(pActive, static_cast<int>(newDriver));
	}

	return dagMod.doIt();
}

// ------------------------------------------------------------------
// createRigParentConstraint
// ------------------------------------------------------------------
const char* CreateParentConstraintCmd::aCommandString = "createRigParentConstraint";

CreateParentConstraintCmd::CreateParentConstraintCmd() {}
CreateParentConstraintCmd::~CreateParentConstraintCmd() {}
void* CreateParentConstraintCmd::creator() { return new CreateParentConstraintCmd(); }
bool CreateParentConstraintCmd::isUndoable() const { return true; }

MSyntax CreateParentConstraintCmd::newSyntax()
{
	MSyntax syntax;
	syntax.addFlag("-n", "-name", MSyntax::kString);
	syntax.addFlag("-c", "-control", MSyntax::kString);
	syntax.addFlag("-d", "-driver", MSyntax::kString);
	syntax.makeFlagMultiUse("-d");
	syntax.addFlag("-l", "-label", MSyntax::kString);
	syntax.makeFlagMultiUse("-l");
	syntax.addFlag("-i", "-initial", MSyntax::kLong);
	return syntax;
}

MStatus CreateParentConstraintCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	if (!argData.isFlagSet("-c"))
	{
		MGlobal::displayError("A rig control is required (-control).");
		return MStatus::kFailure;
	}

	MString controlName;
	argData.getFlagArgument("-c", 0, controlName);
	MObject oControl;
	if (!getNodeByName(controlName, oControl))
	{
		MGlobal::displayError(MString("Control not found: ") + controlName);
		return MStatus::kFailure;
	}

	ConstraintBuildArgs buildArgs;
	status = ParentConstraintNode::resolveControl(oControl, buildArgs.oModule, buildArgs.plugIndex);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	buildArgs.oControl = oControl;

	const unsigned int numDrivers = argData.numberOfFlagUses("-d");
	const unsigned int numLabels = argData.numberOfFlagUses("-l");
	if (numDrivers == 0 || (numLabels != 0 && numLabels != numDrivers))
	{
		MGlobal::displayError("Give at least one -driver, and either no -label or one -label per -driver.");
		return MStatus::kFailure;
	}

	for (unsigned int i = 0; i < numDrivers; ++i)
	{
		MArgList driverArgs;
		argData.getFlagArgumentList("-d", i, driverArgs);

		ConstraintDriverInfo driver;
		if (numLabels > 0)
		{
			MArgList labelArgs;
			argData.getFlagArgumentList("-l", i, labelArgs);
			driver.label = labelArgs.asString(0).asChar();
		}

		status = ParentConstraintNode::resolveSocket(driverArgs.asString(0).asChar(), driver);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		buildArgs.drivers.push_back(driver);
	}

	int initialDriver = 0;
	if (argData.isFlagSet("-i"))
	{
		argData.getFlagArgument("-i", 0, initialDriver);
	}
	buildArgs.initialDriver = static_cast<unsigned int>(initialDriver < 0 ? 0 : initialDriver);

	MString name = MFnDependencyNode(oControl).name() + "_space";
	if (argData.isFlagSet("-n"))
	{
		argData.getFlagArgument("-n", 0, name);
	}
	buildArgs.name = name.asChar();

	// Rig root, if the module is connected to one.
	MFnDependencyNode fnModule(buildArgs.oModule);
	MPlug pRigRoot = fnModule.findPlug(RigAttr::RigRoot, false);
	MPlug pRigRootSource = pRigRoot.source();
	if (!pRigRootSource.isNull())
	{
		buildArgs.oRigRoot = pRigRootSource.node();
	}

	MObject oConstraint;
	status = ParentConstraintNode::createConstraint(buildArgs, fDagMod, oConstraint);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	setResult(MFnDependencyNode(oConstraint).name());
	return MStatus::kSuccess;
}

// createConstraint already ran both doIt stages. After an undo, doIt on the same modifier performs the operations again.
MStatus CreateParentConstraintCmd::redoIt()
{
	return fDagMod.doIt();
}

MStatus CreateParentConstraintCmd::undoIt()
{
	return fDagMod.undoIt();
}

// ------------------------------------------------------------------
// setRigConstraintSpace
// ------------------------------------------------------------------
const char* SetConstraintSpaceCmd::aCommandString = "setRigConstraintSpace";

SetConstraintSpaceCmd::SetConstraintSpaceCmd() {}
SetConstraintSpaceCmd::~SetConstraintSpaceCmd() {}
void* SetConstraintSpaceCmd::creator() { return new SetConstraintSpaceCmd(); }
bool SetConstraintSpaceCmd::isUndoable() const { return true; }

MSyntax SetConstraintSpaceCmd::newSyntax()
{
	MSyntax syntax;
	syntax.addArg(MSyntax::kString);
	syntax.addFlag("-s", "-space", MSyntax::kLong);
	syntax.addFlag("-l", "-label", MSyntax::kString);
	return syntax;
}

MStatus SetConstraintSpaceCmd::doIt(const MArgList& args)
{
	MStatus status;
	MArgDatabase argData(syntax(), args, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MObject oConstraint;
	if (!getNodeByName(argData.commandArgumentString(0), oConstraint) ||
		MFnDependencyNode(oConstraint).typeId() != ParentConstraintNode::id)
	{
		MGlobal::displayError("First argument must be a parentConstraint node.");
		return MStatus::kFailure;
	}

	int newDriver = -1;
	if (argData.isFlagSet("-s"))
	{
		argData.getFlagArgument("-s", 0, newDriver);
	}
	else if (argData.isFlagSet("-l"))
	{
		MString label;
		argData.getFlagArgument("-l", 0, label);

		MPlug pDrivers(oConstraint, ParentConstraintNode::aDrivers);
		for (unsigned int e = 0; e < pDrivers.numElements(); ++e)
		{
			MPlug pElement = pDrivers.elementByPhysicalIndex(e);
			if (pElement.child(ParentConstraintNode::aDriverLabel).asString() == label)
			{
				newDriver = static_cast<int>(pElement.logicalIndex());
				break;
			}
		}
	}

	if (newDriver < 0)
	{
		MGlobal::displayError("Give a valid -space index or -label.");
		return MStatus::kFailure;
	}

	return ParentConstraintNode::matchSwitchSpace(oConstraint, static_cast<unsigned int>(newDriver), fDagMod);
}

MStatus SetConstraintSpaceCmd::redoIt()
{
	return fDagMod.doIt();
}

MStatus SetConstraintSpaceCmd::undoIt()
{
	return fDagMod.undoIt();
}