"""constraintWeight: a baked 0..1 blend from "the control follows its parent" (0) to the type's full result (1).

fk_two_chains: a_00_M_JNT (0,0,0) and b_00_M_JNT (0,2,0) are fkchain_1, under layout_00_M_MOD.end. c_00_M_JNT (1,3,0) is fkchain_2,
whose start plug is parented to fkchain_01_M_MOD.end, so the constrained plug's parent is b. The driver is layout_00_M_MOD.end.
A pose that turns a_00_M_CTRL moves the parent alone and leaves the driver at rest, so the full constraint and the parent's
own motion (weight 0) differ, and the answers are worked by hand. The pivot is c's joint, rotated about the origin by the
parent's motion."""
import math

from maya import cmds
import maya.api.OpenMaya as om

from rigtest import fixtures
from rigtest.case import RigTestCase, rig_name

CHILD_CTRL = rig_name("c_00_L_CTRL")
CHILD_JNT = rig_name("c_00_M_JNT")
A_CTRL = rig_name("a_00_M_CTRL")
LAYOUT_END_CTRL = rig_name("layout_02_M_CTRL")

SIN60 = 3 * math.sin(math.radians(60))


class ConstraintWeightCase(RigTestCase):

    SKELETON = "fk_two_chains"

    def setUp(self):
        super().setUp()
        self.build_standard("fkchain_two_chains", self.SKELETON)

    def constrain(self, constraint_type, weight=None, driver=("layout_00_M_MOD.end",)):
        flags = {} if weight is None else {"constraintWeight": weight}
        return cmds.createRigConstraint(CHILD_CTRL, driver=list(driver), type=constraint_type, **flags)

    def world(self):
        return om.MMatrix(cmds.xform(CHILD_JNT, q=True, ws=True, matrix=True))


class Weight(ConstraintWeightCase):

    def test_default_is_one_and_is_stored_on_the_node(self):
        self.assertAlmostEqual(cmds.getAttr(self.constrain("Orient") + ".constraintWeight"), 1.0)
        self.setUp()
        self.assertAlmostEqual(cmds.getAttr(self.constrain("Orient", 0.25) + ".constraintWeight"), 0.25)

    def test_it_is_not_an_animator_channel(self):
        con = self.constrain("Twist", 0.5)
        self.assertFalse(cmds.getAttr(con + ".constraintWeight", keyable=True))

    def test_weight_one_is_the_full_constraint(self):
        self.constrain("Twist", 1.0)
        cmds.setAttr(A_CTRL + ".rotateX", 60)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 0])
        self.assertWorldTranslation(CHILD_JNT, [1, 3, 0])

    def test_weight_zero_follows_the_parent_for_every_type(self):
        """The same pose with no constraint and with a weight of 0 gives the same world matrix, for every type, Parent included."""
        pose = {A_CTRL + ".rotate": (15, -35, 50), LAYOUT_END_CTRL + ".rotateY": 25, LAYOUT_END_CTRL + ".translate": (2, 1, -3)}

        def apply():
            for plug, value in pose.items():
                if isinstance(value, tuple):
                    cmds.setAttr(plug, *value)
                else:
                    cmds.setAttr(plug, value)

        apply()
        expected = self.world()
        for constraint_type in ("Parent", "Orient", "Point", "Swing", "Twist"):
            with self.subTest(type=constraint_type):
                self.setUp()
                self.constrain(constraint_type, 0.0)
                apply()
                self.assertTrue(self.world().isEquivalent(expected, 1e-4), "%s at weight 0 is not the unconstrained pose" % constraint_type)

    def test_twist_halfway_takes_half_the_parents_twist(self):
        # a twists the parent 60 about X, the driver has none. Full Twist: no rotation, pivot at rest. Weight 0: the parent's 60
        # and its pivot (1, 1.5, 2.598). Halfway: 30 about X and the pivot between.
        self.constrain("Twist", 0.5)
        cmds.setAttr(A_CTRL + ".rotateX", 60)
        self.assertWorldRotation(CHILD_JNT, [30, 0, 0])
        self.assertWorldTranslation(CHILD_JNT, [1, 2.25, SIN60 / 2])

    def test_orient_halfway(self):
        # a turns the parent 90 about Z. Orient keeps the driver's rotation (none) and the parent's pivot (-3, 1, 0).
        self.constrain("Orient", 0.5)
        cmds.setAttr(A_CTRL + ".rotateZ", 90)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 45])
        self.assertWorldTranslation(CHILD_JNT, [-3, 1, 0])

    def test_point_halfway(self):
        # Point keeps the parent's rotation and the driver's pivot (1, 3, 0): halfway to the parent's pivot (-3, 1, 0).
        self.constrain("Point", 0.5)
        cmds.setAttr(A_CTRL + ".rotateZ", 90)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 90])
        self.assertWorldTranslation(CHILD_JNT, [-1, 2, 0])

    def test_parent_halfway_reads_the_parent(self):
        # A full Parent constraint takes the driver's motion alone (none here). Weight 0.5 is halfway to the parent's.
        self.constrain("Parent", 0.5)
        cmds.setAttr(A_CTRL + ".rotateZ", 90)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 45])
        self.assertWorldTranslation(CHILD_JNT, [-1, 2, 0])

    def test_weight_can_be_changed_after_the_build(self):
        con = self.constrain("Twist")
        cmds.setAttr(A_CTRL + ".rotateX", 60)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 0])
        cmds.setAttr(con + ".constraintWeight", 0.0)
        self.assertWorldRotation(CHILD_JNT, [60, 0, 0])
        self.assertWorldTranslation(CHILD_JNT, [1, 1.5, SIN60])

    def test_template_key(self):
        template = fixtures.load_template("fkchain_two_chains")
        template["constraints"] = [{"name": "cTwist_00_M_CON", "type": "Twist", "target": "fkchain_02_L_MOD.start", "constraintWeight": 0.5,
                                    "drivers": [{"socket": "layout_00_M_MOD.end", "label": "World", "weight": 1.0}]}]
        cmds.file(new=True, force=True)
        self.build(template, self.make_skeleton(self.SKELETON))
        self.assertAlmostEqual(cmds.getAttr(rig_name("cTwist_00_M_CON") + ".constraintWeight"), 0.5)
        cmds.setAttr(A_CTRL + ".rotateX", 60)
        self.assertWorldRotation(CHILD_JNT, [30, 0, 0])


