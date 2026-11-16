#include "maya/shared/ControlSpaceScene.h"
#include "core/Constants.h"
#include "maya/ModuleRegistry.h"
#include "maya/nodes/Constraints.h"
#include "maya/shared/RigModule.h"
#include "maya/shared/RigPoseScene.h"
#include "maya/shared/RigTraversal.h"

#include <cstddef>
#include <string>
#include <vector>

#include <maya/MFnDependencyNode.h>
#include <maya/MFnEnumAttribute.h>
#include <maya/MGlobal.h>
#include <maya/MMatrix.h>
#include <maya/MObjectArray.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>

namespace
{
	// The module that reads a control's `space` enum, and which of its controls it is.
	struct ModuleOwnedSpace
	{
		MObject oModule;
		unsigned int slot = 0;
		ModuleSpaceDeltaFn spaceDelta = nullptr;
	};

	// A module-owned space is a `space` enum wired into a module whose descriptor has a spaceDelta. A constraint's enum
	// is wired into a rigConstraint instead, so the two never overlap.
	bool findModuleOwnedSpace(const MObject& oControl, ModuleOwnedSpace& outSpace)
	{
		MStatus status;
		MPlug pSpace = MFnDependencyNode(oControl).findPlug(RigAttr::ControlSpace, false, &status);
		if (!status)
		{
			return false;
		}

		MPlugArray pDestinations;
		pSpace.destinations(pDestinations, &status);
		for (unsigned int i = 0; i < pDestinations.length(); ++i)
		{
			const MObject oNode = pDestinations[i].node();
			if (!RigModuleNodeBase::isRigModule(oNode))
			{
				continue;
			}
			const ModuleDescriptor* pDescriptor = ModuleRegistry::find(RigTraversal::getModuleType(oNode));
			if (!pDescriptor || !pDescriptor->spaceDelta)
			{
				continue;
			}

			MObjectArray oControls;
			if (!RigTraversal::getModuleControls(oNode, oControls))
			{
				continue;
			}
			for (unsigned int slot = 0; slot < oControls.length(); ++slot)
			{
				if (oControls[slot] == oControl)
				{
					outSpace.oModule = oNode;
					outSpace.slot = slot;
					outSpace.spaceDelta = pDescriptor->spaceDelta;
					return true;
				}
			}
		}
		return false;
	}

	// The enum's labels in value order. A module fixes them when it builds the control (static, append-only: D34).
	std::vector<std::string> readEnumLabels(const MPlug& pSpace)
	{
		std::vector<std::string> labels;
		MFnEnumAttribute fnEnum(pSpace.attribute());
		short min = 0;
		short max = -1;
		fnEnum.getMin(min);
		fnEnum.getMax(max);
		for (short i = min; i <= max; ++i)
		{
			labels.push_back(fnEnum.fieldName(i).asChar());
		}
		return labels;
	}
}

bool ControlSpaceScene::getControlSpace(const MObject& oControl, ControlSpace& outSpace)
{
	if (RigConstraintNode::getControlSpace(oControl, outSpace))
	{
		return true;
	}

	ModuleOwnedSpace owned;
	if (!findModuleOwnedSpace(oControl, owned))
	{
		return false;
	}

	const MPlug pSpace = MFnDependencyNode(oControl).findPlug(RigAttr::ControlSpace, false);
	outSpace.constraint.clear();
	outSpace.module = MFnDependencyNode(owned.oModule).name().asChar();
	outSpace.labels = readEnumLabels(pSpace);
	outSpace.current = pSpace.asShort();
	return true;
}

MStatus ControlSpaceScene::setControlSpace(const MObject& oControl, unsigned int newSpace, MDagModifier& dagMod)
{
	const MString controlName = MFnDependencyNode(oControl).name();

	ControlSpace space;
	MObject oConstraint;
	if (RigConstraintNode::getControlSpace(oControl, space, &oConstraint))
	{
		return RigConstraintNode::matchSwitchSpace(oConstraint, newSpace, dagMod);
	}

	ModuleOwnedSpace owned;
	if (!findModuleOwnedSpace(oControl, owned))
	{
		MGlobal::displayError(controlName + " has no space switch.");
		return MStatus::kFailure;
	}

	MPlug pSpace = MFnDependencyNode(oControl).findPlug(RigAttr::ControlSpace, false);
	const std::size_t labelCount = readEnumLabels(pSpace).size();
	if (newSpace >= labelCount)
	{
		MGlobal::displayError(controlName + " has " + static_cast<unsigned int>(labelCount) + " spaces, so there is no space " + newSpace + ".");
		return MStatus::kInvalidParameter;
	}

	// D_old comes from the control's current enum value, so a space set by hand or keyed switches from where it is.
	MMatrix mOldDelta;
	MMatrix mNewDelta;
	MStatus status = owned.spaceDelta(owned.oModule, owned.slot, static_cast<unsigned int>(pSpace.asShort()), mOldDelta);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	status = owned.spaceDelta(owned.oModule, owned.slot, newSpace, mNewDelta);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	status = RigPoseScene::matchControlToNewDelta(oControl, mOldDelta, mNewDelta, dagMod);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	status = dagMod.newPlugValueShort(pSpace, static_cast<short>(newSpace));
	CHECK_MSTATUS_AND_RETURN_IT(status);
	return dagMod.doIt();
}
