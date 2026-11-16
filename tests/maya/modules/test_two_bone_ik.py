"""TwoBoneIk module (D52): contract tests, pose cases from fixtures/poses/twoboneik, and what only this module does.

The pose cases hold hand-worked numbers (design/two-bone-ik.md sections 4.2 and 4.2a). Nothing here is recorded from the rig.
"""
import copy
import math
import os
import tempfile

from maya import cmds

import rigmodules.query
from rigtest import fixtures
from rigtest.case import RigTestCase, rig_name
from rigtest.cases import add_pose_cases
from rigtest.module_contract import ModuleContract

ARM = ["upperArm_ctrl_l", "lowerArm_ctrl_l", "hand_ctrl_l"]
HANDLE = "handIk_ctrl_l"
POLE = "armPole_ctrl_l"
SETTINGS = "armSettings_ctrl_l"
LIMB_JOINTS = ["upperArm_jnt", "lowerArm_jnt", "hand_jnt"]
ALL_CONTROLS = ARM + [HANDLE, POLE, SETTINGS]


class TwoBoneIkContract(ModuleContract, RigTestCase):
    TEMPLATE = "twoboneik_basic"
    SKELETON = "twoboneik_basic"
    MODULE_TYPE = "twoBoneIkModule"
    JOINT_OWNERS = {
        "upperArm_jnt": "arm_1", "lowerArm_jnt": "arm_1", "hand_jnt": "arm_1",
        "handIk_jnt": "arm_1", "armPole_jnt": "arm_1", "armSettings_jnt": "arm_1",
    }
    MIRROR_TEMPLATE = "twoboneik_mirror"
    MIRROR_SKELETON = "twoboneik_mirror"

    def joints_cannot_give(self, control, channel):
        """The contract poses every channel and the blend stays at 1 (IK), so the FK controls the blend hides can't come
        back from joints: the lower and end controls entirely, and the start control's rotation and scale (the IK root
        takes its translation only, D52 decision 16). The joints still match, which the round trip checks."""
        name = control.split(":")[-1].split("_")[0]   # upperArm_ctrl_l, or upperArm_l_ctrl_l in the mirror skeleton
        return name in ("lowerArm", "hand") or (name == "upperArm" and not channel.startswith("translate"))


class TwoBoneIkPoses(RigTestCase):
    pass


add_pose_cases(TwoBoneIkPoses, "twoboneik")


def translation(node):
    return cmds.xform(rig_name(node), query=True, worldSpace=True, translation=True)


def attr(node, name):
    return cmds.getAttr("%s.%s" % (rig_name(node), name))


def set_attr(node, name, value):
    cmds.setAttr("%s.%s" % (rig_name(node), name), value)


def world_matrix(node):
    return cmds.xform(rig_name(node), query=True, worldSpace=True, matrix=True)


