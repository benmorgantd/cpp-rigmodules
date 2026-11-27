"""Ribbon module: contract tests, pose cases from fixtures/poses/ribbon, and what only this module does.

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

FIRST = "tailIK_00_M_CTRL"
INTERIOR = "tailIK_01_M_CTRL"
LAST = "tailIK_02_M_CTRL"
BIND_JOINTS = ["tail%d_00_M_JNT" % i for i in range(1, 6)]
TWEAKERS = ["tail%d_00_M_CTRL" % i for i in range(1, 6)]

CHANNELS = {
    # name: (rest, mirror)
    "startTwist": (0.0, "negate"), "endTwist": (0.0, "negate"),
    "sineAmplitude": (0.0, "copy"), "sineFrequency": (1.0, "copy"), "sinePhase": (0.0, "copy"), "sineOrient": (0.0, "negate"),
    "sineStartFalloff": (0.2, "copy"), "sineEndFalloff": (0.2, "copy"), "startAimBlend": (0.0, "copy"), "endAimBlend": (0.0, "copy"),
    "bendRoundness": (1.0, "copy"), "volumePreserve": (0.0, "copy"), "scaleStart": (1.0, "copy"), "scaleMid": (1.0, "copy"),
    "scaleEnd": (1.0, "copy"),
}


class _RibbonRestore:
    """What the inverse can and can't give back on a ribbon. An interior ik control has no joint, so it is never restored:
    it keeps its sidecar or rest value, and the tweakers carry the difference. The end controls take the whole of the first and last
    bind joints, so those two tweakers can't be told apart from their ends. The round trip poses neither group (a posed interior
    control's frame can't come back through the tweakers when the volume channel gives it a non-uniform scale: a tweaker is a
    plain transform and can't hold the shear), and the other controls and every joint must come back."""
    UNPOSED = ("tailIK_01", "tailTweak_00", "tailTweak_04")
    LAST_BIND_CONTROLS = ("tailIK_02", "tailTweak_04")

    def _pose_channels(self, control):
        if rigmodules.query.get_control(control).usage in self.UNPOSED:
            return {}
        return super()._pose_channels(control)

    def controls_lost_with_joint(self, root, markup):
        """The last bind joint's tweaker and the end control that follows its frame."""
        return [c for c in rigmodules.query.get_rig_controls(root) if c.usage in self.LAST_BIND_CONTROLS and c.side == markup["side"]]


class RibbonContract(_RibbonRestore, ModuleContract, RigTestCase):
    TEMPLATE = "ribbon_basic"
    SKELETON = "ribbon_basic"
    MODULE_TYPE = "ribbonModule"
    JOINT_OWNERS = dict(("tail%d_00_M_JNT" % i, "tail_01_M_MOD") for i in range(1, 6))
    MIRROR_TEMPLATE = "ribbon_mirror"
    MIRROR_SKELETON = "ribbon_mirror"


class RibbonAimedContract(_RibbonRestore, ModuleContract, RigTestCase):
    """The same contract on a ribbon with its ends on the layout's first and last sockets (so the end plug's parent twists the
    ribbon, and the inverse has to take that out) and with the aim blends set from the template."""
    TEMPLATE = "ribbon_aimed"
    SKELETON = "ribbon_basic"
    MODULE_TYPE = "ribbonModule"
    JOINT_OWNERS = RibbonContract.JOINT_OWNERS
    MIRROR_TEMPLATE = False

    def joints_cannot_give(self, control, channel):
        """An aimed end's tangent comes from the other end, so only the twist of its rotation reaches the joints: the joints come
        back exactly, and the control gets a rotation that gives the same twist."""
        return channel.startswith("rotate") and rigmodules.query.get_control(control).usage in ("tailIK_00", "tailIK_02")


