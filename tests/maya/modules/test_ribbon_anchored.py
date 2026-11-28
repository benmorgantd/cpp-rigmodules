"""The anchored Ribbon (D80, design/limb-split.md): a Ribbon whose curve passes through the three frames its start, mid and end plugs carry
from another module's sockets, with its channels hosted on the control behind its settings plug.

An arm is a TwoBoneIk and an anchored Ribbon with the limb's three keys set (startAnchorRotation "swing", endAnchorRotation "twist",
bendSoftness 0), which is the behaviour the removed Limb hard-coded (D62, D80). The pose cases in fixtures/poses/ribbon_anchored are the
Limb's, worked by hand, and phase 1 held the split arm to the Limb's joints on each before the Limb was deleted. Every expected number is
worked by hand. Nothing here is recorded from the rig.
"""
import json
import math

from maya import cmds
import maya.api.OpenMaya as om

import rigmodules.query
from commands.test_two_bone_ik_cmds import SwitchTestCase
from rigtest import config, fixtures
from rigtest.case import RigTestCase, rig_name
from rigtest.cases import add_pose_cases
from rigtest.jointclips import SolveTestCase, rig_root
from rigtest.module_contract import ModuleContract

LIMB_JOINTS = ["upperArm_00_L_JNT", "lowerArm_00_L_JNT", "hand_00_L_JNT"]
BIND_JOINTS = ["bind%d_00_L_JNT" % i for i in range(1, 10)]
RIBBON = "armRibbon_01_L_MOD"
RIBBON_IK = ["armRibbonIK_%02d_L_CTRL" % i for i in (1, 2, 3)]
TWEAKERS = ["bind%d_00_L_CTRL" % i for i in range(1, 10)]
START = "upperArm_00_L_CTRL"
SETTINGS = "armSettings_00_L_CTRL"

# The ribbon's channels on the settings control: name -> (rest, mirror). The rests are the limb template's (bendSoftness 0).
HOSTED_CHANNELS = {
    "startTwist": (0.0, "negate"), "endTwist": (0.0, "negate"),
    "sineAmplitude": (0.0, "copy"), "sineFrequency": (1.0, "copy"), "sinePhase": (0.0, "copy"), "sineOrient": (0.0, "negate"),
    "sineStartFalloff": (0.2, "copy"), "sineEndFalloff": (0.2, "copy"),
    "volumePreserve": (0.0, "copy"), "scaleStart": (1.0, "copy"), "scaleMid": (1.0, "copy"), "scaleEnd": (1.0, "copy"),
    "bendSoftness": (0.0, "copy"),
}


def world_matrix(node):
    return om.MSelectionList().add(node).getDagPath(0).inclusiveMatrix()


def edited(template, edits):
    return fixtures.apply_edits(fixtures.load_template(template), edits)


class AnchoredArmRoundTripRules:
    """What the inverse can and can't give back for the split arm (design/limb-split.md section 5). The ribbon's ik controls have no
    joint, so they keep the sidecar's value or the rest, and the tweakers carry the difference. The round trip doesn't pose them: a posed
    one's scale with volumePreserve shears the bind joints, which no tweaker can hold. The TwoBoneIk's FK controls the blend hides can't
    come back from the joints."""

    def _pose_channels(self, control):
        if rigmodules.query.get_control(control).usage.startswith("armRibbonIK"):
            return {}
        return super()._pose_channels(control)

    def joints_cannot_give(self, control, channel):
        name = control.split(":")[-1].split("_")[0]
        return name in ("lowerArm", "hand") or (name == "upperArm" and not channel.startswith("translate"))


class AnchoredRibbonContract(AnchoredArmRoundTripRules, ModuleContract, RigTestCase):
    TEMPLATE = "ribbon_anchored_arm"
    SKELETON = "limb_arm"
    MODULE_TYPE = "ribbonModule"
    JOINT_OWNERS = dict((joint, RIBBON) for joint in BIND_JOINTS)
    MIRROR_TEMPLATE = "ribbon_anchored_mirror"
    MIRROR_SKELETON = "limb_mirror"

    def controls_lost_with_joint(self, root, markup):
        """The last bind joint's tweaker, which has no joint when the source lacks it."""
        return [c for c in rigmodules.query.get_rig_controls(root) if c.usage == "armRibbonTweak_08" and c.side == markup["side"]]


class AnchoredRibbonPoses(RigTestCase):
    pass


