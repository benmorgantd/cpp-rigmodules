"""Limb module (D62, design/limb.md): contract tests, pose cases from fixtures/poses/limb, and what only this module does.

The pose cases hold hand-worked numbers (each description has the arithmetic). Nothing here is recorded from the rig.
The Limb runs the TwoBoneIk and Ribbon solves, so test_limb_matches_parts holds the two together: a change to either that the Limb
does not carry through fails it.
"""
import copy
import math

from maya import cmds

import rigmodules.query
from commands.test_inverse_solve import SolveTestCase
from commands.test_two_bone_ik_cmds import SwitchTestCase
from commands.test_two_bone_ik_inverse import CapturedWarnings
from rigtest import fixtures
from rigtest.case import RigTestCase, rig_name
from rigtest.cases import add_pose_cases
from rigtest.jointclips import rig_root
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
MATCHED_LIMB = (START, LOWER, HAND, HANDLE, POLE)

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

    # What the inverse can and can't give back (D62 phase 3, design/limb.md section 7). The ribbon's ik controls have no joint, so they
    # are never restored: they keep the sidecar's value or the rest, and the tweakers carry the difference, so every bind joint comes
    # back exactly. The round trip poses every other control. The two-bone half is TwoBoneIk's: the FK controls the blend hides can't
    # come back from the joints.
    def _pose_channels(self, control):
        if rigmodules.query.get_control(control).usage.startswith("armRibbonIK"):
            return {}
        return super()._pose_channels(control)

    def joints_cannot_give(self, control, channel):
        name = control.split(":")[-1].split("_")[0]
        return name in ("lowerArm", "hand") or (name == "upperArm" and not channel.startswith("translate"))

    def controls_lost_with_joint(self, root, markup):
        """The last bind joint's tweaker, which has no joint when the source lacks it."""
        return [c for c in rigmodules.query.get_rig_controls(root) if c.usage == "armTweak_08" and c.side == markup["side"]]


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


