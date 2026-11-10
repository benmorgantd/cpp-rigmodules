"""The animation interface (D34): getRigControlChannels, zeroRigControls, mirrorRigPose and flipRigPose, each on a
whole rig or on a set of controls.

Every test builds mirror_arms: spine (Center chest), clavicle_l/r, arm_l/r (upperArm, lowerArm) and the layout.
Every joint is world aligned, so a Center control's mirror axis is its local X. The upper arm controls carry the
`space` enum of arm_space_l/r.
"""
import json
import math

from maya import cmds

import rigmodules.query
from rigtest.case import RigTestCase, rig_name

ARM_L = rig_name("upperArm_l_ctrl_l")
ARM_R = rig_name("upperArm_r_ctrl_r")
FOREARM_L = rig_name("lowerArm_l_ctrl_l")
FOREARM_R = rig_name("lowerArm_r_ctrl_r")
CHEST = rig_name("chest_ctrl_c")

TRANSFORMS = ["translateX", "translateY", "translateZ", "rotateX", "rotateY", "rotateZ", "scaleX", "scaleY", "scaleZ"]


def rig_root():
    return cmds.ls(rig_name("*"), type="rigRootModule")[0]


def channels(control):
    """Channel name -> its entry, for one control."""
    data = json.loads(cmds.getRigControlChannels(control))
    return {entry["name"]: entry for entry in data[control.lstrip(":")]}


def values(control, names):
    return [round(cmds.getAttr("%s.%s" % (control, name)), 4) for name in names]


def set_values(control, **attrs):
    for name, value in attrs.items():
        cmds.setAttr("%s.%s" % (control, name), value)


def add_channel(control, name, category, **kwargs):
    """A custom channel as a module would add it. category is the mirror mode, or None to leave it out."""
    if category:
        kwargs["category"] = category
    cmds.addAttr(control, longName=name, keyable=True, **kwargs)


class PoseTestCase(RigTestCase):

    def setUp(self):
        super().setUp()
        self.build_standard("mirror_arms", "mirror_arms")


