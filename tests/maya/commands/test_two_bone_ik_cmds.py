"""setRigIkFkBlend, the matched IK/FK switch (D52 section 7.2, phase 6).

Two kinds of check. The hand-worked ones give the control values the switch must write, worked from the limb in
design/two-bone-ik.md section 4.2 (twoboneik_basic: root (0,0,0), mid (1.8,2.4,0), end (5,0,0), pole (2.5,5,0), bones 3
and 4, the pole at height 5 on the elbow's side) and never read off the rig. The round trips check the property the
command exists for: the limb's joints are where they were after a switch, in either direction.
"""
import copy

from maya import cmds

from commands.test_inverse_solve import SolveTestCase
from commands.test_two_bone_ik_inverse import CapturedWarnings
from rigtest import fixtures
from rigtest.case import rig_name
from rigtest.jointclips import rig_root, values_of

UPPER = "upperArm_ctrl_l"
LOWER = "lowerArm_ctrl_l"
END = "hand_ctrl_l"
HANDLE = "handIk_ctrl_l"
POLE = "armPole_ctrl_l"
SETTINGS = "armSettings_ctrl_l"
MODULE = "arm_1"
LAYOUT_CONTROL = "layout_2_ctrl_c"
MATCHED = (UPPER, LOWER, END, HANDLE, POLE)
LIMB_JOINTS = ("upperArm_jnt", "lowerArm_jnt", "hand_jnt")


class SwitchTestCase(SolveTestCase):
    """The helpers every switch test shares. A subclass builds the rig."""

    LIMB = LIMB_JOINTS

    def limb(self):
        """The limb's joints: the marker joints follow controls the switch moves."""
        return {j: m for j, m in self.snapshot().items() if j.split(":")[-1] in self.LIMB}

    def set_plugs(self, plugs):
        for plug, value in plugs.items():
            cmds.setAttr(rig_name(plug), value)

    def switch(self, blend, node=SETTINGS):
        return cmds.setRigIkFkBlend(rig_name(node), blend=blend)

    def control_values(self, controls=MATCHED):
        return {control: values_of(rig_name(control)) for control in controls}

    def assertSwitchKeepsJoints(self, start_blend, plugs):
        """Poses the rig at start_blend, switches to the other side and back. The joints never move."""
        self.set_plugs(plugs)
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), start_blend)
        before = self.limb()
        other = 1.0 - start_blend
        self.switch(other)
        self.assertEqual(cmds.getAttr(rig_name(SETTINGS + ".ikFkBlend")), other)
        self.assertJointsAt(before)
        self.switch(start_blend)
        self.assertEqual(cmds.getAttr(rig_name(SETTINGS + ".ikFkBlend")), start_blend)
        self.assertJointsAt(before)