class LimbSwitch(SwitchTestCase):
    """setRigIkFkBlend on a Limb (D62 phase 3): the two-bone limb's match, with the ribbon riding the matched joints. The cases that are
    worked by hand for the TwoBoneIk (test_two_bone_ik_cmds) are the same maths, so these check what a Limb adds: the bind joints stay
    put with the limb's joints, and the command and the query find the module from any of its controls."""

    LIMB = tuple(LIMB_JOINTS + BIND_JOINTS)

    def setUp(self):
        super().setUp()
        self.build_standard("limb_arm", "limb_arm")

    def test_fk_to_ik_and_back_keeps_every_joint_and_bind_joint(self):
        self.assertSwitchKeepsJoints(0.0, {START + ".rotateZ": 20, START + ".rotateX": 10, LOWER + ".rotateZ": 40, HAND + ".rotateY": 15})

    def test_a_posed_ik_limb_keeps_every_joint_and_bind_joint(self):
        self.assertSwitchKeepsJoints(1.0, {HANDLE + ".translateX": -1, HANDLE + ".translateY": 2, HANDLE + ".rotateZ": 25,
                                           POLE + ".translateX": 1, POLE + ".translateY": -3})

    def test_the_ribbons_own_posing_does_not_disturb_the_match(self):
        """A moved ribbon ik control, a start twist, an end twist and the elbow's softness change the bind joints and nothing the
        two-bone match reads, so the bind joints are where they were after a switch either way."""
        self.assertSwitchKeepsJoints(0.0, {START + ".rotateZ": 30, LOWER + ".rotateZ": 35, RIBBON_IK[1] + ".translateY": 0.5,
                                           SETTINGS + ".startTwist": 20, SETTINGS + ".endTwist": -30, SETTINGS + ".bendSoftness": 0.5})
        self.assertSwitchKeepsJoints(1.0, {HANDLE + ".translateX": -1, RIBBON_IK[3] + ".translateZ": 0.3, SETTINGS + ".volumePreserve": 0.5})

    def test_stretch_soft_roll_and_pin_carry_over(self):
        self.assertSwitchKeepsJoints(1.0, {SETTINGS + ".stretch": 1, SETTINGS + ".soft": 0.1, HANDLE + ".roll": 40, HANDLE + ".translateX": 2,
                                           POLE + ".pin": 0.3})

    def test_a_pose_ik_can_make_warns_about_nothing(self):
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        cmds.setAttr(rig_name(START + ".rotateZ"), -40)
        with CapturedWarnings() as warnings:
            self.switch(1)
        self.assertEqual(warnings.messages, [])

    def test_a_blend_between_warns_that_ik_is_approximate(self):
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0.5)
        cmds.setAttr(rig_name(START + ".rotateZ"), 20)
        with CapturedWarnings() as warnings:
            self.switch(0)
        self.assertTrue(warnings.contain("between 0 and 1"), warnings.messages)

    def test_any_control_of_the_module_names_the_limb(self):
        for node in (START, HANDLE, POLE, SETTINGS, RIBBON_IK[2], TWEAKERS[4], "arm_01_L_MOD"):
            with self.subTest(node=node):
                cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 1)
                cmds.setRigIkFkBlend(rig_name(node), blend=0)
                self.assertEqual(cmds.getAttr(rig_name(SETTINGS + ".ikFkBlend")), 0)

    def test_the_match_is_one_undo_step_and_sets_no_keys(self):
        cmds.undoInfo(state=True, infinity=True)
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        cmds.setAttr(rig_name(START + ".rotateZ"), -40)
        before = self.control_values()
        self.switch(1)
        self.assertNotEqual(before, self.control_values())
        cmds.undo()
        self.assertEqual(self.control_values(), before)
        self.assertEqual(cmds.getAttr(rig_name(SETTINGS + ".ikFkBlend")), 0)
        for control in MATCHED_LIMB + (SETTINGS,):
            self.assertFalse(cmds.listConnections(rig_name(control), type="animCurve"), control)

    def test_the_query_finds_the_limb_from_any_control(self):
        module = rig_name("arm_01_L_MOD").lstrip(":")
        for control in (START, HANDLE, SETTINGS, RIBBON_IK[0], TWEAKERS[0]):
            with self.subTest(control=control):
                limb = rigmodules.query.find_ik_fk_limb(rig_name(control))
                self.assertEqual(limb.module, module)
                self.assertEqual(limb.settings, rig_name(SETTINGS).lstrip(":"))
                self.assertEqual(limb.blend, 1.0)
                self.assertEqual(len(limb.controls), 6 + len(RIBBON_IK) + len(TWEAKERS))

    def test_a_node_that_is_not_a_limb_is_an_error(self):
        self.assertMayaError(lambda: cmds.setRigIkFkBlend(rig_name("upperArm_00_L_JNT"), blend=1), "not a TwoBoneIk or Limb")


FOOT = "foot_00_L_CTRL"
HAND_JOINT = "hand_00_L_JNT"