class RibbonNegXContract(_RibbonRestore, ModuleContract, RigTestCase):
    """The same contract on a chain that runs down -X while the joints' X points back up it (a mirrored limb): the inverse
    must give back the controls the forward solve was driven with, with the chain sign in play."""
    TEMPLATE = "ribbon_negx"
    SKELETON = "ribbon_negx"
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
            self.assertFalse(attr(tweaker, "shown"), "%s is not hidden" % tweaker)
            self.assertTrue(attr(tweaker, "visibility"), "%s is hidden by visibility, which would hide what is parented under it" % tweaker)
        for control in [FIRST, INTERIOR, LAST]:
            self.assertTrue(attr(control, "shown"), "%s is hidden" % control)
        set_attr("tail3_00_M_CTRL", "translateY", 1.5)
        self.assertWorldTranslation(rig_name("tail3_00_M_JNT"), [4, 1.5, 0], 1e-6)   # a tweaker's offset rides on the bind frame

    def test_a_tweaker_offset_moves_only_its_joint(self):
        self.build_standard("ribbon_basic", "ribbon_basic")
        before = {j: world_matrix(j) for j in BIND_JOINTS}
        set_attr("tail2_00_M_CTRL", "translateZ", 2.0)
        for joint in BIND_JOINTS:
            moved = world_matrix(joint) != before[joint]
            self.assertEqual(moved, joint == "tail2_00_M_JNT", "%s moved: %s" % (joint, moved))

    def test_an_interior_control_leaves_the_far_end_alone(self):
        """Locality: the base curve depends on the ends alone, and the detail curve through the controls changes only the
        segments around a moved one (a segment's tangents come from its neighbours). Positions only: a carried frame
        rolls along the whole curve."""
        self.build_standard("ribbon_five", "ribbon_five")
        before = {i: translation("spine%d_00_M_JNT" % i) for i in range(1, 10)}
        set_attr("spineIK_01_M_CTRL", "translateY", 2.0)
        after = {i: translation("spine%d_00_M_JNT" % i) for i in range(1, 10)}
        for i in (1, 7, 8, 9):
            self.assertEqual(after[i], before[i], "spine%d moved with an interior control far from it" % i)
        for i in (2, 3, 6):
            self.assertNotEqual(after[i], before[i], "spine%d should follow the control" % i)
        self.assertWorldTranslation(rig_name("spine3_00_M_JNT"), [2, 2, 0], 1e-6)

    def test_a_chain_running_down_minus_x_is_the_x_mirror_of_one_running_down_plus_x(self):
        """The same moves on both chains give joint positions that are mirrored in x. With the aim blends at their default
        0 the ends' own X axes set the tangents, so a chain whose joints point back up it only works if the ribbon reads the
        axes as pointing the other way. The moves: the end up, the interior up and sideways, a roll on the start."""
        moves = {LAST + ".translateY": 2, INTERIOR + ".translateZ": 1.5, FIRST + ".rotateX": 70, FIRST + ".bendRoundness": 0.7}

        def positions(template, skeleton):
            cmds.file(new=True, force=True)
            self.build_standard(template, skeleton)
            for plug, value in moves.items():
                cmds.setAttr(rig_name(plug), value)
            return {j: translation(j) for j in BIND_JOINTS}

        plus = positions("ribbon_basic", "ribbon_basic")
        minus = positions("ribbon_negx", "ribbon_negx")
        for joint in BIND_JOINTS:
            for axis, (a, b) in enumerate(zip(plus[joint], minus[joint])):
                self.assertAlmostEqual(-a if axis == 0 else a, b, places=4, msg="%s axis %d" % (joint, axis))

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
        set_attr("layout_00_M_CTRL", "translateZ", 5.0)
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
        self.assertWorldTranslation(rig_name("tail3_00_M_JNT"), [4, 2, 0], 1e-6)
        self.assertWorldTranslation(rig_name("tail2_00_M_JNT"), [2, 1, 0], 1e-3)

    def test_the_ends_twist_runs_along_the_whole_chain(self):
        """The start's twist is carried by the interior control as well, so it reaches the far end of the chain and not just
        the next control: with 90 on the start, the middle joint turns 45 and the last joint 0."""
        self.build_standard("ribbon_five", "ribbon_five")
        set_attr("spineIK_00_M_CTRL", "rotateX", 90)
        turns = {}
        for name in ("spine3_00_M_JNT", "spine5_00_M_JNT", "spine7_00_M_JNT"):
            m = world_matrix(name)
            turns[name] = math.degrees(math.atan2(m[6], m[5]))
        self.assertAlmostEqual(turns["spine3_00_M_JNT"], 67.5, places=3)
        self.assertAlmostEqual(turns["spine5_00_M_JNT"], 45.0, places=3)
        self.assertAlmostEqual(turns["spine7_00_M_JNT"], 22.5, places=3)

    # ---- the end follows its plug's frame (design/ribbon-twist-follow.md) -----------------------

    def test_an_arms_ribbons_meet_at_the_elbow_with_the_shoulders_twist_easing_in(self):
        """An FK arm along X with a ribbon on each bone. The upper ribbon's start hangs on the layout (the body: no twist), its end
        on the elbow socket, which carries the shoulder's twist T. Its twist runs 0 to T along the bone, and the forearm ribbon,
        which starts on the elbow socket, starts at T, so the two meet. The arm is nearly straight (a 0.3 bend over 4), so the
        roll about X is the twist to within a degree. Before the end followed its plug, the seam was T."""
        self.build_standard("ribbon_arm", "ribbon_arm")
        set_attr("armSettings_00_L_CTRL", "ikFkBlend", 0)   # FK: the shoulder control's rotation reaches the elbow socket

        def roll(joint):
            m = world_matrix(joint)
            return math.degrees(math.atan2(m[6], m[5]))

        for twist in (60, 170, -170):
            set_attr("upperArm_00_L_CTRL", "rotateX", twist)
            for joint, u in (("up1_00_M_JNT", 0.0), ("up2_00_M_JNT", 0.25), ("up3_00_M_JNT", 0.5), ("up4_00_M_JNT", 0.75), ("up5_00_M_JNT", 1.0)):
                expected = twist * u
                self.assertAlmostEqual(math.remainder(roll(joint) - expected, 360.0), 0.0, delta=1.0, msg="T=%s %s" % (twist, joint))
            seam = math.remainder(roll("fo1_00_M_JNT") - roll("up5_00_M_JNT"), 360.0)
            self.assertAlmostEqual(seam, 0.0, delta=1.0, msg="the seam at the elbow for T=%s" % twist)

    def test_a_tilted_arm_follows_its_end_plugs_twist_about_the_bone(self):
        """The arm test above on an arm turned 35 degrees about Z, every joint turned with it. The end plug's motion is in world
        axes, so its twist must be read about the end control's own X (the bone), not the world X."""
        self.build_standard("ribbon_arm", "ribbon_arm_tilted")
        set_attr("armSettings_00_L_CTRL", "ikFkBlend", 0)
        rest = {j: om.MMatrix(world_matrix(j)) for j in ("up1_00_M_JNT", "up2_00_M_JNT", "up3_00_M_JNT", "up4_00_M_JNT", "up5_00_M_JNT", "fo1_00_M_JNT")}

        def twist_of(joint):
            motion = om.MMatrix(world_matrix(joint)) * rest[joint].inverse()
            q = om.MTransformationMatrix(motion).rotation(asQuaternion=True)
            return math.degrees(2.0 * math.atan2(q.x, q.w))

        set_attr("upperArm_00_L_CTRL", "rotateX", 60)
        for joint, u in (("up1_00_M_JNT", 0.0), ("up2_00_M_JNT", 0.25), ("up3_00_M_JNT", 0.5), ("up4_00_M_JNT", 0.75), ("up5_00_M_JNT", 1.0)):
            self.assertAlmostEqual(twist_of(joint), 60 * u, delta=2.0, msg=joint)
        self.assertAlmostEqual(twist_of("fo1_00_M_JNT") - twist_of("up5_00_M_JNT"), 0.0, delta=2.0, msg="the seam at the elbow")

    # ---- startAimBlend and endAimBlend in moduleArgs (design/ribbon-twist-follow.md) -----------

    def _with_aim(self, template="ribbon_basic", **aim):
        data = copy.deepcopy(fixtures.load_template(template))
        data["rigModules"][0]["moduleArgs"].update(aim)
        return data

    def test_the_aim_keys_set_the_channels_default_and_rest(self):
        """The keys are the same-named channels' rest, so a build starts aimed and the other channels are untouched."""
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

    def test_the_aim_keys_reach_the_mirrored_side(self):
        self.build(self._with_aim("ribbon_mirror", startAimBlend=1), self.make_skeleton("ribbon_mirror"), label="ribbon_aim_mirror")
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        firsts = [c for c in rigmodules.query.get_rig_controls(root) if c.usage == "tailIK_00"]
        self.assertEqual(sorted(c.side for c in firsts), ["Left", "Right"])
        for control in firsts:
            channels = {c.name: c for c in rigmodules.query.get_control_channels(control.node)}
            self.assertAlmostEqual(channels["startAimBlend"].rest, 1.0, places=6, msg=control.side)
            self.assertAlmostEqual(channels["endAimBlend"].rest, 0.0, places=6, msg=control.side)

    # ---- the end driven by another module's socket (section 3.4) -------------------------------


