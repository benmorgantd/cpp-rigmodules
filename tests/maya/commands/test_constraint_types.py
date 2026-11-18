"""Orient, Point and Swing constraints (D31): the driver gives only its rotation (Orient), translation (Point) or
translation and rotation without its twist (Swing), and the rest comes from the child plug's parentStructure parent.

fk_two_chains: a_00_M_JNT (0,0,0) and b_00_M_JNT (0,2,0) are fkchain_1, under layout_00_M_MOD.end. c_00_M_JNT (1,3,0) is fkchain_2,
whose start plug is parented to fkchain_01_M_MOD.end. Every constraint here is on that plug, so its parent is b."""
import math

from maya import cmds
import maya.api.OpenMaya as om

from rigtest import fixtures
from rigtest.case import RigTestCase, rig_name

CHILD_CTRL = rig_name("c_00_L_CTRL")
CHILD_JNT = rig_name("c_00_M_JNT")
A_CTRL = rig_name("a_00_M_CTRL")
LAYOUT_END_CTRL = rig_name("layout_02_M_CTRL")


def world_matrix(node):
    return om.MMatrix(cmds.xform(node, q=True, ws=True, matrix=True))


class ConstraintTypeCase(RigTestCase):

    SKELETON = "fk_two_chains"

    def setUp(self):
        super().setUp()
        self.build_standard("fkchain_two_chains", self.SKELETON)

    def constrain(self, constraint_type, driver=("layout_00_M_MOD.end",)):
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
        # layout_00_M_MOD.end also parents fkchain_1, so its move carries the pivot too: (1,3,0) about Y 90 is (0,3,-1), then +5 in X.
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
        template["constraints"] = [{"name": "cSpace_00_M_CON", "type": "Orient", "target": "fkchain_02_L_MOD.start",
                                    "drivers": [{"socket": "layout_00_M_MOD.end", "label": "World", "weight": 1.0}]}]
        cmds.file(new=True, force=True)
        self.build(template, self.make_skeleton(self.SKELETON))
        self.assertEqual(cmds.getAttr(rig_name("cSpace_00_M_CON") + ".constraintType", asString=True), "Orient")
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
        # fkchain_1 rides layout_00_M_MOD.end too, so the parent's rotation is the layout's.
        self.constrain("Point")
        cmds.setAttr(LAYOUT_END_CTRL + ".translateX", 5)
        self.assertWorldTranslation(CHILD_JNT, [6, 3, 0])
        self.assertWorldRotation(CHILD_JNT, [0, 0, 0])

    def test_scale_comes_from_parent(self):
        self.constrain("Point")
        cmds.setAttr(A_CTRL + ".scale", 2, 2, 2)
        self.assertWorldTranslation(CHILD_JNT, [1, 3, 0])
        self.assertWorldScale(CHILD_JNT, [2, 2, 2])