class LimbIkTarget(RigTestCase):
    """ikTarget (D62 phase 4): a foot-shaped module (an FkChain with one joint at the hand) hangs from the Limb's ik socket and feeds the
    Limb's ikTarget plug. limb_arm is the arm: upper arm (0,0,0), lower arm (4,0,0), hand (4,4,0), bones 4 and 4. Expectations are
    worked from that: the end joint reaches where the foot control is, and a pose of the foot control is whatever direction it moved."""

    def _build(self):
        self.build_standard("limb_foot", "limb_foot")

    def _raise_the_foot(self, amount=1.0):
        cmds.setAttr(rig_name(FOOT + ".translateY"), amount)
        return translation(FOOT)

    def test_a_limb_with_no_target_reaches_its_handle(self):
        self.build_standard("limb_arm", "limb_arm")
        self.assertFalse(cmds.getAttr(rig_name("arm_01_L_MOD.ikTargetConnected")))
        cmds.setAttr(rig_name(HANDLE + ".translateY"), 1.0)
        self.assertWorldTranslation(rig_name(HAND_JOINT), translation(HANDLE), 1e-4)

    def test_the_foot_at_rest_leaves_the_limb_where_it_is(self):
        self._build()
        self.assertTrue(cmds.getAttr(rig_name("arm_01_L_MOD.ikTargetConnected")))
        self.assertWorldTranslation(rig_name(HAND_JOINT), [4, 4, 0], 1e-4)
        self.assertWorldTranslation(rig_name(FOOT), [4, 4, 0], 1e-4)

    def test_the_end_joint_reaches_the_foot_and_the_handle_stays(self):
        """The foot control moves one unit. The handle, which the foot hangs from, does not, and the hand goes where the foot is: its
        distance from the root is then under the reach of 8, so nothing stretches or softens."""
        self._build()
        handle = translation(HANDLE)
        pole = translation(POLE)
        foot = self._raise_the_foot(1.0)
        self.assertAlmostEqual(math.dist(foot, [4, 4, 0]), 1.0, places=4)
        self.assertWorldTranslation(rig_name(HAND_JOINT), foot, 1e-4)
        self.assertWorldTranslation(rig_name(HANDLE), handle, 1e-5)
        self.assertWorldTranslation(rig_name(POLE), pole, 1e-5)

    def test_the_bones_keep_their_lengths_up_to_the_reach(self):
        self._build()
        self._raise_the_foot(1.0)
        upper = translation("lowerArm_00_L_JNT")
        end = translation(HAND_JOINT)
        self.assertAlmostEqual(math.dist([0, 0, 0], upper), 4.0, places=4)
        self.assertAlmostEqual(math.dist(upper, end), 4.0, places=4)

    def test_the_ribbon_follows_the_end_joint(self):
        self._build()
        self._raise_the_foot(1.0)
        self.assertWorldTranslation(rig_name(BIND_JOINTS[-1]), translation(HAND_JOINT), 1e-4)
        self.assertWorldTranslation(rig_name(BIND_JOINTS[0]), [0, 0, 0], 1e-4)

    def test_in_fk_the_target_is_ignored(self):
        self._build()
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        self._raise_the_foot(1.0)
        self.assertWorldTranslation(rig_name(HAND_JOINT), [4, 4, 0], 1e-4)

    def test_a_half_blend_is_halfway_between_fk_and_the_target(self):
        self._build()
        foot = self._raise_the_foot(1.0)
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0.5)
        end = translation(HAND_JOINT)
        for axis in range(3):
            self.assertAlmostEqual(end[axis], (4, 4, 0)[axis] * 0.5 + foot[axis] * 0.5, places=3)

    def test_the_graph_has_no_cycle_in_either_evaluation_mode(self):
        """The module loop Limb to foot to Limb is acyclic at the plug level: the ik socket, the pole and the settings sockets never read
        ikTarget (design/limb.md section 8). Maya reports a real cycle as a warning when it evaluates."""
        self._build()
        mode = cmds.evaluationManager(query=True, mode=True)[0]
        self.addCleanup(cmds.evaluationManager, mode=mode)
        for evaluation in ("off", "parallel"):
            with self.subTest(mode=evaluation):
                cmds.evaluationManager(mode=evaluation)
                with CapturedWarnings() as warnings:
                    foot = self._raise_the_foot(1.0 if evaluation == "off" else 1.5)
                    self.assertWorldTranslation(rig_name(HAND_JOINT), foot, 1e-4)
                    cmds.dgdirty(allPlugs=True)
                    cmds.getAttr(rig_name(HAND_JOINT + ".worldMatrix[0]"))
                self.assertFalse(warnings.contain("ycle"), warnings.messages)

    def test_the_loop_through_a_socket_that_sees_the_target_is_still_an_error(self):
        """The exemption is for the ik, pole and settings sockets only: a foot on the end socket is a real loop (validation case
        limb_foot_loop_through_the_end_socket). Here the template data is checked through the build, which refuses it."""
        data = copy.deepcopy(fixtures.load_template("limb_foot"))
        data["rigModules"][1]["parentStructure"]["start"] = "arm_01_L_MOD.end"
        self.assertMayaError(lambda: self.build(data, self.make_skeleton("limb_foot"), label="limb_foot_loop"), "cycle")
        self.assertNothingBuilt()

    def test_undo_and_redo_rebuild_the_target(self):
        self._build()
        cmds.undo()
        self.assertEqual(cmds.ls(type="rigRootModule"), [])
        cmds.redo()
        foot = self._raise_the_foot(1.0)
        self.assertWorldTranslation(rig_name(HAND_JOINT), foot, 1e-4)

    def test_the_target_plug_has_no_control_and_the_limb_still_lists_its_controls(self):
        self._build()
        controls = rigmodules.query.get_rig_controls(rig_root())
        usages = [c.usage for c in controls if c.module_type == "Limb"]
        self.assertEqual(len(usages), 6 + len(RIBBON_IK) + len(TWEAKERS))


