"""Orient and Point constraints (D31): the driver gives only its rotation (Orient) or translation (Point),
and the rest comes from the child plug's parentStructure parent.

fk_two_chains: a_jnt (0,0,0) and b_jnt (0,2,0) are fkchain_1, under layout.end. c_jnt (1,3,0) is fkchain_2,
whose start plug is parented to fkchain_1.end. Every constraint here is on that plug, so its parent is b."""
from maya import cmds
import maya.api.OpenMaya as om

from rigtest import fixtures
from rigtest.case import RigTestCase, rig_name

CHILD_CTRL = rig_name("c_ctrl_l")
CHILD_JNT = rig_name("c_jnt")
A_CTRL = rig_name("a_ctrl_c")
LAYOUT_END_CTRL = rig_name("layout_2_ctrl_c")


def world_matrix(node):
    return om.MMatrix(cmds.xform(node, q=True, ws=True, matrix=True))


class ConstraintTypeCase(RigTestCase):

    SKELETON = "fk_two_chains"

    def setUp(self):
        super().setUp()
        self.build_standard("fkchain_two_chains", self.SKELETON)

    def constrain(self, constraint_type, driver=("layout.end",)):
        return cmds.createRigConstraint(CHILD_CTRL, driver=list(driver), type=constraint_type)

    def assertWorldMatrix(self, node, expected, message):
        self.assertTrue(world_matrix(node).isEquivalent(expected, 1e-4), "%s: %s" % (node, message))


class Orient(ConstraintTypeCase):

    def test_type_is_stored_on_the_node(self):
        con = self.constrain("Orient")
        self.assertEqual(cmds.getAttr(con + ".constraintType", asString=True), "Orient")

    def test_pivot_follows_parent_rotation_does_not(self):
        # a rotates 90 about Z at the origin: c's pivot swings from (1,3,0) to (-3,1,0), and c keeps the layout's rotation.
        self.constrain("Orient")
        cmds.setAttr(A_CTRL + ".rotateZ", 90)
        self.assertWorldTranslation(CHILD_JNT, [-3, 1, 0])
        self.assertWorldRotation(CHILD_JNT, [0, 0, 0])

    def test_rotation_follows_driver(self):
        # layout.end also parents fkchain_1, so its move carries the pivot too: (1,3,0) about Y 90 is (0,3,-1), then +5 in X.
        self.constrain("Orient")
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateY", 90)
        cmds.setAttr(LAYOUT_END_CTRL + ".translateX", 5)
        self.assertWorldTranslation(CHILD_JNT, [5, 3, -1])
        self.assertWorldRotation(CHILD_JNT, [0, 90, 0])

    def test_scale_comes_from_parent(self):
        self.constrain("Orient")
        cmds.setAttr(A_CTRL + ".scale", 2, 2, 2)
        self.assertWorldTranslation(CHILD_JNT, [2, 6, 0])
        self.assertWorldScale(CHILD_JNT, [2, 2, 2])

    def test_template_type(self):
        template = fixtures.load_template("fkchain_two_chains")
        template["constraints"] = [{"name": "c_space", "type": "Orient", "target": "fkchain_2.start",
                                    "drivers": [{"socket": "layout.end", "label": "World", "weight": 1.0}]}]
        cmds.file(new=True, force=True)
        self.build(template, self.make_skeleton(self.SKELETON))
        self.assertEqual(cmds.getAttr(rig_name("c_space") + ".constraintType", asString=True), "Orient")
        cmds.setAttr(A_CTRL + ".rotateZ", 90)
        self.assertWorldTranslation(CHILD_JNT, [-3, 1, 0])
        self.assertWorldRotation(CHILD_JNT, [0, 0, 0])


class Point(ConstraintTypeCase):

    def test_rotation_follows_parent_pivot_does_not(self):
        self.constrain("Point")
        cmds.setAttr(A_CTRL + ".rotateZ", 90)
        self.assertWorldTranslation(CHILD_JNT, [1, 3, 0])
        self.assertWorldRotation(CHILD_JNT, [0, 0, 90])

    def test_translation_follows_driver(self):
        # fkchain_1 rides layout.end too, so the parent's rotation is the layout's.
        self.constrain("Point")
        cmds.setAttr(LAYOUT_END_CTRL + ".translateX", 5)
        self.assertWorldTranslation(CHILD_JNT, [6, 3, 0])
        self.assertWorldRotation(CHILD_JNT, [0, 0, 0])

    def test_scale_comes_from_parent(self):
        self.constrain("Point")
        cmds.setAttr(A_CTRL + ".scale", 2, 2, 2)
        self.assertWorldTranslation(CHILD_JNT, [1, 3, 0])
        self.assertWorldScale(CHILD_JNT, [2, 2, 2])