class RibbonDerivedControls(RigTestCase):
    """The ik controls come from the bind joints, not from control joints: where they sit, how they are named, and what the
    read phase rejects. Expectations are worked by hand."""

    def _template(self, joints, **args):
        data = copy.deepcopy(fixtures.load_template("ribbon_basic"))
        module_args = data["rigModules"][0]["moduleArgs"]
        module_args["controlName"] = "tail"
        module_args["joints"] = [["Center", usage] for usage in joints]
        module_args.update(args)
        if module_args["controlName"] is None:   # the test wants the default base
            del module_args["controlName"]
        return data

    def _build(self, skeleton, joints, **args):
        self.build(self._template(joints, **args), self.make_skeleton(skeleton), label="ribbon_derived")

    def _rotation_degrees(self, node):
        return [round(a, 3) + 0.0 for a in cmds.xform(rig_name(node), query=True, worldSpace=True, rotation=True)]

    def test_a_control_between_two_joints_takes_the_blend_of_their_rotations(self):
        """Joints at x = 0, 4 and 8 turned 0, 0 and 90 about X, five controls at 0, 2, 4, 6 and 8. The control at 6 is halfway
        from the joint turned 0 to the one turned 90: 45 about X. The one at 2 is between two joints turned 0."""
        self._build("ribbon_twisted", ["tail1", "tail2", "tail3"], controlCount=5)
        self.assertWorldTranslation(rig_name("tailIK_03_M_CTRL"), [6, 0, 0], 1e-3)
        self.assertEqual(self._rotation_degrees("tailIK_03_M_CTRL"), [45, 0, 0])
        self.assertEqual(self._rotation_degrees("tailIK_01_M_CTRL"), [0, 0, 0])
        self.assertEqual(self._rotation_degrees("tailIK_04_M_CTRL"), [90, 0, 0])

    def test_there_are_no_joints_on_the_ik_controls(self):
        self._build("ribbon_basic", ["tail1", "tail2", "tail3", "tail4", "tail5"])
        indices = cmds.getAttr(rig_name("tail_01_M_MOD.jointOPM"), multiIndices=True)
        self.assertEqual(list(indices), [3, 4, 5, 6, 7], "only the tweakers' slots drive a joint")

    # ---- names --------------------------------------------------------------------------------

    def _usages(self):
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        return sorted(c.usage for c in rigmodules.query.get_rig_controls(root) if "IK_" in c.usage or "Tweak_" in c.usage)

    def test_control_name_sets_the_base_of_the_usages(self):
        self._build("ribbon_basic", ["tail1", "tail2", "tail3", "tail4", "tail5"], controlName="arm")
        self.assertEqual(self._usages(), ["armIK_00", "armIK_01", "armIK_02"] + ["armTweak_0%d" % i for i in range(5)])

    def test_the_default_base_drops_a_trailing_number(self):
        self._build("ribbon_numbered", ["arm_00", "arm_01", "arm_02"], controlName=None)
        self.assertEqual(self._usages(), ["armIK_00", "armIK_01", "armIK_02", "armTweak_00", "armTweak_01", "armTweak_02"])

    def test_a_base_with_no_number_is_kept_whole(self):
        self._build("ribbon_basic", ["tail1", "tail2", "tail3", "tail4", "tail5"], controlName=None)
        self.assertEqual(self._usages()[:3], ["tail1IK_00", "tail1IK_01", "tail1IK_02"])

    # ---- the chain checks ---------------------------------------------------------------
    # Each problem checkChain finds is a case in tests/core/ControlPlacementChainTests.cpp. These two prove the build runs it on
    # the joints in the scene and names the joint.

    def _build_turned(self, joint, rotate):
        joints = self.make_skeleton("ribbon_basic")
        cmds.xform(":skel:%s_00_M_JNT" % joint, worldSpace=True, rotation=rotate)
        self.build(self._template(["tail1", "tail2", "tail3", "tail4", "tail5"]), joints, label="ribbon_turned")

    def test_a_joint_turned_65_degrees_off_the_chain_is_an_error(self):
        message = self.assertMayaError(lambda: self._build_turned("tail3", [0, 0, 65]))
        self.assertIn("tail3", message)
        self.assertNothingBuilt()

    def test_a_joint_turned_55_degrees_off_the_chain_builds(self):
        self._build_turned("tail3", [0, 0, 55])
        self.assertExists(rig_name("tailIK_01_M_CTRL"))

    # ---- version 1 templates ------------------------------------------------------------------

    def test_a_version_1_template_reads_its_controls_as_a_count(self):
        """moduleVersion 1 listed control joints. The update step keeps the count and drops the joints, so five listed give five
        ik controls, and nothing looks the joints up (the skeleton has none)."""
        data = self._template(["tail1", "tail2", "tail3", "tail4", "tail5"], controlName="tail")
        data["rigModules"][0].pop("moduleVersion", None)
        data["rigModules"][0]["moduleArgs"]["controls"] = [["Center", "oldCtrl%d" % i] for i in range(5)]
        self.build(data, self.make_skeleton("ribbon_basic"), label="ribbon_v1")
        self.assertEqual(len([u for u in self._usages() if "IK_" in u]), 5)


