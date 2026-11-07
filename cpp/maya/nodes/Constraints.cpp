#include "maya/nodes/Constraints.h"
#include "core/Constants.h"
#include "maya/nodes/RigControlNode.h"
#include "maya/shared/Utilities.h"

#include <cmath>
#include <unordered_map>

#include <maya/MAngle.h>
#include <maya/MArrayDataHandle.h>
#include <maya/MEulerRotation.h>
#include <maya/MFnCompoundAttribute.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MFnMatrixAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MPxTransform.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MVector.h>

namespace
{
	constexpr double kShearTolerance{ 1e-6 };

	// A channel the match may write: not locked, and either unconnected or keyed (an anim curve source).
	// Writing a keyed channel behaves like setAttr: the value holds until the animator keys it or the frame changes.
	bool isChannelWritable(const MPlug& pChannel)
	{
		if (pChannel.isLocked())
		{
			return false;
		}
		const MPlug pSource = pChannel.source();
		return pSource.isNull() || pSource.node().hasFn(MFn::kAnimCurve);
	}

	// The control hangs from its child plug delta (OPM = rest * delta, inheritsTransform off). When that delta changes
	// from mOldDelta to mNewDelta, rewrite the control's translate, rotate and scale so its world matrix stays the same:
	//   L' * rest * D_new == L * rest * D_old, with rest = OPM * D_old^-1
	//   L' = L * OPM * D_new^-1 * D_old * OPM^-1
	// Reads the live OPM, so the caller needs neither the module nor the control's index (D27).
	MStatus matchControlToNewDelta(const MObject& oControl, const MMatrix& mOldDelta, const MMatrix& mNewDelta, MDagModifier& dagMod)
	{
		if (mOldDelta.isEquivalent(mNewDelta))
		{
			return MStatus::kSuccess;
		}

		const MMatrix mLocal = DagUtils::getMatrixFromPlug(MPlug(oControl, MPxTransform::matrix));
		const MMatrix mOPM = DagUtils::getMatrixFromPlug(MPlug(oControl, MPxTransform::offsetParentMatrix));
		const MMatrix mNewLocal = mLocal * mOPM * mNewDelta.inverse() * mOldDelta * mOPM.inverse();

		const MTransformationMatrix tNewLocal(mNewLocal);
		const MVector vTranslate = tNewLocal.getTranslation(MSpace::kTransform);
		double scale[3];
		double shear[3];
		tNewLocal.getScale(scale, MSpace::kTransform);
		tNewLocal.getShear(shear, MSpace::kTransform);

		// Keep the control's rotate order, and the euler closest to its current values so curves don't flip.
		MFnDependencyNode fnControl(oControl);
		const short rotateOrder = MPlug(oControl, MPxTransform::rotateOrder).asShort();
		MEulerRotation eCurrent(
			MPlug(oControl, MPxTransform::rotateX).asMAngle().asRadians(),
			MPlug(oControl, MPxTransform::rotateY).asMAngle().asRadians(),
			MPlug(oControl, MPxTransform::rotateZ).asMAngle().asRadians(),
			static_cast<MEulerRotation::RotationOrder>(rotateOrder));
		MEulerRotation eNew = tNewLocal.eulerRotation();
		eNew.reorderIt(static_cast<MEulerRotation::RotationOrder>(rotateOrder));
		eNew.setToClosestSolution(eCurrent);

		// Same order as kDefaultTransformValues: translate, rotate, scale.
		const double values[9] =
		{
			vTranslate.x, vTranslate.y, vTranslate.z,
			eNew.x, eNew.y, eNew.z,
			scale[0], scale[1], scale[2]
		};

		MString skipped;
		for (unsigned int i = 0; i < 9; ++i)
		{
			MPlug pChannel(oControl, kDefaultTransformValues[i].attr);
			if (!isChannelWritable(pChannel))
			{
				skipped += MString(" ") + pChannel.partialName(false, false, false, false, false, true);
				continue;
			}
			const bool isRotate = i >= 3 && i < 6;
			MStatus status = isRotate
				? dagMod.newPlugValueMAngle(pChannel, MAngle(values[i], MAngle::kRadians))
				: dagMod.newPlugValueDouble(pChannel, values[i]);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}

		if (skipped.length() > 0)
		{
			MGlobal::displayWarning(fnControl.name() + ": locked or driven channels kept their values, so the control moves:" + skipped);
		}
		if (std::abs(shear[0]) > kShearTolerance || std::abs(shear[1]) > kShearTolerance || std::abs(shear[2]) > kShearTolerance)
		{
			MGlobal::displayWarning(fnControl.name() + ": the new space needs shear, which the control's channels can't hold. Position and rotation match, the shape may not.");
		}
		return MStatus::kSuccess;
	}
}

