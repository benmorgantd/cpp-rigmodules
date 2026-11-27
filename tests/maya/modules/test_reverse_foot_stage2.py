"""ReverseFoot, stage 2: the toe and toe tip joints, the toe FK control, the blend the foot takes from its limb (ikFkDriver),
the visibility it drives and the pivot sockets.

The foot is the core test foot (tests/core/FootSolveTests.cpp) at a tenth of its size on the limb_arm hand at (4, 4, 0): a core point
(x, y, z) is the world point (4 + x/10, 3.2 + y/10, 0.6 + z/10), except the toe pivot, which sits at the toe joint (4, 3.3, 1.8), above the
sole. The toe tip joint is at the toe tip pivot (4, 3.2, 2.4). Both joints have the hand's rest rotation, Rz 90, because they are its
children in the fixture skeleton. The roll below the break turns about the toe pivot, so the hand keeps its rest distance (the bone,
1.931321) to the planted toe. Every expected number is worked by hand from those, in the test it is used in. Nothing here is recorded
from the rig.
"""
import copy
import json
import math

import maya.api.OpenMaya as om
from maya import cmds

from rigtest import fixtures
from rigtest.case import RigTestCase, rig_name, world_rotation_matrix

FOOT = "foot_00_L_CTRL"
FOOT_MODULE = "foot_00_L_MOD"
LIMB = "arm_01_L_MOD"
HAND = "hand_00_L_JNT"
HANDLE = "armIK_00_L_CTRL"
START = "upperArm_00_L_CTRL"
SETTINGS = "armSettings_00_L_CTRL"
TOE = "toe_00_L_JNT"
TOE_TIP = "toeTip_00_L_JNT"
TOE_FK = "toeFK_00_L_CTRL"
TOE_REST = (4.0, 3.3, 1.8)
TIP_REST = (4.0, 3.2, 2.4)
BONE = 1.9313208   # |toe - hand| at rest: |(0, -0.7, 1.8)|


def rotation(axis, degrees):
    """A rotation about a world axis, as a matrix of Maya's row vectors."""
    angles = [0.0, 0.0, 0.0]
    angles["xyz".index(axis)] = om.MAngle(degrees, om.MAngle.kDegrees).asRadians()
    return om.MEulerRotation(angles).asMatrix()