FK_CONTROLS = ["tailFK_%02d_M_CTRL" % i for i in range(3)]


class RibbonFkContract(_RibbonRestore, ModuleContract, RigTestCase):
    """The same contract on a ribbon with an fk chain. An fk control has no joint, so like an interior ik control it keeps
    its sidecar or rest value, and the round trip does not pose it."""
    TEMPLATE = "ribbon_fk"
    SKELETON = "ribbon_basic"
    MODULE_TYPE = "ribbonModule"
    JOINT_OWNERS = RibbonContract.JOINT_OWNERS
    MIRROR_TEMPLATE = False
    UNPOSED = _RibbonRestore.UNPOSED + ("tailFK_00", "tailFK_01", "tailFK_02")

    def joints_cannot_give(self, control, channel):
        """An end's tangent comes from the fk chain, so only the twist of its rotation reaches the joints: the joints come back
        exactly, and the control gets a rotation that gives the same twist (the posed swing changes the twist the euler angles
        read as, which is why the channel value is not compared)."""
        return channel.startswith("rotate") and rigmodules.query.get_control(control).usage in ("tailIK_00", "tailIK_02")


class RibbonFkStyle(RigTestCase):
    """The fk chain. The skeleton is five joints along +X at 0, 2, 4, 6 and 8, so the three ik controls and the three fk
    controls sit at 0, 4 and 8. Expectations are worked by hand."""

    def _template(self, **args):
        data = copy.deepcopy(fixtures.load_template("ribbon_fk"))
        data["rigModules"][0]["moduleArgs"].update(args)
        return data

    def _build(self, **args):
        self.build(self._template(**args), self.make_skeleton("ribbon_basic"), label="ribbon_fk")

    def test_the_fk_rest_roll_is_stored_at_build_one_per_fk_control(self):
        """The chain is straight along +X with no roll, so every fk control's rest roll is 0, arriving and leaving. Both arrays hold
        one element per fk control, so compute() reads them and never rebuilds the rest curve to measure them."""
        self._build()
        module = rig_name("tail_01_M_MOD")
        for name in ("fkRollRest", "fkRollRestIn"):
            self.assertEqual(cmds.getAttr(module + "." + name, size=True), 3, name)
            for k in range(3):
                self.assertAlmostEqual(cmds.getAttr("%s.%s[%d]" % (module, name, k)), 0.0, places=6)

    def test_compute_reads_the_stored_rest_roll(self):
        """The ik controls' roll is the measured roll minus the stored rest roll. A stored rest of 0.5 radians on the first fk
        control turns the first ik control about X by 0.5 radians (28.65 degrees) one way or the other."""
        self._build()
        module = rig_name("tail_01_M_MOD")
        self.assertWorldRotation(rig_name(FIRST), [0, 0, 0], 1e-4)
        cmds.setAttr(module + ".fkRollRest[0]", 0.5)
        rotation = cmds.xform(rig_name(FIRST), query=True, worldSpace=True, rotation=True)
        self.assertAlmostEqual(abs(rotation[0]), 28.6479, delta=0.01)

    # ---- twistRamp ------------------------------------------------------------------------
    # The ik controls are at u = 0, 0.5 and 1 and the bind joints at 0, 0.25, 0.5, 0.75 and 1. Rolling the first fk control rolls
    # the whole chain, so the fk roll is the same at every ik control, and a control's roll is its share of it. The joints between
    # the controls ease linearly between their rolls.

    def test_every_ik_control_stores_a_roll_share_of_one_without_a_ramp(self):
        self._build()
        module = rig_name("tail_01_M_MOD")
        self.assertEqual(cmds.getAttr(module + ".fkRollShare", size=True), 3)
        for j in range(3):
            self.assertAlmostEqual(cmds.getAttr("%s.fkRollShare[%d]" % (module, j)), 1.0, places=9)

    def test_the_ramp_is_stored_as_one_share_per_ik_control(self):
        """[[0, 0], [0.5, 0.35], [1, 1]] at u = 0, 0.5 and 1 is 0, 0.35 and 1."""
        self._build(twistRamp=[[0, 0.0], [0.5, 0.35], [1, 1.0]])
        module = rig_name("tail_01_M_MOD")
        for j, share in enumerate((0.0, 0.35, 1.0)):
            self.assertAlmostEqual(cmds.getAttr("%s.fkRollShare[%d]" % (module, j)), share, places=9)

    def test_fk_control_count_sets_the_chain_apart_from_the_ik_controls(self):
        """Two fk controls, at 0 and 8, under three ik controls: turning the first turns everything about the origin, as with
        three."""
        self._build(fkControlCount=2)
        self.assertExists(rig_name("tailFK_01_M_CTRL"))
        self.assertFalse(cmds.objExists(rig_name("tailFK_02_M_CTRL")))
        set_attr("tailFK_00_M_CTRL", "rotateZ", 30)
        c, s = math.cos(math.radians(30)), math.sin(math.radians(30))
        for i, joint in enumerate(BIND_JOINTS):
            self.assertWorldTranslation(rig_name(joint), [2.0 * i * c, 2.0 * i * s, 0], 1e-5)
        self.assertWorldTranslation(rig_name("tailFK_01_M_CTRL"), [8 * c, 8 * s, 0], 1e-6)

    def test_the_fk_chain_follows_the_controls_it_hangs_from(self):
        """The chain is built by DAG parenting: the first fk control under the start ik control, each after it under the one before.
        Nothing transforms by it (inheritsTransform is off), so the visibility and outliner are all it gives."""
        self._build()
        self.assertEqual(cmds.listRelatives(rig_name(FK_CONTROLS[1]), parent=True)[0].split(":")[-1], FK_CONTROLS[0])
        self.assertEqual(cmds.listRelatives(rig_name(FK_CONTROLS[2]), parent=True)[0].split(":")[-1], FK_CONTROLS[1])

    def test_there_is_no_end_child_plug_and_the_start_one_carries_the_ribbon(self):
        self._build()
        module = rig_name("tail_01_M_MOD")
        self.assertEqual(cmds.getAttr("%s.childPlugs" % module, size=True), 1)
        self.assertEqual(cmds.getAttr("%s.childPlugs[0].childPlugName" % module), "start")

    def test_the_fk_chain_has_the_default_ik_count(self):
        """Without fkControlCount the chain has controlCount controls: five ik controls give five fk controls."""
        self.build(self._template(controlCount=5), self.make_skeleton("ribbon_basic"), label="ribbon_fk_five")
        for i in range(5):
            self.assertExists(rig_name("tailFK_%02d_M_CTRL" % i))
        self.assertFalse(cmds.objExists(rig_name("tailFK_05_M_CTRL")))


