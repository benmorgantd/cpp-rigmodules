"""Generates cpp/core/ControlShapeLibrary.cpp, the mesh data for every rigControlNode shape.

Run with mayapy from the repo root, then rebuild:
    "C:\\Program Files\\Autodesk\\Maya2026\\bin\\mayapy.exe" tools\\generate_control_shapes.py

Primitives (Box, Sphere, Cylinder, Capsule, Dome) come from Maya's poly commands, read back through MFnMesh.
Flat outlines are written here: several come from the legacy mayadev library (controlTypes.json), redrawn so
they have faces. Every shape fits the -1..1 cube and follows one convention (D26):
- +X is the normal of flat shapes that ring a bone (Circle, Square, Triangle, PointedCircle, the circle arrows)
  and the long axis of Sphere, Cylinder, Capsule and Dome.
- Pointer shapes (SingleArrow, FourArrow, CircleStick, ArchedDoubleArrow) point down +X and lie in the XZ plane.

Each shape is points, faces (vertex index loops, may be concave, must be planar and free of collinear points)
and lines (edges with no area). The plugin draws the face edges plus lines as the wire, and the faces,
triangulated, as the fill.
"""
import math
import os
import sys

import maya.standalone

maya.standalone.initialize(name="python")

from maya import cmds  # noqa: E402
import maya.api.OpenMaya as om  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT = os.path.join(REPO, "cpp", "core", "ControlShapeLibrary.cpp")

# Same order as the ShapeType enum.
SHAPES = ["Box", "Sphere", "Circle", "Capsule", "Triangle", "Cylinder", "Square",
          "HalfCircleArrow", "ArchedDoubleArrow", "PointedCircle", "Diamond", "CircleArrow", "Dome",
          "SingleArrow", "FourArrow", "CircleStick", "Pivot"]

ROUND_SEGMENTS = 24   # flat circles
AXIS_SEGMENTS = 12    # around the long axis of round 3D shapes
HEIGHT_SEGMENTS = 8   # pole to pole on Sphere and Dome


class Shape(object):
    def __init__(self, points=None, faces=None, lines=None):
        self.points = [tuple(p) for p in (points or [])]
        self.faces = [list(f) for f in (faces or [])]
        self.lines = [tuple(l) for l in (lines or [])]

    def add(self, other):
        """Appends another shape's geometry, offsetting its indices."""
        offset = len(self.points)
        self.points += other.points
        self.faces += [[i + offset for i in face] for face in other.faces]
        self.lines += [(a + offset, b + offset) for a, b in other.lines]
        return self


def polygon(points):
    """One face through every point, in order."""
    return Shape(points, [list(range(len(points)))])


def maya_mesh(transform):
    """Points and faces of a Maya poly mesh, read through MFnMesh."""
    path = om.MSelectionList().add(transform).getDagPath(0)
    fn = om.MFnMesh(path)
    points = [(p.x, p.y, p.z) for p in fn.getPoints(om.MSpace.kObject)]
    faces = [list(fn.getPolygonVertices(i)) for i in range(fn.numPolygons)]
    cmds.delete(transform)
    return Shape(points, faces)


def circle_points(radius, segments, center=(0.0, 0.0, 0.0), plane="yz"):
    points = []
    for i in range(segments):
        a = 2.0 * math.pi * i / segments
        u, v = radius * math.cos(a), radius * math.sin(a)
        cx, cy, cz = center
        points.append((cx, cy + u, cz + v) if plane == "yz" else (cx + u, cy, cz + v))
    return points


def scale_to_unit(shape):
    largest = max(abs(c) for p in shape.points for c in p)
    shape.points = [tuple(c / largest for c in p) for p in shape.points]
    return shape


# ---- primitives from Maya -------------------------------------------------------------------

def box():
    return maya_mesh(cmds.polyCube(width=2, height=2, depth=2, constructionHistory=False)[0])


def sphere():
    return maya_mesh(cmds.polySphere(radius=1, subdivisionsAxis=AXIS_SEGMENTS, subdivisionsHeight=HEIGHT_SEGMENTS,
                                     axis=(1, 0, 0), constructionHistory=False)[0])


def cylinder():
    # subdivisionsCaps=0 leaves each cap as one n-gon, so the wire is two rings and the side lines.
    return maya_mesh(cmds.polyCylinder(radius=1, height=2, subdivisionsAxis=AXIS_SEGMENTS, subdivisionsHeight=1,
                                       subdivisionsCaps=0, axis=(1, 0, 0), constructionHistory=False)[0])


