"""The inverse: poseRigFromJointPose and animateRigFromJointClip put a rig's controls where its joints say.

- The commands: argument handling, every JointClip and ControlAnim sidecar validation rule, and what a clip does to a rig it does
  not match (PoseRigFromJointPose, JointClipValidation, AnimateRigFromJointClip, SidecarValidation).
- The solve: mostly round trips. Pose a rig, write its joints as a JointClip, zero the controls and restore from the clip. Forward
  and inverse must agree, in joints and in control values. The posed values are chosen by hand in each test. A JointClip here is
  flat (every joint a root, local equal to world), which the format allows, so it holds the rig's helper joints too (an aim's
  target and up are not exported joints) and needs no capture (rigtest.jointclips).
- The TwoBoneIk inverse (design/two-bone-ik.md section 6, TwoBoneIkInverse): a limb's controls from its joints. The control values are
  hand-worked from design/two-bone-ik.md sections 4.2, 4.2a and 6 (the limb is twoboneik_basic: root (0,0,0), mid (1.8,2.4,0),
  end (5,0,0), pole (2.5,5,0), bones 3 and 4), never read off the rig. Where a value can't be recovered (the pole's place along
  the axis with no pin, an FK control the blend hides), the test says so and checks the joints instead."""
import copy
import json
import math
import os
import shutil
import tempfile

from maya import cmds

import rigmodules.query
from rigtest.case import CapturedWarnings, RigTestCase, rig_name
from rigtest.jointclips import CHANNELS, SolveTestCase, clip_data, markup_of, rig_root, snapshot, transform_of, values_of, world


CHEST = rig_name("chest_00_M_CTRL")
IDENTITY = {"translate": [0, 0, 0], "rotate": [0, 0, 0, 1], "scale": [1, 1, 1]}


def markup(side, usage):
    return json.dumps({"markupVersion": 1, "usage": usage, "side": side, "export": True, "skin": True})


def joint_clip():
    """A valid JointClip: a marked root with an unmarked child, and one JointPose."""
    return {
        "formatVersion": 1,
        "frameRate": 30.0,
        "startFrame": 1,
        "joints": [
            {"path": "a", "markup": markup("Center", "a"), "rest": copy.deepcopy(IDENTITY)},
            {"path": "a/b", "markup": "", "rest": copy.deepcopy(IDENTITY)},
        ],
        "poses": [[copy.deepcopy(IDENTITY), copy.deepcopy(IDENTITY)]],
    }


def control_anim(controls=None, frame_rate=30.0, version=1):
    """A ControlAnim file over frames 1 to 10. A channel is given as {"keys": [...]}."""
    return {"formatVersion": version, "frameRate": frame_rate, "startFrame": 1, "endFrame": 10, "controls": controls or {}}


class InverseCmdsTestCase(RigTestCase):

    def setUp(self):
        super().setUp()
        self.build_standard("mirror_arms", "mirror_arms")
        self.folder = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.folder, True)

    def write(self, name, data="{}"):
        path = os.path.join(self.folder, name)
        with open(path, "w") as handle:
            handle.write(data if isinstance(data, str) else json.dumps(data))
        return path

    def write_clip(self, name="clip.json", edit=None):
        data = joint_clip()
        if edit:
            edit(data)
        return self.write(name, data)


class PoseRigFromJointPose(InverseCmdsTestCase):

    def test_needs_a_joint_pose_file(self):
        self.assertMayaError(lambda: cmds.poseRigFromJointPose(rig_root()), "poseRigFromJointPose", "jointPose")

    def test_missing_file_is_an_error(self):
        self.assertMayaError(
            lambda: cmds.poseRigFromJointPose(rig_root(), jointPose=os.path.join(self.folder, "none.json")),
            "Could not read the JointClip file")

    def test_unknown_mode_is_an_error(self):
        path = self.write_clip()
        self.assertMayaError(lambda: cmds.poseRigFromJointPose(rig_root(), jointPose=path, mode="retarget"), "retarget", "match")

    def test_missing_sidecar_file_is_an_error(self):
        path = self.write_clip()
        self.assertMayaError(
            lambda: cmds.poseRigFromJointPose(rig_root(), jointPose=path, sidecar=os.path.join(self.folder, "none.json")),
            "Could not read the pose file")

    def test_a_clip_of_several_poses_is_an_error(self):
        path = self.write_clip(edit=lambda data: data["poses"].append(copy.deepcopy(data["poses"][0])))
        self.assertMayaError(lambda: cmds.poseRigFromJointPose(rig_root(), jointPose=path), "exactly one JointPose, not 2")

    def test_a_clip_that_matches_no_joint_puts_every_transform_at_rest(self):
        path = self.write_clip()
        cmds.setAttr(CHEST + ".rotateX", 20)
        changed = cmds.poseRigFromJointPose(rig_root(), jointPose=path, mode="match")
        self.assertEqual(cmds.getAttr(CHEST + ".rotateX"), 0)
        self.assertIn(CHEST.lstrip(":"), [c.lstrip(":") for c in changed])

    def test_a_pose_file_sidecar_fills_what_no_joint_gives(self):
        path = self.write_clip()
        sidecar = self.write("sidecar.json", {"formatVersion": 1, "controls": {"Center": {"chest": {"rotateX": 5.0}}}})
        cmds.poseRigFromJointPose(rig_root(), jointPose=path, sidecar=sidecar)
        self.assertAlmostEqual(cmds.getAttr(CHEST + ".rotateX"), 5.0)

    def test_an_invalid_sidecar_is_an_error(self):
        path = self.write_clip()
        sidecar = self.write("sidecar.json", {"formatVersion": 1, "controls": {"Middle": {}}})
        self.assertMayaError(lambda: cmds.poseRigFromJointPose(rig_root(), jointPose=path, sidecar=sidecar), "sidecar failed validation", "Middle")


class JointClipValidation(InverseCmdsTestCase):
    """Every rule of the JointClip reader, through poseRigFromJointPose (the clip is read before anything is applied)."""

    def assert_rejected(self, edit, *contains):
        path = self.write_clip(edit=edit)
        self.assertMayaError(lambda: cmds.poseRigFromJointPose(rig_root(), jointPose=path),
                             "JointClip file failed validation", *contains)

    def test_not_json(self):
        path = self.write("clip.json", "{ nope")
        self.assertMayaError(lambda: cmds.poseRigFromJointPose(rig_root(), jointPose=path), "JointClip file")

    def test_missing_and_unknown_keys(self):
        self.assert_rejected(lambda d: d.pop("startFrame"), "startFrame")
        self.assert_rejected(lambda d: d.update(extra=1), "extra")

    def test_format_version(self):
        self.assert_rejected(lambda d: d.update(formatVersion=99), "formatVersion")
        self.assert_rejected(lambda d: d.update(formatVersion="1"), "formatVersion", "integer")

    def test_frame_rate_and_start_frame(self):
        self.assert_rejected(lambda d: d.update(frameRate=0), "frameRate")
        self.assert_rejected(lambda d: d.update(frameRate="fast"), "frameRate")
        self.assert_rejected(lambda d: d.update(startFrame=1.5), "startFrame", "integer")

    def test_joints_must_not_be_empty(self):
        self.assert_rejected(lambda d: d.update(joints=[], poses=[[]]), "joints", "non-empty")

    def test_duplicate_joint_path(self):
        self.assert_rejected(lambda d: d["joints"][1].update(path="a"), "'a' is already used")

    def test_a_joint_needs_its_parent_before_it(self):
        self.assert_rejected(lambda d: d["joints"][1].update(path="x/b"), "no joint 'x' before it")
        self.assert_rejected(lambda d: d["joints"].reverse(), "no joint 'a' before it")

    def test_markup_must_parse(self):
        self.assert_rejected(lambda d: d["joints"][0].update(markup="{ nope"), "joints[0].markup")
        self.assert_rejected(lambda d: d["joints"][0].update(markup=markup("Middle", "a")), "joints[0].markup")

    def test_two_joints_cannot_share_side_and_usage(self):
        self.assert_rejected(lambda d: d["joints"][1].update(markup=markup("Center", "a")), "[Center, a]")

    def test_pose_needs_one_transform_per_joint(self):
        self.assert_rejected(lambda d: d["poses"][0].pop(), "poses[0]", "one transform per joint (2)")

    def test_transform_keys_and_numbers(self):
        self.assert_rejected(lambda d: d["poses"][0][1].pop("scale"), "poses[0][1]", "scale")
        self.assert_rejected(lambda d: d["poses"][0][1].update(translate=[0, 0]), "poses[0][1].translate", "3 numbers")
        self.assert_rejected(lambda d: d["poses"][0][1].update(translate=[0, "x", 0]), "poses[0][1].translate[1]")
        self.assert_rejected(lambda d: d["joints"][0]["rest"].update(scale=[1, 1]), "joints[0].rest.scale")

    def test_rotation_must_be_a_unit_quaternion(self):
        self.assert_rejected(lambda d: d["poses"][0][0].update(rotate=[0, 0, 0, 2]), "poses[0][0].rotate", "unit quaternion")
        self.assert_rejected(lambda d: d["poses"][0][0].update(rotate=[0, 0, 0]), "poses[0][0].rotate", "4 numbers")