class TwoBoneIkSwitch(SwitchTestCase):

    def setUp(self):
        super().setUp()
        self.build_standard("twoboneik_basic", "twoboneik_basic")

    # ---- hand-worked values ---------------------------------------------------------------------

    def test_fk_to_ik_writes_the_handle_and_pole_that_hold_a_rotated_limb(self):
        # Upper arm turned -90 about Z: (x,y) -> (y,-x). Mid (1.8,2.4) goes to (2.4,-1.8) and the hand to (0,-5,0).
        # Handle: world (0,-5,0) against its rest (5,0,0) is translate (-5,-5,0), turned -90 with the limb.
        # Pole: the limb's axis is -Y, the elbow bends toward +X, and the mid joint is 1.8 along the axis. The pole sits at
        # its rest height 5 on that side: world (5,-1.8,0), local (2.5,-6.8,0) against its rest (2.5,5,0).
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        cmds.setAttr(rig_name(UPPER + ".rotateZ"), -90)
        self.assertWorldTranslation(rig_name("lowerArm_jnt"), [2.4, -1.8, 0])
        self.assertWorldTranslation(rig_name("hand_jnt"), [0, -5, 0])
        before = self.limb()
        changed = self.switch(1)
        self.assertValues(rig_name(HANDLE), {"translateX": -5, "translateY": -5, "translateZ": 0, "rotateZ": -90}, places=3)
        self.assertValues(rig_name(POLE), {"translateX": 2.5, "translateY": -6.8, "translateZ": 0}, places=3)
        self.assertEqual(cmds.getAttr(rig_name(SETTINGS + ".ikFkBlend")), 1)
        self.assertJointsAt(before)
        self.assertTrue(any(name.endswith(HANDLE) for name in changed), changed)
        self.assertTrue(any(name.endswith(SETTINGS) for name in changed), changed)

    def test_ik_to_fk_writes_the_fk_rotations_that_hold_a_moved_handle(self):
        # Handle to world (4,-3,0): distance 5 along -36.8699 degrees, translate (-1,-3,0) against its rest (5,0,0). The
        # pole's side (63 degrees, left of that axis) is the rest side, so the limb bends the way it did: law of cosines
        # x = 1.8, h = 2.4 gives the mid joint (2.88,0.84,0), and the 3-4-5 limb is the rest limb turned rigidly by
        # -36.8699. So the upper control turns -36.8699, the lower stays at 0, and the hand keeps its world orientation,
        # so the end control undoes the turn: +36.8699.
        cmds.setAttr(rig_name(HANDLE + ".translateX"), -1)
        cmds.setAttr(rig_name(HANDLE + ".translateY"), -3)
        self.assertWorldTranslation(rig_name("lowerArm_jnt"), [2.88, 0.84, 0])
        self.assertWorldTranslation(rig_name("hand_jnt"), [4, -3, 0])
        before = self.limb()
        self.switch(0)
        self.assertValues(rig_name(UPPER), {"rotateZ": -36.8699, "rotateX": 0, "rotateY": 0, "translateX": 0, "translateY": 0}, places=3)
        self.assertValues(rig_name(LOWER), {"rotateZ": 0, "rotateX": 0, "rotateY": 0, "translateX": 0, "translateY": 0}, places=3)
        self.assertValues(rig_name(END), {"rotateZ": 36.8699, "rotateX": 0, "rotateY": 0}, places=3)
        self.assertEqual(cmds.getAttr(rig_name(SETTINGS + ".ikFkBlend")), 0)
        self.assertJointsAt(before)

    def test_ik_to_fk_at_rest_zeroes_fk_and_lines_the_pole_up_with_the_elbow(self):
        # FK matches the rest joints, so its controls are at rest. The IK side keeps its handle, and the pole goes to the
        # elbow's x: world (1.8,5,0), local (-0.7,0,0) (the same pole the restore places, test_two_bone_ik_inverse).
        self.switch(0)
        for control in (UPPER, LOWER, END):
            self.assertValues(rig_name(control), {"translateX": 0, "translateY": 0, "translateZ": 0,
                                                  "rotateX": 0, "rotateY": 0, "rotateZ": 0,
                                                  "scaleX": 1, "scaleY": 1, "scaleZ": 1})
        self.assertValues(rig_name(HANDLE), {"translateX": 0, "translateY": 0, "translateZ": 0})
        self.assertValues(rig_name(POLE), {"translateX": -0.7, "translateY": 0, "translateZ": 0})
        self.assertEqual(cmds.getAttr(rig_name(SETTINGS + ".ikFkBlend")), 0)

    def test_a_raw_blend_change_still_pops(self):
        # The limb is posed in FK with the hand at (0,-5,0). A setAttr to IK does no match: the handle is at rest, so the
        # hand jumps to (5,0,0). The command is what carries the pose over.
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        cmds.setAttr(rig_name(UPPER + ".rotateZ"), -90)
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 1)
        self.assertWorldTranslation(rig_name("hand_jnt"), [5, 0, 0])

    # ---- no pop, either way ---------------------------------------------------------------------

    def test_at_rest_in_both_modes(self):
        for start in (1.0, 0.0):
            with self.subTest(start=start):
                self.assertSwitchKeepsJoints(start, {})

    def test_a_posed_ik_limb(self):
        self.assertSwitchKeepsJoints(1.0, {HANDLE + ".translateX": -1, HANDLE + ".translateY": 2, HANDLE + ".rotateZ": 25,
                                           POLE + ".translateX": 1, POLE + ".translateY": -3})

    def test_a_posed_fk_limb(self):
        self.assertSwitchKeepsJoints(0.0, {UPPER + ".rotateZ": 20, UPPER + ".rotateX": 10, LOWER + ".rotateZ": 40, END + ".rotateY": 15})

    def test_a_stretched_ik_limb(self):
        # Handle at 8 with stretch 1: bones 3*8/7 and 4*8/7. FK takes the stretch as translate and scale.
        self.assertSwitchKeepsJoints(1.0, {SETTINGS + ".stretch": 1, HANDLE + ".translateX": 3})

    def test_a_pinned_ik_limb(self):
        self.assertSwitchKeepsJoints(1.0, {POLE + ".translateX": -0.5, POLE + ".translateY": -2, POLE + ".pin": 0.5})

    def test_a_soft_ik_limb(self):
        self.assertSwitchKeepsJoints(1.0, {SETTINGS + ".soft": 1.0 / 7.0, HANDLE + ".translateX": 2})

    def test_a_soft_and_stretched_ik_limb(self):
        self.assertSwitchKeepsJoints(1.0, {SETTINGS + ".soft": 1.0 / 7.0, SETTINGS + ".stretch": 1, HANDLE + ".translateX": 2})

    def test_a_rolled_ik_limb(self):
        self.assertSwitchKeepsJoints(1.0, {HANDLE + ".roll": 90, HANDLE + ".translateX": -1})

    def test_the_settings_carry_into_fk_for_the_ik_that_follows(self):
        # An FK pose switched to IK with soft and roll set: the match uses those settings, not rest ones. The hand is at
        # distance 6.355 from the root, past where a soft of 0.1 (da = 6.3) starts to ease, so a soft of 0 would put the
        # handle somewhere else.
        self.assertSwitchKeepsJoints(0.0, {SETTINGS + ".soft": 0.1, HANDLE + ".roll": 30,
                                           UPPER + ".rotateZ": 30, LOWER + ".rotateZ": 40})

    def test_a_pole_in_ik_space(self):
        self.assertSwitchKeepsJoints(1.0, {POLE + ".space": 1, HANDLE + ".translateX": -1, HANDLE + ".translateY": 1})

    def test_a_pole_in_ik_space_from_fk(self):
        # The pole is written with the handle, so its Ik-space local is worked from the matched handle.
        self.assertSwitchKeepsJoints(0.0, {POLE + ".space": 1, UPPER + ".rotateZ": -30, LOWER + ".rotateZ": 50})

    def test_the_start_controls_translation_moves_both_sides(self):
        self.assertSwitchKeepsJoints(1.0, {UPPER + ".translateY": 2, HANDLE + ".translateX": -1})
        self.assertSwitchKeepsJoints(0.0, {UPPER + ".translateZ": 1})

    # ---- the controls written from the match leave the joints still at every blend ------------------

    def test_the_matched_controls_hold_the_pose_at_blends_0_half_and_1(self):
        for start in (0.0, 1.0):
            with self.subTest(start=start):
                self.set_plugs({UPPER + ".rotateZ": 15, LOWER + ".rotateZ": 30, HANDLE + ".translateX": -1, HANDLE + ".translateY": 1})
                cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), start)
                before = self.limb()
                self.switch(start)   # a match that keeps the blend
                for blend in (0.0, 0.5, 1.0):
                    cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), blend)
                    self.assertJointsAt(before)

    # ---- equal to the restore ---------------------------------------------------------------------

    def test_the_match_is_what_the_restore_writes(self):
        # Same pose, two ways to the control values: restore it from the joints onto zeroed controls, or pose the controls
        # again and run the switch with the blend it already has (a match that changes nothing else).
        pose = {UPPER + ".rotateZ": 15, LOWER + ".rotateZ": 30, HANDLE + ".translateX": -1, HANDLE + ".translateY": 1,
                POLE + ".translateY": 2}
        for blend in (1.0, 0.0):
            with self.subTest(blend=blend):
                cmds.zeroRigControls(rig_root())
                rest = self.snapshot()
                self.set_plugs(pose)
                cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), blend)
                clip = self.write_clip(rest, [self.snapshot()])

                cmds.zeroRigControls(rig_root())
                sidecar = self.write_sidecar({"Left": {"armSettings": {"ikFkBlend": blend}}})
                cmds.poseRigFromJointPose(rig_root(), jointPose=clip, sidecar=sidecar)
                restored = self.control_values()

                cmds.zeroRigControls(rig_root())
                self.set_plugs(pose)
                cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), blend)
                self.switch(blend)
                matched = self.control_values()
                for control in MATCHED:
                    for channel, value in matched[control].items():
                        self.assertAlmostEqual(value, restored[control][channel], places=4,
                                               msg="%s.%s: switch %s, restore %s" % (control, channel, value, restored[control][channel]))

    # ---- warnings -----------------------------------------------------------------------------------

    def test_a_pose_ik_can_make_warns_about_nothing(self):
        # Including the pole's locked rotation and scale, which the match leaves at the values they hold.
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        cmds.setAttr(rig_name(UPPER + ".rotateZ"), -40)
        with CapturedWarnings() as warnings:
            self.switch(1)
        self.assertEqual(warnings.messages, [])

    def test_ik_cant_make_a_stretched_fk_pose(self):
        # The lower bone is scaled 1.5 along its length, so the hand is further out than the rig's bones reach with stretch
        # and pin off.
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        cmds.setAttr(rig_name(LOWER + ".scaleX"), 1.5)
        with CapturedWarnings() as warnings:
            self.switch(1)
        self.assertTrue(warnings.contain("IK can't make"), warnings.messages)
        self.assertEqual(cmds.getAttr(rig_name(SETTINGS + ".ikFkBlend")), 1)

    def test_stretch_lets_ik_make_a_stretched_straight_limb(self):
        # Straight along +X (upper -53.1301, lower +90 undo the rest bend) with both bones 1.2 times longer: 3.6 and 4.8,
        # the hand at (8.4,0,0) and the mid at (3.6,0,0). Past the reach of 7, stretch 1 scales both bones by 8.4/7.
        # The fixture's joints have no orientation, so a control's translate is in world axes, carried by its parent's
        # turn. The lower control's parent turned -53.1301, so +0.6 along the world +X is 0.6 * (cos 53.1301, sin 53.1301) =
        # (0.36,0.48). The end control's parent has turned -53.1301 + 90 = 36.8699, so +0.8 along +X is
        # 0.8 * (cos -36.8699, sin -36.8699) = (0.64,-0.48).
        pose = {SETTINGS + ".ikFkBlend": 0, UPPER + ".rotateZ": -53.1301, LOWER + ".rotateZ": 90,
                LOWER + ".translateX": 0.36, LOWER + ".translateY": 0.48, END + ".translateX": 0.64, END + ".translateY": -0.48}
        self.set_plugs(pose)
        self.assertWorldTranslation(rig_name("lowerArm_jnt"), [3.6, 0, 0], 1e-3)
        self.assertWorldTranslation(rig_name("hand_jnt"), [8.4, 0, 0], 1e-3)
        before = self.limb()
        with CapturedWarnings() as warnings:
            self.switch(1)
        self.assertTrue(warnings.contain("IK can't make"), warnings.messages)   # stretch is off
        self.set_plugs(pose)
        cmds.setAttr(rig_name(SETTINGS + ".stretch"), 1)
        with CapturedWarnings() as warnings:
            self.switch(1)
        self.assertFalse(warnings.contain("IK can't make"), warnings.messages)
        self.assertJointsAt(before, 1e-3)

    def test_a_blend_between_warns_that_ik_is_approximate(self):
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0.5)
        cmds.setAttr(rig_name(UPPER + ".rotateZ"), 20)
        with CapturedWarnings() as warnings:
            self.switch(0)
        self.assertTrue(warnings.contain("between 0 and 1"), warnings.messages)
        self.assertEqual(cmds.getAttr(rig_name(SETTINGS + ".ikFkBlend")), 0)

    def test_a_locked_channel_keeps_its_value_and_warns(self):
        # The hand goes to (0,-5,0), which needs handle translateX -5. The channel is locked at 0 and stays there.
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        cmds.setAttr(rig_name(UPPER + ".rotateZ"), -90)
        cmds.setAttr(rig_name(HANDLE + ".translateX"), lock=True)
        with CapturedWarnings() as warnings:
            self.switch(1)
        self.assertTrue(warnings.contain("locked or driven"), warnings.messages)
        self.assertValues(rig_name(HANDLE), {"translateX": 0, "translateY": -5})

    def test_a_locked_channel_that_already_holds_the_value_is_not_a_warning(self):
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        cmds.setAttr(rig_name(UPPER + ".rotateZ"), -90)
        cmds.setAttr(rig_name(HANDLE + ".translateX"), -5)
        cmds.setAttr(rig_name(HANDLE + ".translateX"), lock=True)
        with CapturedWarnings() as warnings:
            self.switch(1)
        self.assertEqual(warnings.messages, [])
        self.assertValues(rig_name(HANDLE), {"translateX": -5})

    def test_a_locked_blend_is_left_alone_and_warns(self):
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), lock=True)
        with CapturedWarnings() as warnings:
            self.switch(0)
        self.assertTrue(warnings.contain("ikFkBlend is locked or driven"), warnings.messages)
        self.assertEqual(cmds.getAttr(rig_name(SETTINGS + ".ikFkBlend")), 1)

    # ---- the command's form ---------------------------------------------------------------------------

    def test_the_default_blend_is_ik(self):
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        cmds.setRigIkFkBlend(rig_name(SETTINGS))
        self.assertEqual(cmds.getAttr(rig_name(SETTINGS + ".ikFkBlend")), 1)

    def test_any_control_or_the_module_names_the_limb(self):
        for node in (UPPER, LOWER, END, HANDLE, POLE, SETTINGS, MODULE):
            with self.subTest(node=node):
                cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 1)
                cmds.setRigIkFkBlend(rig_name(node), blend=0)
                self.assertEqual(cmds.getAttr(rig_name(SETTINGS + ".ikFkBlend")), 0)

    def test_it_sets_no_keys(self):
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        cmds.setAttr(rig_name(UPPER + ".rotateZ"), -40)
        self.switch(1)
        for control in MATCHED + (SETTINGS,):
            self.assertFalse(cmds.listConnections(rig_name(control), type="animCurve"), control)

    def test_undo_and_redo_are_one_step(self):
        cmds.undoInfo(state=True, infinity=True)
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        cmds.setAttr(rig_name(UPPER + ".rotateZ"), -90)
        blend = rig_name(SETTINGS + ".ikFkBlend")
        before = self.control_values()
        self.switch(1)
        after = self.control_values()
        self.assertNotEqual(before, after)
        self.assertEqual(cmds.getAttr(blend), 1)
        cmds.undo()
        self.assertEqual(self.control_values(), before)
        self.assertEqual(cmds.getAttr(blend), 0)
        cmds.redo()
        self.assertEqual(self.control_values(), after)
        self.assertEqual(cmds.getAttr(blend), 1)

    # ---- errors ---------------------------------------------------------------------------------------

    def test_a_blend_outside_0_to_1_is_an_error(self):
        for blend in (-0.1, 1.5):
            with self.subTest(blend=blend):
                self.assertMayaError(lambda: cmds.setRigIkFkBlend(rig_name(SETTINGS), blend=blend), "blend", "0 (FK) to 1 (IK)")

    def test_no_object_is_an_error(self):
        with self.assertRaises((RuntimeError, TypeError)):
            cmds.setRigIkFkBlend(blend=0)

    def test_it_never_reads_the_selection(self):
        cmds.select(rig_name(SETTINGS))
        with self.assertRaises((RuntimeError, TypeError)):
            cmds.setRigIkFkBlend(blend=0)

    def test_a_node_that_is_not_a_limb_is_an_error(self):
        self.assertMayaError(lambda: cmds.setRigIkFkBlend(rig_name(LAYOUT_CONTROL), blend=0), "not a TwoBoneIk")
        self.assertMayaError(lambda: cmds.setRigIkFkBlend(rig_root(), blend=0), "not a TwoBoneIk")
        self.assertMayaError(lambda: cmds.setRigIkFkBlend(rig_name("upperArm_jnt"), blend=1), "not a TwoBoneIk")

    def test_a_missing_node_is_an_error(self):
        self.assertMayaError(lambda: cmds.setRigIkFkBlend("noSuchNode", blend=0), "not found")

    def test_two_objects_are_an_error(self):
        with self.assertRaises((RuntimeError, TypeError)):
            cmds.setRigIkFkBlend(rig_name(SETTINGS), rig_name(HANDLE), blend=0)

    def test_a_failed_call_changes_nothing(self):
        before = self.control_values(MATCHED + (SETTINGS,))
        with self.assertRaises(RuntimeError):
            cmds.setRigIkFkBlend(rig_name(SETTINGS), blend=2)
        self.assertEqual(self.control_values(MATCHED + (SETTINGS,)), before)


