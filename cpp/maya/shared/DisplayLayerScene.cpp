#include "maya/shared/DisplayLayerScene.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <set>
#include <vector>

#include <maya/MFn.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MObjectHandle.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>

namespace
{
	// Both live on every layer. The layer's drawInfo drives a node's drawOverride, and its identification feeds the
	// layer manager's displayLayerId array, whose index is the layer's displayOrder.
	constexpr const char* kDrawInfoAttr = "drawInfo";
	constexpr const char* kDrawOverrideAttr = "drawOverride";
	constexpr const char* kIdentificationAttr = "identification";
	constexpr const char* kDisplayOrderAttr = "displayOrder";
	constexpr const char* kLayerManagerName = "layerManager";
	constexpr const char* kLayerIdsAttr = "displayLayerId";

	bool isDeleted(const std::vector<MObjectHandle>& deletedLayers, const MObject& oLayer)
	{
		return std::find(deletedLayers.begin(), deletedLayers.end(), MObjectHandle(oLayer)) != deletedLayers.end();
	}

	// The layer manager ids already in use by layers that stay. Layers queued for deletion free theirs, because the
	// deletes run first. Id 0 belongs to the default layer.
	std::set<unsigned int> usedLayerIds(const MObject& oManager, const std::vector<MObjectHandle>& deletedLayers)
	{
		std::set<unsigned int> used;
		MPlug pIds = MFnDependencyNode(oManager).findPlug(kLayerIdsAttr, false);
		for (unsigned int e = 0; e < pIds.numElements(); ++e)
		{
			MPlug pElement = pIds.elementByPhysicalIndex(e);
			MPlugArray pSources;
			pElement.connectedTo(pSources, true, false);
			if (pSources.length() == 0 || !isDeleted(deletedLayers, pSources[0].node()))
			{
				used.insert(pElement.logicalIndex());
			}
		}
		return used;
	}

	// A layer drives a node's drawOverride, and a node has one source, so a member of another layer is disconnected
	// first. A source that is one of the layers being deleted is skipped: its connection goes with the delete.
	MStatus queueAddMember(const MObject& oLayer, const MObject& oMember, const std::vector<MObjectHandle>& deletedLayers, MDagModifier& dagMod)
	{
		MPlug pDrawOverride = MFnDependencyNode(oMember).findPlug(kDrawOverrideAttr, false);
		MPlugArray pSources;
		pDrawOverride.connectedTo(pSources, true, false);
		if (pSources.length() > 0 && !isDeleted(deletedLayers, pSources[0].node()))
		{
			MStatus status = dagMod.disconnect(pSources[0], pDrawOverride);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}

		MPlug pDrawInfo = MFnDependencyNode(oLayer).findPlug(kDrawInfoAttr, false);
		return dagMod.connect(pDrawInfo, pDrawOverride);
	}
}

MStatus DisplayLayerScene::replaceLayers(const std::vector<DisplayLayerSpec>& specs, MDagModifier& dagMod, MObjectArray& outLayers)
{
	outLayers.clear();

	MObject oManager;
	if (!DagUtils::getMObjectFromName(kLayerManagerName, oManager))
	{
		MGlobal::displayError("The scene has no layerManager node, so display layers can't be created.");
		return MStatus::kFailure;
	}

	// Every delete is queued before any create, so the creates see the names and ids as free.
	std::vector<MObjectHandle> deletedLayers;
	for (const DisplayLayerSpec& spec : specs)
	{
		MObject oExisting;
		if (DagUtils::getMObjectFromName(spec.name, oExisting) && oExisting.hasFn(MFn::kDisplayLayer))
		{
			deletedLayers.emplace_back(oExisting);
			MStatus status = dagMod.deleteNode(oExisting);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
	}

	std::set<unsigned int> usedIds = usedLayerIds(oManager, deletedLayers);
	unsigned int nextId = 1;
	for (const DisplayLayerSpec& spec : specs)
	{
		MStatus status;
		MObject oLayer = dagMod.MDGModifier::createNode("displayLayer", &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = dagMod.renameNode(oLayer, spec.name);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		while (usedIds.count(nextId) > 0)
		{
			++nextId;
		}
		usedIds.insert(nextId);

		MFnDependencyNode fnLayer(oLayer);
		MPlug pOrder = fnLayer.findPlug(kDisplayOrderAttr, false);
		status = dagMod.newPlugValueInt(pOrder, static_cast<int>(nextId));
		CHECK_MSTATUS_AND_RETURN_IT(status);
		MPlug pId = fnLayer.findPlug(kIdentificationAttr, false);
		status = dagMod.newPlugValueInt(pId, static_cast<int>(nextId));
		CHECK_MSTATUS_AND_RETURN_IT(status);
		MPlug pManagerId = MFnDependencyNode(oManager).findPlug(kLayerIdsAttr, false).elementByLogicalIndex(nextId);
		status = dagMod.connect(pId, pManagerId);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		for (unsigned int i = 0; i < spec.members.length(); ++i)
		{
			status = queueAddMember(oLayer, spec.members[i], deletedLayers, dagMod);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
		outLayers.append(oLayer);
	}
	return MStatus::kSuccess;
}