class GetRigControlChannels(PoseTestCase):

    def test_a_plain_control_has_visibility_and_nine_transforms(self):
        self.assertEqual(sorted(channels(CHEST)), sorted(TRANSFORMS + ["visibility"]))

    def test_transform_channels_report_type_rest_and_copy(self):
        found = channels(CHEST)
        self.assertEqual(found["translateX"],
                         {"name": "translateX", "type": "doubleLinear", "rest": 0.0, "min": None, "max": None, "mirror": "copy"})
        self.assertEqual(found["rotateZ"],
                         {"name": "rotateZ", "type": "doubleAngle", "rest": 0.0, "min": None, "max": None, "mirror": "copy"})
        self.assertEqual(found["scaleY"],
                         {"name": "scaleY", "type": "double", "rest": 1.0, "min": None, "max": None, "mirror": "copy"})
        self.assertEqual(found["visibility"]["type"], "bool")
        self.assertEqual(found["visibility"]["rest"], 1.0)

    def test_transforms_come_in_channel_box_order(self):
        names = [entry["name"] for entry in json.loads(cmds.getRigControlChannels(CHEST))[CHEST.lstrip(":")]]
        self.assertEqual([n for n in names if n != "visibility"], TRANSFORMS)

    def test_space_enum_is_a_channel_that_copies(self):
        self.assertEqual(channels(ARM_L)["space"],
                         {"name": "space", "type": "enum", "rest": 0.0, "min": None, "max": None, "mirror": "copy"})
        self.assertNotIn("space", channels(FOREARM_L))

    def test_custom_channel_reports_default_range_and_mirror_mode(self):
        add_channel(CHEST, "lean", "mirrorNegate", attributeType="double", defaultValue=0.5, minValue=-1, maxValue=2)
        add_channel(CHEST, "curl", "mirrorCopy", attributeType="long", defaultValue=3, minValue=0)
        found = channels(CHEST)
        self.assertEqual(found["lean"], {"name": "lean", "type": "double", "rest": 0.5, "min": -1.0, "max": 2.0, "mirror": "negate"})
        self.assertEqual(found["curl"], {"name": "curl", "type": "long", "rest": 3.0, "min": 0.0, "max": None, "mirror": "copy"})

    def test_angle_channel_is_in_ui_units(self):
        # addAttr takes an angle's default and range in radians, whatever the UI unit.
        add_channel(CHEST, "twist", "mirrorNegate", attributeType="doubleAngle",
                    defaultValue=math.radians(90), maxValue=math.radians(180))
        self.assertAlmostEqual(cmds.getAttr(CHEST + ".twist"), 90.0, places=6)
        twist = channels(CHEST)["twist"]
        self.assertEqual(twist["type"], "doubleAngle")
        self.assertAlmostEqual(twist["rest"], 90.0, places=6)
        self.assertAlmostEqual(twist["max"], 180.0, places=6)
        self.assertIsNone(twist["min"])

    def test_custom_channel_without_a_mirror_mode_is_an_error(self):
        add_channel(CHEST, "lean", None, attributeType="double")
        self.assertMayaError(lambda: cmds.getRigControlChannels(CHEST), "chest_ctrl_c.lean", "mirrorCopy", "mirrorNegate")

    def test_enum_or_bool_that_negates_is_an_error(self):
        add_channel(CHEST, "mode", "mirrorNegate", attributeType="enum", enumName="a:b")
        self.assertMayaError(lambda: cmds.getRigControlChannels(CHEST), "chest_ctrl_c.mode", "can't be negated")

    def test_locked_and_non_keyable_channels_are_left_out(self):
        cmds.setAttr(CHEST + ".translateY", lock=True)
        cmds.setAttr(CHEST + ".scaleX", keyable=False)
        found = channels(CHEST)
        self.assertNotIn("translateY", found)
        self.assertNotIn("scaleX", found)
        self.assertIn("translateX", found)

    def test_keyed_channel_stays_and_driven_channel_is_left_out(self):
        cmds.setKeyframe(CHEST, attribute="translateX", time=1, value=0)
        cmds.connectAttr(CHEST + ".translateY", CHEST + ".translateZ")
        found = channels(CHEST)
        self.assertIn("translateX", found)
        self.assertNotIn("translateZ", found)

    def test_several_controls_are_keyed_by_name(self):
        data = json.loads(cmds.getRigControlChannels(ARM_L, CHEST))
        self.assertEqual(sorted(data), ["rig:chest_ctrl_c", "rig:upperArm_l_ctrl_l"])

    def test_object_must_be_a_rig_control(self):
        self.assertMayaError(lambda: cmds.getRigControlChannels(rig_name("upperArm_l_jnt")), "Not a rig control")
        self.assertMayaError(lambda: cmds.getRigControlChannels("missing"), "Not a rig control")

    def test_needs_a_control_and_ignores_the_selection(self):
        cmds.select(CHEST)
        self.assertMayaError(lambda: cmds.getRigControlChannels())

    def test_python_query_returns_channels(self):
        add_channel(CHEST, "lean", "mirrorNegate", attributeType="double", defaultValue=0.5, maxValue=2)
        found = {channel.name: channel for channel in rigmodules.query.get_control_channels(CHEST)}
        self.assertEqual(found["lean"],
                         rigmodules.query.ControlChannel(name="lean", type="double", rest=0.5, min=None, max=2.0, mirror="negate"))
        self.assertEqual(found["scaleX"].rest, 1.0)