class TwoBoneIkSwitchSpaces(SwitchTestCase):
    """The space switches on the IK handle (a rigConstraint) and the pole (the module's own)."""

    def setUp(self):
        super().setUp()
        joints = self.make_skeleton("twoboneik_basic")
        data = copy.deepcopy(fixtures.load_template("twoboneik_basic"))
        data["constraints"] = [{"name": "arm_ik_space", "type": "Parent", "target": "arm_1.ik", "drivers": [
            {"socket": "layout.end", "label": "Chest"}, {"socket": "layout.index_0", "label": "Hips"}]}]
        self.build(data, joints, label="twoboneik_switch_spaces")

    def test_the_handle_in_a_space_that_moved(self):
        # The chest moves and the handle's space is Hips, which stays: the handle's local carries the difference.
        self.set_plugs({LAYOUT_CONTROL + ".translateZ": 3, HANDLE + ".space": 1})
        self.assertSwitchKeepsJoints(0.0, {UPPER + ".rotateZ": 20, LOWER + ".rotateZ": 35})
        self.assertSwitchKeepsJoints(1.0, {HANDLE + ".translateY": 1})

    def test_the_handle_in_chest_space(self):
        self.set_plugs({LAYOUT_CONTROL + ".translateY": 2, LAYOUT_CONTROL + ".translateZ": 1})
        self.assertSwitchKeepsJoints(0.0, {UPPER + ".rotateZ": -25})
        self.assertSwitchKeepsJoints(1.0, {HANDLE + ".translateX": -1})

    def test_the_handle_in_a_space_with_the_pole_in_ik_space(self):
        self.set_plugs({LAYOUT_CONTROL + ".translateX": 1.5, HANDLE + ".space": 1, POLE + ".space": 1})
        self.assertSwitchKeepsJoints(0.0, {UPPER + ".rotateZ": 20, LOWER + ".rotateZ": 35})
        self.assertSwitchKeepsJoints(1.0, {HANDLE + ".translateY": 1, HANDLE + ".translateX": -1})