class Swing(ConstraintTypeCase):
    """The driver's translation and its rotation without the twist about X (design/ribbon-twist-follow.md, phase 3).

    The driver here is layout_00_M_MOD.end, a control at the origin, and c's pivot is at (1,3,0), so as with Point the pivot swings about
    the origin with the driver's whole rotation, twist included (rotateX 90 takes (1,3,0) to (1,0,3), rotateZ 90 to (-3,1,0)).
    Only c's own rotation loses the twist. Its rotate order is xyz, so rotateX is applied first and is the twist, and rotateY and
    rotateZ after it are the swing: twist * swing."""

    def test_type_is_stored_on_the_node(self):
        con = self.constrain("Swing")
        self.assertEqual(cmds.getAttr(con + ".constraintType", asString=True), "Swing")

    def test_a_pure_twist_is_dropped(self):
        # Orient would turn c 90 about X. Swing keeps none of it in c's rotation, and the pivot still swings with the driver.
        self.constrain("Swing")
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateX", 90)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 0])
        self.assertWorldTranslation(CHILD_JNT, [1, 0, 3])

    def test_a_pure_swing_is_kept(self):
        self.constrain("Swing")
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateZ", 90)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 90])
        self.assertWorldTranslation(CHILD_JNT, [-3, 1, 0])

    def test_a_twist_and_a_swing_give_the_swing(self):
        # rotateX 60 then rotateZ 40 is twist 60 * swing Rz(40): the result is the swing alone.
        self.constrain("Swing")
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateX", 60)
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateZ", 40)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 40])

    def test_a_twist_past_half_a_turn_is_the_same_rotation(self):
        # 200 degrees is -160: either way the whole rotation is twist, so no swing is left.
        self.constrain("Swing")
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateX", 200)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 0])

    def test_translation_follows_the_driver(self):
        self.constrain("Swing")
        cmds.setAttr(LAYOUT_END_CTRL + ".translateX", 5)
        self.assertWorldTranslation(CHILD_JNT, [6, 3, 0])

    def test_scale_comes_from_parent(self):
        self.constrain("Swing")
        cmds.setAttr(A_CTRL + ".scale", 2, 2, 2)
        self.assertWorldTranslation(CHILD_JNT, [1, 3, 0])
        self.assertWorldScale(CHILD_JNT, [2, 2, 2])

    def test_template_type(self):
        template = fixtures.load_template("fkchain_two_chains")
        template["constraints"] = [{"name": "cSwing_00_M_CON", "type": "Swing", "target": "fkchain_02_L_MOD.start",
                                    "drivers": [{"socket": "layout_00_M_MOD.end", "label": "World", "weight": 1.0}]}]
        cmds.file(new=True, force=True)
        self.build(template, self.make_skeleton(self.SKELETON))
        self.assertEqual(cmds.getAttr(rig_name("cSwing_00_M_CON") + ".constraintType", asString=True), "Swing")
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateX", 60)
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateZ", 40)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 40])


