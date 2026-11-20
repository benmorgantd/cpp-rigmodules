#include "maya/shared/SkinBrushStroke.h"
#include "core/SkinWeightRestMatch.h"
#include "core/SurfaceBrush.h"
#include "maya/shared/Utilities.h"

#include <algorithm>
#include <cmath>

#include <maya/MFloatArray.h>
#include <maya/MFloatPoint.h>
#include <maya/MFloatPointArray.h>
#include <maya/MFloatVector.h>
#include <maya/MFnMesh.h>
#include <maya/MGlobal.h>
#include <maya/MIntArray.h>
#include <maya/MPointArray.h>

namespace
{
	// A ray this long reaches anything in a scene.
	constexpr float kFarParam = 1.0e9f;
	// Under this the falloff is too faint to be worth a write.
	constexpr double kMinimumScale = 1.0e-3;

	// The smoothing brush: a dab smooths the vertices it reaches one pass, as strongly as the falloff there allows.
	class SmoothBrushOperation : public SkinBrushOperation
	{
	public:
		MStatus bind(const MDagPath& mesh) override
		{
			return fSmooth.bind(mesh);
		}

		MStatus prepare(const std::vector<unsigned int>& vertices, const std::vector<double>& scales, double strength, double /*smoothness*/, const MeshAdjacency& adjacency) override
		{
			return fSmooth.prepare(&vertices, 1, strength, &scales, &adjacency);
		}

		MStatus apply() override
		{
			return fSmooth.apply();
		}

		const SmoothedWeights& edit() const override
		{
			return fSmooth.smoothed();
		}

	private:
		SkinWeightSmooth fSmooth;
	};

	// The rest-match brushes (D66): a dab moves the weights of the vertices it reaches towards the blend of influences
	// that puts each vertex where the rest shape wants it in the current pose. One class, two ways to say where.
	class RestBrushOperation : public SkinBrushOperation
	{
	public:
		explicit RestBrushOperation(RestTarget target)
			: fTarget(target)
		{
		}

		MStatus bind(const MDagPath& mesh) override
		{
			return fRestMatch.bind(mesh, fTarget);
		}

		MStatus refresh() override
		{
			return fRestMatch.refreshPose();
		}

		MStatus prepare(const std::vector<unsigned int>& vertices, const std::vector<double>& scales, double strength, double smoothness, const MeshAdjacency& adjacency) override
		{
			return fRestMatch.prepare(vertices, strength, smoothness, scales, adjacency);
		}

		MStatus apply() override
		{
			return fRestMatch.apply();
		}

		const SmoothedWeights& edit() const override
		{
			return fRestMatch.smoothed();
		}

	private:
		RestTarget fTarget;
		SkinWeightRestMatch fRestMatch;
	};

	struct BrushOperationEntry
	{
		const char* name;
		std::unique_ptr<SkinBrushOperation> (*create)();
	};

	const std::vector<BrushOperationEntry>& operationTable()
	{
		static const std::vector<BrushOperationEntry> table =
		{
			{ "smooth", []() -> std::unique_ptr<SkinBrushOperation> { return std::make_unique<SmoothBrushOperation>(); } },
				{ "restDelta", []() -> std::unique_ptr<SkinBrushOperation> { return std::make_unique<RestBrushOperation>(RestTarget::Delta); } },
				{ "restEdge", []() -> std::unique_ptr<SkinBrushOperation> { return std::make_unique<RestBrushOperation>(RestTarget::Edge); } },
		};
		return table;
	}
}

std::unique_ptr<SkinBrushOperation> createBrushOperation(const std::string& name)
{
	for (const BrushOperationEntry& entry : operationTable())
	{
		if (name == entry.name)
		{
			return entry.create();
		}
	}
	return nullptr;
}

const std::vector<std::string>& brushOperationNames()
{
	static const std::vector<std::string> names = []()
	{
		std::vector<std::string> result;
		for (const BrushOperationEntry& entry : operationTable())
		{
			result.push_back(entry.name);
		}
		return result;
	}();
	return names;
}