class TwoBoneIkBehaviour(RigTestCase):

    def build_with_constraints(self, constraints, template="twoboneik_basic", skeleton="twoboneik_basic"):
        joints = self.make_skeleton(skeleton)
        data = copy.deepcopy(fixtures.load_template(template))
        data["constraints"] = constraints
        self.build(data, joints, label="twoboneik_constraints")

    # ---- blend ---------------------------------------------------------------------------------

    def test_blend_0_is_the_fk_chain_exactly(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        set_attr(SETTINGS, "ikFkBlend", 0)
        set_attr(ARM[0], "rotateZ", 31.7)
        set_attr(ARM[1], "rotateY", -12.3)
        set_attr(ARM[2], "rotateX", 58.1)
        set_attr(ARM[2], "scaleY", 1.25)
        # Move the IK side too: at blend 0 it has no say.
        set_attr(HANDLE, "translateX", -3)
        set_attr(POLE, "translateY", 4)
        for control, joint in zip(ARM, LIMB_JOINTS):
            expected = world_matrix(control)
            actual = world_matrix(joint)
            for e, a in zip(expected, actual):
                self.assertAlmostEqual(e, a, places=9, msg="%s is not at %s at blend 0" % (joint, control))

    def test_blend_1_hand_follows_the_handle(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        set_attr(HANDLE, "translateX", -2)
        set_attr(HANDLE, "translateY", 3)
        set_attr(HANDLE, "translateZ", 1)
        set_attr(HANDLE, "rotateZ", 40)
        self.assertWorldTranslation(rig_name("hand_jnt"), translation(HANDLE), 1e-6)
        self.assertWorldRotation(rig_name("hand_jnt"), [0, 0, 40], 1e-6)

    def test_the_ik_root_follows_the_start_controls_translation(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        set_attr(ARM[0], "translateY", 2)
        self.assertWorldTranslation(rig_name("upperArm_jnt"), [0, 2, 0])
        self.assertWorldTranslation(rig_name("hand_jnt"), [5, 0, 0])   # the handle stays where it is

    def test_visibility_follows_the_blend(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        # (blend, visible controls). FK lower and end show below 1, the IK handle and pole above 0.
        always = [ARM[0], SETTINGS]
        cases = [
            (1.0, always + [HANDLE, POLE]),
            (0.0, always + [ARM[1], ARM[2]]),
            (0.5, ALL_CONTROLS),
        ]
        for blend, visible in cases:
            set_attr(SETTINGS, "ikFkBlend", blend)
            for control in ALL_CONTROLS:
                self.assertEqual(bool(attr(control, "visibility")), control in visible, "%s at blend %s" % (control, blend))

    # ---- sockets and child modules -------------------------------------------------------------

    def test_sockets_and_child_plugs(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        module = rig_name("arm_1")
        sockets = [cmds.getAttr("%s.socketNames[%d]" % (module, i)) for i in range(6)]
        self.assertEqual(sockets, ['["index_0","start"]', '["index_1"]', '["index_2","end"]', '["ik"]', '["pole"]', '["settings"]'])
        plugs = [cmds.getAttr("%s.childPlugs[%d].childPlugName" % (module, i)) for i in (0, 3, 4, 5)]
        self.assertEqual(plugs, ["start", "ik", "pole", "settings"])

    def test_index_sockets_follow_the_blended_joints_and_a_child_keeps_its_visibility(self):
        self.build_standard("twoboneik_hand", "twoboneik_hand")
        finger_control = "finger_ctrl_l"
        # IK: the FK hand control is hidden, the finger isn't, and it rides on the IK hand.
        self.assertFalse(attr(ARM[2], "visibility"))
        self.assertTrue(attr(finger_control, "visibility"))
        parents = cmds.listRelatives(rig_name(finger_control), parent=True, fullPath=True) or []
        self.assertFalse(any(p.endswith("hand_ctrl_l") for p in parents), "a finger under the hidden FK hand would be hidden in IK")
        set_attr(HANDLE, "translateX", -5)
        set_attr(HANDLE, "translateY", 5)
        self.assertWorldTranslation(rig_name("finger_jnt"), [1, 6, 0])
        # FK: the same socket follows the FK hand.
        set_attr(SETTINGS, "ikFkBlend", 0)
        set_attr(ARM[0], "rotateZ", 90)
        self.assertWorldTranslation(rig_name("hand_jnt"), [0, 5, 0])
        self.assertWorldTranslation(rig_name("finger_jnt"), [-1, 6, 0])
        self.assertTrue(attr(finger_control, "visibility"))

    # ---- the settings control and spaces -------------------------------------------------------

    def test_settings_follows_the_start_parent_and_ignores_a_constraint_on_start(self):
        constraints = [{"name": "arm_start_space", "type": "Parent", "target": "arm_1.start", "drivers": [
            {"socket": "layout.end", "label": "Chest"}, {"socket": "layout.index_0", "label": "Hips"}]}]
        self.build_with_constraints(constraints)
        # Chest: everything the layout's end carries follows it.
        set_attr("layout_2_ctrl_c", "translateZ", 3)
        self.assertWorldTranslation(rig_name("armSettings_jnt"), [0, 8, 3])
        self.assertWorldTranslation(rig_name("upperArm_jnt"), [0, 0, 3])
        # Hips: the start control leaves the chest and the settings control doesn't.
        set_attr("upperArm_ctrl_l", "space", 1)
        self.assertWorldTranslation(rig_name("armSettings_jnt"), [0, 8, 3])
        self.assertWorldTranslation(rig_name("upperArm_jnt"), [0, 0, 0])

    def test_start_and_ik_take_constraint_spaces(self):
        constraints = [
            {"name": "arm_start_space", "type": "Parent", "target": "arm_1.start", "drivers": [
                {"socket": "layout.end", "label": "Chest"}, {"socket": "layout.index_0", "label": "Hips"}]},
            {"name": "arm_ik_space", "type": "Parent", "target": "arm_1.ik", "drivers": [
                {"socket": "layout.end", "label": "Chest"}, {"socket": "layout.index_0", "label": "Hips"}]},
        ]
        self.build_with_constraints(constraints)
        for control in (ARM[0], HANDLE):
            self.assertEqual(cmds.attributeQuery("space", node=rig_name(control), listEnum=True)[0], "Chest:Hips")
        set_attr("layout_2_ctrl_c", "translateY", 2)
        self.assertWorldTranslation(rig_name("hand_jnt"), [5, 2, 0])
        set_attr(HANDLE, "space", 1)
        self.assertWorldTranslation(rig_name("hand_jnt"), [5, 0, 0])

    def test_the_pole_owns_its_space_and_the_settings_control_has_none(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        self.assertEqual(cmds.attributeQuery("space", node=rig_name(POLE), listEnum=True)[0], "Parent:Ik")
        for control in ARM + [HANDLE, SETTINGS]:
            self.assertFalse(cmds.attributeQuery("space", node=rig_name(control), exists=True), control)

    # ---- the solve at its edges ----------------------------------------------------------------

    def test_pole_on_the_line_keeps_the_rest_bend_and_gives_no_nan(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        set_attr(POLE, "translateY", -5)   # the pole sits at (2.5, 0, 0), on the root-to-handle line
        self.assertWorldTranslation(rig_name("lowerArm_jnt"), [1.8, 2.4, 0])
        set_attr(HANDLE, "translateX", -1)
        for joint in LIMB_JOINTS:
            self.assertTrue(all(math.isfinite(v) for v in translation(joint)), joint)

    def test_handle_on_the_root_folds_without_nan(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        set_attr(HANDLE, "translateX", -5)
        for joint in LIMB_JOINTS:
            self.assertTrue(all(math.isfinite(v) for v in translation(joint)), joint)
        # Folded: the end is |L1 - L2| = 1 from the root, along the rest axis.
        self.assertWorldTranslation(rig_name("hand_jnt"), [1, 0, 0], 1e-3)

    def test_the_channels_are_limited_to_zero_to_one(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        for control, name in ((SETTINGS, "ikFkBlend"), (SETTINGS, "stretch"), (SETTINGS, "soft"), (POLE, "pin")):
            plug = "%s.%s" % (rig_name(control), name)
            self.assertEqual(cmds.attributeQuery(name, node=rig_name(control), range=True), [0.0, 1.0])
            self.assertMayaError(lambda: cmds.setAttr(plug, 7), "maximum")
            self.assertMayaError(lambda: cmds.setAttr(plug, -1), "minimum")
        # The module's own inputs clamp too, for a value that reaches them another way.
        module = rig_name("arm_1")
        for name in ("ikFkBlend", "stretch", "soft", "pin"):
            self.assertEqual(cmds.attributeQuery(name, node=module, range=True), [0.0, 1.0])

    # ---- the animation interface (D34) ---------------------------------------------------------

    def test_channels_defaults_ranges_and_mirror_modes(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")

        def channels(control):
            return {c.name: c for c in rigmodules.query.get_control_channels(rig_name(control))}

        settings = channels(SETTINGS)
        self.assertEqual(sorted(settings), ["ikFkBlend", "soft", "stretch", "visibility"])
        self.assertEqual((settings["ikFkBlend"].rest, settings["ikFkBlend"].min, settings["ikFkBlend"].max), (1.0, 0.0, 1.0))
        self.assertEqual((settings["stretch"].rest, settings["soft"].rest), (0.0, 0.0))
        self.assertEqual({c.mirror for c in settings.values()}, {"copy"})   # visibility is a built-in channel and copies

        handle = channels(HANDLE)
        self.assertEqual(sorted(handle), ["roll", "rotateX", "rotateY", "rotateZ", "translateX", "translateY", "translateZ"])
        self.assertEqual((handle["roll"].type, handle["roll"].rest, handle["roll"].mirror), ("doubleAngle", 0.0, "negate"))
        self.assertEqual({c.mirror for n, c in handle.items() if n != "roll"}, {"copy"})

        pole = channels(POLE)
        self.assertEqual(sorted(pole), ["pin", "space", "translateX", "translateY", "translateZ"])
        self.assertEqual((pole["pin"].rest, pole["pin"].mirror), (0.0, "copy"))
        self.assertEqual((pole["space"].type, pole["space"].rest, pole["space"].mirror), ("enum", 0, "copy"))

        # The FK controls are FkChain's: translate, rotate and scale. Visibility is a channel only where the module doesn't drive it.
        self.assertEqual(len(channels(ARM[0])), 10)
        for control in ARM[1:]:
            self.assertEqual(len(channels(control)), 9, control)

    def test_keep_list_leaves_only_the_named_channels_keyable_and_unlocked(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        free = {
            HANDLE: {"translateX", "translateY", "translateZ", "rotateX", "rotateY", "rotateZ"},
            POLE: {"translateX", "translateY", "translateZ"},
            SETTINGS: set(),
        }
        for control, kept in free.items():
            for channel in ("translate", "rotate", "scale"):
                for axis in "XYZ":
                    name = channel + axis
                    plug = "%s.%s" % (rig_name(control), name)
                    if name in kept:
                        self.assertTrue(cmds.getAttr(plug, keyable=True) and not cmds.getAttr(plug, lock=True), plug)
                    else:
                        self.assertTrue(cmds.getAttr(plug, lock=True) and not cmds.getAttr(plug, keyable=True), "%s should be locked and hidden" % plug)

    def test_mirroring_a_pose_negates_roll_and_copies_the_rest(self):
        joints = self.make_skeleton("twoboneik_mirror")
        self.build("twoboneik_mirror", joints)
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        left, right = "handIk_l_ctrl_l", "handIk_r_ctrl_r"
        set_attr(left, "roll", 35)
        set_attr("armSettings_l_ctrl_l", "stretch", 0.4)
        set_attr("armPole_l_ctrl_l", "pin", 0.25)
        path = os.path.join(tempfile.mkdtemp(), "arm.json").replace("\\", "/")
        cmds.saveRigPose(root, file=path)
        cmds.zeroRigControls(root)
        cmds.loadRigPose(root, file=path, mirror=True)
        self.assertAlmostEqual(attr(right, "roll"), -35, places=4)
        self.assertAlmostEqual(attr("armSettings_r_ctrl_r", "stretch"), 0.4, places=6)
        self.assertAlmostEqual(attr("armPole_r_ctrl_r", "pin"), 0.25, places=6)
        self.assertAlmostEqual(attr(left, "roll"), 0, places=6)

    # ---- playback ------------------------------------------------------------------------------

    def test_playback_never_pulls_the_match_outputs(self):
        """The match outputs allocate in compute(), so only setRigIkFkBlend may pull them (D36, D52 phase 8). Maya's profiler
        records computeMatch's scope: a keyed playback range must leave it out, while the same recording sees the module's
        other outputs (so it is recording) and sees computeMatch once a match output is pulled (so the scope fires)."""
        import maya.OpenMaya
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        cmds.evaluationManager(mode="off")   # plain DG: only what is pulled evaluates
        module = rig_name("arm_1")
        for frame, blend, handle in ((1, 1.0, 0.0), (6, 0.5, 2.0), (12, 0.0, -2.0), (18, 1.0, 3.0)):
            cmds.setKeyframe(rig_name(SETTINGS) + ".ikFkBlend", time=frame, value=blend)
            cmds.setKeyframe(rig_name(HANDLE) + ".translateX", time=frame, value=handle)

        def recorded_events(action):
            cmds.profiler(reset=True)
            cmds.profiler(sampling=True)
            try:
                action()
            finally:
                cmds.profiler(sampling=False)
            profiler = maya.OpenMaya.MProfiler
            return [(profiler.getEventName(i), profiler.getDescription(i)) for i in range(profiler.getEventCount())]

        def play():
            for frame in range(1, 19):
                cmds.currentTime(frame)
                for joint in LIMB_JOINTS:
                    cmds.getAttr(rig_name(joint) + ".worldMatrix[0]")

        def pull_match():
            cmds.setAttr(rig_name(HANDLE) + ".translateY", 0.5)
            cmds.getAttr(module + ".fkMatchLocal[0]")

        playback = recorded_events(play)
        self.assertTrue(any(name.endswith("arm_1") and description == "jointOPM" for name, description in playback),
                        "the profiler did not record the module's evaluation")
        self.assertFalse(any("computeMatch" in name for name, _ in playback), "playback pulled a match output")
        pulled = recorded_events(pull_match)
        self.assertTrue(any("computeMatch" in name for name, _ in pulled), "computeMatch left no profiler event")
