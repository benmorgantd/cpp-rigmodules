"""rigControlNode shapes without a rig: the shape list, the orientation convention and the node's bounds.

Every shape is a mesh from cpp/core/ControlShapeLibrary.cpp (written by tools/generate_control_shapes.py), and
the bounds are its points through shapeTransform. Expected values are worked out by hand from how the generator
builds each shape, not read back from the plugin. How the shapes look is checked with tests/ViewportGallery.cmd.
"""
from maya import cmds

from rigtest.case import RigTestCase, fmt

SHAPE_TYPES = ["Box", "Sphere", "Circle", "Capsule", "Triangle", "Cylinder", "Square",
               "HalfCircleArrow", "ArchedDoubleArrow", "PointedCircle", "Diamond", "CircleArrow", "Dome",
               "SingleArrow", "FourArrow", "CircleStick", "Pivot"]

# Flat shapes that ring a bone: they face +X, so they lie in the YZ plane.
RING_SHAPES = ["Circle", "Square", "Triangle", "PointedCircle", "HalfCircleArrow", "CircleArrow"]
# Pointer shapes point down +X and lie in the XZ plane.
FLAT_POINTER_SHAPES = ["SingleArrow", "FourArrow", "CircleStick"]


def make_control(shape_type="Box", shape_transform=None):
    control = cmds.createNode("rigControlNode", name="ctrl")
    cmds.setAttr(control + ".shapeType", SHAPE_TYPES.index(shape_type))
    if shape_transform is not None:
        cmds.setAttr(control + ".shapeTransform", shape_transform, type="matrix")
    return control


def bounds(control):
    return list(cmds.getAttr(control + ".boundingBoxMin")[0]) + list(cmds.getAttr(control + ".boundingBoxMax")[0])


def scale(x, y, z):
    return [x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1]


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

    def assertShapeBounds(self, shape, expected, shape_transform=None):
        with self.subTest(shape=shape):
            self.assertBounds(make_control(shape, shape_transform), expected)
        cmds.delete("ctrl")

    def test_every_shape_fits_the_unit_cube(self):
        # So a shapeTransform scale gives the same size on every shape.
        for shape in SHAPE_TYPES:
            with self.subTest(shape=shape):
                bb = bounds(make_control(shape))
                self.assertAlmostEqual(max(abs(v) for v in bb), 1.0, places=6)
            cmds.delete("ctrl")

    def test_solid_shapes(self):
        self.assertShapeBounds("Box", [-1, -1, -1, 1, 1, 1])
        self.assertShapeBounds("Sphere", [-1, -1, -1, 1, 1, 1])
        self.assertShapeBounds("Cylinder", [-1, -1, -1, 1, 1, 1])
        self.assertShapeBounds("Diamond", [-1, -1, -1, 1, 1, 1])
        self.assertShapeBounds("Pivot", [-1, -1, -1, 1, 1, 1])
        # Fills the cube like Box: 2 long along X, 2 across.
        self.assertShapeBounds("Capsule", [-1, -1, -1, 1, 1, 1])

    def test_dome_bulges_toward_x(self):
        self.assertShapeBounds("Dome", [0, -1, -1, 1, 1, 1])

    def test_ring_shapes_face_x(self):
        for shape in RING_SHAPES:
            with self.subTest(shape=shape):
                bb = bounds(make_control(shape))
                self.assertAlmostEqual(bb[0], 0.0, places=6)
                self.assertAlmostEqual(bb[3], 0.0, places=6)
            cmds.delete("ctrl")
        self.assertShapeBounds("Circle", [0, -1, -1, 0, 1, 1])
        self.assertShapeBounds("Square", [0, -1, -1, 0, 1, 1])
        self.assertShapeBounds("Triangle", [0, -1, -1, 0, 1, 1])

    def test_pointer_shapes_point_down_x_in_xz(self):
        for shape in FLAT_POINTER_SHAPES:
            with self.subTest(shape=shape):
                bb = bounds(make_control(shape))
                self.assertAlmostEqual(bb[1], 0.0, places=6)
                self.assertAlmostEqual(bb[4], 0.0, places=6)
                self.assertAlmostEqual(bb[3], 1.0, places=6)
            cmds.delete("ctrl")
        # The legacy single arrow runs from the origin to its tip, 0.28 wide on a 0.99 length.
        self.assertShapeBounds("SingleArrow", [0, 0, -0.282828, 1, 0, 0.282828])
        # Stick from the origin, disc of radius 0.2 at the end.
        self.assertShapeBounds("CircleStick", [0, 0, -0.2, 1, 0, 0.2])
        self.assertShapeBounds("FourArrow", [-1, 0, -1, 1, 0, 1])

    def test_arched_double_arrow_tips_on_x(self):
        # Arch of radius 1 over +Y, heads 0.44 wide, tips at x = +-1 on the XZ plane.
        self.assertShapeBounds("ArchedDoubleArrow", [-1, 0, -0.44, 1, 1, 0.44])

    def test_every_shape_scales_on_every_axis(self):
        # Meshes scale linearly, round shapes included.
        self.assertShapeBounds("Sphere", [-2, -1, -0.5, 2, 1, 0.5], scale(2, 1, 0.5))
        self.assertShapeBounds("Capsule", [-0.1, -2, -0.5, 0.1, 2, 0.5], scale(0.1, 2, 0.5))
        self.assertShapeBounds("Square", [0, -2, -0.5, 0, 2, 0.5], scale(3, 2, 0.5))

    def test_bounds_follow_shape_transform_offset(self):
        # Scale (2, 3, 4), offset 1 along X.
        control = make_control("Box", [2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 1, 0, 0, 1])
        self.assertBounds(control, [-1, -3, -4, 3, 3, 4])

    def test_bounds_update_when_shape_transform_changes(self):
        control = make_control("Box")
        cmds.setAttr(control + ".shapeTransform", scale(5, 5, 5), type="matrix")
        self.assertBounds(control, [-5, -5, -5, 5, 5, 5])

    def test_mirrored_shape_transform_flips_pointers(self):
        # Scale -1 in X: the single arrow now points down -X.
        self.assertShapeBounds("SingleArrow", [-1, 0, -0.282828, 0, 0, 0.282828], scale(-1, 1, 1))

    def test_bounds_are_object_space(self):
        control = make_control("Box")
        cmds.setAttr(control + ".translateY", 5)
        self.assertBounds(control, [-1, -1, -1, 1, 1, 1])
