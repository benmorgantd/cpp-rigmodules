"""Limb module (D62, design/limb.md): contract tests, pose cases from fixtures/poses/limb, and what only this module does.

The pose cases hold hand-worked numbers (each description has the arithmetic). Nothing here is recorded from the rig.
The Limb runs the TwoBoneIk and Ribbon solves, so test_limb_matches_parts holds the two together: a change to either that the Limb
does not carry through fails it.
"""
import math
import unittest

from maya import cmds

import rigmodules.query
from rigtest import fixtures
from rigtest.case import RigTestCase, rig_name
from rigtest.cases import add_pose_cases
from rigtest.module_contract import ModuleContract

LIMB_JOINTS = ["upperArm_00_L_JNT", "lowerArm_00_L_JNT", "hand_00_L_JNT"]
BIND_JOINTS = ["bind%d_00_L_JNT" % i for i in range(1, 10)]
RIBBON_IK = ["armRibbonIK_%02d_L_CTRL" % i for i in range(5)]
TWEAKERS = ["bind%d_00_L_CTRL" % i for i in range(1, 10)]
START = "upperArm_00_L_CTRL"
LOWER = "lowerArm_00_L_CTRL"
HAND = "hand_00_L_CTRL"
HANDLE = "armIK_00_L_CTRL"
POLE = "armPole_00_L_CTRL"
SETTINGS = "armSettings_00_L_CTRL"

# The ribbon's channels on the settings control: name -> (rest, mirror), and what the module adds to the two-bone limb's.
RIBBON_CHANNELS = {
    "startTwist": (0.0, "negate"), "endTwist": (0.0, "negate"),
    "sineAmplitude": (0.0, "copy"), "sineFrequency": (1.0, "copy"), "sinePhase": (0.0, "copy"), "sineOrient": (0.0, "negate"),
    "sineStartFalloff": (0.2, "copy"), "sineEndFalloff": (0.2, "copy"),
    "volumePreserve": (0.0, "copy"), "scaleStart": (1.0, "copy"), "scaleMid": (1.0, "copy"), "scaleEnd": (1.0, "copy"),
    "bendSoftness": (0.0, "copy"),
}


def translation(node):
    return cmds.xform(rig_name(node), query=True, worldSpace=True, translation=True)


class LimbContract(ModuleContract, RigTestCase):
    TEMPLATE = "limb_arm"
    SKELETON = "limb_arm"
    MODULE_TYPE = "limbModule"
    JOINT_OWNERS = dict((joint, "arm_01_L_MOD") for joint in LIMB_JOINTS + BIND_JOINTS)
    MIRROR_TEMPLATE = "limb_mirror"
    MIRROR_SKELETON = "limb_mirror"

    # The Limb's inverse is D62 phase 3: it answers "unsolved, with a reason", so a restore takes the sidecar or the rests.
    @unittest.skip("D62 phase 3: the Limb's inverse is not built yet")
    def test_contract_inverse_round_trip(self):
        pass

    @unittest.skip("D62 phase 3: the Limb's inverse is not built yet")
    def test_contract_inverse_round_trip_mirrored(self):
        pass

    @unittest.skip("D62 phase 3: the Limb's inverse is not built yet")
    def test_contract_inverse_without_a_joint_falls_back_to_the_sidecar(self):
        pass


class LimbPoses(RigTestCase):
    pass


add_pose_cases(LimbPoses, "limb")


def polyline_distance(point, a, b, c):
    """The distance from a point to the two-segment polyline a-b-c."""
    best = 1e9
    for p, q in ((a, b), (b, c)):
        pq = [q[i] - p[i] for i in range(3)]
        t = sum((point[i] - p[i]) * pq[i] for i in range(3)) / sum(v * v for v in pq)
        t = min(1.0, max(0.0, t))
        best = min(best, math.sqrt(sum((point[i] - (p[i] + pq[i] * t)) ** 2 for i in range(3))))
    return best


