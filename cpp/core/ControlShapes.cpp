#include "ControlShapes.h"
#include "ControlShapeLibrary.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <set>
#include <utility>

bool findShapeType(std::string_view name, ShapeType& outShapeType)
{
	const auto& all = RigControlNodeAttr::ShapeTypes::All;
	auto it = std::find(all.begin(), all.end(), name);
	if (it == all.end())
	{
		return false;
	}
	outShapeType = static_cast<ShapeType>(std::distance(all.begin(), it));
	return true;
}

ShapeType getShapeTypeFromString(std::string_view name)
{
	ShapeType shapeType = ShapeType::Box;
	findShapeType(name, shapeType);
	return shapeType;
}

const char* getShapeTypeString(ShapeType shapeType)
{
	const auto index = static_cast<std::size_t>(shapeType);
	const auto& all = RigControlNodeAttr::ShapeTypes::All;
	return index < all.size() ? all[index] : all[ShapeType::Box];
}

namespace
{
	struct Point2
	{
		double u = 0.0;
		double v = 0.0;
	};

	double cross(const Point2& a, const Point2& b, const Point2& c)
	{
		return (b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u);
	}

	bool isInsideTriangle(const Point2& p, const Point2& a, const Point2& b, const Point2& c)
	{
		return cross(a, b, p) >= 0.0 && cross(b, c, p) >= 0.0 && cross(c, a, p) >= 0.0;
	}

	// Ear clipping. The face is projected onto the plane its normal (Newell's method) is most aligned with.
	// False when the face does not clip into (n - 2) triangles: non-planar, self-intersecting, or with collinear points.
	bool triangulateFace(const std::vector<ShapePoint>& points, const std::vector<unsigned int>& face, std::vector<unsigned int>& outTriangles)
	{
		double nx = 0.0;
		double ny = 0.0;
		double nz = 0.0;
		for (std::size_t i = 0; i < face.size(); ++i)
		{
			const ShapePoint& a = points[face[i]];
			const ShapePoint& b = points[face[(i + 1) % face.size()]];
			nx += (a.y - b.y) * (a.z + b.z);
			ny += (a.z - b.z) * (a.x + b.x);
			nz += (a.x - b.x) * (a.y + b.y);
		}

		// Drop the axis the normal is closest to. Flipping v when that component is negative keeps the loop
		// counter-clockwise in 2D, so a convex corner always has a positive cross product.
		std::vector<Point2> projected;
		const double ax = std::abs(nx);
		const double ay = std::abs(ny);
		const double az = std::abs(nz);
		for (unsigned int index : face)
		{
			const ShapePoint& p = points[index];
			if (ax >= ay && ax >= az)
			{
				projected.push_back({ p.y, nx >= 0.0 ? p.z : -p.z });
			}
			else if (ay >= az)
			{
				projected.push_back({ p.z, ny >= 0.0 ? p.x : -p.x });
			}
			else
			{
				projected.push_back({ p.x, nz >= 0.0 ? p.y : -p.y });
			}
		}

		std::vector<std::size_t> remaining(face.size());
		for (std::size_t i = 0; i < remaining.size(); ++i)
		{
			remaining[i] = i;
		}

		const std::size_t expected = outTriangles.size() + 3 * (face.size() - 2);
		while (remaining.size() > 3)
		{
			bool clipped = false;
			for (std::size_t i = 0; i < remaining.size(); ++i)
			{
				const std::size_t prev = remaining[(i + remaining.size() - 1) % remaining.size()];
				const std::size_t curr = remaining[i];
				const std::size_t next = remaining[(i + 1) % remaining.size()];
				if (cross(projected[prev], projected[curr], projected[next]) <= 1e-12)
				{
					continue;   // reflex or collinear corner
				}

				bool isEar = true;
				for (std::size_t other : remaining)
				{
					if (other != prev && other != curr && other != next
						&& isInsideTriangle(projected[other], projected[prev], projected[curr], projected[next]))
					{
						isEar = false;
						break;
					}
				}
				if (!isEar)
				{
					continue;
				}

				outTriangles.insert(outTriangles.end(), { face[prev], face[curr], face[next] });
				remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
				clipped = true;
				break;
			}
			if (!clipped)
			{
				return false;
			}
		}

		if (cross(projected[remaining[0]], projected[remaining[1]], projected[remaining[2]]) <= 1e-12)
		{
			return false;
		}
		outTriangles.insert(outTriangles.end(), { face[remaining[0]], face[remaining[1]], face[remaining[2]] });
		return outTriangles.size() == expected;
	}

	// Builds the draw mesh. outError is empty when the source is valid.
	ShapeMesh buildShapeMesh(const ShapeMeshSource& source, std::string& outError)
	{
		ShapeMesh mesh;
		mesh.points = source.points;
		const unsigned int count = static_cast<unsigned int>(source.points.size());

		std::set<std::pair<unsigned int, unsigned int>> edges;
		auto addEdge = [&](unsigned int a, unsigned int b)
		{
			if (edges.insert({ std::min(a, b), std::max(a, b) }).second)
			{
				mesh.wireIndices.insert(mesh.wireIndices.end(), { a, b });
			}
		};

		for (std::size_t f = 0; f < source.faces.size(); ++f)
		{
			const std::vector<unsigned int>& face = source.faces[f];
			if (face.size() < 3)
			{
				outError = "face " + std::to_string(f) + " has fewer than 3 points";
				return mesh;
			}
			for (unsigned int index : face)
			{
				if (index >= count)
				{
					outError = "face " + std::to_string(f) + " uses point " + std::to_string(index) + " of " + std::to_string(count);
					return mesh;
				}
			}
			for (std::size_t i = 0; i < face.size(); ++i)
			{
				addEdge(face[i], face[(i + 1) % face.size()]);
			}
			if (!triangulateFace(mesh.points, face, mesh.triangleIndices))
			{
				outError = "face " + std::to_string(f) + " does not triangulate (non-planar, self-intersecting or collinear points)";
				return mesh;
			}
		}

		for (const std::array<unsigned int, 2>& line : source.lines)
		{
			if (line[0] >= count || line[1] >= count)
			{
				outError = "a line uses a point outside the " + std::to_string(count) + " points";
				return mesh;
			}
			addEdge(line[0], line[1]);
		}

		if (mesh.wireIndices.empty())
		{
			outError = "no faces or lines";
		}
		return mesh;
	}

	struct ShapeLibrary
	{
		std::vector<ShapeMesh> meshes;
		std::string error;
	};

	// Built once. Function-local static initialization is thread-safe, and validateShapeLibrary runs it at plugin load.
	const ShapeLibrary& getLibrary()
	{
		static const ShapeLibrary library = []()
		{
			ShapeLibrary built;
			for (int i = 0; i < ShapeType::Count; ++i)
			{
				const ShapeType shapeType = static_cast<ShapeType>(i);
				std::string error;
				built.meshes.push_back(buildShapeMesh(getShapeMeshSource(shapeType), error));
				if (!error.empty() && built.error.empty())
				{
					built.error = std::string(getShapeTypeString(shapeType)) + ": " + error;
				}
			}
			return built;
		}();
		return library;
	}
}

const ShapeMesh& getShapeMesh(ShapeType shapeType)
{
	static const ShapeMesh empty;
	const ShapeLibrary& library = getLibrary();
	const auto index = static_cast<std::size_t>(shapeType);
	return index < library.meshes.size() ? library.meshes[index] : empty;
}

bool validateShapeLibrary(std::string& outError)
{
	outError = getLibrary().error;
	return outError.empty();
}