class LimbIkTargetRestore(SolveTestCase):
    """A restore skips ikTarget (the inverse matches the limb with the foot at rest), so the module loop does not stop it ordering the
    modules, and a rig at rest comes back where it was."""

    def test_a_restore_orders_a_limb_and_its_foot(self):
        self.build_standard("limb_foot", "limb_foot")
        rest = self.snapshot()
        clip = self.write_clip(rest, [rest])
        sidecar = self.write_sidecar({})
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip, sidecar=sidecar)
        self.assertJointsAt(rest)


class LimbBindSockets(SolveTestCase):
    """A Limb has a socket for every bind joint (bind_0 to bind_8 on limb_arm), so a child can hang from a twist joint. limb_bind_child has
    a joint, extra, at the third bind joint (2, 0, 0), and the template hangs an FkChain of it from bind_2."""

    EXTRA = "extra_00_L_JNT"
    BIND = "bind3_00_L_JNT"

    def setUp(self):
        super().setUp()
        self.build_standard("limb_bind_child", "limb_bind_child")

    def _pose(self):
        """FK, the upper arm and the forearm turned, so the third bind joint (on the upper arm) moves and turns."""
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        cmds.setAttr(rig_name(START + ".rotateZ"), 25)
        cmds.setAttr(rig_name(LOWER + ".rotateZ"), 30)
        cmds.setAttr(rig_name(SETTINGS + ".startTwist"), 40)

    def test_the_child_sits_on_its_bind_joint_at_rest(self):
        self.assertWorldTranslation(rig_name(self.BIND), [2, 0, 0], 1e-4)
        self.assertWorldTranslation(rig_name(self.EXTRA), [2, 0, 0], 1e-4)

    def test_the_child_follows_its_bind_joint_through_a_pose_and_a_twist(self):
        self._pose()
        self.assertFalse(translation(self.BIND) == [2, 0, 0])
        self.assertWorldTranslation(rig_name(self.EXTRA), translation(self.BIND), 1e-4)
        a = cmds.xform(rig_name(self.BIND), query=True, worldSpace=True, matrix=True)
        b = cmds.xform(rig_name(self.EXTRA), query=True, worldSpace=True, matrix=True)
        for x, y in zip(a, b):
            self.assertAlmostEqual(x, y, places=4)

    def test_the_child_is_not_hidden_by_the_hidden_tweaker(self):
        """The socket has no DAG transform, so the child's controls do not inherit the tweaker's hidden state."""
        control = rig_name("extra_00_L_CTRL")
        self.assertTrue(cmds.getAttr(control + ".visibility"))
        parent = cmds.listRelatives(control, parent=True, fullPath=True)
        self.assertNotIn(rig_name(TWEAKERS[2]), [p.split("|")[-1] for p in parent or []])

    def test_a_restore_from_joints_carries_the_child_with_its_bind_joint(self):
        rest = self.snapshot()
        self._pose()
        posed = self.snapshot()
        clip = self.write_clip(rest, [posed])
        sidecar = self.write_sidecar({"Left": {"armSettings": {"ikFkBlend": 0, "startTwist": 40}}})
        cmds.zeroRigControls(rig_root())
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip, sidecar=sidecar)
        self.assertJointsAt(posed)