class SwingShoulder(RigTestCase):
    """The shoulder the Swing constraint is for (design/ribbon-twist-follow.md): an FK arm along X with a ribbon on each bone. The
    upper ribbon hangs from the shoulder's socket twice, a Swing constraint on its start (the shoulder's position and swing, no
    twist) and a Point constraint on its end (the elbow's position, and the shoulder's whole rotation from the plug's parent).
    The shoulder's twist then eases in along the upper arm, and the forearm, which starts on the elbow socket, meets it.

    With rotate order xyz, rotateX on the shoulder control is the twist about the bone and rotateZ the swing, so the shoulder's
    rotation is Rx(t) * Rz(s) in row order. The upper ribbon's first joint should hold Rz(s) and its last Rx(t) * Rz(s)."""

    # The arm drops 0.3 over 4 (a 4.3 degree tilt of the bone from X, which the TwoBoneIk needs for its bend), so a twist about
    # the tangent differs from one about X by a few degrees at the middle joints. The wrong answers differ by tens of degrees.
    TOLERANCE_DEGREES = 5.0
    JOINTS = ("up1_00_M_JNT", "up2_00_M_JNT", "up3_00_M_JNT", "up4_00_M_JNT", "up5_00_M_JNT", "fo1_00_M_JNT")

    def setUp(self):
        super().setUp()
        self.start("ribbon_arm")

    def start(self, skeleton):
        cmds.file(new=True, force=True)
        self.build_standard("ribbon_arm_swing", skeleton)
        cmds.setAttr(rig_name("armSettings_00_L_CTRL.ikFkBlend"), 0)   # FK: the shoulder control's rotation reaches the elbow socket
        self.rest = {joint: world_matrix(rig_name(joint)) for joint in self.JOINTS}

    def pose(self, twist, swing):
        cmds.setAttr(rig_name("upperArm_00_L_CTRL.rotateX"), twist)
        cmds.setAttr(rig_name("upperArm_00_L_CTRL.rotateZ"), swing)

    def motion(self, joint):
        """The joint's motion in its own rest frame, world * rest^-1: what its control's channels would hold. A rotation about the
        joint's own X is a twist about its bone, whichever way the bone points in the world."""
        return world_matrix(rig_name(joint)) * self.rest[joint].inverse()

    def twist_of(self, joint):
        q = om.MTransformationMatrix(self.motion(joint)).rotation(asQuaternion=True)
        return math.degrees(2.0 * math.atan2(q.x, q.w))

    def assertTurnedBy(self, joint, twist, swing):
        expected = om.MEulerRotation(om.MAngle(twist, om.MAngle.kDegrees).asRadians(), 0, om.MAngle(swing, om.MAngle.kDegrees).asRadians()).asMatrix()
        relative = om.MTransformationMatrix(self.motion(joint) * expected.inverse()).rotation(asQuaternion=True)
        angle = math.degrees(2.0 * math.acos(min(1.0, abs(relative.w))))
        self.assertLess(angle, self.TOLERANCE_DEGREES, "%s is %.2f degrees from twist %s then swing %s" % (joint, angle, twist, swing))

    def test_a_twist_eases_in_from_nothing_at_the_shoulder_to_the_whole_twist_at_the_elbow(self):
        """The twist of each joint about its own X: within a couple of degrees of the twist's share at the joint's place along the
        ribbon."""
        for twist in (60, 170, -170):
            self.pose(twist, 0)
            for joint, share in (("up1_00_M_JNT", 0.0), ("up2_00_M_JNT", 0.25), ("up3_00_M_JNT", 0.5), ("up4_00_M_JNT", 0.75), ("up5_00_M_JNT", 1.0)):
                self.assertAlmostEqual(math.remainder(self.twist_of(joint) - twist * share, 360.0), 0.0, delta=2.0, msg="twist %s, %s" % (twist, joint))

    def test_a_swing_alone_turns_every_joint_by_the_swing(self):
        self.pose(0, 40)
        for joint in ("up1_00_M_JNT", "up3_00_M_JNT", "up5_00_M_JNT", "fo1_00_M_JNT"):
            self.assertTurnedBy(joint, 0, 40)

    def test_the_start_takes_the_swing_and_not_the_twist_and_the_end_takes_both(self):
        self.pose(60, 40)
        self.assertTurnedBy("up1_00_M_JNT", 0, 40)
        self.assertTurnedBy("up3_00_M_JNT", 30, 40)
        self.assertTurnedBy("up5_00_M_JNT", 60, 40)

    def test_the_forearm_meets_the_upper_arm_at_the_elbow(self):
        self.pose(60, 40)
        self.assertTurnedBy("fo1_00_M_JNT", 60, 40)

    def test_a_tilted_arm_is_the_same_whichever_way_its_bone_points(self):
        """The same poses on an arm turned 35 degrees about Z, every joint turned with it (+X down the bone). A delta is a motion
        in world axes, so a twist read about the world X would be wrong here: it must be read about the bone."""
        self.start("ribbon_arm_tilted")
        self.pose(60, 0)
        for joint, share in (("up1_00_M_JNT", 0.0), ("up3_00_M_JNT", 0.5), ("up5_00_M_JNT", 1.0)):
            self.assertAlmostEqual(self.twist_of(joint), 60 * share, delta=2.0, msg=joint)
        self.pose(60, 40)
        self.assertTurnedBy("up1_00_M_JNT", 0, 40)
        self.assertTurnedBy("up5_00_M_JNT", 60, 40)
        self.assertTurnedBy("fo1_00_M_JNT", 60, 40)

    def test_without_the_swing_constraint_the_whole_arm_turns_by_the_twist(self):
        """Parenting the start on the shoulder alone carries its twist rigidly (the probe's 'no ease' wiring): every joint takes it."""
        data = fixtures.load_template("ribbon_arm_swing")
        data["constraints"] = [c for c in data["constraints"] if c["name"] != "upperStartSwing_00_M_CON"]
        cmds.file(new=True, force=True)
        self.build(data, self.make_skeleton("ribbon_arm"), label="ribbon_arm_no_swing")
        cmds.setAttr(rig_name("armSettings_00_L_CTRL.ikFkBlend"), 0)
        self.rest = {joint: world_matrix(rig_name(joint)) for joint in self.JOINTS}
        self.pose(60, 0)
        for joint in ("up1_00_M_JNT", "up3_00_M_JNT", "up5_00_M_JNT"):
            self.assertTurnedBy(joint, 60, 0)