def capsule():
    # roundCap adds a hemisphere of the radius to each end: total length = height + 2 * radius = 2.
    # Then Y and Z are stretched x2 so it fills the cube like Box (author's choice over a uniform scale, which
    # would leave the cube): the caps become half-ellipsoids.
    shape = maya_mesh(cmds.polyCylinder(radius=0.5, height=1, subdivisionsAxis=AXIS_SEGMENTS, subdivisionsHeight=1,
                                        subdivisionsCaps=3, roundCap=True, axis=(1, 0, 0),
                                        constructionHistory=False)[0])
    shape.points = [(x, 2.0 * y, 2.0 * z) for x, y, z in shape.points]
    return shape


def dome():
    # The +X half of a sphere, closed with one n-gon base.
    transform = cmds.polySphere(radius=1, subdivisionsAxis=AXIS_SEGMENTS, subdivisionsHeight=HEIGHT_SEGMENTS,
                                axis=(1, 0, 0), constructionHistory=False)[0]
    fn = om.MFnMesh(om.MSelectionList().add(transform).getDagPath(0))
    points = fn.getPoints(om.MSpace.kObject)
    back = [i for i in range(fn.numPolygons) if max(points[v].x for v in fn.getPolygonVertices(i)) < 1e-6]
    cmds.delete(["%s.f[%d]" % (transform, i) for i in back])
    cmds.polyCloseBorder(transform, constructionHistory=False)
    return maya_mesh(transform)


# ---- flat shapes facing +X ------------------------------------------------------------------

def circle():
    return polygon(circle_points(1.0, ROUND_SEGMENTS))


def square():
    return polygon([(0, -1, -1), (0, 1, -1), (0, 1, 1), (0, -1, 1)])


def triangle():
    return polygon([(0, 0, 1), (0, 1, -1), (0, -1, -1)])


def pointed_circle():
    # Legacy outline: a ring with a point, in XZ with the point along +Z. Laid in YZ, facing +X.
    legacy = [(0.0, -3.0), (-0.93, -2.85), (-1.76, -2.43), (-2.43, -1.76), (-2.85, -0.93), (-3.0, 0.0),
              (-2.85, 0.93), (-2.43, 1.76), (-1.76, 2.43), (-0.93, 2.85), (0.0, 4.0), (0.93, 2.85), (1.76, 2.43),
              (2.43, 1.76), (2.85, 0.93), (3.0, 0.0), (2.85, -0.93), (2.43, -1.76), (1.76, -2.43), (0.93, -2.85)]
    return scale_to_unit(polygon([(0.0, -x, z) for x, z in legacy]))


HALF_CIRCLE_ARROW = [(0.14, 0.95), (0.52, 0.86), (0.84, 0.6), (1.02, 0.23), (1.02, -0.18), (0.84, -0.56),
                     (1.07, -0.74), (0.14, -0.97), (0.14, -0.02), (0.39, -0.2), (0.46, -0.06), (0.46, 0.1),
                     (0.39, 0.24), (0.27, 0.34), (0.14, 0.38)]


def half_circle_arrow():
    # Legacy outline in XZ, laid in YZ facing +X, so it shows a rotation about the bone.
    return scale_to_unit(polygon([(0.0, -x, z) for x, z in HALF_CIRCLE_ARROW]))


def circle_arrow():
    # Two half arrows, the second turned 180 degrees about +X.
    first = half_circle_arrow()
    second = Shape([(x, -y, -z) for x, y, z in first.points], first.faces)
    return first.add(second)


# ---- pointer shapes: point down +X, lie in XZ ------------------------------------------------

def single_arrow():
    # Legacy outline points along +Z from the origin. Turned so it points along +X.
    legacy = [(-0.14, 0.0), (-0.14, 0.71), (-0.28, 0.71), (0.0, 0.99), (0.28, 0.71), (0.14, 0.71), (0.14, 0.0)]
    return scale_to_unit(polygon([(z, 0.0, -x) for x, z in legacy]))


def four_arrow():
    legacy = [(-0.2, -0.59), (-0.4, -0.59), (0.0, -0.99), (0.4, -0.59), (0.2, -0.59), (0.2, -0.2),
              (0.59, -0.2), (0.59, -0.4), (0.99, 0.0), (0.59, 0.4), (0.59, 0.2), (0.2, 0.2), (0.2, 0.59),
              (0.4, 0.59), (0.0, 0.99), (-0.4, 0.59), (-0.2, 0.59), (-0.2, 0.2), (-0.59, 0.2), (-0.59, 0.4),
              (-0.99, 0.0), (-0.59, -0.4), (-0.59, -0.2), (-0.2, -0.2)]
    return scale_to_unit(polygon([(x, 0.0, z) for x, z in legacy]))


def circle_stick():
    # A pin: a stick from the origin along +X, ending in a disc.
    radius = 0.2
    shape = Shape([(0.0, 0.0, 0.0), (1.0 - 2 * radius, 0.0, 0.0)], lines=[(0, 1)])
    return shape.add(polygon(circle_points(radius, 16, center=(1.0 - radius, 0.0, 0.0), plane="xz")))


