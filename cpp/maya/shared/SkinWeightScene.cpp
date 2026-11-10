#include "maya/shared/SkinWeightScene.h"
#include "core/Constants.h"
#include "core/SkinWeightTable.h"
#include "maya/shared/JointMarkupScene.h"
#include "maya/shared/RigTraversal.h"
#include "maya/shared/Utilities.h"

#include <set>
#include <string>
#include <utility>

#include <maya/MFnDependencyNode.h>
#include <maya/MFnMatrixData.h>
#include <maya/MFnMesh.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MFnSkinCluster.h>
#include <maya/MGlobal.h>
#include <maya/MItDag.h>
#include <maya/MMatrix.h>
#include <maya/MNamespace.h>
#include <maya/MPlug.h>
#include <maya/MUuid.h>

namespace
{
	// The skinCluster nearest the mesh shape in its history, or a null object.
	MObject nearestSkinCluster(const MDagPath& shape)
	{
		return DagUtils::findUpstreamNode(shape.node(), MFn::kSkinClusterFilter);
	}

	MStatus readTopology(const MDagPath& shape, unsigned int& outVertexCount, unsigned int& outFaceCount, std::uint64_t& outHash)
	{
		MStatus status;
		MFnMesh fnMesh(shape, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		MIntArray polygonCounts;
		MIntArray polygonConnects;
		status = fnMesh.getVertices(polygonCounts, polygonConnects);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		std::vector<int> counts(polygonCounts.length());
		polygonCounts.get(counts.data());
		std::vector<int> connects(polygonConnects.length());
		polygonConnects.get(connects.data());

		outVertexCount = static_cast<unsigned int>(fnMesh.numVertices());
		outFaceCount = static_cast<unsigned int>(fnMesh.numPolygons());
		outHash = topologyHash(outVertexCount, counts, connects);
		return MStatus::kSuccess;
	}

	// Turns the skinCluster's normalizeWeights off for its lifetime, then puts it back. With interactive
	// normalization on, setWeights normalizes partway through a vertex and warns, though the values it is given
	// already sum to one. The plug is set and restored directly, so the scene ends unchanged and needs no undo.
	class ScopedNormalizeOff
	{
	public:
		explicit ScopedNormalizeOff(const MFnSkinCluster& fnSkin)
			: fPlug(fnSkin.findPlug("normalizeWeights", false))
		{
			fOriginal = fPlug.asShort();
			fPlug.setShort(0);
		}

		~ScopedNormalizeOff()
		{
			fPlug.setShort(fOriginal);
		}

	private:
		MPlug fPlug;
		short fOriginal = 0;
	};

	// The physical index of each path in the skinCluster's influence list, or false if one is missing.
	bool influenceIndices(const MFnSkinCluster& fnSkin, const MDagPathArray& influences, MIntArray& outIndices)
	{
		MDagPathArray clusterInfluences;
		fnSkin.influenceObjects(clusterInfluences);
		outIndices.clear();
		for (unsigned int i = 0; i < influences.length(); ++i)
		{
			int found = -1;
			for (unsigned int c = 0; c < clusterInfluences.length() && found < 0; ++c)
			{
				if (clusterInfluences[c] == influences[i])
				{
					found = static_cast<int>(c);
				}
			}
			if (found < 0)
			{
				return false;
			}
			outIndices.append(found);
		}
		return true;
	}
}

MStatus SkinWeightScene::getMeshId(const MDagPath& mesh, std::string& outId)
{
	MStatus status;
	MFnDependencyNode fnMesh(mesh.node(), &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	outId.clear();
	if (!fnMesh.hasAttribute(RigAttr::MeshId))
	{
		return MStatus::kSuccess;
	}
	const MString id = fnMesh.findPlug(RigAttr::MeshId, false, &status).asString();
	CHECK_MSTATUS_AND_RETURN_IT(status);
	outId = id.asChar();
	return MStatus::kSuccess;
}

MStatus SkinWeightScene::queueNewMeshId(const MDagPath& mesh, MDGModifier& dgMod, std::string& outId)
{
	MUuid uuid;
	MStatus status = uuid.generate();
	CHECK_MSTATUS_AND_RETURN_IT(status);
	outId = uuid.asString().asChar();

	// MEL keeps the attribute add and the value set in one undoable modifier: a plug for an attribute the
	// modifier adds doesn't exist until it runs.
	const MString node = MelUtils::quoted(mesh.fullPathName());
	if (!MFnDependencyNode(mesh.node()).hasAttribute(RigAttr::MeshId))
	{
		status = dgMod.commandToExecute(MString("addAttr -longName ") + RigAttr::MeshId + " -dataType \"string\" " + node);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	return dgMod.commandToExecute(MString("setAttr -type \"string\" ") + MelUtils::quoted(mesh.fullPathName() + "." + RigAttr::MeshId)
		+ " " + MelUtils::quoted(outId.c_str()));
}

MStatus SkinWeightScene::readSkinWeights(const MDagPath& mesh, SkinWeights& outWeights)
{
	MStatus status;
	const MString meshName = mesh.partialPathName();
	status = getMeshId(mesh, outWeights.meshId);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	if (outWeights.meshId.empty())
	{
		MGlobal::displayError(meshName + " has no meshId. Give it one with setRigMeshId.");
		return MStatus::kFailure;
	}

	// Two meshes with one id in one namespace would load each other's weights, so the duplicate has to get a new id
	// first. Another namespace may hold the same mesh legitimately (a second instance of the asset). MItDag over mesh
	// shapes visits only meshes, and the id lives on each shape's parent transform.
	const MString meshNamespace = DagUtils::getAbsoluteNamespace(mesh.node());
	for (MItDag itDag(MItDag::kDepthFirst, MFn::kMesh, &status); status && !itDag.isDone(); itDag.next())
	{
		MDagPath otherPath;
		itDag.getPath(otherPath);
		otherPath.pop();
		if (otherPath.node() == mesh.node() || DagUtils::getAbsoluteNamespace(otherPath.node()) != meshNamespace)
		{
			continue;
		}
		std::string otherId;
		if (getMeshId(otherPath, otherId) && otherId == outWeights.meshId)
		{
			MGlobal::displayError(meshName + " and " + otherPath.partialPathName() + " share meshId " + otherId.c_str()
				+ ". Give the duplicate a new one with setRigMeshId(newId=True).");
			return MStatus::kFailure;
		}
	}

	MDagPath shape;
	status = MeshUtils::getMeshShape(mesh, shape);
	if (!status)
	{
		return status;
	}
	MObject oSkinCluster = nearestSkinCluster(shape);
	if (oSkinCluster.isNull())
	{
		MGlobal::displayError(meshName + " has no skinCluster.");
		return MStatus::kFailure;
	}
	MFnSkinCluster fnSkin(oSkinCluster, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	MDagPathArray influences;
	fnSkin.influenceObjects(influences, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	bool hasErrors = false;
	std::set<std::pair<std::string, std::string>> seen;   // (side, usage)
	outWeights.influences.clear();
	for (unsigned int i = 0; i < influences.length(); ++i)
	{
		bool hasMarkup = false;
		JointMarkup markup;
		const MString influenceName = influences[i].partialPathName();
		if (!JointMarkupScene::readMarkup(influences[i].node(), hasMarkup, markup) || !hasMarkup)
		{
			MGlobal::displayError(MString("Influence ") + influenceName + " has no joint markup. Mark it with rigJointMarkup.");
			hasErrors = true;
			continue;
		}
		if (!markup.isSkinned)
		{
			MGlobal::displayError(MString("Influence ") + influenceName + " is marked skin: false.");
			hasErrors = true;
		}
		if (!seen.insert({ markup.side, markup.usage }).second)
		{
			MGlobal::displayError(MString("Two influences share the markup ") + formatJointSelector({ markup.side, markup.usage }).c_str() + ".");
			hasErrors = true;
		}
		outWeights.influences.push_back({ markup.side, markup.usage });
	}
	if (hasErrors)
	{
		MGlobal::displayError(meshName + "'s weights were not saved.");
		return MStatus::kFailure;
	}

	status = readTopology(shape, outWeights.vertexCount, outWeights.faceCount, outWeights.topologyHash);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	outWeights.meshName = MNamespace::stripNamespaceFromName(MFnDependencyNode(mesh.node()).name()).asChar();

	const MObject oVertices = MeshUtils::createAllVerticesComponent(outWeights.vertexCount);
	MDoubleArray weights;
	unsigned int influenceCount = 0;
	status = fnSkin.getWeights(shape, oVertices, weights, influenceCount);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// Copied out once: MDoubleArray's operator[] is an exported call per element.
	std::vector<double> table(weights.length());
	weights.get(table.data());
	outWeights.weights.assign(outWeights.vertexCount, {});
	for (unsigned int v = 0; v < outWeights.vertexCount; ++v)
	{
		for (unsigned int i = 0; i < influenceCount; ++i)
		{
			const double weight = table[static_cast<std::size_t>(v) * influenceCount + i];
			if (weight > 0.0)
			{
				outWeights.weights[v].emplace_back(i, weight);
			}
		}
	}

	const short methodIndex = fnSkin.findPlug("skinningMethod", false, &status).asShort();
	CHECK_MSTATUS_AND_RETURN_IT(status);
	if (methodIndex < 0 || methodIndex >= static_cast<short>(SkinMethods::All.size()))
	{
		MGlobal::displayError(fnSkin.name() + " has an unknown skinningMethod.");
		return MStatus::kFailure;
	}
	outWeights.skinMethod = SkinMethods::All[methodIndex];

	outWeights.blendWeights.clear();
	if (outWeights.skinMethod == SkinMethods::WeightBlended)
	{
		MDoubleArray blend;
		status = fnSkin.getBlendWeights(shape, oVertices, blend);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		for (unsigned int v = 0; v < blend.length(); ++v)
		{
			outWeights.blendWeights.push_back(blend[v]);
		}
	}

	// Saving never changes a weight, so unnormalized weights are an error for the rigger to fix, not something to
	// normalize here. Pruning and normalizing are separate tools.
	const std::vector<unsigned int> unnormalized = findUnnormalizedVertices(outWeights);
	if (!unnormalized.empty())
	{
		MGlobal::displayError(meshName + ": " + describeUnnormalizedVertices(outWeights, unnormalized).c_str() + ". Normalize them, then save again.");
		return MStatus::kFailure;
	}

	sortSkinInfluences(outWeights);
	return MStatus::kSuccess;
}

MStatus SkinWeightLoad::prepare(const MDagPath& mesh, const SkinWeights& weights)
{
	MStatus status;
	fMesh = mesh;
	fWeights = weights;
	const MString meshName = mesh.partialPathName();
	const auto fail = [&meshName]()
	{
		MGlobal::displayError(meshName + ": the weight file doesn't match, so nothing was loaded.");
		return MStatus::kFailure;
	};

	std::string meshId;
	status = SkinWeightScene::getMeshId(mesh, meshId);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	if (meshId.empty())
	{
		MGlobal::displayError(meshName + " has no meshId. Give it one with setRigMeshId.");
		return fail();
	}
	if (meshId != weights.meshId)
	{
		MGlobal::displayError(MString("The file is for meshId ") + weights.meshId.c_str() + " ('" + weights.meshName.c_str()
			+ "'), and " + meshName + " has " + meshId.c_str() + ".");
		return fail();
	}

	MDagPath shape;
	status = MeshUtils::getMeshShape(mesh, shape);
	if (!status)
	{
		return status;
	}
	unsigned int vertexCount = 0;
	unsigned int faceCount = 0;
	std::uint64_t hash = 0;
	status = readTopology(shape, vertexCount, faceCount, hash);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	if (vertexCount != weights.vertexCount || faceCount != weights.faceCount || hash != weights.topologyHash)
	{
		const std::string saved = std::to_string(weights.vertexCount) + " vertices and " + std::to_string(weights.faceCount)
			+ " faces (" + formatTopologyHash(weights.topologyHash) + ")";
		const std::string scene = std::to_string(vertexCount) + " and " + std::to_string(faceCount) + " (" + formatTopologyHash(hash) + ")";
		MGlobal::displayError(MString("The topology changed: the file has ") + saved.c_str() + ", " + meshName + " has " + scene.c_str() + ".");
		return fail();
	}

	const MString meshNamespace = DagUtils::getAbsoluteNamespace(mesh.node());
	MarkedJointSet joints;
	ValidationReport jointReport;
	JointMarkupScene::collectNamespaceJoints(meshNamespace, joints, jointReport);
	bool hasErrors = jointReport.hasErrors();
	for (const std::string& error : jointReport.errors)
	{
		MGlobal::displayError(MString("Namespace ") + meshNamespace + ": " + error.c_str());
	}

	fInfluences.clear();
	for (const JointSelector& influence : weights.influences)
	{
		std::size_t entry = 0;
		if (!joints.index.find(influence, entry))
		{
			MGlobal::displayError(MString("No joint in namespace ") + meshNamespace + " is marked " + formatJointSelector(influence).c_str() + ".");
			hasErrors = true;
			continue;
		}
		fInfluences.append(joints.dagPaths[static_cast<unsigned int>(entry)]);
	}
	if (hasErrors)
	{
		return fail();
	}

	// Queue the influence changes. MEL, because the API can't create a skinCluster or edit its influence list.
	MObject oExisting = nearestSkinCluster(shape);
	fCreatesSkinCluster = oExisting.isNull();
	fBindAtRest.assign(fInfluences.length(), fCreatesSkinCluster);
	const int methodIndex = SkinMethods::indexOf(weights.skinMethod);
	if (fCreatesSkinCluster)
	{
		MString command = MString("skinCluster -toSelectedBones -bindMethod 0 -normalizeWeights 1 -obeyMaxInfluences false -skinMethod ")
			+ std::to_string(methodIndex).c_str();
		for (unsigned int i = 0; i < fInfluences.length(); ++i)
		{
			command += " " + MelUtils::quoted(fInfluences[i].fullPathName());
		}
		command += " " + MelUtils::quoted(mesh.fullPathName());
		return fInfluenceMod.commandToExecute(command);
	}

	MFnSkinCluster fnSkin(oExisting, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	const MString cluster = MelUtils::quoted(fnSkin.name());
	MDagPathArray clusterInfluences;
	fnSkin.influenceObjects(clusterInfluences);
	for (unsigned int i = 0; i < fInfluences.length(); ++i)
	{
		bool isPresent = false;
		for (unsigned int c = 0; c < clusterInfluences.length() && !isPresent; ++c)
		{
			isPresent = clusterInfluences[c] == fInfluences[i];
		}
		if (!isPresent)
		{
			fBindAtRest[i] = true;
			status = fInfluenceMod.commandToExecute(MString("skinCluster -edit -addInfluence ") + MelUtils::quoted(fInfluences[i].fullPathName())
				+ " -weight 0 " + cluster);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
	}
	for (unsigned int c = 0; c < clusterInfluences.length(); ++c)
	{
		bool isListed = false;
		for (unsigned int i = 0; i < fInfluences.length() && !isListed; ++i)
		{
			isListed = clusterInfluences[c] == fInfluences[i];
		}
		if (!isListed)
		{
			status = fInfluenceMod.commandToExecute(MString("skinCluster -edit -removeInfluence ") + MelUtils::quoted(clusterInfluences[c].fullPathName())
				+ " " + cluster);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
	}
	return MStatus::kSuccess;
}

MStatus SkinWeightLoad::findSkinCluster(MObject& oOutSkinCluster) const
{
	MDagPath shape;
	MStatus status = MeshUtils::getMeshShape(fMesh, shape);
	if (!status)
	{
		return status;
	}
	oOutSkinCluster = nearestSkinCluster(shape);
	if (oOutSkinCluster.isNull())
	{
		MGlobal::displayError(fMesh.partialPathName() + " has no skinCluster after the influence changes.");
		return MStatus::kFailure;
	}
	return MStatus::kSuccess;
}

MStatus SkinWeightLoad::apply()
{
	fSkinCluster = MObject::kNullObj;
	fSettingsMod.reset();
	fOldWeights.clear();
	fOldBlendWeights.clear();
	MStatus status = fInfluenceMod.doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	status = findSkinCluster(fSkinCluster);
	if (!status)
	{
		return status;
	}
	MFnSkinCluster fnSkin(fSkinCluster, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	if (!influenceIndices(fnSkin, fInfluences, fInfluenceIndices))
	{
		MGlobal::displayError(fnSkin.name() + " is missing an influence after the influence changes.");
		return MStatus::kFailure;
	}

	// Bind new influences at rest and set the skinning method, through a modifier so undo restores both.
	fSettingsMod = std::make_unique<MDGModifier>();
	MPlug pBindPreMatrix = fnSkin.findPlug("bindPreMatrix", false, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	for (unsigned int i = 0; i < fInfluences.length(); ++i)
	{
		if (!fBindAtRest[i])
		{
			continue;
		}
		MMatrix mRestWorld;
		status = RigTraversal::getJointRestWorld(fInfluences[i], mRestWorld);
		if (!status)
		{
			return status;
		}
		MFnMatrixData fnMatrixData;
		MObject oMatrixData = fnMatrixData.create(mRestWorld.inverse(), &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		const unsigned int logicalIndex = fnSkin.indexForInfluenceObject(fInfluences[i], &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		MPlug pElement = pBindPreMatrix.elementByLogicalIndex(logicalIndex, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = fSettingsMod->newPlugValue(pElement, oMatrixData);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	MPlug pSkinningMethod = fnSkin.findPlug("skinningMethod", false, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	status = fSettingsMod->newPlugValueShort(pSkinningMethod, static_cast<short>(SkinMethods::indexOf(fWeights.skinMethod)));
	CHECK_MSTATUS_AND_RETURN_IT(status);
	status = fSettingsMod->doIt();
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// Every influence gets a value on every vertex, exactly as the file has it (0 where the file leaves one out), so
	// nothing keeps an old weight. The file was checked as normalized, so nothing is rescaled.
	MDagPath shape;
	status = MeshUtils::getMeshShape(fMesh, shape);
	if (!status)
	{
		return status;
	}
	// The whole table, built in a plain array (MDoubleArray's operator[] is an exported call per element), then
	// written for every vertex and every influence in one setWeights call.
	const unsigned int influenceCount = fInfluences.length();
	std::vector<double> table(static_cast<std::size_t>(fWeights.vertexCount) * influenceCount, 0.0);
	for (unsigned int v = 0; v < fWeights.vertexCount; ++v)
	{
		for (const auto& [index, weight] : fWeights.weights[v])
		{
			table[static_cast<std::size_t>(v) * influenceCount + index] = weight;
		}
	}
	MDoubleArray values(table.data(), static_cast<unsigned int>(table.size()));
	const MObject oVertices = MeshUtils::createAllVerticesComponent(fWeights.vertexCount);
	{
		// A skinCluster this load created goes away on undo, so its old weights aren't needed. Skipping the copy
		// is about 15% of the setWeights time on a dense mesh.
		ScopedNormalizeOff normalizeOff(fnSkin);
		status = fnSkin.setWeights(shape, oVertices, fInfluenceIndices, values, false, fCreatesSkinCluster ? nullptr : &fOldWeights);
	}
	CHECK_MSTATUS_AND_RETURN_IT(status);

	if (!fWeights.blendWeights.empty())
	{
		status = fnSkin.getBlendWeights(shape, oVertices, fOldBlendWeights);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		MDoubleArray blend(static_cast<unsigned int>(fWeights.blendWeights.size()));
		for (unsigned int v = 0; v < blend.length(); ++v)
		{
			blend[v] = fWeights.blendWeights[v];
		}
		status = fnSkin.setBlendWeights(shape, oVertices, blend);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	return MStatus::kSuccess;
}

MStatus SkinWeightLoad::undo()
{
	MStatus status;
	// A skinCluster this load created goes away with the influence modifier, weights and all.
	if (!fCreatesSkinCluster && !fSkinCluster.isNull() && fOldWeights.length() != 0)
	{
		MFnSkinCluster fnSkin(fSkinCluster, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		MDagPath shape;
		status = MeshUtils::getMeshShape(fMesh, shape);
		if (!status)
		{
			return status;
		}
		const MObject oVertices = MeshUtils::createAllVerticesComponent(fWeights.vertexCount);
		{
			ScopedNormalizeOff normalizeOff(fnSkin);
			status = fnSkin.setWeights(shape, oVertices, fInfluenceIndices, fOldWeights, false);
		}
		CHECK_MSTATUS_AND_RETURN_IT(status);
		if (fOldBlendWeights.length() != 0)
		{
			status = fnSkin.setBlendWeights(shape, oVertices, fOldBlendWeights);
			CHECK_MSTATUS_AND_RETURN_IT(status);
		}
	}
	if (fSettingsMod)
	{
		status = fSettingsMod->undoIt();
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	return fInfluenceMod.undoIt();
}

MString SkinWeightLoad::skinClusterName() const
{
	return fSkinCluster.isNull() ? MString() : MFnDependencyNode(fSkinCluster).name();
}

MStatus SkinWeightPrune::prepare(const MDagPath& mesh, unsigned int maxInfluences)
{
	MStatus status = MeshUtils::getMeshShape(mesh, fShape);
	if (!status)
	{
		return status;
	}
	fSkinCluster = nearestSkinCluster(fShape);
	if (fSkinCluster.isNull())
	{
		MGlobal::displayError(mesh.partialPathName() + " has no skinCluster.");
		return MStatus::kFailure;
	}
	MFnSkinCluster fnSkin(fSkinCluster, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MFnMesh fnMesh(fShape, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// The whole table in one call: every vertex, every influence.
	MDoubleArray weights;
	unsigned int influenceCount = 0;
	status = fnSkin.getWeights(fShape, MeshUtils::createAllVerticesComponent(static_cast<unsigned int>(fnMesh.numVertices())), weights, influenceCount);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	// Work in plain arrays: MDoubleArray's operator[] is an exported call per element.
	std::vector<double> original(weights.length());
	weights.get(original.data());
	std::vector<double> table = original;
	const std::vector<unsigned int> changed = pruneToMaxInfluences(table, influenceCount, maxInfluences);
	fChangedCount = static_cast<unsigned int>(changed.size());

	// Only the changed rows are written back, old and new, as one component.
	std::vector<int> changedIndices(changed.begin(), changed.end());
	std::vector<double> newRows;
	std::vector<double> oldRows;
	newRows.reserve(changed.size() * influenceCount);
	oldRows.reserve(changed.size() * influenceCount);
	for (const unsigned int row : changed)
	{
		const std::size_t start = static_cast<std::size_t>(row) * influenceCount;
		newRows.insert(newRows.end(), table.begin() + start, table.begin() + start + influenceCount);
		oldRows.insert(oldRows.end(), original.begin() + start, original.begin() + start + influenceCount);
	}
	fNewValues = MDoubleArray(newRows.data(), static_cast<unsigned int>(newRows.size()));
	fOldValues = MDoubleArray(oldRows.data(), static_cast<unsigned int>(oldRows.size()));

	MFnSingleIndexedComponent fnComponent;
	fChangedVertices = fnComponent.create(MFn::kMeshVertComponent, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	MIntArray changedVertexIndices(changedIndices.data(), fChangedCount);
	status = fnComponent.addElements(changedVertexIndices);
	CHECK_MSTATUS_AND_RETURN_IT(status);

	fInfluenceIndices.setLength(influenceCount);
	for (unsigned int i = 0; i < influenceCount; ++i)
	{
		fInfluenceIndices[i] = static_cast<int>(i);
	}
	return MStatus::kSuccess;
}

MStatus SkinWeightPrune::write(MDoubleArray& values)
{
	if (fChangedCount == 0)
	{
		return MStatus::kSuccess;
	}
	MStatus status;
	MFnSkinCluster fnSkin(fSkinCluster, &status);
	CHECK_MSTATUS_AND_RETURN_IT(status);
	ScopedNormalizeOff normalizeOff(fnSkin);
	return fnSkin.setWeights(fShape, fChangedVertices, fInfluenceIndices, values, false);
}

MStatus SkinWeightPrune::apply()
{
	return write(fNewValues);
}

MStatus SkinWeightPrune::undo()
{
	return write(fOldValues);
}

unsigned int SkinWeightPrune::changedVertexCount() const
{
	return fChangedCount;
}
