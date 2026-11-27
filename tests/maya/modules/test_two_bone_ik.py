"""TwoBoneIk module: contract tests, pose cases from fixtures/poses/twoboneik, and what only this module does.

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

ARM = ["upperArm_00_L_CTRL", "lowerArm_00_L_CTRL", "hand_00_L_CTRL"]
HANDLE = "armIK_00_L_CTRL"
POLE = "armPole_00_L_CTRL"
SETTINGS = "armSettings_00_L_CTRL"
LIMB_JOINTS = ["upperArm_00_M_JNT", "lowerArm_00_M_JNT", "hand_00_M_JNT"]
ALL_CONTROLS = ARM + [HANDLE, POLE, SETTINGS]


class TwoBoneIkContract(ModuleContract, RigTestCase):
    TEMPLATE = "twoboneik_basic"
    SKELETON = "twoboneik_basic"
    MODULE_TYPE = "twoBoneIkModule"
    JOINT_OWNERS = {"upperArm_00_M_JNT": "arm_01_L_MOD", "lowerArm_00_M_JNT": "arm_01_L_MOD", "hand_00_M_JNT": "arm_01_L_MOD"}
    MIRROR_TEMPLATE = "twoboneik_mirror"
    MIRROR_SKELETON = "twoboneik_mirror"

    def joints_cannot_give(self, control, channel):
        """The contract poses every channel and the blend stays at 1 (IK), so the FK controls the blend hides can't come
        back from joints: the lower and end controls entirely, and the start control's rotation and scale (the IK root
        takes its translation only; design/two-bone-ik.md). The joints still match, which the round trip checks."""
        name = control.split(":")[-1].split("_")[0]   # upperArm_00_L_CTRL, or upperArm_00_L_CTRL in the mirror skeleton
        return name in ("lowerArm", "hand") or (name == "upperArm" and not channel.startswith("translate"))

    def controls_lost_with_joint(self, root, markup):
        """The IK handle and pole are solved from all three limb joints, so a source that lacks the hand gives
        none of the limb's controls: every one of them comes from the sidecar. The settings control has no joint anyway."""
        return [c for c in rigmodules.query.get_rig_controls(root) if cmds.nodeType(c.module) == self.MODULE_TYPE]


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
        self.assertWorldTranslation(rig_name("hand_00_M_JNT"), translation(HANDLE), 1e-6)
        self.assertWorldRotation(rig_name("hand_00_M_JNT"), [0, 0, 40], 1e-6)

    def test_the_ik_root_follows_the_start_controls_translation(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        set_attr(ARM[0], "translateY", 2)
        self.assertWorldTranslation(rig_name("upperArm_00_M_JNT"), [0, 2, 0])
        self.assertWorldTranslation(rig_name("hand_00_M_JNT"), [5, 0, 0])   # the handle stays where it is

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
                self.assertEqual(bool(attr(control, "shown")), control in visible, "%s at blend %s" % (control, blend))
                self.assertTrue(attr(control, "visibility"), "%s: the blend must not hide it through visibility (D78)" % control)

    # ---- the ikFkBlend template key --------------------------------------------------------------

    def _with_blend(self, value):
        data = copy.deepcopy(fixtures.load_template("twoboneik_basic"))
        data["rigModules"][0]["moduleArgs"]["ikFkBlend"] = value
        return data

    def test_the_ik_fk_blend_key_sets_the_channels_value_and_rest(self):
        """ikFkBlend: 0 builds the limb in FK. The channel holds 0 and its rest is 0, so zeroRigControls returns to FK and not to IK."""
        self.build(self._with_blend(0), self.make_skeleton("twoboneik_basic"), label="twoboneik_fk")
        self.assertAlmostEqual(attr(SETTINGS, "ikFkBlend"), 0.0, places=9)
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        settings = [c for c in rigmodules.query.get_rig_controls(root) if c.usage == "armSettings"][0]
        channels = {c.name: c for c in rigmodules.query.get_control_channels(settings.node)}
        self.assertEqual((channels["ikFkBlend"].rest, channels["ikFkBlend"].min, channels["ikFkBlend"].max), (0.0, 0.0, 1.0))
        set_attr(SETTINGS, "ikFkBlend", 1)
        cmds.zeroRigControls(rig_name(SETTINGS))
        self.assertAlmostEqual(attr(SETTINGS, "ikFkBlend"), 0.0, places=9)

    def test_a_limb_built_with_ik_fk_blend_0_follows_its_fk_controls(self):
        self.build(self._with_blend(0), self.make_skeleton("twoboneik_basic"), label="twoboneik_fk")
        set_attr(ARM[0], "rotateZ", 20)
        set_attr(HANDLE, "translateX", -3)   # the IK side has no say at blend 0
        for control, joint in zip(ARM, LIMB_JOINTS):
            for e, a in zip(world_matrix(control), world_matrix(joint)):
                self.assertAlmostEqual(e, a, places=9, msg="%s is not at %s" % (joint, control))
        self.assertFalse(attr(HANDLE, "shown"))
        self.assertTrue(attr(ARM[1], "shown"))

    def test_an_ik_fk_blend_outside_zero_to_one_is_an_error(self):
        for value in (1.5, -0.1):
            message = self.assertMayaError(lambda: self.build(self._with_blend(value), self.make_skeleton("twoboneik_basic"), label="twoboneik_bad"))
            self.assertIn("ikFkBlend", message)
            self.assertIn("between 0", message)
            self.assertNothingBuilt()

    # ---- sockets and child modules -------------------------------------------------------------

    def test_sockets_and_child_plugs(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        module = rig_name("arm_01_L_MOD")
        sockets = [cmds.getAttr("%s.socketNames[%d]" % (module, i)) for i in range(6)]
        self.assertEqual(sockets, ['["index_0","start"]', '["index_1"]', '["index_2","end"]', '["ik"]', '["pole"]', '["settings"]'])
        plugs = [cmds.getAttr("%s.childPlugs[%d].childPlugName" % (module, i)) for i in (0, 3, 4, 5)]
        self.assertEqual(plugs, ["start", "ik", "pole", "settings"])

    def test_index_sockets_follow_the_blended_joints_and_a_child_keeps_its_visibility(self):
        self.build_standard("twoboneik_hand", "twoboneik_hand")
        finger_control = "finger_00_L_CTRL"
        # IK: the FK hand control is not drawn, the finger is, and it rides on the IK hand. The finger's DAG parent is the FK hand
        # control (the socket's transform) and is still shown, because the blend drives `shown` and not visibility.
        self.assertFalse(attr(ARM[2], "shown"))
        self.assertTrue(attr(finger_control, "shown"))
        self.assertTrue(attr(finger_control, "visibility"))
        parents = cmds.listRelatives(rig_name(finger_control), parent=True, fullPath=True) or []
        self.assertTrue(any(p.endswith("hand_00_L_CTRL") for p in parents), "the finger's DAG parent is the FK hand control")
        set_attr(HANDLE, "translateX", -5)
        set_attr(HANDLE, "translateY", 5)
        self.assertWorldTranslation(rig_name("finger_00_M_JNT"), [1, 6, 0])
        # FK: the same socket follows the FK hand.
        set_attr(SETTINGS, "ikFkBlend", 0)
        set_attr(ARM[0], "rotateZ", 90)
        self.assertWorldTranslation(rig_name("hand_00_M_JNT"), [0, 5, 0])
        self.assertWorldTranslation(rig_name("finger_00_M_JNT"), [-1, 6, 0])
        self.assertTrue(attr(finger_control, "visibility"))

    def test_a_dag_parent_and_a_matrix_parent_can_differ(self):
        """parentStructure is the DAG parent (the settings control, a socket with a control) and matrixParentStructure the matrix the
        finger follows (the blended hand, a socket that is the joint's world). Names the rigger chose, so the build does not compare them."""
        data = copy.deepcopy(fixtures.load_template("twoboneik_hand"))
        data["rigModules"][1]["parentStructure"] = {"start": "arm_01_L_MOD.settings"}
        data["rigModules"][1]["matrixParentStructure"] = {"start": "arm_01_L_MOD.end"}
        self.build(data, self.make_skeleton("twoboneik_hand"), label="twoboneik_dag_matrix")
        finger_control = "finger_00_L_CTRL"
        parents = cmds.listRelatives(rig_name(finger_control), parent=True, fullPath=True) or []
        self.assertTrue(any(p.endswith(SETTINGS) for p in parents), "the DAG parent is the settings control: %s" % parents)
        set_attr(HANDLE, "translateX", -5)
        set_attr(HANDLE, "translateY", 5)
        self.assertWorldTranslation(rig_name("finger_00_M_JNT"), [1, 6, 0])   # it still rides the IK hand

    def test_without_a_matrix_parent_the_matrix_follows_the_dag_parent(self):
        """The default: no matrixParentStructure entry, so the finger's matrix comes from its DAG parent socket (the FK hand control's
        socket is the blended hand's world, so it is the same as the matrix test above)."""
        self.build_standard("twoboneik_hand", "twoboneik_hand")
        set_attr(HANDLE, "translateX", -5)
        set_attr(HANDLE, "translateY", 5)
        self.assertWorldTranslation(rig_name("finger_00_M_JNT"), [1, 6, 0])

    # ---- the settings control and spaces -------------------------------------------------------

    def test_settings_follows_the_start_parent_and_ignores_a_constraint_on_start(self):
        constraints = [{"name": "armStartSpace_00_L_CON", "type": "Parent", "target": "arm_01_L_MOD.start", "drivers": [
            {"socket": "layout_00_M_MOD.end", "label": "Chest"}, {"socket": "layout_00_M_MOD.index_0", "label": "Hips"}]}]
        self.build_with_constraints(constraints)
        # Chest: everything the layout's end carries follows it.
        set_attr("layout_02_M_CTRL", "translateZ", 3)
        self.assertWorldTranslation(rig_name(SETTINGS), [0, 8, 3])
        self.assertWorldTranslation(rig_name("upperArm_00_M_JNT"), [0, 0, 3])
        # Hips: the start control leaves the chest and the settings control doesn't.
        set_attr("upperArm_00_L_CTRL", "space", 1)
        self.assertWorldTranslation(rig_name(SETTINGS), [0, 8, 3])
        self.assertWorldTranslation(rig_name("upperArm_00_M_JNT"), [0, 0, 0])

    def test_start_and_ik_take_constraint_spaces(self):
        constraints = [
            {"name": "armStartSpace_00_L_CON", "type": "Parent", "target": "arm_01_L_MOD.start", "drivers": [
                {"socket": "layout_00_M_MOD.end", "label": "Chest"}, {"socket": "layout_00_M_MOD.index_0", "label": "Hips"}]},
            {"name": "armIkSpace_00_L_CON", "type": "Parent", "target": "arm_01_L_MOD.ik", "drivers": [
                {"socket": "layout_00_M_MOD.end", "label": "Chest"}, {"socket": "layout_00_M_MOD.index_0", "label": "Hips"}]},
        ]
        self.build_with_constraints(constraints)
        for control in (ARM[0], HANDLE):
            self.assertEqual(cmds.attributeQuery("space", node=rig_name(control), listEnum=True)[0], "Chest:Hips")
        set_attr("layout_02_M_CTRL", "translateY", 2)
        self.assertWorldTranslation(rig_name("hand_00_M_JNT"), [5, 2, 0])
        set_attr(HANDLE, "space", 1)
        self.assertWorldTranslation(rig_name("hand_00_M_JNT"), [5, 0, 0])

    def test_the_pole_owns_its_space_and_the_settings_control_has_none(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        self.assertEqual(cmds.attributeQuery("space", node=rig_name(POLE), listEnum=True)[0], "Parent:Ik")
        for control in ARM + [HANDLE, SETTINGS]:
            self.assertFalse(cmds.attributeQuery("space", node=rig_name(control), exists=True), control)

    # ---- the solve at its edges ----------------------------------------------------------------

    def test_pole_on_the_line_keeps_the_rest_bend_and_gives_no_nan(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        set_attr(POLE, "translateY", -5)   # the pole sits at (2.5, 0, 0), on the root-to-handle line
        self.assertWorldTranslation(rig_name("lowerArm_00_M_JNT"), [1.8, 2.4, 0])
        set_attr(HANDLE, "translateX", -1)
        for joint in LIMB_JOINTS:
            self.assertTrue(all(math.isfinite(v) for v in translation(joint)), joint)

    def test_handle_on_the_root_folds_without_nan(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        set_attr(HANDLE, "translateX", -5)
        for joint in LIMB_JOINTS:
            self.assertTrue(all(math.isfinite(v) for v in translation(joint)), joint)
        # Folded: the end is |L1 - L2| = 1 from the root, along the rest axis.
        self.assertWorldTranslation(rig_name("hand_00_M_JNT"), [1, 0, 0], 1e-3)

    def test_the_channels_are_limited_to_zero_to_one(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        for control, name in ((SETTINGS, "ikFkBlend"), (SETTINGS, "stretch"), (SETTINGS, "soft"), (POLE, "pin")):
            plug = "%s.%s" % (rig_name(control), name)
            self.assertEqual(cmds.attributeQuery(name, node=rig_name(control), range=True), [0.0, 1.0])
            self.assertMayaError(lambda: cmds.setAttr(plug, 7), "maximum")
            self.assertMayaError(lambda: cmds.setAttr(plug, -1), "minimum")
        # The module's own inputs clamp too, for a value that reaches them another way.
        module = rig_name("arm_01_L_MOD")
        for name in ("ikFkBlend", "stretch", "soft", "pin"):
            self.assertEqual(cmds.attributeQuery(name, node=module, range=True), [0.0, 1.0])

    # ---- the animation interface ---------------------------------------------------------

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
        left, right = "armIK_00_L_CTRL", "armIK_00_R_CTRL"
        set_attr(left, "roll", 35)
        set_attr("armSettings_00_L_CTRL", "stretch", 0.4)
        set_attr("armPole_00_L_CTRL", "pin", 0.25)
        path = os.path.join(tempfile.mkdtemp(), "arm.json").replace("\\", "/")
        cmds.saveRigPose(root, file=path)
        cmds.zeroRigControls(root)
        cmds.loadRigPose(root, file=path, mirror=True)
        self.assertAlmostEqual(attr(right, "roll"), -35, places=4)
        self.assertAlmostEqual(attr("armSettings_00_R_CTRL", "stretch"), 0.4, places=6)
        self.assertAlmostEqual(attr("armPole_00_R_CTRL", "pin"), 0.25, places=6)
        self.assertAlmostEqual(attr(left, "roll"), 0, places=6)

    # ---- playback ------------------------------------------------------------------------------

    def test_playback_never_pulls_the_match_outputs(self):
        """The match outputs allocate in compute(), so only setRigIkFkBlend may pull them. Maya's profiler
        records computeMatch's scope: a keyed playback range must leave it out, while the same recording sees the module's
        other outputs (so it is recording) and sees computeMatch once a match output is pulled (so the scope fires)."""
        import maya.OpenMaya
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        cmds.evaluationManager(mode="off")   # plain DG: only what is pulled evaluates
        module = rig_name("arm_01_L_MOD")
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
        self.assertTrue(any(name.endswith("arm_01_L_MOD") and description == "jointOPM" for name, description in playback),
                        "the profiler did not record the module's evaluation")
        self.assertFalse(any("computeMatch" in name for name, _ in playback), "playback pulled a match output")
        pulled = recorded_events(pull_match)
        self.assertTrue(any("computeMatch" in name for name, _ in pulled), "computeMatch left no profiler event")


class TwoBoneIkDerivedControls(RigTestCase):
    """The IK handle, pole and settings controls come from the three limb joints: where they sit, how they are named, and
    what an old template does. Expectations are worked by hand from the fixtures' joint positions."""

    def _template(self, name="twoboneik_basic", **args):
        """A copy of a fixture template with its offsets and controlName replaced by args."""
        data = copy.deepcopy(fixtures.load_template(name))
        module_args = data["rigModules"][0]["moduleArgs"]
        for key in ("controlName", "poleOffset", "settingsOffset"):
            module_args.pop(key, None)
        module_args.update(args)
        return data

    def _usages(self):
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        return sorted(c.usage for c in rigmodules.query.get_rig_controls(root) if c.usage.endswith(("IK", "Pole", "Settings")))

    def _build_basic(self, **args):
        self.build(self._template(**args), self.make_skeleton("twoboneik_basic"), label="derived")

    # ---- placement -----------------------------------------------------------------------------

    def test_the_handle_sits_on_the_end_joint(self):
        self._build_basic(controlName="arm")
        self.assertWorldTranslation(rig_name(HANDLE), [5, 0, 0])

    def test_the_default_pole_is_pushed_out_along_the_bend_by_the_limbs_length(self):
        """Root (0,0,0), mid (1.8,2.4,0), end (5,0,0): bones 3 and 4, so the limb is 7 long. The mid is 2.4 above the root-to-end
        line (+X), so the bend is +Y and the pole is mid + 7 * (0,1,0) = (1.8, 9.4, 0)."""
        self._build_basic(controlName="arm")
        self.assertWorldTranslation(rig_name(POLE), [1.8, 9.4, 0])

    def test_the_default_settings_control_is_a_quarter_of_the_limbs_length_along_world_x_from_the_end(self):
        """The limb is 7 long, so the settings control is the end (5,0,0) plus (1.75,0,0)."""
        self._build_basic(controlName="arm")
        self.assertWorldTranslation(rig_name(SETTINGS), [6.75, 0, 0])

    def test_the_default_settings_control_of_a_mirrored_limb_is_the_reflection(self):
        """Without settingsOffset the right side's default is -X from its end, the reflection of the left's."""
        data = self._template("twoboneik_mirror", controlName="arm", poleOffset=[0.7, 2.6, 0])
        self.build_standard(data, "twoboneik_mirror")
        left = cmds.xform(rig_name("armSettings_00_L_CTRL"), query=True, worldSpace=True, translation=True)
        right = cmds.xform(rig_name("armSettings_00_R_CTRL"), query=True, worldSpace=True, translation=True)
        for expected, actual in zip([left[0], left[1], left[2]], [-right[0], right[1], right[2]]):
            self.assertAlmostEqual(expected, actual, places=4)
        self.assertGreater(left[0], 6.0)

    def test_offsets_move_the_pole_and_the_settings_control(self):
        """The pole is the mid joint plus poleOffset and the settings control the end joint plus settingsOffset, in world space."""
        self._build_basic(controlName="arm", poleOffset=[1, -2, 3], settingsOffset=[0.5, 4, -1])
        self.assertWorldTranslation(rig_name(POLE), [2.8, 0.4, 3])
        self.assertWorldTranslation(rig_name(SETTINGS), [5.5, 4, -1])
        self.assertWorldTranslation(rig_name(HANDLE), [5, 0, 0])

    def test_the_other_side_of_a_mirrored_limb_negates_the_offsets_x(self):
        """Left: mid (2.8,2.4,0) + (0.7,2.6,0) = (3.5,5,0), end (6,0,0) + (-5,8,0) = (1,8,0). Right is the reflection: X negated in
        the offsets, on joints that are already reflected, so (-3.5,5,0) and (-1,8,0)."""
        self.build_standard("twoboneik_mirror", "twoboneik_mirror")
        self.assertWorldTranslation(rig_name("armPole_00_L_CTRL"), [3.5, 5, 0])
        self.assertWorldTranslation(rig_name("armSettings_00_L_CTRL"), [1, 8, 0])
        self.assertWorldTranslation(rig_name("armPole_00_R_CTRL"), [-3.5, 5, 0])
        self.assertWorldTranslation(rig_name("armSettings_00_R_CTRL"), [-1, 8, 0])
        self.assertWorldTranslation(rig_name("armIK_00_R_CTRL"), [-6, 0, 0])

    def test_the_handle_takes_the_end_joints_rotation_and_the_others_world_axes(self):
        """The tilted arm's joints are turned 35 degrees about Z. The handle turns with the hand. The pole and settings controls'
        rotation is locked, so they stay on world axes: only their translate channels read in world axes."""
        self.build_standard("ribbon_arm", "ribbon_arm_tilted")
        self.assertWorldRotation(rig_name(HANDLE), [0, 0, 35], 1e-4)
        self.assertWorldRotation(rig_name(POLE), [0, 0, 0], 1e-4)
        self.assertWorldRotation(rig_name(SETTINGS), [0, 0, 0], 1e-4)

    # ---- a world-aligned handle ----------------------------------------------------------

    def _assert_bind_pose_kept(self):
        """The IK joints take their own rest offsets, so any handle rotation leaves the joints on the FK controls at rest."""
        for control, joint in zip(ARM, LIMB_JOINTS):
            for e, a in zip(world_matrix(control), world_matrix(joint)):
                self.assertAlmostEqual(e, a, places=6, msg="%s moved off %s" % (joint, control))

    def test_ik_orient_to_world_gives_the_handle_world_axes_on_a_turned_hand(self):
        """The tilted arm's hand is turned 35 degrees about Z, and the handle normally turns with it. With ikOrientToWorld the
        handle is on world axes and the hand is where it was."""
        data = self._template("ribbon_arm", controlName="arm", poleOffset=[0, -4.7, 0], settingsOffset=[-8, 8, 0], ikOrientToWorld=True)
        self.build(data, self.make_skeleton("ribbon_arm_tilted"), label="world_handle")
        self.assertWorldRotation(rig_name(HANDLE), [0, 0, 0], 1e-6)
        self.assertWorldRotation(rig_name("hand_00_M_JNT"), [0, 0, 35], 1e-4)
        self.assertWorldTranslation(rig_name(HANDLE), translation("hand_00_M_JNT"), 1e-6)

    def test_ik_orient_to_world_keeps_the_bind_pose_and_turning_the_handle_turns_the_hand(self):
        self._build_basic(controlName="arm", ikOrientToWorld=True)
        self._assert_bind_pose_kept()
        set_attr(HANDLE, "rotateZ", 40)
        self.assertWorldRotation(rig_name("hand_00_M_JNT"), [0, 0, 40], 1e-6)

    def test_ik_align_to_pole_turns_the_handle_about_y_so_z_faces_the_pole(self):
        """End (5,0,0), mid (1.8,2.4,0), poleOffset (0,2.6,3): the pole is (1.8,5,3), so the horizontal line from the end to it is
        (-3.2,0,3). +Z along it is a turn about Y of atan2(-3.2, 3) = -46.85 degrees, with nothing about X or Z."""
        self._build_basic(controlName="arm", poleOffset=[0, 2.6, 3], ikOrientToWorld=True, ikAlignToPole=True)
        self.assertWorldRotation(rig_name(HANDLE), [0, math.degrees(math.atan2(-3.2, 3)), 0], 1e-4)
        self.assertWorldTranslation(rig_name(HANDLE), [5, 0, 0])
        self._assert_bind_pose_kept()

    def test_ik_align_to_pole_on_the_other_side_of_a_mirrored_limb_is_the_reflection(self):
        """Left: end (6,0,0), pole (2.8+0.7, 2.4+2.6, 3) = (3.5,5,3), so the horizontal line is (-2.5,0,3). The right is the
        reflection with its pole X negated by the mirror, (2.5,0,3), so the two turns are equal and opposite."""
        data = self._template("twoboneik_mirror", controlName="arm", poleOffset=[0.7, 2.6, 3], ikOrientToWorld=True, ikAlignToPole=True)
        self.build_standard(data, "twoboneik_mirror")
        turn = math.degrees(math.atan2(-2.5, 3))
        self.assertWorldRotation(rig_name("armIK_00_L_CTRL"), [0, turn, 0], 1e-4)
        self.assertWorldRotation(rig_name("armIK_00_R_CTRL"), [0, -turn, 0], 1e-4)

    def test_ik_align_to_pole_without_ik_orient_to_world_is_an_error(self):
        message = self.assertMayaError(lambda: self._build_basic(controlName="arm", ikAlignToPole=True))
        self.assertIn("ikAlignToPole", message)
        self.assertIn("ikOrientToWorld", message)
        self.assertNothingBuilt()

    def test_ik_align_to_pole_with_the_pole_straight_above_the_end_is_an_error(self):
        """End (5,0,0), mid (1.8,2.4,0): poleOffset (3.2,2.6,0) puts the pole at (5,5,0), straight above the end."""
        message = self.assertMayaError(lambda: self._build_basic(
            controlName="arm", poleOffset=[3.2, 2.6, 0], ikOrientToWorld=True, ikAlignToPole=True))
        self.assertIn("ikAlignToPole", message)
        self.assertNothingBuilt()

    # ---- poleParentDefault (amended 2026-11-26) ---------------------------------------------

    def _move_handle_and_get_pole_motion(self):
        """Moves the handle by a pure translation and returns (the handle's world motion, the pole's). The handle takes the end
        joint's axes, so its translate is not in world axes: the test compares world motions, not channel values."""
        pole_rest, handle_rest = translation(POLE), translation(HANDLE)
        cmds.setAttr(rig_name(HANDLE + ".translate"), 1, 2, 0)
        handle_motion = [n - r for n, r in zip(translation(HANDLE), handle_rest)]
        pole_motion = [n - r for n, r in zip(translation(POLE), pole_rest)]
        self.assertGreater(sum(v * v for v in handle_motion), 1.0)
        return handle_motion, pole_motion

    def test_left_out_the_pole_is_built_in_parent_and_stays_when_the_handle_moves(self):
        self._build_basic(controlName="arm")
        self.assertEqual(attr(POLE, "space"), 0)
        _, pole_motion = self._move_handle_and_get_pole_motion()
        for value in pole_motion:
            self.assertAlmostEqual(value, 0.0, places=4)

    def test_parent_is_the_same_as_left_out(self):
        self._build_basic(controlName="arm", poleParentDefault="parent")
        self.assertEqual(attr(POLE, "space"), 0)
        self.assertEqual(rigmodules.query.find_control_space(rig_name(POLE)).current, 0)

    def test_ik_builds_the_pole_in_ik_as_its_rest_and_it_follows_the_handle(self):
        self._build_basic(controlName="arm", poleParentDefault="ik")
        self.assertEqual(attr(POLE, "space"), 1)
        space = {c.name: c for c in rigmodules.query.get_control_channels(rig_name(POLE))}["space"]
        self.assertEqual(space.rest, 1)   # zeroRigControls and a pose without space give Ik
        handle_motion, pole_motion = self._move_handle_and_get_pole_motion()
        for handle_value, pole_value in zip(handle_motion, pole_motion):
            self.assertAlmostEqual(pole_value, handle_value, places=4)

    def test_ik_at_rest_puts_the_pole_where_parent_does(self):
        self._build_basic(controlName="arm", poleOffset=[0, 2.6, 0], poleParentDefault="ik")
        self.assertWorldTranslation(rig_name(POLE), [1.8, 5, 0])
        self._assert_bind_pose_kept()

    def test_both_sides_of_a_mirrored_limb_take_it(self):
        data = self._template("twoboneik_mirror", controlName="arm", poleParentDefault="ik")
        self.build_standard(data, "twoboneik_mirror")
        self.assertEqual(cmds.getAttr(rig_name("armPole_00_L_CTRL.space")), 1)
        self.assertEqual(cmds.getAttr(rig_name("armPole_00_R_CTRL.space")), 1)

    def test_any_other_value_is_an_error(self):
        message = self.assertMayaError(lambda: self._build_basic(controlName="arm", poleParentDefault="Ik"))
        self.assertIn("poleParentDefault", message)
        self.assertIn('"parent" or "ik"', message)
        self.assertNothingBuilt()

    # ---- a straight limb -----------------------------------------------------------------------

    def test_a_straight_limb_builds_and_its_pole_goes_out_along_the_root_joints_negative_y(self):
        """Mocap skeletons have straight limbs, so it is not an error. Left: root (1,0,0), mid (4,0,0), end (8,0,0), bones 3
        and 4, so the pole is mid + 7 * (0,-1,0) = (4,-7,0). The Right root is turned 180 degrees about X, so its -Y is +Y and
        the pole is (-4,7,0): the direction is the root's, not the world's. The settings default is a quarter of the 7 length
        along +X from the end (-X on the mirrored side): (9.75,0,0) and (-9.75,0,0)."""
        self.build_standard("twoboneik_straight", "twoboneik_straight")
        self.assertWorldTranslation(rig_name("armPole_00_L_CTRL"), [4, -7, 0])
        self.assertWorldTranslation(rig_name("armPole_00_R_CTRL"), [-4, 7, 0])
        self.assertWorldTranslation(rig_name("armSettings_00_L_CTRL"), [9.75, 0, 0])
        self.assertWorldTranslation(rig_name("armSettings_00_R_CTRL"), [-9.75, 0, 0])

    def test_a_straight_limb_bends_toward_its_pole_when_the_handle_comes_in(self):
        self.build_standard("twoboneik_straight", "twoboneik_straight")
        set_attr("armIK_00_L_CTRL", "translateX", -4)
        for joint in ("upperArm_00_L_JNT", "lowerArm_00_L_JNT", "hand_00_L_JNT"):
            self.assertTrue(all(math.isfinite(v) for v in translation(joint)), joint)
        # The pole is below the limb, so the elbow bends down.
        self.assertLess(translation("lowerArm_00_L_JNT")[1], -0.1)

    # ---- names ---------------------------------------------------------------------------------

    def test_control_name_sets_the_base_of_the_usages(self):
        self._build_basic(controlName="wing")
        self.assertEqual(self._usages(), ["wingIK", "wingPole", "wingSettings"])
        for node in ("wingIK_00_L_CTRL", "wingPole_00_L_CTRL", "wingSettings_00_L_CTRL"):
            self.assertExists(rig_name(node))

    def test_the_default_base_is_the_first_joints_usage(self):
        self._build_basic()
        self.assertEqual(self._usages(), ["upperArmIK", "upperArmPole", "upperArmSettings"])

    # ---- no joints on the derived controls -----------------------------------------------------

    def test_there_are_no_joints_on_the_derived_controls(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        indices = cmds.getAttr(rig_name("arm_01_L_MOD.jointOPM"), multiIndices=True)
        self.assertEqual(list(indices), [0, 1, 2], "only the three limb joints are driven")

    # ---- an old template -----------------------------------------------------------------------

    def test_a_version_1_template_reads_and_builds_with_the_derived_places(self):
        """Version 1 named three marker joints (ik, pole, settings). The update step drops them and has no scene to turn their
        positions into offsets, so the controls are where a version 2 template with no offsets puts them, which is not where
        the old pole (2.5,5,0) and settings joint (0,8,0) were. The marker joints stay in the skeleton, unclaimed."""
        self.build_standard("twoboneik_v1", "twoboneik_v1")
        self.assertWorldTranslation(rig_name("upperArmIK_00_L_CTRL"), [5, 0, 0])
        self.assertWorldTranslation(rig_name("upperArmPole_00_L_CTRL"), [1.8, 9.4, 0])
        self.assertWorldTranslation(rig_name("upperArmSettings_00_L_CTRL"), [6.75, 0, 0])
        for joint in LIMB_JOINTS:
            self.assertTrue(all(math.isfinite(v) for v in translation(joint)), joint)