class AnimateRigFromJointClip(InverseCmdsTestCase):

    def test_needs_a_joint_clip_file(self):
        self.assertMayaError(lambda: cmds.animateRigFromJointClip(rig_root()), "animateRigFromJointClip", "jointClip")

    def test_unknown_mode_is_an_error(self):
        path = self.write_clip()
        self.assertMayaError(lambda: cmds.animateRigFromJointClip(rig_root(), jointClip=path, mode="retarget"), "retarget", "match")

    def test_missing_sidecar_file_is_an_error(self):
        path = self.write_clip()
        self.assertMayaError(
            lambda: cmds.animateRigFromJointClip(rig_root(), jointClip=path, sidecar=os.path.join(self.folder, "none.json")),
            "Could not read the ControlAnim file")

    def test_start_after_end_is_an_error(self):
        path = self.write_clip()
        self.assertMayaError(
            lambda: cmds.animateRigFromJointClip(rig_root(), jointClip=path, startFrame=10, endFrame=5), "startFrame 10", "endFrame 5")

    def test_an_invalid_clip_is_an_error(self):
        path = self.write("clip.json")
        self.assertMayaError(lambda: cmds.animateRigFromJointClip(rig_root(), jointClip=path), "JointClip file failed validation")

    def test_the_clip_must_run_at_the_scene_frame_rate(self):
        path = self.write_clip()   # 30 fps, and a new scene is 24
        self.assertMayaError(lambda: cmds.animateRigFromJointClip(rig_root(), jointClip=path), "30 fps", "24 fps")
        self.assertFalse(cmds.keyframe(CHEST, query=True, keyframeCount=True))

    def test_the_range_must_be_inside_the_clip(self):
        cmds.currentUnit(time="ntsc")
        path = self.write_clip()   # one frame, 1
        self.assertMayaError(lambda: cmds.animateRigFromJointClip(rig_root(), jointClip=path, startFrame=1, endFrame=3), "frames 1 to 3", "frames 1 to 1")
        self.assertMayaError(lambda: cmds.animateRigFromJointClip(rig_root(), jointClip=path, startFrame=0), "frames 0 to 1")

    def test_the_sidecar_must_run_at_the_clip_frame_rate(self):
        cmds.currentUnit(time="ntsc")
        path = self.write_clip()
        sidecar = self.write("sidecar.json", control_anim(frame_rate=24.0))
        self.assertMayaError(lambda: cmds.animateRigFromJointClip(rig_root(), jointClip=path, sidecar=sidecar), "sidecar runs at 24", "JointClip at 30")

    def test_a_clip_that_matches_no_joint_and_has_no_sidecar_keys_nothing(self):
        cmds.currentUnit(time="ntsc")
        path = self.write_clip()
        changed = cmds.animateRigFromJointClip(rig_root(), jointClip=path)
        self.assertEqual(changed or [], [])
        self.assertEqual(cmds.ls(type="animCurve") or [], [])

    def test_a_clip_that_matches_no_joint_keys_what_the_sidecar_gives_with_its_own_keys(self):
        cmds.currentUnit(time="ntsc")
        path = self.write_clip()
        sidecar = self.write("sidecar.json", control_anim({"Center": {"chest": {"rotateX": {"keys": [[1, 5.0, "auto", "flat"]]}}}}))
        changed = cmds.animateRigFromJointClip(rig_root(), jointClip=path, sidecar=sidecar)
        self.assertEqual([c.lstrip(":") for c in changed], [CHEST.lstrip(":")])
        self.assertEqual(cmds.keyframe(CHEST + ".rotateX", query=True, timeChange=True), [1])
        self.assertEqual(cmds.keyframe(CHEST + ".rotateX", query=True, valueChange=True), [5.0])
        self.assertEqual(cmds.keyTangent(CHEST + ".rotateX", query=True, outTangentType=True), ["flat"])


class SidecarValidation(InverseCmdsTestCase):
    """The animation command's sidecar is a ControlAnim file. Its reader's rules are in test_rig_animation_cmds, and
    here a bad one changes nothing."""

    def setUp(self):
        super().setUp()
        cmds.currentUnit(time="ntsc")
        self.clip = self.write_clip()

    def assert_rejected(self, path, *contains):
        self.assertMayaError(lambda: cmds.animateRigFromJointClip(rig_root(), jointClip=self.clip, sidecar=path),
                             "sidecar failed validation", *contains)
        self.assertEqual(cmds.ls(type="animCurve") or [], [])

    def test_not_json(self):
        self.assert_rejected(self.write("sidecar.json", "{ nope"), "ControlAnim file")

    def test_missing_and_unknown_keys(self):
        self.assert_rejected(self.write("a.json", {"formatVersion": 1, "frameRate": 30.0}), "controls")
        data = control_anim()
        data["extra"] = 1
        self.assert_rejected(self.write("b.json", data), "extra")

    def test_the_old_animation_sidecar_is_not_a_format_any_more(self):
        self.assert_rejected(self.write("c.json", {"formatVersion": 1, "frameRate": 30.0, "frames": {"1": {}}}), "frames")

    def test_format_version(self):
        self.assert_rejected(self.write("d.json", control_anim(version=99)), "formatVersion")

    def test_a_key_outside_the_range(self):
        self.assert_rejected(self.write("e.json", control_anim({"Center": {"chest": {"rotateX": {"keys": [[50, 1.0]]}}}})), "outside")


class HandWrittenClip(SolveTestCase):
    """A clip worked out by hand, not read off a rig."""

    def test_a_rotated_first_joint_is_a_rotated_first_control(self):
        self.build_standard("fkchain_three", "fk_three")
        half = math.sqrt(0.5)
        poses = {
            "a_00_M_JNT": {"translate": [0, 0, 0], "rotate": [0, 0, half, half], "scale": [1, 1, 1]},
            "b_00_M_JNT": {"translate": [0, 2, 0], "rotate": [0, 0, 0, 1], "scale": [1, 1, 1]},
            "c_00_M_JNT": {"translate": [0, 2, 0], "rotate": [0, 0, 0, 1], "scale": [1, 1, 1]},
        }
        clip = {
            "formatVersion": 1, "frameRate": 24.0, "startFrame": 1,
            "joints": [
                {"path": "a_00_M_JNT", "markup": markup_of(rig_name("a_00_M_JNT")), "rest": transform_of(world(rig_name("a_00_M_JNT")))},
                {"path": "a_00_M_JNT/b_00_M_JNT", "markup": markup_of(rig_name("b_00_M_JNT")), "rest": poses["b_00_M_JNT"]},
                {"path": "a_00_M_JNT/b_00_M_JNT/c_00_M_JNT", "markup": markup_of(rig_name("c_00_M_JNT")), "rest": poses["c_00_M_JNT"]},
            ],
            "poses": [[poses["a_00_M_JNT"], poses["b_00_M_JNT"], poses["c_00_M_JNT"]]],
        }
        cmds.poseRigFromJointPose(rig_root(), jointPose=self.write("hand.json", clip))

        # a turns 90 degrees about Z and carries b and c rigidly, which are still at their own rest locals.
        self.assertValues(rig_name("a_00_M_CTRL"), {"rotateZ": 90, "rotateX": 0, "rotateY": 0, "translateX": 0, "translateY": 0})
        self.assertValues(rig_name("b_00_M_CTRL"), {"rotateZ": 0, "translateX": 0, "translateY": 0})
        self.assertValues(rig_name("c_00_M_CTRL"), {"rotateZ": 0, "translateX": 0, "translateY": 0})
        self.assertWorldTranslation(rig_name("b_00_M_JNT"), [-2, 0, 0])
        self.assertWorldTranslation(rig_name("c_00_M_JNT"), [-4, 0, 0])