MTypeId ParentConstraintNode::id(0x00218E);

MObject ParentConstraintNode::aDrivers;
MObject ParentConstraintNode::aDriverMatrix;
MObject ParentConstraintNode::aDriverRest;
MObject ParentConstraintNode::aDriverLabel;
MObject ParentConstraintNode::aActiveDriver;
MObject ParentConstraintNode::aOutParentMatrix;
MObject ParentConstraintNode::aRigRoot;

ParentConstraintNode::ParentConstraintNode()
{
}
ParentConstraintNode::~ParentConstraintNode()
{
}

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

	aDriverLabel = tAttr.create(ConstraintAttr::DriverLabel, ConstraintAttr::DriverLabel, MFnData::kString, MObject::kNullObj, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	tAttr.setStorable(true);

	aDrivers = cAttr.create(ConstraintAttr::Drivers, ConstraintAttr::Drivers, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	cAttr.addChild(aDriverMatrix);
	cAttr.addChild(aDriverRest);
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
	attributeAffects(aActiveDriver, aOutParentMatrix);

	return MStatus::kSuccess;
}

// out = rest^-1 * world for the active driver only. Inactive drivers are never read, so they are never evaluated.
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
		mOut = MathUtils::computeDeltaFromRest(mRest, mWorld);
	}

	data.outputValue(aOutParentMatrix).setMMatrix(mOut);
	data.setClean(plug);
	return MStatus::kSuccess;
}

// ------------------------------------------------------------------
// Scene resolution
// ------------------------------------------------------------------