class ZeroRigControls(PoseTestCase):

    def test_every_channel_goes_to_its_rest(self):
        set_values(ARM_L, rotateY=30, translateX=1.5)
        set_values(CHEST, scaleX=2, visibility=False)
        changed = cmds.zeroRigControls(rig_root())
        self.assertEqual(sorted(changed), ["rig:chest_ctrl_c", "rig:upperArm_l_ctrl_l"])
        self.assertEqual(values(ARM_L, TRANSFORMS), [0, 0, 0, 0, 0, 0, 1, 1, 1])
        self.assertEqual(values(CHEST, TRANSFORMS), [0, 0, 0, 0, 0, 0, 1, 1, 1])
        self.assertTrue(cmds.getAttr(CHEST + ".visibility"))

    def test_custom_channel_goes_to_its_default_and_space_to_its_first_driver(self):
        add_channel(CHEST, "lean", "mirrorNegate", attributeType="double", defaultValue=0.5)
        set_values(CHEST, lean=-3)
        set_values(ARM_L, space=1)
        cmds.zeroRigControls(rig_root())
        self.assertEqual(values(CHEST, ["lean"]), [0.5])
        self.assertEqual(cmds.getAttr(ARM_L + ".space"), 0)

    def test_a_rig_at_rest_changes_nothing(self):
        self.assertEqual(cmds.zeroRigControls(rig_root()) or [], [])

    def test_locked_channel_keeps_its_value(self):
        set_values(CHEST, translateX=2, translateY=3)
        cmds.setAttr(CHEST + ".translateY", lock=True)
        cmds.zeroRigControls(rig_root())
        self.assertEqual(values(CHEST, ["translateX", "translateY"]), [0, 3])

    def test_undo_and_redo(self):
        set_values(ARM_L, rotateY=30)
        cmds.zeroRigControls(rig_root())
        cmds.undo()
        self.assertEqual(values(ARM_L, ["rotateY"]), [30])
        cmds.redo()
        self.assertEqual(values(ARM_L, ["rotateY"]), [0])

    def test_object_must_be_a_rig_root_or_control(self):
        self.assertMayaError(lambda: cmds.zeroRigControls(rig_name("upperArm_l_jnt")), "Not a rig root or rig control")
        self.assertMayaError(lambda: cmds.zeroRigControls("missing"), "Not a rig root or rig control")

    def test_controls_reset_only_those_controls(self):
        set_values(ARM_L, rotateY=30)
        set_values(ARM_R, rotateY=40)
        set_values(CHEST, translateX=2)
        changed = cmds.zeroRigControls(ARM_L, CHEST)
        self.assertEqual(sorted(changed), ["rig:chest_ctrl_c", "rig:upperArm_l_ctrl_l"])
        self.assertEqual(values(ARM_L, ["rotateY"]), [0])
        self.assertEqual(values(CHEST, ["translateX"]), [0])
        self.assertEqual(values(ARM_R, ["rotateY"]), [40])

    def test_controls_undo_and_redo(self):
        set_values(ARM_L, rotateY=30)
        cmds.zeroRigControls(ARM_L)
        cmds.undo()
        self.assertEqual(values(ARM_L, ["rotateY"]), [30])
        cmds.redo()
        self.assertEqual(values(ARM_L, ["rotateY"]), [0])

    def test_a_rig_root_cannot_be_mixed_with_controls(self):
        self.assertMayaError(lambda: cmds.zeroRigControls(rig_root(), ARM_L), "can't be combined")

    def test_nothing_given_is_an_error_and_the_selection_is_ignored(self):
        cmds.select(ARM_L)
        self.assertMayaError(lambda: cmds.zeroRigControls())

    def test_a_control_given_twice_is_reset_once(self):
        set_values(ARM_L, rotateY=30)
        self.assertEqual(cmds.zeroRigControls(ARM_L, ARM_L), ["rig:upperArm_l_ctrl_l"])