class TypedSpaceSwitch(ConstraintTypeCase):
    """D27 matching with the node's own output for each driver."""

    def pose(self):
        cmds.setAttr(A_CTRL + ".rotate", 10, 20, 30)
        cmds.setAttr(rig_name("b_00_M_CTRL") + ".rotateX", -40)
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateY", 25)

    def test_switch_does_not_move_control(self):
        for constraint_type in ("Orient", "Point", "Swing"):
            with self.subTest(type=constraint_type):
                self.setUp()
                con = self.constrain(constraint_type, ["layout_00_M_MOD.end", "fkchain_01_M_MOD.start"])
                self.pose()
                before = world_matrix(CHILD_CTRL)
                cmds.setRigConstraintSpace(con, space=1)
                self.assertWorldMatrix(CHILD_CTRL, before, "moved on a %s space switch" % constraint_type)

    def test_orient_switch_writes_only_rotate(self):
        con = self.constrain("Orient", ["layout_00_M_MOD.end", "fkchain_01_M_MOD.start"])
        self.pose()
        cmds.setRigConstraintSpace(con, space=1)
        for value in cmds.getAttr(CHILD_CTRL + ".translate")[0]:
            self.assertAlmostEqual(value, 0.0, places=4)
        self.assertNotEqual(cmds.getAttr(CHILD_CTRL + ".rotate")[0], (0.0, 0.0, 0.0))

    def test_point_switch_writes_only_translate(self):
        con = self.constrain("Point", ["layout_00_M_MOD.end", "fkchain_01_M_MOD.start"])
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
        self.constrain("Orient", ["layout_00_M_MOD.end", "fkchain_01_M_MOD.start"])
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
                cmds.setAttr(rig_name("b_00_M_CTRL") + ".rotate", 70, 10, -20)
                cmds.setAttr(rig_name("b_00_M_CTRL") + ".translate", 0.5, 0, 1)
                cmds.setAttr(LAYOUT_END_CTRL + ".rotate", -30, 60, 5)
                cmds.setAttr(LAYOUT_END_CTRL + ".translate", 2, 1, -3)
                cmds.setAttr(CHILD_CTRL + ".rotate", 5, 40, -15)
                cmds.setAttr(CHILD_CTRL + ".translate", 0.3, -0.2, 0.7)
                self.assertWorldMatrix(CHILD_JNT, world_matrix(reference),
                                       "%s differs from Maya's point and orient constraints" % constraint_type)


class Rejected(ConstraintTypeCase):

    def test_unknown_type(self):
        self.assertMayaError(lambda: self.constrain("Scale"), "Scale", "(has: Parent, Orient, Point, Swing)")

    def test_orient_without_parent(self):
        cmds.disconnectAttr(rig_name("fkchain_01_M_MOD") + ".outputSocketMatrix[1]",
                            rig_name("fkchain_02_L_MOD") + ".childPlugs[0].childPlugParentMatrix")
        self.assertMayaError(lambda: self.constrain("Orient"), "no parent")

    def test_parent_without_parent_is_allowed(self):
        cmds.disconnectAttr(rig_name("fkchain_01_M_MOD") + ".outputSocketMatrix[1]",
                            rig_name("fkchain_02_L_MOD") + ".childPlugs[0].childPlugParentMatrix")
        self.assertExists(self.constrain("Parent"))