class FkChainRoundTrip(SolveTestCase):

    def test_every_control_comes_back(self):
        self.build_standard("fkchain_three", "fk_three")
        posed = {"a_00_M_CTRL.rotateZ": 30, "a_00_M_CTRL.translateX": 0.5, "b_00_M_CTRL.rotateX": 20, "b_00_M_CTRL.rotateY": -15,
                 "c_00_M_CTRL.rotateZ": 45, "c_00_M_CTRL.scaleY": 1.5}
        _, posed_joints, controls = self.round_trip(posed)
        self.assertJointsAt(posed_joints)
        for control, expected in controls.items():
            self.assertValues(control, expected)

    def test_a_pose_at_rest_is_the_rest_pose(self):
        self.build_standard("fkchain_three", "fk_three")
        rest = self.snapshot()
        cmds.setAttr(rig_name("b_00_M_CTRL.rotateZ"), 40)
        cmds.poseRigFromJointPose(rig_root(), jointPose=self.write_clip(rest, [rest]))
        self.assertValues(rig_name("b_00_M_CTRL"), {"rotateZ": 0})
        self.assertJointsAt(rest)

    def test_undo_and_redo(self):
        self.build_standard("fkchain_three", "fk_three")
        rest = self.snapshot()
        cmds.setAttr(rig_name("a_00_M_CTRL.rotateZ"), 60)
        clip = self.write_clip(rest, [self.snapshot()])
        cmds.zeroRigControls(rig_root())
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip)
        self.assertValues(rig_name("a_00_M_CTRL"), {"rotateZ": 60})
        cmds.undo()
        self.assertValues(rig_name("a_00_M_CTRL"), {"rotateZ": 0})
        cmds.redo()
        self.assertValues(rig_name("a_00_M_CTRL"), {"rotateZ": 60})

    def test_controls_in_a_scope_are_the_only_ones_set(self):
        self.build_standard("fkchain_three", "fk_three")
        rest = self.snapshot()
        cmds.setAttr(rig_name("a_00_M_CTRL.rotateZ"), 30)
        cmds.setAttr(rig_name("b_00_M_CTRL.rotateZ"), 20)
        clip = self.write_clip(rest, [self.snapshot()])
        cmds.zeroRigControls(rig_root())
        changed = cmds.poseRigFromJointPose(rig_name("b_00_M_CTRL"), jointPose=clip)
        self.assertEqual([c.split(":")[-1] for c in changed], ["b_00_M_CTRL"])
        self.assertValues(rig_name("a_00_M_CTRL"), {"rotateZ": 0})
        # b's control value is relative to a, which stayed at rest, so b matches its joint only through its own value.
        self.assertValues(rig_name("b_00_M_CTRL"), {"rotateZ": 20})

    def test_a_source_without_the_joints_leaves_the_controls_to_the_sidecar_then_rest(self):
        self.build_standard("fkchain_three", "fk_three")
        rest = self.snapshot()
        cmds.setAttr(rig_name("c_00_M_CTRL.rotateZ"), 25)
        clip = self.write_clip(rest, [self.snapshot()], skip=("c_00_M_JNT",))
        cmds.zeroRigControls(rig_root())
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip)
        self.assertValues(rig_name("c_00_M_CTRL"), {"rotateZ": 0})

        sidecar = self.write_sidecar({"Center": {"c": {"rotateZ": 25.0}}})
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip, sidecar=sidecar)
        self.assertValues(rig_name("c_00_M_CTRL"), {"rotateZ": 25})

    def test_a_missing_joint_in_the_middle_keeps_the_controls_above_it(self):
        self.build_standard("fkchain_three", "fk_three")
        rest = self.snapshot()
        cmds.setAttr(rig_name("a_00_M_CTRL.rotateZ"), 30)
        cmds.setAttr(rig_name("b_00_M_CTRL.rotateZ"), 20)
        cmds.setAttr(rig_name("c_00_M_CTRL.rotateZ"), 10)
        clip = self.write_clip(rest, [self.snapshot()], skip=("b_00_M_JNT",))
        cmds.zeroRigControls(rig_root())
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip)
        self.assertValues(rig_name("a_00_M_CTRL"), {"rotateZ": 30})
        self.assertValues(rig_name("b_00_M_CTRL"), {"rotateZ": 0})
        # c is solved against b at rest, so it takes up the 20 degrees b's joint would have given.
        self.assertValues(rig_name("c_00_M_CTRL"), {"rotateZ": 30})

    def test_a_sidecar_value_never_beats_a_joint(self):
        self.build_standard("fkchain_three", "fk_three")
        rest = self.snapshot()
        cmds.setAttr(rig_name("a_00_M_CTRL.rotateZ"), 30)
        clip = self.write_clip(rest, [self.snapshot()])
        cmds.zeroRigControls(rig_root())
        sidecar = self.write_sidecar({"Center": {"a": {"rotateZ": 99.0}}})
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip, sidecar=sidecar)
        self.assertValues(rig_name("a_00_M_CTRL"), {"rotateZ": 30})

    def test_a_source_with_different_bone_lengths_is_a_warning_and_still_runs(self):
        self.build_standard("fkchain_three", "fk_three")
        rest = self.snapshot()
        data = self.clip_data(rest, [rest])
        for entry in data["joints"]:
            entry["rest"]["translate"][1] += 1.0
        for transform in data["poses"][0]:
            transform["translate"][1] += 1.0
        cmds.poseRigFromJointPose(rig_root(), jointPose=self.write("long.json", data))


class AimRoundTrip(SolveTestCase):

    def test_target_and_up_come_back_and_the_aim_joint_follows(self):
        self.build_standard("aim_basic", "aim_basic")
        posed = {"eyeTarget_00_R_CTRL.translateX": 2.0, "eyeTarget_00_R_CTRL.translateY": 1.0, "eyeUp_00_R_CTRL.translateX": -1.5}
        _, posed_joints, controls = self.round_trip(posed)
        self.assertJointsAt(posed_joints)
        for control, expected in controls.items():
            self.assertValues(control, expected)

    def test_a_missing_up_joint_leaves_that_control_to_rest(self):
        self.build_standard("aim_basic", "aim_basic")
        rest = self.snapshot()
        cmds.setAttr(rig_name("eyeUp_00_R_CTRL.translateX"), -1.5)
        clip = self.write_clip(rest, [self.snapshot()], skip=("eyeUp_00_M_JNT",))
        cmds.zeroRigControls(rig_root())
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip)
        self.assertValues(rig_name("eyeUp_00_R_CTRL"), {"translateX": 0})


class LayoutRoundTrip(SolveTestCase):
    """Layout has no joints. Its controls come from the sidecar, and everything below hangs from them, so the sweep
    solves it first and the other modules only absorb what the sidecar doesn't explain."""

    def setUp(self):
        super().setUp()
        self.build_standard("mirror_arms", "mirror_arms")
        self.layout = [c for c in cmds.ls(rig_name("layout_*_M_CTRL"), type="rigControlNode")]
        self.assertTrue(self.layout)

    def test_the_sidecar_restores_the_layout_and_the_modules_below_stay_put(self):
        _, posed_joints, controls = self.round_trip(
            {"layout_00_M_CTRL.translateX": 3.0, "chest_00_M_CTRL.rotateZ": 15},
            sidecar=self.write_sidecar({"Center": {"layout_0": {"translateX": 3.0}}}))
        self.assertJointsAt(posed_joints)
        self.assertValues(rig_name("layout_00_M_CTRL"), {"translateX": 3.0})
        self.assertValues(rig_name("chest_00_M_CTRL"), {"rotateZ": 15, "translateX": 0})

    def test_without_a_sidecar_the_layout_is_at_rest_and_the_controls_below_carry_the_move(self):
        _, posed_joints, _ = self.round_trip({"layout_00_M_CTRL.translateX": 3.0})
        self.assertJointsAt(posed_joints)
        self.assertValues(rig_name("layout_00_M_CTRL"), {"translateX": 0.0})
        self.assertValues(rig_name("chest_00_M_CTRL"), {"translateX": 3.0})