class MirrorRigPose(PoseTestCase):

    def test_left_values_copy_onto_right(self):
        set_values(ARM_L, translateX=1, rotateX=10, rotateY=20, rotateZ=30, scaleY=2)
        set_values(FOREARM_R, rotateZ=45)
        changed = cmds.mirrorRigPose(rig_root())
        self.assertEqual(sorted(changed), ["rig:lowerArm_r_ctrl_r", "rig:upperArm_r_ctrl_r"])
        self.assertEqual(values(ARM_R, TRANSFORMS), [1, 0, 0, 10, 20, 30, 1, 2, 1])
        self.assertEqual(values(FOREARM_R, ["rotateZ"]), [0])

    def test_source_side_and_center_keep_their_values(self):
        set_values(ARM_L, rotateY=20)
        set_values(CHEST, translateX=2, rotateY=15)
        cmds.mirrorRigPose(rig_root())
        self.assertEqual(values(ARM_L, ["rotateY"]), [20])
        self.assertEqual(values(CHEST, ["translateX", "rotateY"]), [2, 15])

    def test_right_to_left(self):
        set_values(ARM_R, rotateZ=-40)
        set_values(ARM_L, rotateZ=5)
        changed = cmds.mirrorRigPose(rig_root(), leftToRight=False)
        self.assertEqual(changed, ["rig:upperArm_l_ctrl_l"])
        self.assertEqual(values(ARM_L, ["rotateZ"]), [-40])
        self.assertEqual(values(ARM_R, ["rotateZ"]), [-40])

    def test_custom_channels_copy_or_negate_as_declared(self):
        for control in (ARM_L, ARM_R):
            add_channel(control, "lean", "mirrorNegate", attributeType="double")
            add_channel(control, "curl", "mirrorCopy", attributeType="double")
        set_values(ARM_L, lean=0.4, curl=0.7)
        cmds.mirrorRigPose(rig_root())
        self.assertEqual(values(ARM_R, ["lean", "curl"]), [-0.4, 0.7])

    def test_space_copies(self):
        set_values(ARM_L, space=1)
        cmds.mirrorRigPose(rig_root())
        self.assertEqual(cmds.getAttr(ARM_R + ".space"), 1)

    def test_a_channel_the_mirror_cannot_take_is_skipped(self):
        set_values(ARM_L, translateX=1, translateY=2)
        cmds.setAttr(ARM_R + ".translateX", lock=True)
        cmds.mirrorRigPose(rig_root())
        self.assertEqual(values(ARM_R, ["translateX", "translateY"]), [0, 2])

    def test_undo_and_redo(self):
        set_values(ARM_L, rotateY=20)
        cmds.mirrorRigPose(rig_root())
        cmds.undo()
        self.assertEqual(values(ARM_R, ["rotateY"]), [0])
        cmds.redo()
        self.assertEqual(values(ARM_R, ["rotateY"]), [20])

    def test_object_must_be_a_rig_root_or_control(self):
        self.assertMayaError(lambda: cmds.mirrorRigPose(rig_name("upperArm_l_jnt")), "Not a rig root or rig control")

    def test_left_controls_mirror_from_their_side_and_leave_other_controls_alone(self):
        set_values(ARM_L, rotateY=20)
        set_values(FOREARM_L, rotateZ=45)
        set_values(ARM_R, rotateZ=9)
        changed = cmds.mirrorRigPose(ARM_L)
        self.assertEqual(changed, ["rig:upperArm_r_ctrl_r"])
        self.assertEqual(values(ARM_R, ["rotateY", "rotateZ"]), [20, 0])
        self.assertEqual(values(FOREARM_R, ["rotateZ"]), [0])

    def test_right_controls_mirror_from_their_side_by_default(self):
        set_values(ARM_R, rotateZ=-40)
        set_values(ARM_L, rotateZ=5)
        self.assertEqual(cmds.mirrorRigPose(ARM_R), ["rig:upperArm_l_ctrl_l"])
        self.assertEqual(values(ARM_L, ["rotateZ"]), [-40])

    def test_controls_from_both_sides_mirror_left_to_right(self):
        set_values(ARM_L, rotateY=20)
        set_values(FOREARM_R, rotateZ=45)
        cmds.mirrorRigPose(ARM_L, FOREARM_R)
        self.assertEqual(values(ARM_R, ["rotateY"]), [20])
        self.assertEqual(values(FOREARM_L, ["rotateZ"]), [0])
        self.assertEqual(values(FOREARM_R, ["rotateZ"]), [45])

    def test_a_pair_given_from_both_ends_mirrors_left_onto_right(self):
        set_values(ARM_L, rotateY=20)
        set_values(ARM_R, rotateY=-7)
        cmds.mirrorRigPose(ARM_L, ARM_R)
        self.assertEqual(values(ARM_L, ["rotateY"]), [20])
        self.assertEqual(values(ARM_R, ["rotateY"]), [20])

    def test_left_to_right_flag_wins_over_the_controls_side(self):
        set_values(ARM_R, rotateY=-7)
        set_values(ARM_L, rotateY=20)
        cmds.mirrorRigPose(ARM_R, ARM_L, leftToRight=False)
        self.assertEqual(values(ARM_L, ["rotateY"]), [-7])
        cmds.undo()
        cmds.mirrorRigPose(ARM_R, leftToRight=True)   # no Left control given, so nothing is a source
        self.assertEqual(values(ARM_L, ["rotateY"]), [20])

    def test_center_controls_alone_mirror_nothing(self):
        set_values(CHEST, translateX=2)
        self.assertEqual(cmds.mirrorRigPose(CHEST) or [], [])
        self.assertEqual(values(CHEST, ["translateX"]), [2])

    def test_controls_undo_and_redo(self):
        set_values(ARM_L, rotateY=20)
        cmds.mirrorRigPose(ARM_L)
        cmds.undo()
        self.assertEqual(values(ARM_R, ["rotateY"]), [0])
        cmds.redo()
        self.assertEqual(values(ARM_R, ["rotateY"]), [20])


