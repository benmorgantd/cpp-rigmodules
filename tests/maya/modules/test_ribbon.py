"""Ribbon module (D56): contract tests, pose cases from fixtures/poses/ribbon, and what only this module does.

The pose cases hold hand-worked numbers (each description has the arithmetic). Nothing here is recorded from the rig.
"""
import copy
import math
import os
import tempfile

from maya import cmds
import maya.api.OpenMaya as om

import rigmodules.query
from rigtest import fixtures
from rigtest.case import RigTestCase, rig_name
from rigtest.cases import add_pose_cases
from rigtest.module_contract import ModuleContract

FIRST = "tailCtrlA_ctrl_c"
INTERIOR = "tailCtrlB_ctrl_c"
LAST = "tailCtrlC_ctrl_c"
BIND_JOINTS = ["tail%d_jnt" % i for i in range(1, 6)]
TWEAKERS = ["tail%d_ctrl_c" % i for i in range(1, 6)]

CHANNELS = {
    # name: (rest, mirror)
    "startTwist": (0.0, "negate"), "endTwist": (0.0, "negate"),
    "sineAmplitude": (0.0, "copy"), "sineFrequency": (1.0, "copy"), "sinePhase": (0.0, "copy"), "sineOrient": (0.0, "negate"),
    "sineStartFalloff": (0.2, "copy"), "sineEndFalloff": (0.2, "copy"), "startAimBlend": (0.0, "copy"), "endAimBlend": (0.0, "copy"),
    "bendRoundness": (1.0, "copy"), "volumePreserve": (0.0, "copy"), "scaleStart": (1.0, "copy"), "scaleMid": (1.0, "copy"),
    "scaleEnd": (1.0, "copy"),
}


class RibbonContract(ModuleContract, RigTestCase):
    TEMPLATE = "ribbon_basic"
    SKELETON = "ribbon_basic"
    MODULE_TYPE = "ribbonModule"
    JOINT_OWNERS = dict(
        [("tailCtrl%s_jnt" % c, "tail_1") for c in "ABC"] + [("tail%d_jnt" % i, "tail_1") for i in range(1, 6)])
    MIRROR_TEMPLATE = "ribbon_mirror"
    MIRROR_SKELETON = "ribbon_mirror"


class RibbonAimedContract(ModuleContract, RigTestCase):
    """The same contract on a ribbon with its ends on the layout's first and last sockets (so the end plug's parent twists the
    ribbon, and the inverse has to take that out) and with the aim blends set from the template."""
    TEMPLATE = "ribbon_aimed"
    SKELETON = "ribbon_basic"
    MODULE_TYPE = "ribbonModule"
    JOINT_OWNERS = RibbonContract.JOINT_OWNERS
    MIRROR_TEMPLATE = False


class RibbonPoses(RigTestCase):
    pass


add_pose_cases(RibbonPoses, "ribbon")


def attr(node, name):
    return cmds.getAttr("%s.%s" % (rig_name(node), name))


def set_attr(node, name, value):
    cmds.setAttr("%s.%s" % (rig_name(node), name), value)


def translation(node):
    return cmds.xform(rig_name(node), query=True, worldSpace=True, translation=True)


def world_matrix(node):
    return cmds.xform(rig_name(node), query=True, worldSpace=True, matrix=True)