class SpaceRoundTrip(SolveTestCase):
    """The arm's space switch: clavicle (0) or the world (1). The sidecar says which, and the sweep builds the same
    delta the constraint does."""

    ARM = "upperArm_00_L_CTRL"
    POSE = {"clavicle_00_L_CTRL.rotateZ": 25, ARM + ".rotateZ": 35, "lowerArm_00_L_CTRL.rotateZ": -10, "chest_00_M_CTRL.rotateZ": 10}

    def setUp(self):
        super().setUp()
        self.build_standard("mirror_arms", "mirror_arms")

    def pose_and_capture(self, space):
        """Poses the rig in the given space and writes its joints. Returns (clip path, posed joints, posed controls)."""
        rest = self.snapshot()
        cmds.setAttr(rig_name(self.ARM + ".space"), space)
        for plug, value in self.POSE.items():
            cmds.setAttr(rig_name(plug), value)
        posed_joints = self.snapshot()
        controls = {c: values_of(c) for c in cmds.ls(rig_name("*_L_CTRL"), type="rigControlNode")}
        return self.write_clip(rest, [posed_joints]), posed_joints, controls

    def check_space(self, space):
        clip, posed_joints, controls = self.pose_and_capture(space)
        cmds.zeroRigControls(rig_root())
        sidecar = self.write_sidecar({"Left": {"upperArm": {"space": space}}})
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip, sidecar=sidecar)
        self.assertJointsAt(posed_joints)
        self.assertEqual(cmds.getAttr(rig_name(self.ARM + ".space")), space)
        for control, expected in controls.items():
            self.assertValues(control, expected)

    def test_clavicle_space(self):
        self.check_space(0)

    def test_world_space(self):
        self.check_space(1)

    def test_the_two_spaces_give_different_control_values_for_the_same_joints(self):
        clip, posed_joints, _ = self.pose_and_capture(1)
        cmds.zeroRigControls(rig_root())
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip, sidecar=self.write_sidecar({"Left": {"upperArm": {"space": 1}}}))
        in_world = values_of(rig_name(self.ARM))
        cmds.zeroRigControls(rig_root())
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip, sidecar=self.write_sidecar({"Left": {"upperArm": {"space": 0}}}))
        in_clavicle = values_of(rig_name(self.ARM))
        self.assertJointsAt(posed_joints)
        # In the world the arm's own rotateZ is the whole 25 + 35 + 10 degrees the clavicle and chest give it.
        self.assertNotAlmostEqual(in_world["rotateZ"], in_clavicle["rotateZ"], places=1)

    def test_a_missing_space_keeps_the_constraints_current_driver(self):
        clip, posed_joints, _ = self.pose_and_capture(1)
        cmds.zeroRigControls(rig_root())
        cmds.setAttr(rig_name(self.ARM + ".space"), 1)
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip)
        self.assertJointsAt(posed_joints)
        self.assertEqual(cmds.getAttr(rig_name(self.ARM + ".space")), 1)


class PushOutRoundTrip(SolveTestCase):
    """A push out is not inverted: the sweep runs it forward with the capsule where the source's joint is. Otherwise
    the solved control would take the push as its own motion, and the rig would push it a second time."""

    def setUp(self):
        super().setUp()
        self.build("push_out_basic", self.make_skeleton("fk_pushout"))

    def test_the_pushed_child_comes_back_with_its_control_at_rest(self):
        _, posed_joints, _ = self.round_trip({"a_00_M_CTRL.translateX": 2})
        self.assertWorldTranslation(rig_name("c_00_L_JNT"), [4, 0, 0])   # pushed from 3 by the sphere moved to x = 2
        self.assertJointsAt(posed_joints)
        self.assertValues(rig_name("c_00_L_CTRL"), {"translateX": 0, "translateY": 0, "translateZ": 0, "rotateZ": 0})
        self.assertValues(rig_name("a_00_M_CTRL"), {"translateX": 2})


class OrientConstraintRoundTrip(SolveTestCase):
    """The other constraint types go through the same delta function the constraint node uses."""

    CONTROL = rig_name("c_00_L_CTRL")

    def test_an_orient_space_round_trip(self):
        self.build_standard("fkchain_two_chains", "fk_two_chains")
        cmds.createRigConstraint(self.CONTROL, driver=["fkchain_01_M_MOD.end", "layout_00_M_MOD.end"], type="Orient")
        cmds.setAttr(self.CONTROL + ".space", 1)
        rest = self.snapshot()
        cmds.setAttr(rig_name("a_00_M_CTRL.rotateZ"), 40)
        cmds.setAttr(self.CONTROL + ".rotateZ", 15)
        posed_joints = self.snapshot()
        clip = self.write_clip(rest, [posed_joints])
        cmds.zeroRigControls(rig_root())
        control = rigmodules.query.get_control(self.CONTROL)
        sidecar = self.write_sidecar({control.side: {control.usage: {"space": 1}}})
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip, sidecar=sidecar)
        self.assertJointsAt(posed_joints)
        self.assertValues(self.CONTROL, {"rotateZ": 15})


    def test_a_partial_weight_orient_round_trip(self):
        """The sweep passes the constraint's constraintWeight to the same delta function."""
        self.build_standard("fkchain_two_chains", "fk_two_chains")
        cmds.createRigConstraint(self.CONTROL, driver=["fkchain_01_M_MOD.end", "layout_00_M_MOD.end"], type="Orient", constraintWeight=0.4)
        cmds.setAttr(self.CONTROL + ".space", 1)
        rest = self.snapshot()
        cmds.setAttr(rig_name("a_00_M_CTRL.rotateZ"), 40)
        cmds.setAttr(rig_name("layout_02_M_CTRL.rotateY"), 30)
        cmds.setAttr(self.CONTROL + ".rotateZ", 15)
        posed_joints = self.snapshot()
        clip = self.write_clip(rest, [posed_joints])
        cmds.zeroRigControls(rig_root())
        control = rigmodules.query.get_control(self.CONTROL)
        layout_end = rigmodules.query.get_control(rig_name("layout_02_M_CTRL"))
        sidecar = self.write_sidecar({
            control.side: {control.usage: {"space": 1}},
            layout_end.side: {layout_end.usage: {"rotateY": 30}}})
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip, sidecar=sidecar)
        self.assertJointsAt(posed_joints)
        self.assertValues(self.CONTROL, {"rotateZ": 15})

    def test_a_swing_space_round_trip(self):
        """A Swing driver is the layout's end, which no joint can give, so its twist and swing come from the sidecar. The control's
        own rotation comes back from the joints through the same delta function."""
        self.build_standard("fkchain_two_chains", "fk_two_chains")
        cmds.createRigConstraint(self.CONTROL, driver=["fkchain_01_M_MOD.end", "layout_00_M_MOD.end"], type="Swing")
        cmds.setAttr(self.CONTROL + ".space", 1)
        rest = self.snapshot()
        cmds.setAttr(rig_name("layout_02_M_CTRL.rotateX"), 50)
        cmds.setAttr(rig_name("layout_02_M_CTRL.rotateZ"), 30)
        cmds.setAttr(self.CONTROL + ".rotateZ", 15)
        posed_joints = self.snapshot()
        clip = self.write_clip(rest, [posed_joints])
        cmds.zeroRigControls(rig_root())
        control = rigmodules.query.get_control(self.CONTROL)
        layout_end = rigmodules.query.get_control(rig_name("layout_02_M_CTRL"))
        sidecar = self.write_sidecar({
            control.side: {control.usage: {"space": 1}},
            layout_end.side: {layout_end.usage: {"rotateX": 50, "rotateZ": 30}}})
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip, sidecar=sidecar)
        self.assertJointsAt(posed_joints)
        self.assertValues(self.CONTROL, {"rotateZ": 15})

    def test_a_twist_space_round_trip(self):
        """As for Swing: the driver's twist and the parent's swing come from the sidecar, and the control's own rotation comes back
        through the same delta function."""
        self.build_standard("fkchain_two_chains", "fk_two_chains")
        cmds.createRigConstraint(self.CONTROL, driver=["fkchain_01_M_MOD.end", "layout_00_M_MOD.end"], type="Twist")
        cmds.setAttr(self.CONTROL + ".space", 1)
        rest = self.snapshot()
        cmds.setAttr(rig_name("layout_02_M_CTRL.rotateX"), 50)
        cmds.setAttr(rig_name("a_00_M_CTRL.rotateZ"), 30)
        cmds.setAttr(self.CONTROL + ".rotateZ", 15)
        posed_joints = self.snapshot()
        clip = self.write_clip(rest, [posed_joints])
        cmds.zeroRigControls(rig_root())
        control = rigmodules.query.get_control(self.CONTROL)
        layout_end = rigmodules.query.get_control(rig_name("layout_02_M_CTRL"))
        sidecar = self.write_sidecar({
            control.side: {control.usage: {"space": 1}},
            layout_end.side: {layout_end.usage: {"rotateX": 50}}})
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip, sidecar=sidecar)
        self.assertJointsAt(posed_joints)
        self.assertValues(self.CONTROL, {"rotateZ": 15})