class RibbonBindSockets(RigTestCase):
    """Every bind joint (bind_0 to bind_4) and every fk control (fk_0 to fk_2) is a socket, so a child plug or a constraint can
    take it. ribbon_bind_child has a joint, extra, at the third bind joint (4, 0, 0), and an FkChain of it hangs from bind_2. The fk
    version hangs it from fk_1, which sits at the same place."""

    EXTRA = "extra_00_M_JNT"
    BIND = "tail3_00_M_JNT"

    def _build(self, template, label):
        self.build(fixtures.load_template(template), self.make_skeleton("ribbon_bind_child"), label=label)

    def _pose(self):
        """The end control lifted and the start's twist turned, so the third bind joint moves and turns."""
        set_attr(LAST, "translateY", 3.0)
        set_attr(FIRST, "startTwist", 40.0)

    def _assert_follows(self, source):
        a = world_matrix(source)
        b = world_matrix(self.EXTRA)
        for x, y in zip(a, b):
            self.assertAlmostEqual(x, y, places=4)

    def test_the_child_follows_its_bind_joint_through_a_pose_and_a_twist(self):
        self._build("ribbon_bind_child", "ribbon_bind_child")
        self._pose()
        self.assertNotEqual(translation(self.BIND), [4, 0, 0])
        self._assert_follows(self.BIND)

    def test_the_child_is_not_hidden_by_the_hidden_tweaker(self):
        """The bind socket has no DAG transform, so the child's control keeps its place in the DAG and does not inherit the tweaker's
        hidden state."""
        self._build("ribbon_bind_child", "ribbon_bind_child")
        control = rig_name("extra_00_M_CTRL")
        self.assertTrue(cmds.getAttr(control + ".visibility"))
        parents = cmds.listRelatives(control, parent=True, fullPath=True) or []
        self.assertNotIn(rig_name(TWEAKERS[2]), [p.split("|")[-1] for p in parents])

    def test_a_constraint_can_be_driven_by_a_bind_joint(self):
        self._build("ribbon_bind_constraint", "ribbon_bind_constraint")
        self._pose()
        self.assertNotEqual(translation(self.BIND), [4, 0, 0])
        self._assert_follows(self.BIND)

    def test_a_child_follows_an_fk_control_and_hangs_under_it(self):
        self._build("ribbon_fk_child", "ribbon_fk_child")
        self.assertWorldTranslation(rig_name(self.EXTRA), [4, 0, 0], 1e-4)
        set_attr("tailFK_00_M_CTRL", "rotateZ", 30.0)   # turns the chain about the start, so fk_1 (4 along it) swings
        self.assertNotEqual(translation(self.EXTRA), [4, 0, 0])
        self._assert_follows("tailFK_01_M_CTRL")
        parents = cmds.listRelatives(rig_name("extra_00_M_CTRL"), parent=True, fullPath=True) or []
        self.assertEqual([p.split("|")[-1].lstrip(":") for p in parents], [rig_name("tailFK_01_M_CTRL").lstrip(":")])