add_pose_cases(AnchoredRibbonPoses, "ribbon_anchored")


def translation(node):
    return cmds.xform(rig_name(node), query=True, worldSpace=True, translation=True)


def polyline_distance(point, a, b, c):
    """The distance from a point to the two-segment polyline a-b-c."""
    best = 1e9
    for p, q in ((a, b), (b, c)):
        pq = [q[i] - p[i] for i in range(3)]
        t = sum((point[i] - p[i]) * pq[i] for i in range(3)) / sum(v * v for v in pq)
        t = min(1.0, max(0.0, t))
        best = min(best, math.sqrt(sum((point[i] - (p[i] + pq[i] * t)) ** 2 for i in range(3))))
    return best


REST_BINDS = [(0, 0, 0), (1, 0, 0), (2, 0, 0), (3, 0, 0), (4, 0, 0), (4, 1, 0), (4, 2, 0), (4, 3, 0), (4, 4, 0)]


class AnchoredRibbonBehaviour(RigTestCase):
    def _build(self, template="ribbon_anchored_arm"):
        self.build_standard(template, "limb_arm")

    # ---- controls and sockets ----------------------------------------------------------------

    def test_the_ribbon_has_interior_ik_controls_only(self):
        """No ik control at either end: those are the TwoBoneIk's FK and IK controls (section 3.4). The three interior ones are numbered
        from _01, and the elbow's, _02, rests on the elbow."""
        self._build()
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        usages = sorted(c.usage for c in rigmodules.query.get_rig_controls(root) if c.module.split(":")[-1] == RIBBON)
        self.assertEqual(usages, sorted(["armRibbonIK_01", "armRibbonIK_02", "armRibbonIK_03"] + ["armRibbonTweak_%02d" % i for i in range(9)]))
        self.assertWorldTranslation(rig_name(RIBBON_IK[1]), [4, 0, 0])
        self.assertWorldTranslation(rig_name(RIBBON_IK[0]), [2, 0, 0])
        self.assertWorldTranslation(rig_name(RIBBON_IK[2]), [4, 2, 0])

    def test_the_ik_controls_hang_from_the_fk_upper_control_and_the_tweakers_from_the_first(self):
        """DAG parents (author, 2026-11-27): every ik control a leaf child of the control behind the start plug's socket, the FK upper
        control, and every tweaker a leaf child of the first ik control."""
        self._build()
        start = cmds.ls(rig_name(START), long=True)
        for control in RIBBON_IK:
            self.assertEqual(cmds.listRelatives(rig_name(control), parent=True, fullPath=True), start, control)
        for control in RIBBON_IK[1:]:
            self.assertFalse(cmds.listRelatives(rig_name(control), children=True, type="transform"), "%s is not a leaf" % control)
        first = cmds.ls(rig_name(RIBBON_IK[0]), long=True)
        for tweaker in TWEAKERS:
            self.assertEqual(cmds.listRelatives(rig_name(tweaker), parent=True, fullPath=True), first, tweaker)

    def test_one_interior_control_is_the_mids_and_keeps_the_corner(self):
        """controlCount 1 is the fewest: the one ik control, _01, rests on the elbow, and with the forearm turned 45 degrees (FK) the
        elbow bind joint stays on the elbow and the last one on the hand, at (4 - 2.8284, 2.8284, 0)."""
        template = edited("ribbon_anchored_arm", [{"set": "/rigModules/1/moduleArgs/controlCount", "value": 1}])
        self.build(template, self.make_skeleton("limb_arm"))
        self.assertEqual(sorted(n.split(":")[-1] for n in cmds.ls(rig_name("armRibbonIK_*"), type="rigControlNode")), ["armRibbonIK_01_L_CTRL"])
        self.assertWorldTranslation(rig_name("armRibbonIK_01_L_CTRL"), [4, 0, 0])
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        cmds.setAttr(rig_name("lowerArm_00_L_CTRL.rotateZ"), 45)
        self.assertWorldTranslation(rig_name("bind5_00_L_JNT"), [4, 0, 0])
        self.assertWorldTranslation(rig_name("bind9_00_L_JNT"), [1.1715728752538097, 2.8284271247461903, 0], 1e-3)

    def test_an_anchor_plug_whose_socket_is_not_at_its_anchor_joint_fails_the_build(self):
        """The plan built the curve from anchorJoints, so a plug on another socket would hand it frames from another rest (section 2.2)."""
        template = edited("ribbon_anchored_arm", [{"set": "/rigModules/1/parentStructure/mid", "value": "arm_01_L_MOD.index_2"}])
        joints = self.make_skeleton("limb_arm")
        self.assertMayaError(lambda: self.build(template, joints))
        self.assertNothingBuilt()

    # ---- the hosted channels -----------------------------------------------------------------

    def test_the_ribbons_channels_are_on_the_limbs_settings_control(self):
        """Hosting (section 3.5): the 13 ribbon channels sit on the TwoBoneIk's settings control beside its own, at the limb's rests, and
        drive the ribbon node. The aim blends and bendRoundness are not in the anchored form, and no ribbon control carries a channel."""
        self._build()
        settings = rig_name(SETTINGS)
        keyable = set(cmds.listAttr(settings, keyable=True))
        for name, (rest, mirror) in HOSTED_CHANNELS.items():
            self.assertIn(name, keyable, name)
            channel = [c for c in rigmodules.query.get_control_channels(settings) if c.name == name][0]
            self.assertAlmostEqual(channel.rest, rest, places=6, msg=name)
            self.assertEqual(channel.mirror, mirror, name)
            driven = cmds.listConnections("%s.%s" % (settings, name), source=False, destination=True, plugs=True) or []
            self.assertEqual([plug.split(":")[-1] for plug in driven], ["%s.%s" % (RIBBON, name)], name)
        for name in ("ikFkBlend", "stretch", "soft"):
            self.assertIn(name, keyable)
        for gone in ("startAimBlend", "endAimBlend", "bendRoundness"):
            self.assertNotIn(gone, keyable)
        for control in RIBBON_IK + TWEAKERS:
            for name in HOSTED_CHANNELS:
                self.assertNotIn(name, cmds.listAttr(rig_name(control), keyable=True) or [], "%s on %s" % (name, control))

    def test_two_ribbons_cannot_host_the_same_channel_on_one_control(self):
        """Hosting rule 2 (section 3.5): a second anchored ribbon on the same settings plug would add startTwist and the rest to a control
        that has them, so the build fails and leaves nothing. Its bind joints are copies of the first ribbon's, at the same places, and
        the same twin hosted on another control builds, so the failure is the clash."""
        def build_twin(settings_socket):
            joints = self.make_skeleton("limb_arm")
            for index in range(1, 10):
                source = ":%s:bind%d_00_L_JNT" % (config.SKELETON_NAMESPACE, index)
                full = ":%s:twin%d_00_L_JNT" % (config.SKELETON_NAMESPACE, index)
                cmds.select(cmds.listRelatives(source, parent=True, fullPath=True)[0])
                cmds.joint(name=full, position=cmds.xform(source, query=True, worldSpace=True, translation=True))
                cmds.xform(full, worldSpace=True, rotation=cmds.xform(source, query=True, worldSpace=True, rotation=True))
                cmds.select(clear=True)
                cmds.rigJointMarkup(full, usage="twin%d" % index, side="Left", export=True, skin=True)
                joints.append(full)
            template = fixtures.load_template("ribbon_anchored_arm")
            twin = json.loads(json.dumps(template["rigModules"][1]))
            twin["name"] = "armTwin_01_L_MOD"
            twin["moduleArgs"]["joints"] = [["Left", "twin%d" % index] for index in range(1, 10)]
            twin["moduleArgs"]["controlName"] = "armTwin"
            twin["parentStructure"]["settings"] = settings_socket
            template["rigModules"].append(twin)
            return lambda: self.build(template, joints)

        build_twin("arm_01_L_MOD.index_0")()
        self.assertIn("bendSoftness", cmds.listAttr(rig_name(START), keyable=True))
        cmds.file(new=True, force=True)
        self.assertMayaError(build_twin("arm_01_L_MOD.settings"))
        self.assertNothingBuilt()

    def test_the_hosted_softness_rounds_the_elbow(self):
        """With the forearm turned 45 degrees more (FK), the elbow bind joint at the template's softness, 0, points along the bisector of
        the two bones (0 and 135 degrees give 67.5). The hosted channel reaches the ribbon: at 1 the curve leaves the bones, so the bind
        joint before the elbow moves."""
        self._build()
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        cmds.setAttr(rig_name("lowerArm_00_L_CTRL.rotateZ"), 45)
        self.assertWorldRotation(rig_name("bind5_00_L_JNT"), [0, 0, 67.5], 1e-3)
        sharp = world_matrix(rig_name("bind4_00_L_JNT"))
        cmds.setAttr(rig_name(SETTINGS + ".bendSoftness"), 1)
        self.assertFalse(world_matrix(rig_name("bind4_00_L_JNT")).isEquivalent(sharp, 1e-3), "bendSoftness did not reach the ribbon")

    def test_the_hosted_channels_mirror_by_their_own_modes(self):
        """The mirror mode is on the attribute (D34), wherever it sits, so the mirror pose copies sineAmplitude and negates startTwist
        from the left settings control to the right."""
        self.build(self.MIRROR, self.make_skeleton("limb_mirror"))
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        cmds.setAttr(rig_name("armSettings_00_L_CTRL.sineAmplitude"), 0.5)
        cmds.setAttr(rig_name("armSettings_00_L_CTRL.startTwist"), 20)
        cmds.mirrorRigPose(root)
        self.assertAlmostEqual(cmds.getAttr(rig_name("armSettings_00_R_CTRL.sineAmplitude")), 0.5, places=6)
        self.assertAlmostEqual(cmds.getAttr(rig_name("armSettings_00_R_CTRL.startTwist")), -20, places=4)

    MIRROR = "ribbon_anchored_mirror"

    # ---- the anchor rotations ----------------------------------------------------------------

    def test_a_swing_start_keeps_the_shoulders_twist_off_the_first_bind_joint(self):
        """The limb keys: the upper arm turned 60 degrees about X (FK) leaves the first bind joint untwisted (Limb's rule)."""
        self._build()
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        cmds.setAttr(rig_name("upperArm_00_L_CTRL.rotateX"), 60)
        self.assertWorldRotation(rig_name("bind1_00_L_JNT"), [0, 0, 0], 1e-3)

    def test_a_full_start_gives_the_first_bind_joint_the_anchors_twist(self):
        """The neutral default on an FkChain of three, as a tail would be: the first control turned 60 degrees about X twists the first
        bind joint, which sits on the start anchor, by all 60."""
        self._build("ribbon_anchored_chain")
        cmds.setAttr(rig_name("upperArm_00_L_CTRL.rotateX"), 60)
        self.assertWorldTranslation(rig_name("bind1_00_L_JNT"), [0, 0, 0])
        self.assertWorldRotation(rig_name("bind1_00_L_JNT"), [60, 0, 0], 1e-3)

    def test_a_twist_end_keeps_the_hands_bend_off_the_last_bind_joint(self):
        """The limb keys: the hand bent 30 degrees about Z (FK) leaves the last bind joint on the forearm's axis, at (0, 0, 90)."""
        self._build()
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        cmds.setAttr(rig_name("hand_00_L_CTRL.rotateZ"), 30)
        self.assertWorldTranslation(rig_name("bind9_00_L_JNT"), [4, 4, 0])
        self.assertWorldRotation(rig_name("bind9_00_L_JNT"), [0, 0, 90], 1e-3)

    def test_a_full_end_bends_the_last_bind_joint_with_its_anchor(self):
        """The neutral default: the last chain control bent 30 degrees about Z takes the last bind joint, which sits on the end anchor,
        from (0, 0, 90) to (0, 0, 120)."""
        self._build("ribbon_anchored_chain")
        cmds.setAttr(rig_name("hand_00_L_CTRL.rotateZ"), 30)
        self.assertWorldTranslation(rig_name("bind9_00_L_JNT"), [4, 4, 0])
        self.assertWorldRotation(rig_name("bind9_00_L_JNT"), [0, 0, 120], 1e-3)

    def test_the_neutral_softness_is_the_smooth_curve(self):
        """Left out, bendSoftness rests at 1 on the host (the neutral default, author 2026-11-27), and the chain rebuilds its rest."""
        self._build("ribbon_anchored_chain")
        channel = [c for c in rigmodules.query.get_control_channels(rig_name(START)) if c.name == "bendSoftness"][0]
        self.assertEqual((channel.rest, channel.min, channel.max), (1.0, 0.0, 1.0))