class AnimateFromClip(SolveTestCase):
    """animateRigFromJointClip keys every channel the joints gave on every frame, from the joints' animation."""

    A = rig_name("a_00_M_CTRL")
    B = rig_name("b_00_M_CTRL")
    FRAMES = (1, 2, 3, 4, 5)

    def setUp(self):
        super().setUp()
        self.build_standard("fkchain_three", "fk_three")
        self.rest = self.snapshot()

    def key(self, plug, values, step=1):
        for frame, value in zip(self.FRAMES, values):
            cmds.setKeyframe(rig_name(plug), time=frame * step, value=value, inTangentType="linear", outTangentType="linear")

    def capture(self):
        """The joints on every frame, from the animation."""
        snapshots = []
        for frame in self.FRAMES:
            cmds.currentTime(frame)
            snapshots.append(self.snapshot())
        return snapshots

    def clear_animation(self):
        cmds.delete(cmds.ls(type="animCurve"))
        cmds.currentTime(1)
        cmds.zeroRigControls(rig_root())

    def times(self, node, channel):
        return cmds.keyframe("%s.%s" % (node, channel), query=True, timeChange=True) or []

    def values(self, node, channel):
        return cmds.keyframe("%s.%s" % (node, channel), query=True, valueChange=True) or []

    def assertCurve(self, node, channel, expected, places=3):
        actual = self.values(node, channel)
        self.assertEqual(len(actual), len(expected), "%s.%s has keys %s" % (node, channel, actual))
        for a, e in zip(actual, expected):
            self.assertAlmostEqual(a, e, places=places, msg="%s.%s: wanted %s, got %s" % (node, channel, expected, actual))

    def test_every_channel_is_baked_on_every_frame_and_the_joints_follow(self):
        self.key("a_00_M_CTRL.rotateZ", (0, 10, 20, 30, 40))
        self.key("b_00_M_CTRL.rotateX", (0, 5, 10, 15, 20))
        snapshots = self.capture()
        clip = self.write_clip(self.rest, snapshots)
        self.clear_animation()

        changed = cmds.animateRigFromJointClip(rig_root(), jointClip=clip)

        # The controls the joints gave. The layout's have no joints and no sidecar entry, so they keep their keys (none).
        self.assertEqual(sorted(c.lstrip(":") for c in changed), sorted(c.lstrip(":") for c in (self.A, self.B, rig_name("c_00_M_CTRL"))))
        for layout in cmds.ls(rig_name("layout_*"), type="rigControlNode"):
            self.assertFalse(cmds.keyframe(layout, query=True, keyframeCount=True), layout)
        for node in (self.A, self.B):
            for channel in CHANNELS:
                self.assertEqual(self.times(node, channel), [1, 2, 3, 4, 5], "%s.%s" % (node, channel))
        self.assertCurve(self.A, "rotateZ", [0, 10, 20, 30, 40])
        self.assertCurve(self.B, "rotateX", [0, 5, 10, 15, 20])
        self.assertCurve(self.A, "translateX", [0] * 5)
        for frame, snapshot in zip(self.FRAMES, snapshots):
            cmds.currentTime(frame)
            self.assertJointsAt(snapshot)

    def test_a_range_keys_only_those_frames(self):
        self.key("a_00_M_CTRL.rotateZ", (0, 10, 20, 30, 40))
        clip = self.write_clip(self.rest, self.capture())
        self.clear_animation()
        cmds.animateRigFromJointClip(rig_root(), jointClip=clip, startFrame=2, endFrame=4)
        self.assertEqual(self.times(self.A, "rotateZ"), [2, 3, 4])
        self.assertCurve(self.A, "rotateZ", [10, 20, 30])

    def test_the_clip_keeps_its_own_frame_numbers(self):
        for frame, value in zip((11, 12), (0, 30)):
            cmds.setKeyframe(self.A + ".rotateZ", time=frame, value=value)
        snapshots = []
        for frame in (11, 12):
            cmds.currentTime(frame)
            snapshots.append(self.snapshot())
        clip = self.write_clip(self.rest, snapshots, start_frame=11)
        self.clear_animation()
        cmds.animateRigFromJointClip(rig_root(), jointClip=clip)
        self.assertEqual(self.times(self.A, "rotateZ"), [11, 12])
        self.assertCurve(self.A, "rotateZ", [0, 30])

    def test_a_scope_keys_only_those_controls(self):
        self.key("a_00_M_CTRL.rotateZ", (0, 10, 20, 30, 40))
        self.key("b_00_M_CTRL.rotateX", (0, 5, 10, 15, 20))
        clip = self.write_clip(self.rest, self.capture())
        self.clear_animation()
        cmds.animateRigFromJointClip(self.B, jointClip=clip)
        self.assertEqual(self.times(self.B, "rotateX"), [1, 2, 3, 4, 5])
        self.assertEqual(self.times(self.A, "rotateZ"), [])

    def test_the_whole_call_is_one_undo(self):
        self.key("a_00_M_CTRL.rotateZ", (0, 10, 20, 30, 40))
        clip = self.write_clip(self.rest, self.capture())
        self.clear_animation()
        cmds.animateRigFromJointClip(rig_root(), jointClip=clip)
        count = len(cmds.ls(type="animCurve"))
        self.assertGreater(count, 0)
        cmds.undo()
        self.assertEqual(cmds.ls(type="animCurve") or [], [])
        cmds.redo()
        self.assertEqual(len(cmds.ls(type="animCurve")), count)
        self.assertCurve(self.A, "rotateZ", [0, 10, 20, 30, 40])

    def test_a_long_turn_does_not_flip_the_euler_curve(self):
        # 170 degrees a frame is the most a quaternion can carry. The curve must keep climbing, not wrap.
        self.key("a_00_M_CTRL.rotateZ", (0, 170, 340, 510, 680))
        clip = self.write_clip(self.rest, self.capture())
        self.clear_animation()
        cmds.animateRigFromJointClip(rig_root(), jointClip=clip)
        self.assertCurve(self.A, "rotateZ", [0, 170, 340, 510, 680])

    def test_keys_an_animator_set_on_other_frames_survive(self):
        self.key("a_00_M_CTRL.rotateZ", (0, 10, 20, 30, 40))
        clip = self.write_clip(self.rest, self.capture(), start_frame=1)
        cmds.setKeyframe(self.A + ".rotateZ", time=50, value=5)
        cmds.animateRigFromJointClip(rig_root(), jointClip=clip)
        self.assertEqual(self.times(self.A, "rotateZ"), [1, 2, 3, 4, 5, 50])
        self.assertCurve(self.A, "rotateZ", [0, 10, 20, 30, 40, 5])