class ReverseFootStage2(RigTestCase):

    def _build(self):
        self.build_standard("reverse_foot", "reverse_foot", markers="reverse_foot")

    def world(self, node):
        return cmds.xform(rig_name(node), query=True, worldSpace=True, translation=True)

    def assertWorld(self, node, position, tolerance=1e-4):
        actual = self.world(node)
        self.assertTrue(all(abs(a - e) <= tolerance for a, e in zip(actual, position)), "%s: expected %s, got %s" % (node, position, actual))

    def assertTurned(self, node, about_z, then_about_x=0.0):
        """The world rotation is Rz(about_z) and then Rx(then_about_x), both about the world's axes (a point turns by Rz first)."""
        expected = rotation("z", about_z) * rotation("x", then_about_x)
        self.assertTrue(world_rotation_matrix(rig_name(node)).isEquivalent(expected, 1e-4), "%s is not Rz %s then Rx %s" % (node, about_z, then_about_x))

    def test_the_foot_and_toe_fk_sockets_carry_their_controls_worlds(self):
        """`foot` and `toeFk` (outputSocketMatrix elements 8 and 9) are the foot control's and the toe FK control's world matrices, with
        the handle moved (the foot control rides it) and the toe FK control turned."""
        self._build()
        cmds.setAttr(rig_name(HANDLE + ".translateX"), 1)
        cmds.setAttr(rig_name(TOE_FK + ".rotateX"), 20)
        for control, element in ((FOOT, 8), (TOE_FK, 9)):
            socket = cmds.getAttr(rig_name(FOOT_MODULE) + ".outputSocketMatrix[%d]" % element)
            world = cmds.xform(rig_name(control), query=True, worldSpace=True, matrix=True)
            for a, b in zip(socket, world):
                self.assertAlmostEqual(a, b, places=4, msg=control)

    def test_the_plugs_dag_parents_place_the_foot_control_and_the_toe_fk_control(self):
        """The ik plug carries the foot control and the ankle plug the toe FK control, so the template's dagParent places each in the
        outliner: `"ik": "arm_01_L_MOD.ik"` puts the foot control under the IK handle, and `"ankle": "arm_01_L_MOD.end"` puts the toe FK
        control under the FK hand (the end socket carries the FK end control's transform). The DAG is organization only (D1)."""
        self._build()
        for control, parent in ((FOOT, HANDLE), (TOE_FK, "hand_00_L_CTRL")):
            parents = cmds.listRelatives(rig_name(control), parent=True) or []
            self.assertEqual([p.split(":")[-1] for p in parents], [parent], control)

    def test_the_toes_sit_at_rest_with_the_foot_at_rest(self):
        self._build()
        self.assertWorld(TOE, TOE_REST)
        self.assertWorld(TOE_TIP, TIP_REST)
        self.assertTurned(TOE, 90)
        self.assertTurned(TOE_TIP, 90)

    def test_the_toe_follows_the_foot_when_the_leg_reaches(self):
        """A roll of 20 turns the ankle about the toe pivot (4, 3.3, 1.8): the hand's offset (0, 0.7, -1.8) from it turned 20 about X puts
        the hand at (4, 3.3 + 0.7 cos20 + 1.8 sin20, 1.8 + 0.7 sin20 - 1.8 cos20) = (4, 4.573421, 0.347967), still the bone's 1.931321 from
        the toe, so the toe stays planted at its rest and the length-keeping pull changes nothing. (With the old ball pivot apart from the toe
        it slid the toe and dropped the toe tip.) The tap of 25 turns the toe 25 about X, and the toe tip, 0.6 beyond and 0.1 below the toe,
        swings with it: (0, -0.1 cos25 - 0.6 sin25, -0.1 sin25 + 0.6 cos25) from the toe, so (4, 2.955798, 2.301523)."""
        self._build()
        cmds.setAttr(rig_name(FOOT + ".footRoll"), 20)
        cmds.setAttr(rig_name(FOOT + ".toeTap"), 25)
        self.assertWorld(HAND, (4.0, 4.573421, 0.347967))
        self.assertWorld(TOE, TOE_REST)
        self.assertTurned(TOE, 90, 25)
        self.assertWorld(TOE_TIP, (4.0, 2.955798, 2.301523))
        self.assertTurned(TOE_TIP, 90, 25)

    def test_the_ball_pitch_and_roll_leave_the_toe_and_toe_tip_planted(self):
        """The regression of 2026-11-25: footRoll inside the ball regime, ballRaise and ballRoll turn the ankle about the toe pivot, which is
        the toe joint, so the toe and toe tip stay at their rests and keep the rest rotation, Rz 90."""
        self._build()
        for channel, value in (("footRoll", 25), ("ballRaise", 10), ("ballRoll", 12)):
            cmds.setAttr(rig_name(FOOT + "." + channel), value)
        self.assertWorld(TOE, TOE_REST)
        self.assertWorld(TOE_TIP, TIP_REST)
        self.assertTurned(TOE, 90)
        self.assertTurned(TOE_TIP, 90)

    def test_the_ball_swivel_turns_the_whole_foot_toe_tip_included(self):
        """ballSwivel 15 turns the whole foot about up through the toe pivot (4, 3.3, 1.8), the author's "squash a bug" (2026-11-25). The toe
        stays at the pivot and turns Rz 90 then Ry 15. The toe tip, offset (0, -0.1, 0.6) from the toe, swings to (0.6 sin15, -0.1, 0.6 cos15),
        so (4.155291, 3.2, 2.379555). The hand, offset (0, 0.7, -1.8), goes to (4 - 1.8 sin15, 4, 1.8 - 1.8 cos15) = (3.534126, 4, 0.061334)."""
        self._build()
        cmds.setAttr(rig_name(FOOT + ".ballSwivel"), 15)
        self.assertWorld(HAND, (3.534126, 4.0, 0.061334))
        self.assertWorld(TOE, TOE_REST)
        self.assertWorld(TOE_TIP, (4.155291, 3.2, 2.379555))
        expected = rotation("z", 90) * rotation("y", 15)
        for node in (TOE, TOE_TIP):
            self.assertTrue(world_rotation_matrix(rig_name(node)).isEquivalent(expected, 1e-4), "%s is not Rz 90 then Ry 15" % node)

    def test_the_toe_tap_alone_turns_the_toe_and_moves_nothing_else(self):
        """The toe tip swings 25 about the toe: (0, -0.1 cos25 - 0.6 sin25, -0.1 sin25 + 0.6 cos25) = (0, -0.34419, 0.501459) from it."""
        self._build()
        cmds.setAttr(rig_name(FOOT + ".toeTap"), 25)
        self.assertWorld(HAND, (4.0, 4.0, 0.0))
        self.assertWorld(TOE, TOE_REST)
        self.assertTurned(TOE, 90, 25)
        self.assertWorld(TOE_TIP, (4.0, 3.3 - 0.34419, 1.8 + 0.501459))

    def test_at_blend_0_the_toe_is_the_fk_toe_and_the_channels_do_nothing(self):
        """The Limb ignores ikTarget at blend 0 and the toe is the FK toe, so a roll of 20 moves neither the hand nor the toe. The toe FK
        control turns the toe about its own Z (the world's, because Rz 90 keeps Z): 30 more makes Rz 120 at the toe, and the toe tip, offset
        (0, -0.1, 0.6), goes to (0.1 sin30, -0.1 cos30, 0.6) = (0.05, -0.086603, 0.6) from the toe."""
        self._build()
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        cmds.setAttr(rig_name(FOOT + ".footRoll"), 20)
        self.assertWorld(HAND, (4.0, 4.0, 0.0))
        self.assertWorld(TOE, TOE_REST)
        cmds.setAttr(rig_name(TOE_FK + ".rotateZ"), 30)
        self.assertWorld(TOE, TOE_REST)
        self.assertTurned(TOE, 120)
        self.assertWorld(TOE_TIP, (4.05, 3.3 - 0.086603, 2.4))
        self.assertTurned(TOE_TIP, 120)

    def test_at_blend_0_the_toe_rides_the_actual_ankle(self):
        """The FK start control turned 90 about Z swings the whole limb about the shoulder at the origin: the hand goes (4, 4, 0) to
        (-4, 4, 0), the toe (4, 3.3, 1.8) to (-3.3, 4, 1.8) and the toe tip (4, 3.2, 2.4) to (-3.2, 4, 2.4), each turned 90 more (Rz 180)."""
        self._build()
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        cmds.setAttr(rig_name(START + ".rotateZ"), 90)
        self.assertWorld(HAND, (-4.0, 4.0, 0.0))
        self.assertWorld(TOE, (-3.3, 4.0, 1.8))
        self.assertTurned(TOE, 180)
        self.assertWorld(TOE_TIP, (-3.2, 4.0, 2.4))

    def test_at_blend_half_the_toe_is_halfway_between_the_fk_and_ik_toes(self):
        """A roll of 20. The hand blends halfway from (4, 4, 0) to its target (4, 4.573421, 0.347967): (4, 4.286711, 0.173984), turned 10
        about X. The FK toe rides it: the hand-to-toe offset (0, -0.7, 1.8) turned 10 is (0, -1.001932, 1.651100), so the toe is at
        (4, 3.284778, 1.825084). The IK toe is the planted toe pivot (4, 3.3, 1.8), not turned (its own pivot does not carry it), pulled
        to keep the bone length 1.931321 from the hand. The halfway hand is the midpoint of a chord of the circle about the toe pivot, so
        the line from it to the pivot is the offset turned 10, and the pulled IK toe lands on the FK toe. The blended toe is there too,
        turned 5 about X (halfway from 10 to 0), and the toe tip (0, -0.1 cos5 - 0.6 sin5, -0.1 sin5 + 0.6 cos5) from it: (4, 3.132865, 2.414085)."""
        self._build()
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0.5)
        cmds.setAttr(rig_name(FOOT + ".footRoll"), 20)
        self.assertWorld(HAND, (4.0, 4.286711, 0.173984))
        self.assertWorld(TOE, (4.0, 3.284778, 1.825084))
        self.assertTurned(TOE, 90, 5)
        self.assertWorld(TOE_TIP, (4.0, 3.132865, 2.414085))

    def test_a_leg_that_cannot_reach_keeps_the_bone_length_and_aims_at_the_pivot(self):
        """The handle goes 10 up (its local X is the world's Y) with the foot at rest: the toe pivot would be at (4, 13.3, 1.8), far beyond the
        arm's reach of 8. The hand stops short, and the toe stays one bone length from it, on the line to where the pivot would be."""
        self._build()
        cmds.setAttr(rig_name(HANDLE + ".translateX"), 10)
        hand = self.world(HAND)
        wanted = (4.0, 13.3, 1.8)
        toward = [w - h for w, h in zip(wanted, hand)]
        length = math.sqrt(sum(v * v for v in toward))
        self.assertGreater(length, 3 * BONE, "the pivot is far from the hand: the limb can't reach")
        self.assertWorld(TOE, [h + v * BONE / length for h, v in zip(hand, toward)])
        toe = self.world(TOE)
        self.assertAlmostEqual(math.sqrt(sum((t - h) ** 2 for t, h in zip(toe, hand))), BONE, places=4)

    def test_the_blend_hides_the_controls_it_makes_useless(self):
        self._build()

        def visible(control):
            # `shown`, not visibility: visibility inherits through the DAG, and a control can be a DAG parent.
            return cmds.getAttr(rig_name(control + ".shown"))

        self.assertEqual((visible(FOOT), visible(TOE_FK)), (True, False))   # IK: the channels drive and the toe FK control hides
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0.5)
        self.assertEqual((visible(FOOT), visible(TOE_FK)), (True, True))
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        self.assertEqual((visible(FOOT), visible(TOE_FK)), (False, True))   # FK: the foot's channels have nothing to move

    def test_the_foot_follows_the_limbs_blend_and_an_animator_cannot_key_it(self):
        self._build()
        module = rig_name(FOOT_MODULE)
        self.assertSource(module + ".ikFkBlend", rig_name(LIMB) + ".ikFkBlend")
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0.25)
        self.assertAlmostEqual(cmds.getAttr(module + ".ikFkBlend"), 0.25)
        self.assertFalse(cmds.getAttr(module + ".ikFkBlend", keyable=True))
        self.assertFalse(cmds.getAttr(module + ".ikFkBlend", channelBox=True))
        self.assertNotIn("ikFkBlend", cmds.listAttr(rig_name(FOOT), keyable=True))
        self.assertNotIn("ikFkBlend", cmds.listAttr(rig_name(TOE_FK), keyable=True))

    def test_the_mirrored_foot_follows_the_other_sides_limb(self):
        self.build_standard("reverse_foot_mirror", "reverse_foot_mirror", markers="reverse_foot_mirror")
        right = rig_name("foot_00_R_MOD")
        left = rig_name(FOOT_MODULE)
        self.assertEqual(cmds.getAttr(right + ".ikFkDriver"), "arm_00_R_MOD")   # the mirror template's limb is arm_00_L_MOD
        self.assertSource(right + ".ikFkBlend", rig_name("arm_00_R_MOD") + ".ikFkBlend")
        self.assertSource(left + ".ikFkBlend", rig_name("arm_00_L_MOD") + ".ikFkBlend")
        cmds.setAttr(rig_name("armSettings_00_R_CTRL.ikFkBlend"), 0.25)
        self.assertAlmostEqual(cmds.getAttr(right + ".ikFkBlend"), 0.25)
        self.assertAlmostEqual(cmds.getAttr(left + ".ikFkBlend"), 1.0, msg="the left foot keeps following the left limb")

    def test_the_toe_fk_control_turns_and_nothing_else(self):
        self._build()
        self.assertEqual(sorted(a for a in cmds.listAttr(rig_name(TOE_FK), keyable=True) if a != "visibility"), ["rotateX", "rotateY", "rotateZ"])
        markup = json.loads(cmds.getAttr(rig_name(TOE_FK + ".controlMarkup")))
        self.assertEqual((markup["side"], markup["usage"]), ("Left", "toeFK"))

    def test_a_numbered_toe_usage_puts_fk_before_the_number(self):
        """A toe joint marked ball_00 (the solomon asset's) gives the toe FK usage ballFK_00 and the control ballFK_00_L_CTRL. Before the fix
        it gave ball_00FK, which is not camelCase, and the build failed validation."""
        joints = self.make_skeleton("reverse_foot")
        index = [i for i, j in enumerate(joints) if j.endswith(TOE)][0]
        joints[index] = ":" + cmds.rename(joints[index], "skel:ball_00_L_JNT")
        cmds.rigJointMarkup(joints[index], edit=True, usage="ball_00")
        template = copy.deepcopy(fixtures.load_template("reverse_foot"))
        template["rigModules"][1]["moduleArgs"]["joints"][1] = ["Left", "ball_00"]
        self.build(template, joints, markers="reverse_foot")
        control = rig_name("ballFK_00_L_CTRL")
        self.assertTrue(cmds.objExists(control))
        markup = json.loads(cmds.getAttr(control + ".controlMarkup"))
        self.assertEqual((markup["side"], markup["usage"]), ("Left", "ballFK_00"))

    def test_the_pivots_and_the_toes_are_sockets_with_their_worlds(self):
        """The pivot sockets follow the chain order (heel, toeTip, bankOut, bankIn, toe). At a roll of 20 (break 30): the heel does not
        turn, so its socket stays at the heel marker (4, 3.2, -0.4). The toe pivot socket turns 20 about X at the toe pivot (4, 3.3, 1.8).
        The toe tip pivot is before the toe in the chain, so nothing turns it: it stays at (4, 3.2, 2.4), not turned. Past the break (roll 50)
        the toe tip turns 20 about its own position, which does not move it. The roles toe and toeTip are named toePivot and toeTipPivot,
        because toe and toeTip are the bind joints' sockets. There is no ball socket (the ball pivot was removed on 2026-11-25)."""
        self._build()
        cmds.setAttr(rig_name(FOOT + ".footRoll"), 20)
        module = rig_name(FOOT_MODULE)
        names = [json.loads(cmds.getAttr("%s.socketNames[%d]" % (module, i)))[0] for i in range(10)]
        self.assertEqual(names, ["ankle", "toe", "toeTip", "heel", "toeTipPivot", "bankOut", "bankIn", "toePivot", "foot", "toeFk"])
        self.assertEqual(cmds.getAttr(module + ".socketNames", size=True), 10)

        def socket(index):
            matrix = cmds.getAttr("%s.outputSocketMatrix[%d]" % (module, index))
            return matrix[12:15], matrix

        def assertAt(index, expected):
            position, _ = socket(index)
            self.assertTrue(all(abs(a - e) < 1e-4 for a, e in zip(position, expected)), "socket %d: expected %s, got %s" % (index, expected, position))

        assertAt(3, (4.0, 3.2, -0.4))
        self.assertAlmostEqual(socket(3)[1][5], 1.0, places=4, msg="the heel socket is not turned")
        assertAt(7, TOE_REST)
        self.assertAlmostEqual(socket(7)[1][6], math.sin(math.radians(20)), places=4, msg="the toe pivot socket is turned 20 about X")
        assertAt(4, (4.0, 3.2, 2.4))
        self.assertAlmostEqual(socket(4)[1][5], 1.0, places=4, msg="the toe tip pivot socket is not turned below the break")
        cmds.setAttr(rig_name(FOOT + ".footRoll"), 50)
        assertAt(4, (4.0, 3.2, 2.4))   # past the break the toe tip turns about itself and stays on the ground
        self.assertAlmostEqual(socket(4)[1][6], math.sin(math.radians(20)), places=4, msg="the toe tip pivot socket is turned 20 past the break")
        cmds.setAttr(rig_name(FOOT + ".footRoll"), 20)
        # The toe and toe tip sockets are the joints' worlds.
        assertAt(1, self.world(TOE))
        assertAt(2, self.world(TOE_TIP))

    def test_the_toe_joints_are_the_foots_and_the_ankle_the_limbs(self):
        self._build()
        for joint in (TOE, TOE_TIP):
            self.assertOpmDrivenBy(rig_name(joint), FOOT_MODULE)
        self.assertOpmDrivenBy(rig_name(HAND), LIMB)

    def test_the_graph_stays_acyclic_with_the_toe_reading_the_end_socket(self):
        """The toe reads the Limb's end socket through the ankle plug, and the ankle socket that feeds the Limb never does: a cold evaluation
        in DG and in parallel mode gives the same toe."""
        self._build()
        for evaluation in ("off", "parallel"):
            cmds.evaluationManager(mode=evaluation)
            cmds.setAttr(rig_name(FOOT + ".footRoll"), 20)
            cmds.dgdirty(allPlugs=True)
            self.assertWorld(HAND, (4.0, 4.573421, 0.347967))   # worked in test_the_toe_follows_the_foot_when_the_leg_reaches
            self.assertWorld(TOE, TOE_REST)
            cmds.setAttr(rig_name(FOOT + ".footRoll"), 0)