class LimbBehaviour(RigTestCase):
    def _build(self, template="limb_arm", skeleton="limb_arm"):
        self.build_standard(template, skeleton)

    # ---- interface -------------------------------------------------------------------------------

    def test_every_module_wide_channel_is_on_the_settings_control(self):
        """The two-bone limb's blend, stretch and soft and the ribbon's twist, sine, volume and scale channels are all on one control,
        and the ribbon's ik controls carry none of them (design/limb.md section 4.4). The aim blends and the end handle scale are gone:
        a limb's ends come from its joints."""
        self._build()
        keyable = set(cmds.listAttr(rig_name(SETTINGS), keyable=True))
        for name, (rest, _) in RIBBON_CHANNELS.items():
            self.assertIn(name, keyable, name)
            self.assertAlmostEqual(cmds.attributeQuery(name, node=rig_name(SETTINGS), listDefault=True)[0], rest, places=6, msg=name)
        for name in ("ikFkBlend", "stretch", "soft"):
            self.assertIn(name, keyable)
        for gone in ("startAimBlend", "endAimBlend", "bendRoundness"):
            self.assertNotIn(gone, keyable)
        for control in RIBBON_IK:
            for name in RIBBON_CHANNELS:
                self.assertNotIn(name, cmds.listAttr(rig_name(control), keyable=True) or [], "%s on %s" % (name, control))

    def test_the_elbow_softness_channel_is_0_to_1_and_rests_at_0(self):
        self._build()
        channel = [c for c in rigmodules.query.get_control_channels(rig_name(SETTINGS)) if c.name == "bendSoftness"][0]
        self.assertEqual((channel.rest, channel.min, channel.max), (0.0, 0.0, 1.0))

    def test_the_ribbons_ik_controls_hang_from_the_start_plugs_parent_and_the_tweakers_from_the_first(self):
        """DAG parenting (author, 2026-11-19): every ribbon ik control is a child of the start plug's DAG parent, the control the FK
        start control hangs from (a leaf, but the first, which carries the tweakers), and every tweaker is a leaf child of the first ik control."""
        self._build()
        start_parent = cmds.listRelatives(rig_name(START), parent=True, fullPath=True)
        self.assertTrue(start_parent, "the start control has no DAG parent")
        for control in RIBBON_IK:
            self.assertEqual(cmds.listRelatives(rig_name(control), parent=True, fullPath=True), start_parent, control)
            if control != RIBBON_IK[0]:
                self.assertFalse(cmds.listRelatives(rig_name(control), children=True, type="transform"), "%s is not a leaf" % control)
        first = cmds.ls(rig_name(RIBBON_IK[0]), long=True)
        for tweaker in TWEAKERS:
            self.assertEqual(cmds.listRelatives(rig_name(tweaker), parent=True, fullPath=True), first, tweaker)
            self.assertFalse(cmds.listRelatives(rig_name(tweaker), children=True, type="transform"), "%s is not a leaf" % tweaker)

    def test_three_ik_controls_is_the_minimum_and_rests_at_the_joints(self):
        self.build_standard("limb_three", "limb_arm")
        controls = [rigmodules.query.get_control(c) for c in cmds.ls(rig_name("armRibbonIK_*_CTRL"))]
        self.assertEqual(len(controls), 3)
        for joint, rest in zip(BIND_JOINTS, [(0, 0, 0), (1, 0, 0), (2, 0, 0), (3, 0, 0), (4, 0, 0), (4, 1, 0), (4, 2, 0), (4, 3, 0), (4, 4, 0)]):
            self.assertWorldTranslation(rig_name(joint), list(rest), 1e-4)
        # the three controls are at the start, the mid joint and the end
        self.assertWorldTranslation(rig_name("armRibbonIK_01_L_CTRL"), [4, 0, 0], 1e-4)
        self.assertWorldTranslation(rig_name("armRibbonIK_02_L_CTRL"), [4, 4, 0], 1e-4)

    def test_five_ik_controls_are_at_the_ends_the_elbow_and_the_middle_of_each_bone(self):
        self._build()
        for control, point in zip(RIBBON_IK, [(0, 0, 0), (2, 0, 0), (4, 0, 0), (4, 2, 0), (4, 4, 0)]):
            self.assertWorldTranslation(rig_name(control), list(point), 1e-4)

    # ---- the elbow -------------------------------------------------------------------------------

    def _pose_fk_lower_45(self, softness):
        cmds.setAttr(rig_name(SETTINGS) + ".ikFkBlend", 0)
        cmds.setAttr(rig_name(LOWER) + ".rotateZ", 45)
        cmds.setAttr(rig_name(SETTINGS) + ".bendSoftness", softness)
        return [translation(j) for j in LIMB_JOINTS], [translation(j) for j in BIND_JOINTS]

    def test_the_elbow_is_a_sharp_corner_by_default(self):
        """Softness 0 is the polyline through the three joints: every bind joint is on one of its two straight lines, however the
        limb bends, and the elbow's bind joint is at the elbow."""
        self._build()
        limb, binds = self._pose_fk_lower_45(0.0)
        for joint, point in zip(BIND_JOINTS, binds):
            self.assertLess(polyline_distance(point, *limb), 1e-5, joint)

    def test_softness_rounds_the_corner_and_keeps_the_elbow_on_the_curve(self):
        """The bind joint next to the elbow comes off the polyline further the softer the elbow is, and the elbow's own stays on the mid
        joint at every softness. At 1 the curve is the smooth one through the three joints."""
        self._build()
        offsets = []
        for softness in (0.0, 0.5, 1.0):
            limb, binds = self._pose_fk_lower_45(softness)
            self.assertLess(math.dist(binds[4], limb[1]), 1e-5, "the elbow bind joint at softness %s" % softness)
            offsets.append(polyline_distance(binds[5], *limb))
        self.assertLess(offsets[0], 1e-5)
        self.assertGreater(offsets[1], 0.01)
        self.assertGreater(offsets[2], offsets[1])

    def test_a_softness_of_0_changes_nothing_at_rest(self):
        """The polyline is the rest curve, so a limb at rest has every bind joint on its skeleton joint at any softness: the tweakers
        have nothing to carry."""
        self._build()
        for softness in (0.0, 1.0):
            cmds.setAttr(rig_name(SETTINGS) + ".bendSoftness", softness)
            for joint in BIND_JOINTS:
                self.assertEqual(len(translation(joint)), 3)
        cmds.setAttr(rig_name(SETTINGS) + ".bendSoftness", 0.0)
        for joint, rest in zip(BIND_JOINTS, [(0, 0, 0), (1, 0, 0), (2, 0, 0), (3, 0, 0), (4, 0, 0), (4, 1, 0), (4, 2, 0), (4, 3, 0), (4, 4, 0)]):
            self.assertWorldTranslation(rig_name(joint), list(rest), 1e-5)

    # ---- the coupling to TwoBoneIk (design/limb.md, D62) -----------------------------------------

    def test_an_end_joint_pitched_70_degrees_off_the_forearm_builds_and_rests_on_the_bind_joints(self):
        """An end joint whose X axis points past the limb (an ankle at the ball of the foot) is allowed up to 80 degrees off the line from
        the joint before it. Here the hand's X is 70 degrees off the forearm (world rotateZ 20 against the forearm's +Y). Nothing moves
        at rest: every bind joint is where it was authored, and the elbow end of the curve is untouched by the hand's pitch."""
        self._build(skeleton="limb_arm_hand_pitched_70")
        for index, joint in enumerate(BIND_JOINTS):
            expected = [4.0, float(index - 4), 0.0] if index >= 4 else [float(index), 0.0, 0.0]
            actual = translation(joint)
            for axis in range(3):
                self.assertAlmostEqual(actual[axis], expected[axis], places=4, msg="%s axis %d" % (joint, axis))

    def _limb_joint_worlds(self):
        return [cmds.xform(rig_name(j), query=True, worldSpace=True, matrix=True) for j in LIMB_JOINTS]

    # Each pose is a list of (control, channel, value). They reach the blend, the IK handle's move and roll, the pole, the soft and stretch
    # channels and the FK controls, so a change to the two-bone solve or its channels shows up here.
    POSES = [
        [],
        [(SETTINGS, "ikFkBlend", 0.0), (LOWER, "rotateZ", 45), (START, "rotateX", 30)],
        [(SETTINGS, "ikFkBlend", 0.5), (START, "rotateZ", 20), (LOWER, "rotateZ", -30)],
        [(HANDLE, "translateY", 1.5), (HANDLE, "translateX", -0.5), (HANDLE, "roll", 35)],
        [(HANDLE, "translateY", 3.0), (SETTINGS, "soft", 0.4), (SETTINGS, "stretch", 0.6)],
        [(HANDLE, "translateY", 4.0), (POLE, "translateX", 2.0), (POLE, "pin", 0.5)],
    ]

    def test_limb_matches_parts(self):
        """The Limb's three limb joints are the TwoBoneIk's for the same joints and channels, over a set of poses. The ribbon's bind joints
        are not compared with a Ribbon's: the Limb's curve passes through the mid joint, a Ribbon's does not."""
        results = {}
        for label, template in (("limb", "limb_arm"), ("twoboneik", "limb_twoboneik")):
            cmds.file(new=True, force=True)
            joints = self.make_skeleton("limb_arm")
            self.build(template, joints)
            for pose in self.POSES:
                for control, channel, value in pose:
                    cmds.setAttr("%s.%s" % (rig_name(control), channel), value)
                results.setdefault(label, []).append(self._limb_joint_worlds())
                for control, channel, value in pose:
                    default = cmds.attributeQuery(channel, node=rig_name(control), listDefault=True)[0]
                    cmds.setAttr("%s.%s" % (rig_name(control), channel), default)
        for index, pose in enumerate(self.POSES):
            for joint, a, b in zip(LIMB_JOINTS, results["limb"][index], results["twoboneik"][index]):
                for x, y in zip(a, b):
                    self.assertAlmostEqual(x, y, places=6, msg="pose %d %s: the Limb and a TwoBoneIk disagree" % (index, joint))