class AnimateWithSidecar(SolveTestCase):
    """The animation command's sidecar is a ControlAnim. Its curves are sampled on the clip's frames for the solve,
    and what only the sidecar gave is keyed with the sidecar's own keys and tangents, not baked."""

    ARM = rig_name("upperArm_00_L_CTRL")
    FRAMES = (1, 2, 3)

    def setUp(self):
        super().setUp()
        self.build_standard("mirror_arms", "mirror_arms")
        self.rest = self.snapshot()

    def capture(self):
        snapshots = []
        for frame in self.FRAMES:
            cmds.currentTime(frame)
            snapshots.append(self.snapshot())
        return snapshots

    def write_control_anim(self, controls, rate=24.0):
        self.counter += 1
        return self.write("anim%d.json" % self.counter, {
            "formatVersion": 1, "frameRate": rate, "startFrame": 1, "endFrame": 3, "controls": controls})

    def restore(self, snapshots, controls):
        clip = self.write_clip(self.rest, snapshots)
        cmds.delete(cmds.ls(type="animCurve"))
        cmds.currentTime(1)
        cmds.zeroRigControls(rig_root())
        return cmds.animateRigFromJointClip(rig_root(), jointClip=clip, sidecar=self.write_control_anim(controls))

    def times(self, plug):
        return cmds.keyframe(plug, query=True, timeChange=True) or []

    def values(self, plug):
        return cmds.keyframe(plug, query=True, valueChange=True) or []

    def assertJointsFollow(self, snapshots):
        for frame, snapshot in zip(self.FRAMES, snapshots):
            cmds.currentTime(frame)
            self.assertJointsAt(snapshot)

    def test_the_layout_comes_from_the_sidecar_with_the_sidecars_own_keys(self):
        for frame, value in zip(self.FRAMES, (1.0, 2.0, 3.0)):
            cmds.setKeyframe(rig_name("layout_00_M_CTRL.translateX"), time=frame, value=value)
        snapshots = self.capture()
        layout = {"Center": {"layout_0": {"translateX": {"keys": [[1, 1.0, "linear", "linear"], [3, 3.0, "linear", "linear"]]}}}}

        self.restore(snapshots, layout)

        plug = rig_name("layout_00_M_CTRL.translateX")
        self.assertEqual(self.times(plug), [1, 3], "the sidecar's keys, not one on every frame")
        self.assertEqual(self.values(plug), [1.0, 3.0])
        self.assertEqual(cmds.keyTangent(plug, query=True, outTangentType=True), ["linear", "linear"])
        self.assertJointsFollow(snapshots)

    def test_a_space_switch_keeps_its_stepped_keys_and_the_joints_follow(self):
        arm = self.ARM
        for frame, value in ((1, 0), (3, 1)):
            cmds.setKeyframe(arm + ".space", time=frame, value=value, inTangentType="auto", outTangentType="step")
        for frame, value in zip(self.FRAMES, (0, 20, 40)):
            cmds.setKeyframe(rig_name("clavicle_00_L_CTRL.rotateZ"), time=frame, value=value)
        snapshots = self.capture()
        space = {"Left": {"upperArm": {"space": {"keys": [[1, 0, "auto", "step"], [3, 1, "auto", "step"]]}}}}

        self.restore(snapshots, space)

        self.assertEqual(self.times(arm + ".space"), [1, 3])
        self.assertEqual(self.values(arm + ".space"), [0, 1])
        self.assertEqual(cmds.keyTangent(arm + ".space", query=True, outTangentType=True), ["step", "step"])
        self.assertEqual(self.times(arm + ".rotateZ"), [1, 2, 3], "what the joints gave is baked")
        self.assertJointsFollow(snapshots)

    def test_a_channel_the_joints_give_is_baked_from_the_joints_not_the_sidecar(self):
        cmds.setKeyframe(self.ARM + ".rotateZ", time=1, value=0)
        cmds.setKeyframe(self.ARM + ".rotateZ", time=3, value=40)
        snapshots = self.capture()
        wrong = {"Left": {"upperArm": {"rotateZ": {"keys": [[1, 99.0], [3, 99.0]]}}}}

        self.restore(snapshots, wrong)

        self.assertEqual(self.times(self.ARM + ".rotateZ"), [1, 2, 3])
        for actual, expected in zip(self.values(self.ARM + ".rotateZ"), (0, 20, 40)):
            self.assertAlmostEqual(actual, expected, places=2)
        self.assertJointsFollow(snapshots)

    def test_a_weighted_fixed_tangent_on_an_angle_channel_samples_as_the_rigs_own_curve(self):
        """The joints must land where the animator's weighted curve put them, on a frame between the keys."""
        plug = rig_name("layout_00_M_CTRL.rotateZ")
        cmds.setKeyframe(plug, time=1, value=0)
        cmds.setKeyframe(plug, time=3, value=60)
        cmds.keyTangent(plug, edit=True, weightedTangents=True)
        cmds.keyTangent(plug, edit=True, time=(1, 1), lock=False, outAngle=70, outWeight=3.0)
        cmds.keyTangent(plug, edit=True, time=(3, 3), lock=False, inAngle=-40, inWeight=0.5)
        snapshots = self.capture()
        self.assertNotAlmostEqual(cmds.getAttr(plug, time=2), 30.0, places=1, msg="the curve is not a straight line")
        layout = {"Center": {"layout_0": {"rotateZ": {"weighted": True, "keys": [[1, 0.0, "auto", [70.0, 3.0]], [3, 60.0, [-40.0, 0.5], "auto"]]}}}}

        self.restore(snapshots, layout)

        self.assertJointsFollow(snapshots)

    def test_the_sampling_leaves_no_scratch_curve_and_the_restore_is_one_undo_step(self):
        cmds.setKeyframe(rig_name("layout_00_M_CTRL.translateX"), time=1, value=1)
        cmds.setKeyframe(rig_name("layout_00_M_CTRL.translateX"), time=3, value=3)
        snapshots = self.capture()
        layout = {"Center": {"layout_0": {"translateX": {"keys": [[1, 1.0], [3, 3.0]]}}}}
        self.restore(snapshots, layout)
        curves = cmds.ls(type="animCurve")
        self.assertEqual([c for c in curves if not cmds.listConnections(c + ".output")], [], "every curve drives a control")
        cmds.undo()
        self.assertEqual(cmds.ls(type="animCurve") or [], [])
        cmds.redo()
        self.assertEqual(sorted(cmds.ls(type="animCurve")), sorted(curves))

    def test_keys_outside_the_restore_range_are_left_out(self):
        for frame, value in zip(self.FRAMES, (1.0, 2.0, 3.0)):
            cmds.setKeyframe(rig_name("layout_00_M_CTRL.translateX"), time=frame, value=value)
        snapshots = self.capture()
        clip = self.write_clip(self.rest, snapshots)
        sidecar = self.write_control_anim({"Center": {"layout_0": {"translateX": {"keys": [[1, 1.0], [2, 2.0], [3, 3.0]]}}}})
        cmds.delete(cmds.ls(type="animCurve"))
        cmds.animateRigFromJointClip(rig_root(), jointClip=clip, sidecar=sidecar, startFrame=2, endFrame=3)
        self.assertEqual(self.times(rig_name("layout_00_M_CTRL.translateX")), [2, 3])

    def test_a_sidecar_that_covers_less_than_the_clip_holds_its_last_value(self):
        for frame in self.FRAMES:
            cmds.setKeyframe(rig_name("layout_00_M_CTRL.translateX"), time=frame, value=1.0)
        snapshots = self.capture()
        clip = self.write_clip(self.rest, snapshots)
        sidecar = self.write("short.json", {"formatVersion": 1, "frameRate": 24.0, "startFrame": 1, "endFrame": 2,
                                            "controls": {"Center": {"layout_0": {"translateX": {"keys": [[1, 1.0]]}}}}})
        cmds.delete(cmds.ls(type="animCurve"))
        cmds.animateRigFromJointClip(rig_root(), jointClip=clip, sidecar=sidecar)
        self.assertJointsFollow(snapshots)


UPPER = "upperArm_00_L_CTRL"
LOWER = "lowerArm_00_L_CTRL"
END = "hand_00_L_CTRL"
HANDLE = "armIK_00_L_CTRL"
POLE = "armPole_00_L_CTRL"
SETTINGS = "armSettings_00_L_CTRL"
LIMB_JOINTS = ("upperArm_00_M_JNT", "lowerArm_00_M_JNT", "hand_00_M_JNT")
SETTINGS_USAGE = "armSettings"
HANDLE_USAGE = "armIK"
POLE_USAGE = "armPole"