LOWER = "lowerArm_00_L_CTRL"
HAND = "hand_00_L_CTRL"
HANDLE = "armIK_00_L_CTRL"
POLE = "armPole_00_L_CTRL"


class AnchoredArmElbowAndRamp(RigTestCase):
    """The split arm's elbow and twist ramp, moved from the Limb's tests (D80 phase 3): limb_arm is upper arm (0,0,0), lower arm (4,0,0),
    hand (4,4,0), with bind joints at whole units along the two bones."""

    def _build(self, template="ribbon_anchored_arm", skeleton="limb_arm"):
        self.build_standard(template, skeleton)

    def _pose_fk_lower_45(self, softness):
        cmds.setAttr(rig_name(SETTINGS) + ".ikFkBlend", 0)
        cmds.setAttr(rig_name(LOWER) + ".rotateZ", 45)
        cmds.setAttr(rig_name(SETTINGS) + ".bendSoftness", softness)
        return [translation(j) for j in LIMB_JOINTS], [translation(j) for j in BIND_JOINTS]

    def test_the_elbow_is_a_sharp_corner_at_the_limbs_softness(self):
        """Softness 0 (the arm template's) is the polyline through the three joints: every bind joint is on one of its two straight
        lines, however the limb bends, and the elbow's bind joint is at the elbow."""
        self._build()
        limb, binds = self._pose_fk_lower_45(0.0)
        for joint, point in zip(BIND_JOINTS, binds):
            self.assertLess(polyline_distance(point, *limb), 1e-5, joint)

    def test_softness_rounds_the_corner_and_keeps_the_elbow_on_the_curve(self):
        """The bind joint after the elbow comes off the polyline further the softer the elbow is, and the elbow's own stays on the mid
        joint at every softness."""
        self._build()
        offsets = []
        for softness in (0.0, 0.5, 1.0):
            limb, binds = self._pose_fk_lower_45(softness)
            self.assertLess(math.dist(binds[4], limb[1]), 1e-5, "the elbow bind joint at softness %s" % softness)
            offsets.append(polyline_distance(binds[5], *limb))
        self.assertLess(offsets[0], 1e-5)
        self.assertGreater(offsets[1], 0.01)
        self.assertGreater(offsets[2], offsets[1])

    def test_at_rest_every_bind_joint_is_where_it_was_authored(self):
        self._build()
        for joint, rest in zip(BIND_JOINTS, REST_BINDS):
            self.assertWorldTranslation(rig_name(joint), list(rest), 1e-5)

    def test_the_ramp_is_stored_as_one_share_per_curve_control(self):
        """[[0, 1], [1, 0]] is 1 - u at each of the curve's controls (the two ends and the three interior ik controls), and u is the
        control's stored parameter."""
        template = edited("ribbon_anchored_arm", [{"set": "/rigModules/1/moduleArgs/twistRamp", "value": [[0, 1.0], [1, 0.0]]}])
        self.build(template, self.make_skeleton("limb_arm"))
        module = rig_name(RIBBON)
        self.assertEqual(cmds.getAttr(module + ".fkRollShare", size=True), 5)
        for j in range(5):
            knot = cmds.getAttr("%s.knotU[%d]" % (module, j))
            self.assertAlmostEqual(cmds.getAttr("%s.fkRollShare[%d]" % (module, j)), 1.0 - knot, places=9)

    def test_an_end_joint_pitched_70_degrees_off_the_forearm_builds_and_rests_on_the_bind_joints(self):
        """An end anchor whose X axis points past the limb (an ankle at the ball of the foot) is allowed up to 80 degrees off the line from
        the anchor before it. Here the hand's X is 70 degrees off the forearm. Nothing moves at rest: every bind joint is where it was
        authored."""
        self._build(skeleton="limb_arm_hand_pitched_70")
        for joint, rest in zip(BIND_JOINTS, REST_BINDS):
            self.assertWorldTranslation(rig_name(joint), list(rest), 1e-4)

    def test_a_bind_joint_with_a_child_builds(self):
        """A bind joint may have a child joint (author, 2026-11-22): limb_arm_child has extra_00_L_JNT under bind3."""
        self._build(skeleton="limb_arm_child")
        self.assertTrue(cmds.objExists(rig_name("bind3_00_L_JNT")))
        self.assertTrue(cmds.objExists(rig_name("extra_00_L_JNT")))