class TypedSpaceSwitch(ConstraintTypeCase):
    """D27 matching with the node's own output for each driver."""

    def pose(self):
        cmds.setAttr(A_CTRL + ".rotate", 10, 20, 30)
        cmds.setAttr(rig_name("b_ctrl_c") + ".rotateX", -40)
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateY", 25)

    def test_switch_does_not_move_control(self):
        for constraint_type in ("Orient", "Point"):
            with self.subTest(type=constraint_type):
                self.setUp()
                con = self.constrain(constraint_type, ["layout.end", "fkchain_1.start"])
                self.pose()
                before = world_matrix(CHILD_CTRL)
                cmds.setRigConstraintSpace(con, space=1)
                self.assertWorldMatrix(CHILD_CTRL, before, "moved on a %s space switch" % constraint_type)

    def test_orient_switch_writes_only_rotate(self):
        con = self.constrain("Orient", ["layout.end", "fkchain_1.start"])
        self.pose()
        cmds.setRigConstraintSpace(con, space=1)
        for value in cmds.getAttr(CHILD_CTRL + ".translate")[0]:
            self.assertAlmostEqual(value, 0.0, places=4)
        self.assertNotEqual(cmds.getAttr(CHILD_CTRL + ".rotate")[0], (0.0, 0.0, 0.0))

    def test_point_switch_writes_only_translate(self):
        con = self.constrain("Point", ["layout.end", "fkchain_1.start"])
        self.pose()
        cmds.setRigConstraintSpace(con, space=1)
        for value in cmds.getAttr(CHILD_CTRL + ".rotate")[0]:
            self.assertAlmostEqual(value, 0.0, places=4)
        self.assertNotEqual(cmds.getAttr(CHILD_CTRL + ".translate")[0], (0.0, 0.0, 0.0))

    def test_create_on_posed_rig_does_not_move_control(self):
        self.pose()
        before = world_matrix(CHILD_CTRL)
        self.constrain("Orient")
        self.assertWorldMatrix(CHILD_CTRL, before, "moved when an Orient constraint was created on a posed rig")

    def test_every_space_gives_rest_at_rest(self):
        rest = world_matrix(CHILD_CTRL)
        self.constrain("Orient", ["layout.end", "fkchain_1.start"])
        for space in (1, 0):
            cmds.setAttr(CHILD_CTRL + ".space", space)
            self.assertWorldMatrix(CHILD_CTRL, rest, "space %d does not give the rest pose" % space)


class MatchesMayaConstraints(ConstraintTypeCase):
    """Rotated rests and an animated child, against Maya's own point and orient constraints on a group.

    The point constraint targets a locator parented to the socket at c's rest position, with no offset, so the pivot
    swings with the socket. maintainOffset on a point constraint stores a world offset and would only slide it."""

    SKELETON = "fk_two_chains_rotated"

    def socket_follower(self, socket_plug, name):
        node = cmds.createNode("transform", name=name)
        cmds.connectAttr(socket_plug, node + ".offsetParentMatrix")
        return node

    def reference(self, con, constraint_type):
        """A group that follows c's rest under the same rules, and a child carrying c's control values."""
        parent = self.socket_follower(cmds.listConnections(con + ".parentMatrix", plugs=True)[0], "ref_parent")
        driver = self.socket_follower(cmds.listConnections(con + ".drivers[0].driverMatrix", plugs=True)[0], "ref_driver")
        rest = cmds.xform(CHILD_JNT, q=True, ws=True, matrix=True)
        translate_source, rotate_source = (parent, driver) if constraint_type == "Orient" else (driver, parent)

        pivot = cmds.createNode("transform", name="ref_pivot", parent=translate_source)
        cmds.xform(pivot, ws=True, matrix=rest)
        group = cmds.createNode("transform", name="ref_group")
        cmds.xform(group, ws=True, matrix=rest)
        cmds.pointConstraint(pivot, group)
        cmds.orientConstraint(rotate_source, group, maintainOffset=True)

        child = cmds.createNode("transform", name="ref_child", parent=group)
        cmds.connectAttr(CHILD_CTRL + ".translate", child + ".translate")
        cmds.connectAttr(CHILD_CTRL + ".rotate", child + ".rotate")
        cmds.connectAttr(CHILD_CTRL + ".rotateOrder", child + ".rotateOrder")
        return child

    def test_matches_point_and_orient_constraint_rig(self):
        for constraint_type in ("Orient", "Point"):
            with self.subTest(type=constraint_type):
                self.setUp()
                con = self.constrain(constraint_type)
                reference = self.reference(con, constraint_type)
                cmds.setAttr(A_CTRL + ".rotate", 15, -35, 50)
                cmds.setAttr(rig_name("b_ctrl_c") + ".rotate", 70, 10, -20)
                cmds.setAttr(rig_name("b_ctrl_c") + ".translate", 0.5, 0, 1)
                cmds.setAttr(LAYOUT_END_CTRL + ".rotate", -30, 60, 5)
                cmds.setAttr(LAYOUT_END_CTRL + ".translate", 2, 1, -3)
                cmds.setAttr(CHILD_CTRL + ".rotate", 5, 40, -15)
                cmds.setAttr(CHILD_CTRL + ".translate", 0.3, -0.2, 0.7)
                self.assertWorldMatrix(CHILD_JNT, world_matrix(reference),
                                       "%s differs from Maya's point and orient constraints" % constraint_type)


class Rejected(ConstraintTypeCase):

    def test_unknown_type(self):
        self.assertMayaError(lambda: self.constrain("Scale"), "Scale", "(has: Parent, Orient, Point)")

    def test_orient_without_parent(self):
        cmds.disconnectAttr(rig_name("fkchain_1") + ".outputSocketMatrix[1]",
                            rig_name("fkchain_2") + ".childPlugs[0].childPlugParentMatrix")
        self.assertMayaError(lambda: self.constrain("Orient"), "no parent")

    def test_parent_without_parent_is_allowed(self):
        cmds.disconnectAttr(rig_name("fkchain_1") + ".outputSocketMatrix[1]",
                            rig_name("fkchain_2") + ".childPlugs[0].childPlugParentMatrix")
        self.assertExists(self.constrain("Parent"))