class TwoBoneIkInverse(SolveTestCase):

    def setUp(self):
        super().setUp()
        self.build_standard("twoboneik_basic", "twoboneik_basic")

    def limb(self, joints):
        """The limb's joints out of a snapshot: the marker joints follow controls the inverse sets its own way."""
        return {j: m for j, m in joints.items() if j.split(":")[-1] in LIMB_JOINTS}

    def pose(self, plugs):
        """Sets plugs ({'control.channel': value}), and returns the rest and the posed snapshots."""
        rest = self.snapshot()
        for plug, value in plugs.items():
            cmds.setAttr(rig_name(plug), value)
        return rest, self.snapshot()

    def restore(self, rest, posed, sidecar=None, keep=()):
        """Writes the posed joints, zeroes the controls (except the plugs in keep) and restores from the clip."""
        clip = self.write_clip(rest, [posed])
        kept = {plug: cmds.getAttr(rig_name(plug)) for plug in keep}
        cmds.zeroRigControls(rig_root())
        for plug, value in kept.items():
            cmds.setAttr(rig_name(plug), value)
        kwargs = {"sidecar": self.write_sidecar(sidecar)} if sidecar else {}
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip, **kwargs)

    def left(self, usage, **channels):
        return {"Left": {usage: channels}}

    # ---- IK, blend 1 ----------------------------------------------------------------------------

    def test_a_moved_handle_comes_back_and_the_joints_follow(self):
        rest, posed = self.pose({HANDLE + ".translateX": -1, HANDLE + ".translateY": 2})
        self.assertWorldTranslation(rig_name("hand_00_M_JNT"), [4, 2, 0])
        self.restore(rest, posed)
        self.assertValues(rig_name(HANDLE), {"translateX": -1, "translateY": 2, "translateZ": 0})
        self.assertJointsAt(self.limb(posed))

    def test_the_handle_takes_the_hand_joints_rotation(self):
        rest, posed = self.pose({HANDLE + ".rotateZ": 40, HANDLE + ".translateX": -1})
        self.restore(rest, posed)
        self.assertValues(rig_name(HANDLE), {"rotateZ": 40, "translateX": -1})
        self.assertJointsAt(self.limb(posed))

    def test_a_moved_start_control_moves_the_ik_root(self):
        rest, posed = self.pose({UPPER + ".translateY": 2})
        self.restore(rest, posed)
        self.assertValues(rig_name(UPPER), {"translateY": 2})
        self.assertValues(rig_name(HANDLE), {"translateX": 0, "translateY": 0})
        self.assertJointsAt(self.limb(posed))

    def test_the_pole_comes_back_on_the_side_the_elbow_bends(self):
        # The pole goes under the limb. With no pin its place along the axis isn't in the joints: it comes back at the
        # mid joint's x (1.8) at its rest height (5), on the -Y side: world (1.8,-5,0), local (-0.7,-10,0).
        rest, posed = self.pose({POLE + ".translateY": -10})
        self.restore(rest, posed)
        self.assertValues(rig_name(POLE), {"translateX": -0.7, "translateY": -10, "translateZ": 0})
        self.assertJointsAt(self.limb(posed))

    # ---- pin, roll, soft, stretch: the sidecar's values and the rig's own ---------------------------

    def test_pin_half_gives_the_pole_back_exactly(self):
        # design 4.2: handle at rest, pole at (2,3,0), pin 0.5: the mid joint is at (1.9,2.7,0). Pin is what ties the pole
        # to the joints: along = (1.9-0.5*1.8)/0.5 = 2, height = (2.7^2-1.2^2)/(0.5*(2.7+1.2)) = 3.
        rest, posed = self.pose({POLE + ".translateX": -0.5, POLE + ".translateY": -2, POLE + ".pin": 0.5})
        self.assertWorldTranslation(rig_name("lowerArm_00_M_JNT"), [1.9, 2.7, 0])
        self.restore(rest, posed, self.left(POLE_USAGE, pin=0.5))
        self.assertValues(rig_name(POLE), {"translateX": -0.5, "translateY": -2, "translateZ": 0})
        self.assertAlmostEqual(cmds.getAttr(rig_name(POLE + ".pin")), 0.5)
        self.assertJointsAt(posed)

    def test_pin_full_puts_the_pole_on_the_elbow(self):
        rest, posed = self.pose({POLE + ".translateX": -0.5, POLE + ".translateY": -2, POLE + ".pin": 1})
        self.assertWorldTranslation(rig_name("lowerArm_00_M_JNT"), [2, 3, 0])
        self.restore(rest, posed, self.left(POLE_USAGE, pin=1))
        self.assertValues(rig_name(POLE), {"translateX": -0.5, "translateY": -2})
        self.assertJointsAt(posed)

    def test_roll_from_the_sidecar_is_undone_when_placing_the_pole(self):
        # design 4.2: roll 90 turns the bend from +Y to +Z, mid (1.8,0,2.4). The pole is placed so that rolling by 90
        # puts the elbow there: back on +Y, world (1.8,5,0), local (-0.7,0,0).
        rest, posed = self.pose({HANDLE + ".roll": 90})
        self.assertWorldTranslation(rig_name("lowerArm_00_M_JNT"), [1.8, 0, 2.4])
        self.restore(rest, posed, self.left(HANDLE_USAGE, roll=90))
        self.assertValues(rig_name(POLE), {"translateX": -0.7, "translateY": 0, "translateZ": 0})
        self.assertAlmostEqual(cmds.getAttr(rig_name(HANDLE + ".roll")), 90, places=3)
        self.assertJointsAt(self.limb(posed))

    def test_without_the_roll_the_pole_goes_to_the_elbow_side_and_the_joints_still_match(self):
        # No roll in the sidecar and none in the scene: the plane is the pole's, so the pole is on +Z at its rest
        # height, world (1.8,0,5), local (-0.7,-5,5).
        rest, posed = self.pose({HANDLE + ".roll": 90})
        self.restore(rest, posed)
        self.assertValues(rig_name(POLE), {"translateX": -0.7, "translateY": -5, "translateZ": 5})
        self.assertJointsAt(self.limb(posed))

    def test_soft_alone_recovers_the_handle_past_the_wrist(self):
        # design 4.2a: soft 1/7, handle at distance 7: the hand stops at 6.632121 and the handle is at 7.
        rest, posed = self.pose({SETTINGS + ".soft": 1.0 / 7.0, HANDLE + ".translateX": 2})
        self.assertWorldTranslation(rig_name("hand_00_M_JNT"), [6.632121, 0, 0], 1e-5)
        self.restore(rest, posed, self.left(SETTINGS_USAGE, soft=1.0 / 7.0))
        self.assertValues(rig_name(HANDLE), {"translateX": 2.0}, places=4)
        self.assertJointsAt(self.limb(posed))

    def test_soft_with_stretch_reaches_the_handle(self):
        # design 4.2a: with stretch 1 the hand is at 7, the mid joint at (2.942992,1.168305,0).
        rest, posed = self.pose({SETTINGS + ".soft": 1.0 / 7.0, SETTINGS + ".stretch": 1, HANDLE + ".translateX": 2})
        self.assertWorldTranslation(rig_name("lowerArm_00_M_JNT"), [2.942992, 1.168305, 0], 1e-5)
        self.restore(rest, posed, self.left(SETTINGS_USAGE, soft=1.0 / 7.0, stretch=1))
        self.assertValues(rig_name(HANDLE), {"translateX": 2.0}, places=4)
        self.assertJointsAt(self.limb(posed))

    def test_a_stretched_source_comes_back_with_stretch_in_the_sidecar(self):
        # Handle at 8, stretch 1: bones 3*8/7 and 4*8/7, a straight limb, hand at (8,0,0).
        rest, posed = self.pose({SETTINGS + ".stretch": 1, HANDLE + ".translateX": 3})
        self.assertWorldTranslation(rig_name("hand_00_M_JNT"), [8, 0, 0])
        self.assertWorldTranslation(rig_name("lowerArm_00_M_JNT"), [24.0 / 7.0, 0, 0])
        self.restore(rest, posed, self.left(SETTINGS_USAGE, stretch=1))
        self.assertValues(rig_name(HANDLE), {"translateX": 3.0}, places=4)
        self.assertJointsAt(self.limb(posed))

    def test_a_limb_extended_near_full_reach(self):
        # Handle at 6.9 of 7, no soft: the hand is at (6.9,0,0) and the handle is recovered exactly.
        rest, posed = self.pose({HANDLE + ".translateX": 1.9})
        self.assertWorldTranslation(rig_name("hand_00_M_JNT"), [6.9, 0, 0])
        self.restore(rest, posed)
        self.assertValues(rig_name(HANDLE), {"translateX": 1.9}, places=4)
        self.assertJointsAt(self.limb(posed))

    def test_a_handle_past_full_reach_comes_back_at_the_wrist(self):
        # Handle at 8 with no stretch: the hand stops at (7,0,0). Any handle from there on gives that pose, so the
        # handle comes back at the wrist, translate 2, and the joints match.
        rest, posed = self.pose({HANDLE + ".translateX": 3})
        self.assertWorldTranslation(rig_name("hand_00_M_JNT"), [7, 0, 0])
        self.restore(rest, posed)
        self.assertValues(rig_name(HANDLE), {"translateX": 2.0}, places=4)
        self.assertJointsAt(self.limb(posed))

    def test_a_channel_the_sidecar_lacks_is_the_rigs_current_one(self):
        # Soft is set in the scene and not zeroed, and not in the sidecar: the forward solve will use it, so does the inverse.
        rest, posed = self.pose({SETTINGS + ".soft": 1.0 / 7.0, HANDLE + ".translateX": 2})
        self.restore(rest, posed, keep=(SETTINGS + ".soft",))
        self.assertValues(rig_name(HANDLE), {"translateX": 2.0}, places=4)
        self.assertJointsAt(self.limb(posed))

    def test_the_sidecar_wins_over_the_rigs_current_channel(self):
        rest, posed = self.pose({SETTINGS + ".soft": 1.0 / 7.0, HANDLE + ".translateX": 2})
        cmds.setAttr(rig_name(SETTINGS + ".soft"), 0.5)
        self.restore(rest, posed, self.left(SETTINGS_USAGE, soft=1.0 / 7.0), keep=(SETTINGS + ".soft",))
        self.assertValues(rig_name(HANDLE), {"translateX": 2.0}, places=4)
        self.assertAlmostEqual(cmds.getAttr(rig_name(SETTINGS + ".soft")), 1.0 / 7.0, places=5)
        self.assertJointsAt(self.limb(posed))

    # ---- the pole's space -----------------------------------------------------------------------

    def test_a_pole_in_ik_space_is_placed_against_the_matched_handle(self):
        # Handle moved -1 in x, pole in Ik space so it rides along: the mid joint is at x = 1.125 (law of cosines at
        # distance 4: (16+9-16)/8). The pole is at its rest height above that, world (1.125,5,0). Its Ik space moved
        # by (-1,0,0), so its local is 1.125 - (2.5 - 1) = -0.375.
        rest, posed = self.pose({POLE + ".space": 1, HANDLE + ".translateX": -1})
        self.restore(rest, posed, self.left(POLE_USAGE, space=1))
        self.assertValues(rig_name(HANDLE), {"translateX": -1})
        self.assertValues(rig_name(POLE), {"translateX": -0.375, "translateY": 0, "translateZ": 0})
        self.assertEqual(cmds.getAttr(rig_name(POLE + ".space")), 1)
        self.assertJointsAt(self.limb(posed))

    # ---- FK and the blend -----------------------------------------------------------------------

    def test_fk_comes_back_at_blend_0_and_ik_is_matched_to_it(self):
        rest, posed = self.pose({SETTINGS + ".ikFkBlend": 0, UPPER + ".rotateZ": 20, LOWER + ".rotateZ": 40})
        self.restore(rest, posed, self.left(SETTINGS_USAGE, ikFkBlend=0))
        self.assertValues(rig_name(UPPER), {"rotateZ": 20})
        self.assertValues(rig_name(LOWER), {"rotateZ": 40})
        self.assertValues(rig_name(END), {"rotateZ": 0})
        self.assertEqual(cmds.getAttr(rig_name(SETTINGS + ".ikFkBlend")), 0)
        self.assertJointsAt(self.limb(posed))
        # The IK handle and pole were matched to the FK pose, so switching to IK leaves the joints where they were.
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 1)
        self.assertJointsAt(self.limb(posed))

    def test_fk_scale_is_reproduced_at_blend_0(self):
        rest, posed = self.pose({SETTINGS + ".ikFkBlend": 0, LOWER + ".scaleX": 1.5, END + ".translateY": 0.5})
        self.restore(rest, posed, self.left(SETTINGS_USAGE, ikFkBlend=0))
        self.assertValues(rig_name(LOWER), {"scaleX": 1.5})
        self.assertValues(rig_name(END), {"translateY": 0.5})
        self.assertJointsAt(self.limb(posed))

    def test_a_blend_between_warns_that_ik_is_approximate_and_fk_matches_the_joints(self):
        # At 0.5 the joints are a mix of the two chains, so the FK controls aren't the posed FK values: they are the ones
        # that reproduce the joints. Showing FK alone proves it.
        rest, posed = self.pose({SETTINGS + ".ikFkBlend": 0.5, UPPER + ".rotateZ": 10, LOWER + ".rotateZ": 25, HANDLE + ".translateX": -1})
        with CapturedWarnings() as warnings:
            self.restore(rest, posed, self.left(SETTINGS_USAGE, ikFkBlend=0.5))
        self.assertTrue(warnings.contain("between 0 and 1"), warnings.messages)
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        self.assertJointsAt(self.limb(posed))

    def test_no_warning_for_a_pose_ik_can_make(self):
        rest, posed = self.pose({HANDLE + ".translateX": -1, POLE + ".translateY": 2})
        with CapturedWarnings() as warnings:
            self.restore(rest, posed)
        self.assertFalse(warnings.contain("IK can't"), warnings.messages)
        self.assertFalse(warnings.contain("between 0 and 1"), warnings.messages)

    # ---- sources IK can't match, and missing joints ------------------------------------------------

    def test_a_source_with_a_longer_arm_and_no_stretch_warns_and_fk_matches(self):
        # The hand is 0.5 further out than the rig's bones can reach with stretch and pin off.
        rest = self.snapshot()
        data = self.clip_data(rest, [rest])
        names = [entry["path"].split("/")[-1] for entry in data["joints"]]
        hand = names.index("hand_00_M_JNT")
        data["poses"][0][hand]["translate"][0] += 0.5
        with CapturedWarnings() as warnings:
            cmds.poseRigFromJointPose(rig_root(), jointPose=self.write("long_arm.json", data))
        self.assertTrue(warnings.contain("IK can't make"), warnings.messages)
        self.assertValues(rig_name(END), {"translateX": 0.5})

    def test_a_missing_limb_joint_takes_the_controls_from_the_sidecar(self):
        rest, posed = self.pose({HANDLE + ".translateX": -1})
        clip = self.write_clip(rest, [posed], skip=("lowerArm_00_M_JNT",))
        cmds.zeroRigControls(rig_root())
        sidecar = self.write_sidecar(self.left(HANDLE_USAGE, translateX=-1))
        with CapturedWarnings() as warnings:
            cmds.poseRigFromJointPose(rig_root(), jointPose=clip, sidecar=sidecar)
        self.assertTrue(warnings.contain("the source lacks [Left, lowerArm]"), warnings.messages)
        self.assertValues(rig_name(HANDLE), {"translateX": -1})
        self.assertValues(rig_name(UPPER), {"rotateZ": 0, "translateX": 0})

    def test_the_settings_control_comes_from_the_sidecar_only(self):
        rest, posed = self.pose({SETTINGS + ".stretch": 1, HANDLE + ".translateX": 3})
        self.restore(rest, posed, self.left(SETTINGS_USAGE, stretch=1))
        self.assertAlmostEqual(cmds.getAttr(rig_name(SETTINGS + ".stretch")), 1.0)
        self.assertValues(rig_name(SETTINGS), {"translateX": 0, "translateY": 0, "translateZ": 0})

    # ---- keys -----------------------------------------------------------------------------------

    def test_a_clip_animates_the_limb(self):
        rest = self.snapshot()
        frames = []
        for x in (-1.0, 0.0, 1.0):
            cmds.setAttr(rig_name(HANDLE + ".translateX"), x)
            frames.append(self.snapshot())
        clip = self.write_clip(rest, frames)
        cmds.zeroRigControls(rig_root())
        cmds.animateRigFromJointClip(rig_root(), jointClip=clip)
        for frame, x in zip((1, 2, 3), (-1.0, 0.0, 1.0)):
            self.assertAlmostEqual(cmds.getAttr(rig_name(HANDLE + ".translateX"), time=frame), x, places=3)