def arched_double_arrow():
    # A strip arching over +Y from -X to +X, with a head at each end pointing down to the XZ plane.
    radius, half_width, head_half_width, head_angle = 1.0, 0.22, 0.44, math.radians(25)
    steps = 10
    shape = Shape()
    for i in range(steps + 1):
        a = head_angle + (math.pi - 2 * head_angle) * i / steps
        x, y = radius * math.cos(a), radius * math.sin(a)
        shape.points += [(x, y, -half_width), (x, y, half_width)]
    for i in range(steps):
        a, b = 2 * i, 2 * i + 2
        shape.faces.append([a, b, b + 1, a + 1])
    # Each head is a triangle whose base passes through the strip's end edge.
    for angle, tip_x in ((head_angle, radius), (math.pi - head_angle, -radius)):
        x, y = radius * math.cos(angle), radius * math.sin(angle)
        base = len(shape.points)
        shape.points += [(x, y, -head_half_width), (tip_x, 0.0, 0.0), (x, y, head_half_width)]
        shape.faces.append([base, base + 1, base + 2])
    return shape


# ---- symmetric 3D shapes ---------------------------------------------------------------------

def diamond():
    points = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
    faces = [[0, 2, 4], [2, 1, 4], [1, 3, 4], [3, 0, 4], [2, 0, 5], [1, 2, 5], [3, 1, 5], [0, 3, 5]]
    return Shape(points, faces)


def pivot():
    # Three flat double-headed arrows, one along each axis, each in its own plane.
    shaft, head, head_start = 0.06, 0.18, 0.7
    outline = [(-1.0, 0.0), (-head_start, head), (-head_start, shaft), (head_start, shaft), (head_start, head),
               (1.0, 0.0), (head_start, -head), (head_start, -shaft), (-head_start, -shaft), (-head_start, -head)]
    shape = Shape()
    for axis_map in (lambda u, v: (u, v, 0.0), lambda u, v: (0.0, u, v), lambda u, v: (v, 0.0, u)):
        shape.add(polygon([axis_map(u, v) for u, v in outline]))
    return shape


BUILDERS = {
    "Box": box, "Sphere": sphere, "Circle": circle, "Capsule": capsule, "Triangle": triangle,
    "Cylinder": cylinder, "Square": square, "HalfCircleArrow": half_circle_arrow,
    "ArchedDoubleArrow": arched_double_arrow, "PointedCircle": pointed_circle, "Diamond": diamond,
    "CircleArrow": circle_arrow, "Dome": dome, "SingleArrow": single_arrow, "FourArrow": four_arrow,
    "CircleStick": circle_stick, "Pivot": pivot,
}


# ---- C++ output -----------------------------------------------------------------------------

def number(value):
    value = round(value, 6)
    if value == 0:
        value = 0.0
    return repr(float(value))


def block(lines, indent):
    """A braced, comma-separated list of one-line items, one per line."""
    pad = "\t" * indent
    if not lines:
        return pad + "{}"
    body = ",\n".join(pad + "\t" + line for line in lines)
    return "%s{\n%s\n%s}" % (pad, body, pad)


def shape_case(name, shape):
    points = ["{ %s, %s, %s }" % tuple(number(c) for c in p) for p in shape.points]
    faces = ["{ %s }" % ", ".join(str(i) for i in face) for face in shape.faces]
    lines = ["{ %d, %d }" % line for line in shape.lines]
    data = ",\n".join([block(points, 4), block(faces, 4), block(lines, 4)])
    return ("\t\tcase ShapeType::%s:\n\t\t{\n\t\t\tstatic const ShapeMeshSource source =\n\t\t\t{\n%s\n\t\t\t};\n"
            "\t\t\treturn source;\n\t\t}" % (name, data))


HEADER = """// Generated by tools/generate_control_shapes.py. Do not edit by hand: change the generator and rerun it.
#include "ControlShapeLibrary.h"

const ShapeMeshSource& getShapeMeshSource(ShapeType shapeType)
{
	static const ShapeMeshSource empty;
	switch (shapeType)
	{
"""

FOOTER = """
		default:
			return empty;
	}
}
"""


def main():
    cases = []
    for name in SHAPES:
        shape = BUILDERS[name]()
        largest = max(abs(c) for p in shape.points for c in p)
        if abs(largest - 1.0) > 1e-6:
            raise RuntimeError("%s does not fit the -1..1 cube exactly (largest coordinate %s)" % (name, largest))
        cases.append(shape_case(name, shape))
        print("%-18s %3d points %3d faces %2d lines" % (name, len(shape.points), len(shape.faces), len(shape.lines)))
    text = HEADER + "\n".join(cases) + FOOTER
    with open(OUTPUT, "w", newline="\r\n") as handle:
        handle.write(text)
    print("wrote " + OUTPUT)


if __name__ == "__main__":
    try:
        main()
    finally:
        maya.standalone.uninitialize()
    sys.exit(0)