class RibbonBehaviour(RigTestCase):

    def test_the_channels_are_on_the_first_control_with_their_rests_and_mirror_modes(self):
        self.build_standard("ribbon_basic", "ribbon_basic")
        channels = {c.name: c for c in rigmodules.query.get_control_channels(rig_name(FIRST))}
        for name, (rest, mirror) in CHANNELS.items():
            self.assertIn(name, channels, "%s has no %s channel" % (FIRST, name))
            self.assertAlmostEqual(channels[name].rest, rest, places=6, msg=name)
            self.assertEqual(channels[name].mirror, mirror, name)
        for control in [INTERIOR, LAST] + TWEAKERS:
            extra = set(c.name for c in rigmodules.query.get_control_channels(rig_name(control))) & set(CHANNELS)
            self.assertEqual(extra, set(), "%s carries ribbon channels: only the first control does" % control)

    def test_the_tweakers_are_hidden_and_still_keyable(self):
        self.build_standard("ribbon_basic", "ribbon_basic")
        for tweaker in TWEAKERS:
            self.assertFalse(attr(tweaker, "visibility"), "%s is not hidden" % tweaker)
        for control in [FIRST, INTERIOR, LAST]:
            self.assertTrue(attr(control, "visibility"), "%s is hidden" % control)
        set_attr("tail3_ctrl_c", "translateY", 1.5)
        self.assertWorldTranslation(rig_name("tail3_jnt"), [4, 1.5, 0], 1e-6)   # a tweaker's offset rides on the bind frame

    def test_a_tweaker_offset_moves_only_its_joint(self):
        self.build_standard("ribbon_basic", "ribbon_basic")
        before = {j: world_matrix(j) for j in BIND_JOINTS}
        set_attr("tail2_ctrl_c", "translateZ", 2.0)
        for joint in BIND_JOINTS:
            moved = world_matrix(joint) != before[joint]
            self.assertEqual(moved, joint == "tail2_jnt", "%s moved: %s" % (joint, moved))

    def test_the_marker_joints_follow_their_controls(self):
        self.build_standard("ribbon_basic", "ribbon_basic")
        set_attr(INTERIOR, "translateY", 2.0)
        self.assertWorldTranslation(rig_name("tailCtrlB_jnt"), [4, 2, 0], 1e-6)

    def test_an_interior_control_leaves_the_far_end_alone(self):
        """Locality: the base curve depends on the ends alone, and the detail curve through the controls changes only the
        segments around a moved one (a segment's tangents come from its neighbours). Positions only: a carried frame
        rolls along the whole curve."""
        self.build_standard("ribbon_five", "ribbon_five")
        before = {i: translation("spine%d_jnt" % i) for i in range(1, 10)}
        set_attr("spineCtrlB_ctrl_c", "translateY", 2.0)
        after = {i: translation("spine%d_jnt" % i) for i in range(1, 10)}
        for i in (1, 7, 8, 9):
            self.assertEqual(after[i], before[i], "spine%d moved with an interior control far from it" % i)
        for i in (2, 3, 6):
            self.assertNotEqual(after[i], before[i], "spine%d should follow the control" % i)
        self.assertWorldTranslation(rig_name("spine3_jnt"), [2, 2, 0], 1e-6)

    def test_evaluation_is_a_pure_function_of_the_inputs(self):
        """No state survives an evaluation (4.1): going to a pose, then another, then back, gives the same matrices."""
        self.build_standard("ribbon_basic", "ribbon_basic")
        pose_a = {LAST + ".rotateX": 200, FIRST + ".endTwist": 200, INTERIOR + ".translateY": 1.5, FIRST + ".bendRoundness": 0.4}
        pose_b = {LAST + ".rotateX": -170, FIRST + ".endTwist": -20, INTERIOR + ".translateY": -1.0, FIRST + ".bendRoundness": 1.0,
                  FIRST + ".sineAmplitude": 0.1}
        for plug, value in pose_a.items():
            cmds.setAttr(rig_name(plug), value)
        first = {j: world_matrix(j) for j in BIND_JOINTS}
        for plug, value in pose_b.items():
            cmds.setAttr(rig_name(plug), value)
        self.assertNotEqual({j: world_matrix(j) for j in BIND_JOINTS}, first)
        cmds.currentTime(7)
        cmds.currentTime(1)
        for plug, value in pose_a.items():
            cmds.setAttr(rig_name(plug), value)
        cmds.setAttr(rig_name(FIRST + ".sineAmplitude"), 0)
        for joint in BIND_JOINTS:
            self.assertEqual(world_matrix(joint), first[joint], "%s differs after a round trip" % joint)

    def test_the_ribbon_is_the_same_wherever_the_parent_is(self):
        self.build_standard("ribbon_basic", "ribbon_basic")
        set_attr(INTERIOR, "translateY", 2.0)
        before = {j: world_matrix(j) for j in BIND_JOINTS}
        set_attr("layout_0_ctrl_c", "translateZ", 5.0)
        for joint in BIND_JOINTS:
            moved = world_matrix(joint)
            self.assertAlmostEqual(moved[14], before[joint][14] + 5.0, places=6, msg=joint)
            for k in range(14):
                self.assertAlmostEqual(moved[k], before[joint][k], places=6, msg=joint)

    def test_a_saved_and_reopened_scene_evaluates_the_same(self):
        """A saved scene keeps only the stored rest elements that differ from their default (knot 0, an identity offset), so a
        reopened ribbon must read a missing element as its default and not fail its compute."""
        self.build_standard("ribbon_basic", "ribbon_basic")
        path = os.path.join(tempfile.mkdtemp(), "ribbon_reload.ma")
        cmds.file(rename=path)
        cmds.file(save=True, type="mayaAscii")
        cmds.file(path, open=True, force=True)
        set_attr(INTERIOR, "translateY", 2.0)
        self.assertWorldTranslation(rig_name("tail3_jnt"), [4, 2, 0], 1e-6)
        self.assertWorldTranslation(rig_name("tail2_jnt"), [2, 1, 0], 1e-3)

    def test_the_ends_twist_runs_along_the_whole_chain(self):
        """The start's twist is carried by the interior control as well, so it reaches the far end of the chain and not just
        the next control: with 90 on the start, the interior marker turns 45 and the last joint 0."""
        self.build_standard("ribbon_five", "ribbon_five")
        set_attr("spineCtrlA_ctrl_c", "rotateX", 90)
        turns = {}
        for name in ("spineCtrlB_jnt", "spineCtrlC_jnt", "spineCtrlD_jnt"):
            m = world_matrix(name)
            turns[name] = math.degrees(math.atan2(m[6], m[5]))
        self.assertAlmostEqual(turns["spineCtrlB_jnt"], 67.5, places=3)
        self.assertAlmostEqual(turns["spineCtrlC_jnt"], 45.0, places=3)
        self.assertAlmostEqual(turns["spineCtrlD_jnt"], 22.5, places=3)

    # ---- the end follows its plug's frame (design/ribbon-twist-follow.md) -----------------------

    def test_an_arms_ribbons_meet_at_the_elbow_with_the_shoulders_twist_easing_in(self):
        """An FK arm along X with a ribbon on each bone. The upper ribbon's start hangs on the layout (the body: no twist), its end
        on the elbow socket, which carries the shoulder's twist T. Its twist runs 0 to T along the bone, and the forearm ribbon,
        which starts on the elbow socket, starts at T, so the two meet. The arm is nearly straight (a 0.3 bend over 4), so the
        roll about X is the twist to within a degree. Before the end followed its plug, the seam was T."""
        self.build_standard("ribbon_arm", "ribbon_arm")
        set_attr("armSettings_ctrl_l", "ikFkBlend", 0)   # FK: the shoulder control's rotation reaches the elbow socket

        def roll(joint):
            m = world_matrix(joint)
            return math.degrees(math.atan2(m[6], m[5]))

        for twist in (60, 170, -170):
            set_attr("upperArm_ctrl_l", "rotateX", twist)
            for joint, u in (("up1_jnt", 0.0), ("up2_jnt", 0.25), ("up3_jnt", 0.5), ("up4_jnt", 0.75), ("up5_jnt", 1.0)):
                expected = twist * u
                self.assertAlmostEqual(math.remainder(roll(joint) - expected, 360.0), 0.0, delta=1.0, msg="T=%s %s" % (twist, joint))
            seam = math.remainder(roll("fo1_jnt") - roll("up5_jnt"), 360.0)
            self.assertAlmostEqual(seam, 0.0, delta=1.0, msg="the seam at the elbow for T=%s" % twist)

    def test_a_tilted_arm_follows_its_end_plugs_twist_about_the_bone(self):
        """The arm test above on an arm turned 35 degrees about Z, every joint turned with it. The end plug's motion is in world
        axes, so its twist must be read about the end control's own X (the bone), not the world X."""
        self.build_standard("ribbon_arm", "ribbon_arm_tilted")
        set_attr("armSettings_ctrl_l", "ikFkBlend", 0)
        rest = {j: om.MMatrix(world_matrix(j)) for j in ("up1_jnt", "up2_jnt", "up3_jnt", "up4_jnt", "up5_jnt", "fo1_jnt")}

        def twist_of(joint):
            motion = om.MMatrix(world_matrix(joint)) * rest[joint].inverse()
            q = om.MTransformationMatrix(motion).rotation(asQuaternion=True)
            return math.degrees(2.0 * math.atan2(q.x, q.w))

        set_attr("upperArm_ctrl_l", "rotateX", 60)
        for joint, u in (("up1_jnt", 0.0), ("up2_jnt", 0.25), ("up3_jnt", 0.5), ("up4_jnt", 0.75), ("up5_jnt", 1.0)):
            self.assertAlmostEqual(twist_of(joint), 60 * u, delta=2.0, msg=joint)
        self.assertAlmostEqual(twist_of("fo1_jnt") - twist_of("up5_jnt"), 0.0, delta=2.0, msg="the seam at the elbow")

    # ---- startAimBlend and endAimBlend in moduleArgs (design/ribbon-twist-follow.md) -----------

    def _with_aim(self, template="ribbon_basic", **aim):
        data = copy.deepcopy(fixtures.load_template(template))
        data["rigModules"][0]["moduleArgs"].update(aim)
        return data

    def test_the_aim_keys_set_the_channels_default_and_rest(self):
        """The keys are the same-named channels' rest (D34), so a build starts aimed and the other channels are untouched."""
        self.build(self._with_aim(startAimBlend=1, endAimBlend=0.5), self.make_skeleton("ribbon_basic"), label="ribbon_aim")
        channels = {c.name: c for c in rigmodules.query.get_control_channels(rig_name(FIRST))}
        rests = dict((name, rest) for name, (rest, mirror) in CHANNELS.items())
        rests.update(startAimBlend=1.0, endAimBlend=0.5)
        for name, rest in rests.items():
            self.assertAlmostEqual(channels[name].rest, rest, places=6, msg="%s rest" % name)
            self.assertAlmostEqual(attr(FIRST, name), rest, places=6, msg="%s value at build" % name)
        self.assertEqual(channels["startAimBlend"].mirror, "copy")

    def test_zeroing_the_control_returns_to_the_aimed_state(self):
        self.build(self._with_aim(startAimBlend=1, endAimBlend=1), self.make_skeleton("ribbon_basic"), label="ribbon_aim")
        set_attr(FIRST, "startAimBlend", 0.2)
        cmds.zeroRigControls(rig_name(FIRST))
        self.assertAlmostEqual(attr(FIRST, "startAimBlend"), 1.0, places=6)

    def test_an_aimed_build_straightens_the_ribbon_with_no_channel_set(self):
        """aim_blend_makes_a_straight_line.json with the blends from the template: the end moves 2 up and the joints stay on the
        line from the start to it."""
        self.build(self._with_aim(startAimBlend=1, endAimBlend=1), self.make_skeleton("ribbon_basic"), label="ribbon_aim")
        set_attr(LAST, "translateY", 2.0)
        for joint, point in (("tail2_jnt", [2, 0.5, 0]), ("tail3_jnt", [4, 1.0, 0]), ("tail4_jnt", [6, 1.5, 0])):
            self.assertWorldTranslation(rig_name(joint), point, 1e-3)

    def test_the_aim_keys_reach_the_mirrored_side(self):
        self.build(self._with_aim("ribbon_mirror", startAimBlend=1), self.make_skeleton("ribbon_mirror"), label="ribbon_aim_mirror")
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        firsts = [c for c in rigmodules.query.get_rig_controls(root) if c.usage == "tailCtrlA"]
        self.assertEqual(sorted(c.side for c in firsts), ["Left", "Right"])
        for control in firsts:
            channels = {c.name: c for c in rigmodules.query.get_control_channels(control.node)}
            self.assertAlmostEqual(channels["startAimBlend"].rest, 1.0, places=6, msg=control.side)
            self.assertAlmostEqual(channels["endAimBlend"].rest, 0.0, places=6, msg=control.side)

    def test_an_aim_key_outside_zero_to_one_is_an_error(self):
        for key, value in (("startAimBlend", 1.5), ("endAimBlend", -0.1)):
            message = self.assertMayaError(
                lambda: self.build(self._with_aim(**{key: value}), self.make_skeleton("ribbon_basic"), label="ribbon_aim_bad"))
            self.assertIn(key, message)
            self.assertIn("between 0 and 1", message)
            self.assertNothingBuilt()

    def test_a_misspelled_aim_key_is_an_error(self):
        message = self.assertMayaError(
            lambda: self.build(self._with_aim(startAimBlen=1), self.make_skeleton("ribbon_basic"), label="ribbon_aim_typo"))
        self.assertIn("startAimBlen", message)
        self.assertNothingBuilt()

    # ---- the plan step's checks (D17) ----------------------------------------------------------

    def _build_with(self, edit, skeleton="ribbon_basic"):
        data = copy.deepcopy(fixtures.load_template("ribbon_basic"))
        edit(data["rigModules"][0]["moduleArgs"])
        return self.assertMayaError(lambda: self.build(data, self.make_skeleton(skeleton), label="ribbon_bad"))

    def test_one_control_joint_is_an_error(self):
        message = self._build_with(lambda args: args.update(controls=[["Center", "tailCtrlA"]]))
        self.assertIn("control joints", message)
        self.assertNothingBuilt()

    def test_one_bind_joint_is_an_error(self):
        message = self._build_with(lambda args: args.update(joints=[["Center", "tail1"]]))
        self.assertIn("bind joints", message)
        self.assertNothingBuilt()

    def test_a_joint_cannot_be_a_control_and_a_bind_joint(self):
        message = self._build_with(lambda args: args.update(joints=[["Center", "tailCtrlA"]] + args["joints"]))
        self.assertIn("tailCtrlA", message)
        self.assertNothingBuilt()

    def test_more_than_eight_control_joints_is_an_error(self):
        message = self._build_with(lambda args: args.update(controls=[["Center", "tailCtrlA"]] * 9))
        self.assertIn("between 2 and 8", message)
        self.assertNothingBuilt()

    # ---- the end driven by another module's socket (section 3.4) -------------------------------

    def test_the_end_follows_its_own_parent(self):
        """The start and end child plugs carry their controls separately: with the start on layout_0 and the end on layout_2,
        lifting layout_2 lifts the end, and the interior control, which rides the base curve between them, rises half as far."""
        data = copy.deepcopy(fixtures.load_template("ribbon_basic"))
        data["rigModules"][0]["parentStructure"] = {"start": "layout.index_0", "end": "layout.index_2"}
        self.build(data, self.make_skeleton("ribbon_basic"), label="ribbon_driven_end")
        set_attr("layout_2_ctrl_c", "translateY", 3.0)
        self.assertWorldTranslation(rig_name("tail1_jnt"), [0, 0, 0], 1e-6)
        self.assertWorldTranslation(rig_name("tail5_jnt"), [8, 3, 0], 1e-6)
        self.assertWorldTranslation(rig_name("tail3_jnt"), [4, 1.5, 0], 1e-6)   # the base curve at u = 0.5 is the average of its ends
        self.assertWorldTranslation(rig_name("tailCtrlC_jnt"), [8, 3, 0], 1e-6)

    def test_an_unlisted_end_follows_the_start_parent(self):
        """With no entry for `end` it takes start's parent, not layout.end: lifting the start's parent lifts the whole ribbon,
        and lifting the layout's end control (where `end` would otherwise hang) does nothing to it."""
        data = copy.deepcopy(fixtures.load_template("ribbon_basic"))
        data["rigModules"][0]["parentStructure"] = {"start": "layout.index_0"}
        self.build(data, self.make_skeleton("ribbon_basic"), label="ribbon_end_follows_start")
        set_attr("layout_2_ctrl_c", "translateY", 3.0)
        self.assertWorldTranslation(rig_name("tail5_jnt"), [8, 0, 0], 1e-6)
        set_attr("layout_0_ctrl_c", "translateZ", 5.0)
        for joint, x in zip(BIND_JOINTS, (0, 2, 4, 6, 8)):
            self.assertWorldTranslation(rig_name(joint), [x, 0, 5], 1e-6)