class TwoBoneIkSwitchMirror(SwitchTestCase):
    """The right limb is the left one reflected: its controls and joints are mirrored, and the switch holds there too."""

    LIMB = ("upperArm_l_jnt", "lowerArm_l_jnt", "hand_l_jnt", "upperArm_r_jnt", "lowerArm_r_jnt", "hand_r_jnt")

    def setUp(self):
        super().setUp()
        self.build_standard("twoboneik_mirror", "twoboneik_mirror")

    def test_both_limbs_keep_their_joints(self):
        sides = {"l": ("upperArm_l_ctrl_l", "lowerArm_l_ctrl_l", "armSettings_l_ctrl_l", "handIk_l_ctrl_l", "armPole_l_ctrl_l"),
                 "r": ("upperArm_r_ctrl_r", "lowerArm_r_ctrl_r", "armSettings_r_ctrl_r", "handIk_r_ctrl_r", "armPole_r_ctrl_r")}
        for side, (upper, lower, settings, handle, pole) in sides.items():
            with self.subTest(side=side):
                cmds.setAttr(rig_name(settings + ".ikFkBlend"), 0)
                self.set_plugs({upper + ".rotateZ": 25, lower + ".rotateZ": 35})
                before = self.limb()
                self.switch(1, settings)
                self.assertJointsAt(before)
                self.set_plugs({handle + ".translateY": 1, pole + ".translateZ": 1})
                before = self.limb()
                self.switch(0, settings)
                self.assertJointsAt(before)