// "module.socket" -> module node, socket index, rest matrix. Socket names live in socketNames as JSON string lists.
// A module name that already carries a namespace ("other:layout") is used as given.
MStatus ParentConstraintNode::resolveSocket(const std::string& socketId, const MString& rigNamespace, ConstraintDriverInfo& outDriver)
{
	std::string moduleName, socketName;
	if (!StringUtils::splitIdentifier(socketId, moduleName, socketName))
	{
		MGlobal::displayError(MString("Driver must be 'module.socket': ") + socketId.c_str());
		return MStatus::kFailure;
	}

	MString moduleNodeName(moduleName.c_str());
	if (moduleName.find(':') == std::string::npos)
	{
		moduleNodeName = rigNamespace + ":" + moduleNodeName;
	}

	MObject oModule;
	MStatus status = RigModuleNodeBase::findModuleByName(moduleNodeName, oModule);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	RigModuleSocketInfo socketInfo;
	status = RigModuleNodeBase::resolveSocket(oModule, socketName.c_str(), socketInfo);
	if (!status)
	{
		MGlobal::displayError(MString("Socket '") + socketName.c_str() + "' not found on module '" + moduleName.c_str() + "'.");
		return MStatus::kFailure;
	}

	outDriver.oModule = oModule;
	outDriver.socketIndex = socketInfo.outputIndex;
	outDriver.mRestWorld = socketInfo.mRestWorld;
	if (outDriver.label.empty())
	{
		outDriver.label = socketId;
	}
	return MStatus::kSuccess;
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

	bool createEnumAttr = args.drivers.size() > 1;

	MPlug pParentMatrix;
	MPlug pParentRest;
	status = RigModuleNodeBase::getChildPlugMatrixPlugs(args.oModule, args.plugIndex, pParentMatrix, pParentRest);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	const MMatrix mCurrentDelta = MathUtils::computeDeltaFromRest(
		DagUtils::getMatrixFromPlug(pParentRest), DagUtils::getMatrixFromPlug(pParentMatrix));

	const ConstraintDriverInfo& initialDriver = args.drivers[args.initialDriver];
	const MMatrix mInitialDriverDelta = MathUtils::computeDeltaFromRest(
		initialDriver.mRestWorld,
		DagUtils::getMatrixFromPlug(RigModuleNodeBase::getSocketMatrixPlug(initialDriver.oModule, initialDriver.socketIndex)));

	// STAGE 1: the constraint node and the control's enum. The enum must exist in the scene before it can be wired.
	MObject oConstraint = dagMod.MDGModifier::createNode(ParentConstraintNode::id);
	dagMod.renameNode(oConstraint, args.name.c_str());

	if (createEnumAttr)
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
		MPlug pDriverLabel = pDriver.child(aDriverLabel);

		status = dagMod.connect(RigModuleNodeBase::getSocketMatrixPlug(driver.oModule, driver.socketIndex), pDriverMatrix);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		DagUtils::setMatrixPlug(pDriverRest, driver.mRestWorld, dagMod);
		dagMod.newPlugValueString(pDriverLabel, MString(driver.label.c_str()));
	}

	// On a posed rig the control would move to its new parent. Keep it in place through its own values (D27).
	// At rest both deltas are identity and this writes nothing.
	status = matchControlToNewDelta(args.oControl, mCurrentDelta, mInitialDriverDelta, dagMod);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MPlug pActiveDriver(oConstraint, aActiveDriver);
	if (createEnumAttr)
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
		if (constraintData.type != ConstraintTypes::Parent)
		{
			MGlobal::displayError(MString("Unknown constraint type '") + constraintData.type.c_str() + "' on " + constraintData.name.c_str());
			anyFailed = true;
			continue;
		}

		// Target "module.childPlug" -> the control and plug index from the build result.
		const RigModuleBuildResult* pTargetModule = nullptr;
		const RigModuleChildPlugInfo* pChildPlug = nullptr;
		if (!RigModuleNodeBase::lookupChildPlug(rigModuleMap, constraintData.target, pTargetModule, pChildPlug))
		{
			MGlobal::displayError(
				MString("Constraint target must be an existing 'module.childPlug': ") + constraintData.target.c_str() +
				" (constraint " + constraintData.name.c_str() + ")");
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
		args.oModule = pTargetModule->moduleNode;
		args.plugIndex = pChildPlug->plugIndex;
		args.oRigRoot = oRigRoot;

		// Drivers resolve through the build results, the same way pass 2 resolves parents.
		double bestWeight = 0.0;
		bool driversValid = true;
		for (unsigned int i = 0; i < constraintData.drivers.size(); ++i)
		{
			const RigConstraintDriverData& driverData = constraintData.drivers[i];

			const RigModuleBuildResult* pDriverModule = nullptr;
			const RigModuleSocketInfo* pSocket = nullptr;
			if (!RigModuleNodeBase::lookupParentSocket(rigModuleMap, driverData.socket, pDriverModule, pSocket))
			{
				MGlobal::displayError(MString("Driver must be an existing 'module.socket': ") + driverData.socket.c_str());
				driversValid = false;
				break;
			}

			ConstraintDriverInfo driver;
			driver.oModule = pDriverModule->moduleNode;
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
	const MMatrix mNewDelta = MathUtils::computeDeltaFromRest(
		DagUtils::getMatrixFromPlug(pNewDriver.child(aDriverRest)),
		DagUtils::getMatrixFromPlug(pNewDriver.child(aDriverMatrix)));

	// The control hosts the space enum that drives activeDriver. A constraint without one has a single driver,
	// so its delta never changes and there is nothing to match.
	MPlug pActive(oConstraint, aActiveDriver);
	MPlug pSource = pActive.source();
	if (!pSource.isNull())
	{
		status = matchControlToNewDelta(pSource.node(), mOld, mNewDelta, dagMod);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		dagMod.newPlugValueShort(pSource, static_cast<short>(newDriver));
	}
	else if (!mOld.isEquivalent(mNewDelta))
	{
		MGlobal::displayError("Constraint has no space enum, so the control to match can't be found.");
		return MStatus::kFailure;
	}
	else
	{
		dagMod.newPlugValueInt(pActive, static_cast<int>(newDriver));
	}

	return dagMod.doIt();
}