class SpaceSwitch(ConstraintWeightCase):

    def pose(self):
        cmds.setAttr(A_CTRL + ".rotate", 10, 20, 30)
        cmds.setAttr(rig_name("b_00_M_CTRL") + ".rotateX", -40)
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateY", 25)

    def test_a_switch_at_partial_weight_does_not_move_the_control(self):
        for constraint_type in ("Parent", "Orient", "Point", "Swing", "Twist"):
            with self.subTest(type=constraint_type):
                self.setUp()
                con = self.constrain(constraint_type, 0.4, ["layout_00_M_MOD.end", "fkchain_01_M_MOD.start"])
                self.pose()
                before = self.world()
                cmds.setRigConstraintSpace(con, space=1)
                self.assertTrue(self.world().isEquivalent(before, 1e-4), "moved on a %s space switch at weight 0.4" % constraint_type)

    def test_creating_on_a_posed_rig_does_not_move_the_control(self):
        self.pose()
        before = self.world()
        self.constrain("Twist", 0.3)
        self.assertTrue(self.world().isEquivalent(before, 1e-4))

    def test_a_switch_at_weight_zero_changes_nothing(self):
        con = self.constrain("Orient", 0.0, ["layout_00_M_MOD.end", "fkchain_01_M_MOD.start"])
        self.pose()
        before = self.world()
        values = cmds.getAttr(CHILD_CTRL + ".rotate")[0]
        cmds.setRigConstraintSpace(con, space=1)
        self.assertTrue(self.world().isEquivalent(before, 1e-4))
        self.assertEqual(cmds.getAttr(CHILD_CTRL + ".rotate")[0], values)


class Rejected(ConstraintWeightCase):

    def test_weight_above_one(self):
        self.assertMayaError(lambda: self.constrain("Orient", 1.5), "constraintWeight", "between 0 and 1")

    def test_negative_weight(self):
        self.assertMayaError(lambda: self.constrain("Orient", -0.1), "constraintWeight", "between 0 and 1")

    def test_parent_below_one_needs_a_parent(self):
        cmds.disconnectAttr(rig_name("fkchain_01_M_MOD") + ".outputSocketMatrix[1]",
                            rig_name("fkchain_02_L_MOD") + ".childPlugs[0].childPlugParentMatrix")
        self.assertMayaError(lambda: self.constrain("Parent", 0.5), "no parent")

    def test_parent_at_one_without_a_parent_is_still_allowed(self):
        cmds.disconnectAttr(rig_name("fkchain_01_M_MOD") + ".outputSocketMatrix[1]",
                            rig_name("fkchain_02_L_MOD") + ".childPlugs[0].childPlugParentMatrix")
        self.assertExists(self.constrain("Parent", 1.0))