class AnchoredArmSwitch(SwitchTestCase):
    """setRigIkFkBlend on the split arm: the TwoBoneIk's match, with the ribbon riding the matched joints, so the bind joints stay put
    (moved from the Limb's tests). The TwoBoneIk's own cases are in test_two_bone_ik_cmds."""

    LIMB = tuple(LIMB_JOINTS + BIND_JOINTS)

    def setUp(self):
        super().setUp()
        self.build_standard("ribbon_anchored_arm", "limb_arm")

    def test_fk_to_ik_and_back_keeps_every_joint_and_bind_joint(self):
        self.assertSwitchKeepsJoints(0.0, {START + ".rotateZ": 20, START + ".rotateX": 10, LOWER + ".rotateZ": 40, HAND + ".rotateY": 15})

    def test_a_posed_ik_limb_keeps_every_joint_and_bind_joint(self):
        self.assertSwitchKeepsJoints(1.0, {HANDLE + ".translateX": -1, HANDLE + ".translateY": 2, HANDLE + ".rotateZ": 25,
                                           POLE + ".translateX": 1, POLE + ".translateY": -3})

    def test_the_ribbons_own_posing_does_not_disturb_the_match(self):
        """A moved ribbon ik control, the twists and the elbow's softness change the bind joints and nothing the two-bone match reads."""
        self.assertSwitchKeepsJoints(0.0, {START + ".rotateZ": 30, LOWER + ".rotateZ": 35, RIBBON_IK[0] + ".translateY": 0.5,
                                           SETTINGS + ".startTwist": 20, SETTINGS + ".endTwist": -30, SETTINGS + ".bendSoftness": 0.5})
        self.assertSwitchKeepsJoints(1.0, {HANDLE + ".translateX": -1, RIBBON_IK[2] + ".translateZ": 0.3, SETTINGS + ".volumePreserve": 0.5})

    def test_a_ribbon_control_is_not_the_limb(self):
        """The Ribbon is a module of its own, so its controls name no limb: the query gives None and the command an error."""
        self.assertIsNone(rigmodules.query.find_ik_fk_limb(rig_name(RIBBON_IK[1])))
        self.assertMayaError(lambda: cmds.setRigIkFkBlend(rig_name(RIBBON_IK[1]), blend=0), "not a TwoBoneIk module")
        limb = rigmodules.query.find_ik_fk_limb(rig_name(SETTINGS))
        self.assertEqual(len(limb.controls), 6)