// SkinBrushEdit ----------------------------------------------------------------------------------
MStatus SkinBrushEdit::apply() const
{
	for (const MeshEdit& mesh : fMeshes)
	{
		MStatus status = mesh.rows.write(mesh.vertices, mesh.finalRows);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	return MStatus::kSuccess;
}

MStatus SkinBrushEdit::undo() const
{
	for (auto it = fMeshes.rbegin(); it != fMeshes.rend(); ++it)
	{
		MStatus status = it->rows.write(it->vertices, it->oldRows);
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	return MStatus::kSuccess;
}

bool SkinBrushEdit::empty() const
{
	return fMeshes.empty();
}

unsigned int SkinBrushEdit::vertexCount() const
{
	unsigned int count = 0;
	for (const MeshEdit& mesh : fMeshes)
	{
		count += static_cast<unsigned int>(mesh.vertices.size());
	}
	return count;
}

// SkinBrushStroke --------------------------------------------------------------------------------
// Everything kept per mesh for the length of a stroke. Held by pointer because the field and the accelerator point
// into it.
struct SkinBrushStroke::MeshState
{
	MDagPath path;                          // as the caller gave it, which names the mesh in a dab
	MDagPath shape;
	std::unique_ptr<MFnMesh> fnMesh;        // kept: Maya caches the ray accelerator on the function set
	std::unique_ptr<SkinBrushOperation> operation;
	SkinWeightRows rows;
	MeshAdjacency adjacency;
	std::vector<float> positions;           // world, x y z per vertex
	MMeshIsectAccelParams accel;
	SurfaceDistance field;
	StrokeJournal journal;
	std::vector<unsigned int> allowed;
	bool isRestricted = false;

	MStatus readPositions()
	{
		MPointArray points;
		MStatus status = fnMesh->getPoints(points, MSpace::kWorld);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		positions.resize(3 * static_cast<std::size_t>(points.length()));
		for (unsigned int i = 0; i < points.length(); ++i)
		{
			positions[3 * i] = static_cast<float>(points[i].x);
			positions[3 * i + 1] = static_cast<float>(points[i].y);
			positions[3 * i + 2] = static_cast<float>(points[i].z);
		}
		return MStatus::kSuccess;
	}
};

SkinBrushStroke::SkinBrushStroke()
{
}

SkinBrushStroke::~SkinBrushStroke()
{
}

MStatus SkinBrushStroke::begin(const MDagPathArray& meshes, const std::string& operation)
{
	fMeshes.clear();
	fDabs.clear();
	if (!createBrushOperation(operation))
	{
		MGlobal::displayError(MString("Unknown brush operation: ") + operation.c_str());
		return MStatus::kInvalidParameter;
	}
	for (unsigned int i = 0; i < meshes.length(); ++i)
	{
		auto state = std::make_unique<MeshState>();
		state->path = meshes[i];
		MStatus status = MeshUtils::getMeshShape(meshes[i], state->shape);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = state->rows.bind(meshes[i]);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		state->operation = createBrushOperation(operation);
		status = state->operation->bind(meshes[i]);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		state->fnMesh = std::make_unique<MFnMesh>(state->shape, &status);
		CHECK_MSTATUS_AND_RETURN_IT(status);

		MIntArray polygonCounts;
		MIntArray polygonConnects;
		status = state->fnMesh->getVertices(polygonCounts, polygonConnects);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		std::vector<int> counts(polygonCounts.length());
		polygonCounts.get(counts.data());
		std::vector<int> connects(polygonConnects.length());
		polygonConnects.get(connects.data());
		state->adjacency = buildMeshAdjacency(counts, connects, state->rows.vertexCount());

		state->accel = MFnMesh::autoUniformGridParams();
		status = state->readPositions();
		CHECK_MSTATUS_AND_RETURN_IT(status);
		state->field.bind(state->adjacency, state->positions);
		state->journal.reset(state->rows.influenceCount());
		fMeshes.push_back(std::move(state));
	}
	return MStatus::kSuccess;
}

MStatus SkinBrushStroke::refresh()
{
	for (const std::unique_ptr<MeshState>& mesh : fMeshes)
	{
		mesh->fnMesh->freeCachedIntersectionAccelerator();
		MStatus status = mesh->readPositions();
		CHECK_MSTATUS_AND_RETURN_IT(status);
		status = mesh->operation->refresh();
		CHECK_MSTATUS_AND_RETURN_IT(status);
	}
	return MStatus::kSuccess;
}

void SkinBrushStroke::restrict(unsigned int mesh, std::vector<unsigned int> sortedVertices)
{
	if (mesh < fMeshes.size())
	{
		fMeshes[mesh]->allowed = std::move(sortedVertices);
		fMeshes[mesh]->isRestricted = true;
	}
}

unsigned int SkinBrushStroke::meshCount() const
{
	return static_cast<unsigned int>(fMeshes.size());
}

std::string SkinBrushStroke::meshName(unsigned int mesh) const
{
	return mesh < fMeshes.size() ? fMeshes[mesh]->path.fullPathName().asChar() : std::string();
}

MPoint SkinBrushStroke::vertexPosition(unsigned int mesh, unsigned int vertex) const
{
	const float* p = fMeshes[mesh]->positions.data() + 3 * static_cast<std::size_t>(vertex);
	return MPoint(p[0], p[1], p[2]);
}

double SkinBrushStroke::boundsDiagonal() const
{
	double low[3] = { 0.0, 0.0, 0.0 };
	double high[3] = { 0.0, 0.0, 0.0 };
	bool isFirst = true;
	for (const std::unique_ptr<MeshState>& mesh : fMeshes)
	{
		const std::vector<float>& positions = mesh->positions;
		for (std::size_t i = 0; i + 2 < positions.size(); i += 3)
		{
			for (int axis = 0; axis < 3; ++axis)
			{
				const double value = positions[i + axis];
				low[axis] = isFirst ? value : std::min(low[axis], value);
				high[axis] = isFirst ? value : std::max(high[axis], value);
			}
			isFirst = false;
		}
	}
	return std::sqrt((high[0] - low[0]) * (high[0] - low[0]) + (high[1] - low[1]) * (high[1] - low[1]) + (high[2] - low[2]) * (high[2] - low[2]));
}

void SkinBrushStroke::pick(const MPoint& origin, const MVector& direction, bool throughMesh, std::vector<BrushHit>& hits)
{
	hits.clear();
	const MVector ray = direction.normal();
	const MFloatPoint source(origin);
	const MFloatVector rayDirection(ray);

	const auto addHit = [&hits, &ray](unsigned int meshIndex, MeshState& mesh, const MFloatPoint& point, float param, int face)
	{
		BrushHit hit;
		hit.mesh = meshIndex;
		hit.face = face;
		hit.point = MPoint(point);
		hit.distance = param;
		if (mesh.fnMesh->getPolygonNormal(face, hit.normal, MSpace::kWorld))
		{
			hit.normal = hit.normal * ray > 0.0 ? -hit.normal : hit.normal;
		}
		hits.push_back(hit);
	};

	for (unsigned int m = 0; m < fMeshes.size(); ++m)
	{
		MeshState& mesh = *fMeshes[m];
		if (throughMesh)
		{
			MFloatPointArray points;
			MFloatArray params;
			MIntArray faces;
			if (mesh.fnMesh->allIntersections(source, rayDirection, nullptr, nullptr, false, MSpace::kWorld, kFarParam, false, &mesh.accel, false, points, &params, &faces, nullptr, nullptr, nullptr))
			{
				for (unsigned int i = 0; i < points.length(); ++i)
				{
					addHit(m, mesh, points[i], params[i], faces[i]);
				}
			}
			continue;
		}
		MFloatPoint point;
		float param = 0.0f;
		int face = -1;
		if (mesh.fnMesh->closestIntersection(source, rayDirection, nullptr, nullptr, false, MSpace::kWorld, kFarParam, false, &mesh.accel, point, &param, &face, nullptr, nullptr, nullptr))
		{
			addHit(m, mesh, point, param, face);
		}
	}

	std::sort(hits.begin(), hits.end(), [](const BrushHit& a, const BrushHit& b)
	{
		return a.distance < b.distance;
	});
	if (!throughMesh && hits.size() > 1)
	{
		hits.resize(1);
	}
}

void SkinBrushStroke::reach(const std::vector<BrushHit>& hits, double radius, FalloffMode mode, std::vector<BrushReach>& reaches)
{
	reaches.clear();
	if (!(radius > 0.0))
	{
		return;
	}
	for (unsigned int m = 0; m < fMeshes.size(); ++m)
	{
		MeshState& mesh = *fMeshes[m];
		std::vector<SurfaceSeed> seeds;
		std::vector<Point3> centers;
		MIntArray corners;
		for (const BrushHit& hit : hits)
		{
			if (hit.mesh != m)
			{
				continue;
			}
			if (mode == FalloffMode::Volume)
			{
				centers.push_back({ hit.point.x, hit.point.y, hit.point.z });
				continue;
			}
			if (!mesh.fnMesh->getPolygonVertices(hit.face, corners))
			{
				continue;
			}
			for (unsigned int c = 0; c < corners.length(); ++c)
			{
				const MPoint corner = vertexPosition(m, static_cast<unsigned int>(corners[c]));
				seeds.push_back({ static_cast<unsigned int>(corners[c]), corner.distanceTo(hit.point) });
			}
		}
		if (seeds.empty() && centers.empty())
		{
			continue;
		}

		std::vector<unsigned int> volumeVertices;
		std::vector<double> volumeDistances;
		if (mode == FalloffMode::Volume)
		{
			volumeReach(mesh.positions, centers, radius, volumeVertices, volumeDistances);
		}
		else
		{
			mesh.field.reach(seeds, radius);
		}
		BrushReach found;
		found.mesh = m;
		const std::vector<unsigned int>& vertices = mode == FalloffMode::Volume ? volumeVertices : mesh.field.vertices();
		const std::vector<double>& distances = mode == FalloffMode::Volume ? volumeDistances : mesh.field.distances();
		for (std::size_t i = 0; i < vertices.size(); ++i)
		{
			const double scale = brushFalloff(distances[i] / radius);
			if (scale < kMinimumScale)
			{
				continue;
			}
			if (mesh.isRestricted && !std::binary_search(mesh.allowed.begin(), mesh.allowed.end(), vertices[i]))
			{
				continue;
			}
			found.vertices.push_back(vertices[i]);
			found.scales.push_back(scale);
		}
		if (!found.vertices.empty())
		{
			reaches.push_back(std::move(found));
		}
	}
}

MStatus SkinBrushStroke::dab(const std::vector<BrushHit>& hits, double radius, double strength, double smoothness, FalloffMode mode)
{
	if (!(radius > 0.0) || !(strength > 0.0 && strength <= 1.0) || !(smoothness >= 0.0 && smoothness <= 1.0))
	{
		MGlobal::displayError("A brush needs a radius above 0, a strength above 0 and at most 1 and a smoothness from 0 to 1.");
		return MStatus::kInvalidParameter;
	}
	std::vector<BrushReach> reaches;
	reach(hits, radius, mode, reaches);
	fLastReached = 0;
	fLastChanged = 0;
	for (const BrushReach& found : reaches)
	{
		fLastReached += static_cast<unsigned int>(found.vertices.size());
		MeshState& mesh = *fMeshes[found.mesh];
		MStatus status = mesh.operation->prepare(found.vertices, found.scales, strength, smoothness, mesh.adjacency);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		fLastChanged += static_cast<unsigned int>(mesh.operation->edit().vertices.size());
		if (mesh.operation->edit().vertices.empty())
		{
			continue;
		}
		status = mesh.operation->apply();
		CHECK_MSTATUS_AND_RETURN_IT(status);
		mesh.journal.record(mesh.operation->edit());
	}
	for (const BrushHit& hit : hits)
	{
		BrushDab record;
		record.mesh = meshName(hit.mesh);
		record.face = hit.face;
		record.x = hit.point.x;
		record.y = hit.point.y;
		record.z = hit.point.z;
		fDabs.push_back(record);
	}
	return MStatus::kSuccess;
}

unsigned int SkinBrushStroke::lastReachedCount() const
{
	return fLastReached;
}

unsigned int SkinBrushStroke::lastChangedCount() const
{
	return fLastChanged;
}

MStatus SkinBrushStroke::finish(SkinBrushEdit& edit)
{
	edit = SkinBrushEdit();
	for (const std::unique_ptr<MeshState>& mesh : fMeshes)
	{
		if (mesh->journal.empty())
		{
			continue;
		}
		SkinBrushEdit::MeshEdit meshEdit;
		meshEdit.rows = mesh->rows;
		meshEdit.vertices = mesh->journal.sortedVertices();
		MStatus status = mesh->rows.read(meshEdit.vertices, meshEdit.finalRows);
		CHECK_MSTATUS_AND_RETURN_IT(status);
		meshEdit.oldRows = mesh->journal.oldRows(meshEdit.vertices, meshEdit.finalRows);
		edit.fMeshes.push_back(std::move(meshEdit));
		mesh->journal.reset(mesh->rows.influenceCount());
	}
	edit.dabs = std::move(fDabs);
	fDabs.clear();
	return MStatus::kSuccess;
}