class FlipRigPose(PoseTestCase):

    def test_left_and_right_swap(self):
        set_values(ARM_L, translateX=1, rotateX=10, rotateY=20, rotateZ=30)
        set_values(ARM_R, translateX=-2, rotateX=-1, rotateY=-2, rotateZ=-3)
        changed = cmds.flipRigPose(rig_root())
        self.assertEqual(sorted(changed), ["rig:upperArm_l_ctrl_l", "rig:upperArm_r_ctrl_r"])
        self.assertEqual(values(ARM_L, ["translateX", "rotateX", "rotateY", "rotateZ"]), [-2, -1, -2, -3])
        self.assertEqual(values(ARM_R, ["translateX", "rotateX", "rotateY", "rotateZ"]), [1, 10, 20, 30])

    def test_center_control_reflects_across_world_yz(self):
        # World-aligned rest: the mirror axis is local X. Its translate flips, and so do the other two rotates.
        set_values(CHEST, translateX=1, translateY=2, translateZ=3, rotateX=10, rotateY=20, rotateZ=30, scaleX=2)
        cmds.flipRigPose(rig_root())
        self.assertEqual(values(CHEST, TRANSFORMS), [-1, 2, 3, 10, -20, -30, 2, 1, 1])

    def test_center_rotation_keeps_large_values(self):
        set_values(CHEST, rotateY=200)
        cmds.flipRigPose(rig_root())
        self.assertEqual(values(CHEST, ["rotateY"]), [-200])

    def test_center_custom_channels_negate_only_when_declared(self):
        add_channel(CHEST, "lean", "mirrorNegate", attributeType="double")
        add_channel(CHEST, "curl", "mirrorCopy", attributeType="double")
        set_values(CHEST, lean=0.4, curl=0.7)
        cmds.flipRigPose(rig_root())
        self.assertEqual(values(CHEST, ["lean", "curl"]), [-0.4, 0.7])

    def test_side_custom_channels_swap_and_negate(self):
        for control in (ARM_L, ARM_R):
            add_channel(control, "lean", "mirrorNegate", attributeType="double")
        set_values(ARM_L, lean=0.4)
        set_values(ARM_R, lean=0.1)
        cmds.flipRigPose(rig_root())
        self.assertEqual(values(ARM_L, ["lean"]), [-0.1])
        self.assertEqual(values(ARM_R, ["lean"]), [-0.4])

    def test_flipping_twice_restores_the_pose(self):
        set_values(ARM_L, rotateY=20, translateZ=1)
        set_values(CHEST, translateX=1, rotateZ=30)
        cmds.flipRigPose(rig_root())
        cmds.flipRigPose(rig_root())
        self.assertEqual(values(ARM_L, ["rotateY", "translateZ"]), [20, 1])
        self.assertEqual(values(ARM_R, ["rotateY", "translateZ"]), [0, 0])
        self.assertEqual(values(CHEST, ["translateX", "rotateZ"]), [1, 30])

    def test_undo_and_redo(self):
        set_values(ARM_L, rotateY=20)
        set_values(CHEST, translateX=1)
        cmds.flipRigPose(rig_root())
        cmds.undo()
        self.assertEqual(values(ARM_L, ["rotateY"]), [20])
        self.assertEqual(values(CHEST, ["translateX"]), [1])
        cmds.redo()
        self.assertEqual(values(ARM_R, ["rotateY"]), [20])
        self.assertEqual(values(CHEST, ["translateX"]), [-1])

    def test_object_must_be_a_rig_root_or_control(self):
        self.assertMayaError(lambda: cmds.flipRigPose(rig_name("upperArm_l_jnt")), "Not a rig root or rig control")

    def test_one_control_swaps_with_its_mirror_and_nothing_else_moves(self):
        set_values(ARM_L, rotateY=20)
        set_values(ARM_R, rotateY=-3)
        set_values(FOREARM_L, rotateZ=45)
        set_values(CHEST, translateX=1)
        changed = cmds.flipRigPose(ARM_R)
        self.assertEqual(sorted(changed), ["rig:upperArm_l_ctrl_l", "rig:upperArm_r_ctrl_r"])
        self.assertEqual(values(ARM_L, ["rotateY"]), [-3])
        self.assertEqual(values(ARM_R, ["rotateY"]), [20])
        self.assertEqual(values(FOREARM_L, ["rotateZ"]), [45])
        self.assertEqual(values(CHEST, ["translateX"]), [1])

    def test_a_pair_given_from_both_ends_swaps_once(self):
        set_values(ARM_L, rotateY=20)
        set_values(ARM_R, rotateY=-3)
        cmds.flipRigPose(ARM_L, ARM_R)
        self.assertEqual(values(ARM_L, ["rotateY"]), [-3])
        self.assertEqual(values(ARM_R, ["rotateY"]), [20])

    def test_a_center_control_alone_flips_through_its_channels(self):
        set_values(CHEST, translateX=1, rotateY=20)
        set_values(ARM_L, rotateY=5)
        self.assertEqual(cmds.flipRigPose(CHEST), ["rig:chest_ctrl_c"])
        self.assertEqual(values(CHEST, ["translateX", "rotateY"]), [-1, -20])
        self.assertEqual(values(ARM_L, ["rotateY"]), [5])

    def test_controls_undo_and_redo(self):
        set_values(ARM_L, rotateY=20)
        set_values(CHEST, translateX=1)
        cmds.flipRigPose(ARM_L, CHEST)
        cmds.undo()
        self.assertEqual(values(ARM_L, ["rotateY"]), [20])
        self.assertEqual(values(CHEST, ["translateX"]), [1])
        cmds.redo()
        self.assertEqual(values(ARM_R, ["rotateY"]), [20])
        self.assertEqual(values(CHEST, ["translateX"]), [-1])


class FlipCenterOffAxis(RigTestCase):

    def test_center_control_with_no_rest_axis_along_world_x_keeps_its_values(self):
        joints = self.make_skeleton("mirror_arms")
        cmds.xform(joints[0], worldSpace=True, rotation=[0, 30, 0])   # chest_jnt, yawed off the mirror plane
        self.build("mirror_arms", joints)
        set_values(CHEST, translateX=1, rotateY=20)
        changed = cmds.flipRigPose(rig_root()) or []
        self.assertNotIn("rig:chest_ctrl_c", changed)
        self.assertEqual(values(CHEST, ["translateX", "rotateY"]), [1, 20])

    def test_center_control_pitched_in_the_mirror_plane_still_flips(self):
        joints = self.make_skeleton("mirror_arms")
        cmds.xform(joints[0], worldSpace=True, rotation=[40, 0, 0])   # local X still lies along world X
        self.build("mirror_arms", joints)
        set_values(CHEST, translateX=1, rotateY=20, rotateX=5)
        cmds.flipRigPose(rig_root())
        self.assertEqual(values(CHEST, ["translateX", "rotateY", "rotateX"]), [-1, -20, 5])