class AnchoredRibbonIkSockets(SolveTestCase):
    """An anchored ribbon has a socket for every interior ik control (index_0 to index_2, D78). ribbon_anchored_ik_child hangs an FkChain
    of `extra` (at the third bind joint, (2, 0, 0)) from index_1, the elbow's control, so it follows that control's motion since rest, and
    the socket carries the visible control."""

    EXTRA = "extra_00_L_JNT"
    CONTROL = RIBBON_IK[1]

    def setUp(self):
        super().setUp()
        self.build_standard("ribbon_anchored_ik_child", "limb_bind_child")

    def test_the_child_sits_at_rest_and_hangs_under_the_control(self):
        self.assertWorldTranslation(rig_name(self.EXTRA), [2, 0, 0], 1e-4)
        parents = cmds.listRelatives(rig_name("extra_00_L_CTRL"), parent=True, fullPath=True) or []
        self.assertEqual([p.split("|")[-1].lstrip(":") for p in parents], [rig_name(self.CONTROL).lstrip(":")])

    def test_the_child_follows_the_ribbon_control_through_a_move(self):
        rest = translation(self.CONTROL)
        cmds.setAttr(rig_name(self.CONTROL + ".translateY"), 1.5)
        moved = translation(self.CONTROL)
        self.assertNotEqual(moved, rest)
        self.assertWorldTranslation(rig_name(self.EXTRA), [x + m - r for x, m, r in zip((2, 0, 0), moved, rest)], 1e-4)


class AnchoredRibbonBindSockets(SolveTestCase):
    """An anchored ribbon has a socket for every bind joint (bind_0 to bind_8 here), so a child can hang from a twist joint.
    ribbon_anchored_bind_child hangs an FkChain of `extra`, at the third bind joint (2, 0, 0), from bind_2."""

    EXTRA = "extra_00_L_JNT"
    BIND = "bind3_00_L_JNT"

    def setUp(self):
        super().setUp()
        self.build_standard("ribbon_anchored_bind_child", "limb_bind_child")

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
        a = cmds.xform(rig_name(self.BIND), query=True, worldSpace=True, matrix=True)
        b = cmds.xform(rig_name(self.EXTRA), query=True, worldSpace=True, matrix=True)
        for x, y in zip(a, b):
            self.assertAlmostEqual(x, y, places=4)

    def test_the_child_is_not_hidden_by_the_hidden_tweaker(self):
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
