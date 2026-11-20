#pragma once

#include "core/SkinWeightSmooth.h"
#include "core/SurfaceBrush.h"
#include "maya/shared/SkinWeightScene.h"

#include <memory>
#include <string>
#include <vector>

#include <maya/MDagPath.h>
#include <maya/MDagPathArray.h>
#include <maya/MPoint.h>
#include <maya/MStatus.h>
#include <maya/MVector.h>

// The engine under skin weight brushes (D65). It knows nothing about the mouse or the viewport: a context picks hits and
// hands them over, and a script can hand over hits too, which is how it is tested. A brush is one SkinBrushOperation
// (what a dab does to the weights under it) plus this engine (where a dab lands, how far it reaches over the surface
// and how a whole stroke undoes). To add a brush, write an operation and add it to createBrushOperation: the picking,
// the falloff, the visualizer, the undo and both commands come with it.

// What a dab does to the weights of some vertices of one bound mesh. It works the new weights out without writing
// them, so the stroke can journal them; apply then writes them.
class SkinBrushOperation
{
public:
	virtual ~SkinBrushOperation() = default;
	virtual MStatus bind(const MDagPath& mesh) = 0;
	// The pose changed between strokes: read again whatever the operation keeps of it. Most operations keep none.
	virtual MStatus refresh()
	{
		return MStatus::kSuccess;
	}
	// vertices: sorted and unique. scales: parallel, each in (0, 1], the brush falloff there. strength: in (0, 1].
	// smoothness: in [0, 1], used by the operations that solve a change per vertex (the rest-match ones, D66) and
	// ignored by the rest. adjacency is the mesh's neighbor map, built once per stroke.
	virtual MStatus prepare(const std::vector<unsigned int>& vertices, const std::vector<double>& scales, double strength, double smoothness, const MeshAdjacency& adjacency) = 0;
	// Writes what prepare worked out.
	virtual MStatus apply() = 0;
	// The rows prepare changed: only influences and vertices that moved.
	virtual const SmoothedWeights& edit() const = 0;
};

// Null for a name that is not a brush operation.
std::unique_ptr<SkinBrushOperation> createBrushOperation(const std::string& name);
const std::vector<std::string>& brushOperationNames();

// Where the brush touched a mesh: the face and the point on it. mesh indexes the meshes the stroke was begun with.
struct BrushHit
{
	unsigned int mesh = 0;
	int face = -1;
	MPoint point;
	MVector normal;        // the face's, turned towards the viewer
	double distance = 0.0; // along the ray, for ordering
};

// A hit by name, which is what a script or a journal line needs to repeat it.
struct BrushDab
{
	std::string mesh;
	int face = -1;
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
};

// What the brush would affect on one mesh: the vertices and how strongly (the falloff, 0 to 1).
struct BrushReach
{
	unsigned int mesh = 0;
	std::vector<unsigned int> vertices;
	std::vector<double> scales;
};

// A finished stroke, which undo and redo write. It holds every touched vertex's weights from before the stroke and as
// it left them, whole rows, so either end is put back exactly whatever the operation did.
class SkinBrushEdit
{
public:
	MStatus apply() const;
	MStatus undo() const;
	bool empty() const;
	unsigned int vertexCount() const;

	std::vector<BrushDab> dabs;

private:
	friend class SkinBrushStroke;
	struct MeshEdit
	{
		SkinWeightRows rows;
		std::vector<unsigned int> vertices;
		std::vector<double> oldRows;
		std::vector<double> finalRows;
	};
	std::vector<MeshEdit> fMeshes;
};

class SkinBrushStroke
{
public:
	SkinBrushStroke();
	~SkinBrushStroke();
	SkinBrushStroke(const SkinBrushStroke&) = delete;
	SkinBrushStroke& operator=(const SkinBrushStroke&) = delete;

	// Binds the meshes (each needs a skinCluster) and builds what is kept for the whole stroke: the neighbor map, the
	// vertex positions and the ray accelerator. Dabs may then land on any of them.
	MStatus begin(const MDagPathArray& meshes, const std::string& operation);
	// Reads the deformed vertex positions again and drops the ray accelerator, which a deforming mesh makes stale.
	MStatus refresh();
	// Only these vertices may change on a mesh (sorted, unique). Without a call, every vertex may.
	void restrict(unsigned int mesh, std::vector<unsigned int> sortedVertices);

	unsigned int meshCount() const;
	std::string meshName(unsigned int mesh) const;
	MPoint vertexPosition(unsigned int mesh, unsigned int vertex) const;
	// The diagonal of the box around every mesh's vertices in world space, 0 with no meshes. It is what a default
	// brush radius is a fraction of, so the brush fits a mesh of any size.
	double boundsDiagonal() const;

	// The hits of a ray: the closest one over every mesh, or with throughMesh every surface the ray crosses.
	void pick(const MPoint& origin, const MVector& direction, bool throughMesh, std::vector<BrushHit>& hits);

	// What a brush of this radius at these hits reaches: the vertices within radius of a hit, with falloff from it.
	// Surface: the radius is a length along the mesh from the hit's face. Volume: it is a distance in space.
	void reach(const std::vector<BrushHit>& hits, double radius, FalloffMode mode, std::vector<BrushReach>& reaches);

	// One dab at these hits. A mesh hit more than once (through a mesh) is dabbed once, the nearest hit setting each
	// vertex's falloff.
	MStatus dab(const std::vector<BrushHit>& hits, double radius, double strength, double smoothness, FalloffMode mode);

	// What the last dab did, for the debug print: the vertices it reached and the ones whose weights changed.
	unsigned int lastReachedCount() const;
	unsigned int lastChangedCount() const;

	// Ends the stroke: moves what it changed into edit, and starts the next one empty.
	MStatus finish(SkinBrushEdit& edit);

private:
	struct MeshState;
	std::vector<std::unique_ptr<MeshState>> fMeshes;
	std::vector<BrushDab> fDabs;
	unsigned int fLastReached = 0;
	unsigned int fLastChanged = 0;
};
