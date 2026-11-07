"""rigControlNode shapes without a rig: the shape list, the +X normal convention and the node's bounds.

Expected bounds are worked out by hand from the shape points in cpp/core/ControlShapes.cpp and from what the
draw manager draws, measured against a +-1 cube in a VP2 playblast: box and rect sizes are half extents, sphere
and circle sizes are radii, and capsule and cylinder take a radius (scale Y) and a full length (scale X).
"""
from maya import cmds

from rigtest.case import RigTestCase, fmt

SHAPE_TYPES = ["Box", "Sphere", "Circle", "Capsule", "Triangle", "Cylinder", "Square",
               "HalfCircleArrow", "ArchedDoubleArrow", "PointedCircle", "Diamond", "CircleArrow", "Dome",
               "SingleArrow", "FourArrow", "CircleStick", "Pivot"]

# Library shapes authored flat. With +X as the normal they lie in the YZ plane.
FLAT_CURVE_SHAPES = ["Triangle", "HalfCircleArrow", "PointedCircle", "CircleArrow", "SingleArrow", "FourArrow", "CircleStick"]
# Every shape drawn from the curve library rather than a draw manager primitive.
CURVE_SHAPES = FLAT_CURVE_SHAPES + ["ArchedDoubleArrow", "Diamond", "Dome", "Pivot"]


def make_control(shape_type="Box", shape_transform=None):
    control = cmds.createNode("rigControlNode", name="ctrl")
    cmds.setAttr(control + ".shapeType", SHAPE_TYPES.index(shape_type))
    if shape_transform is not None:
        cmds.setAttr(control + ".shapeTransform", shape_transform, type="matrix")
    return control


def bounds(control):
    return list(cmds.getAttr(control + ".boundingBoxMin")[0]) + list(cmds.getAttr(control + ".boundingBoxMax")[0])


class ShapeTypes(RigTestCase):

    def test_enum_names_in_stored_order(self):
        # Scenes store the index, so existing shapes must keep their values and new ones go on the end.
        control = cmds.createNode("rigControlNode")
        self.assertEqual(cmds.attributeQuery("shapeType", node=control, listEnum=True)[0].split(":"), SHAPE_TYPES)

    def test_control_has_markup_attribute(self):
        control = cmds.createNode("rigControlNode")
        self.assertTrue(cmds.attributeQuery("controlMarkup", node=control, exists=True))
        self.assertFalse(cmds.attributeQuery("controlMarkup", node=control, keyable=True))


class Bounds(RigTestCase):

    def assertBounds(self, control, expected, tolerance=1e-4):
        actual = bounds(control)
        if any(abs(a - e) > tolerance for a, e in zip(actual, expected)):
            self.fail("%s bounds: expected %s, got %s" % (control, fmt(expected), fmt(actual)))

    def test_box_default_bounds(self):
        self.assertBounds(make_control("Box"), [-1, -1, -1, 1, 1, 1])

    def test_bounds_follow_shape_transform_scale_and_offset(self):
        # Scale (2, 3, 4), offset 1 along X.
        control = make_control("Box", [2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 1, 0, 0, 1])
        self.assertBounds(control, [-1, -3, -4, 3, 3, 4])

    def test_bounds_update_when_shape_transform_changes(self):
        control = make_control("Box")
        cmds.setAttr(control + ".shapeTransform", [5, 0, 0, 0, 0, 5, 0, 0, 0, 0, 5, 0, 0, 0, 0, 1], type="matrix")
        self.assertBounds(control, [-5, -5, -5, 5, 5, 5])

    def test_capsule_long_axis_is_x(self):
        # Cylinder part 1 long along X, plus a cap of radius 1 at each end.
        self.assertBounds(make_control("Capsule"), [-1.5, -1, -1, 1.5, 1, 1])

    def test_short_wide_capsule_includes_its_caps(self):
        # Scale X 0.1, Y 2: half length 0.05 plus cap radius 2. A unit box pushed through the matrix would give 0.15.
        control = make_control("Capsule", [0.1, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])
        self.assertBounds(control, [-2.05, -2, -2, 2.05, 2, 2])

    def test_cylinder_length_is_scale_x(self):
        self.assertBounds(make_control("Cylinder"), [-0.5, -1, -1, 0.5, 1, 1])

    def test_circle_and_square_face_x(self):
        self.assertBounds(make_control("Circle"), [0, -1, -1, 0, 1, 1])
        cmds.delete("ctrl")
        # Square sizes along Y and Z separately.
        self.assertBounds(make_control("Square", [3, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0.5, 0, 0, 0, 0, 1]), [0, -2, -0.5, 0, 2, 0.5])

    def test_sphere_radius_is_scale_x(self):
        control = make_control("Sphere", [2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0.5, 0, 0, 0, 0, 1])
        self.assertBounds(control, [-2, -2, -2, 2, 2, 2])

    def test_triangle_faces_x(self):
        # Points (0, 0, 1), (0, 1, -1), (0, -1, -1).
        self.assertBounds(make_control("Triangle"), [0, -1, -1, 0, 1, 1])

    def test_mirrored_shape_transform_keeps_bounds(self):
        # Scale -1 in X: the triangle is flat in X, so its bounds do not change.
        control = make_control("Triangle", [-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])
        self.assertBounds(control, [0, -1, -1, 0, 1, 1])

    def test_flat_curve_shapes_lie_in_yz_plane(self):
        for shape in FLAT_CURVE_SHAPES:
            with self.subTest(shape=shape):
                bb = bounds(make_control(shape))
                self.assertAlmostEqual(bb[0], 0.0, places=6)
                self.assertAlmostEqual(bb[3], 0.0, places=6)
                cmds.delete("ctrl")

    def test_curve_shapes_have_unit_extent(self):
        # The library is scaled so the largest coordinate is 1, so shapeTransform scale means the same on every shape.
        for shape in CURVE_SHAPES:
            with self.subTest(shape=shape):
                bb = bounds(make_control(shape))
                self.assertAlmostEqual(max(abs(v) for v in bb), 1.0, places=6)
                cmds.delete("ctrl")

    def test_bounds_are_object_space(self):
        control = make_control("Box")
        cmds.setAttr(control + ".translateY", 5)
        self.assertBounds(control, [-1, -1, -1, 1, 1, 1])
