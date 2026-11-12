"""The inverse solve (D41): poseRigFromJointPose and animateRigFromJointClip put a rig's controls where its joints say.

Most tests are round trips: pose a rig, write its joints as a JointClip, zero the controls and restore from the clip.
Forward and inverse must agree, in joints and in control values. The posed values are chosen by hand in each test. A
JointClip here is flat (every joint a root, local equal to world), which the format allows, so it holds the rig's helper
joints too (an aim's target and up are not exported joints) and needs no capture.
"""
import json
import math
import os
import shutil
import tempfile

from maya import cmds

import rigmodules.query
from rigtest.case import RigTestCase, rig_name
from rigtest.jointclips import CHANNELS, clip_data, markup_of, rig_root, snapshot, transform_of, values_of, world


class SolveTestCase(RigTestCase):
    """A rig, and the helpers that write its joints as a JointClip."""

    def setUp(self):
        super().setUp()
        self.folder = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.folder, True)
        self.counter = 0

    def write(self, name, data):
        path = os.path.join(self.folder, name)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(data if isinstance(data, str) else json.dumps(data))
        return path

    def snapshot(self):
        return snapshot()

    def clip_data(self, rest, frames, **kwargs):
        return clip_data(rest, frames, **kwargs)

    def write_clip(self, rest, frames, **kwargs):
        self.counter += 1
        return self.write("clip%d.json" % self.counter, self.clip_data(rest, frames, **kwargs))

    def write_sidecar(self, controls):
        """A pose file: {side: {usage: {channel: value}}}."""
        self.counter += 1
        return self.write("sidecar%d.json" % self.counter, {"formatVersion": 1, "controls": controls})

    def assertJointsAt(self, expected, tolerance=1e-4):
        for joint, matrix in expected.items():
            if not world(joint).isEquivalent(matrix, tolerance):
                self.fail("%s is off\n  wanted %s\n  got    %s" % (joint, list(matrix), list(world(joint))))

    def assertValues(self, control, expected, places=3):
        actual = values_of(control)
        for name, value in expected.items():
            self.assertAlmostEqual(actual[name], value, places=places, msg="%s.%s: wanted %s, got %s" % (control, name, value, actual[name]))

    def round_trip(self, posed, sidecar=None):
        """posed: {plug: value} set on the rig. Writes the posed joints, zeroes the controls and restores from the
        clip. Returns (rest snapshot, posed snapshot, posed control values by control)."""
        rest = self.snapshot()
        for plug, value in posed.items():
            cmds.setAttr(rig_name(plug), value)
        posed_joints = self.snapshot()
        controls = {c: values_of(c) for c in cmds.ls(rig_name("*"), type="rigControlNode")}
        clip = self.write_clip(rest, [posed_joints])
        cmds.zeroRigControls(rig_root())
        kwargs = {"sidecar": sidecar} if sidecar else {}
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip, **kwargs)
        return rest, posed_joints, controls


class HandWrittenClip(SolveTestCase):
    """A clip worked out by hand, not read off a rig."""

    def test_a_rotated_first_joint_is_a_rotated_first_control(self):
        self.build_standard("fkchain_three", "fk_three")
        half = math.sqrt(0.5)
        poses = {
            "a_jnt": {"translate": [0, 0, 0], "rotate": [0, 0, half, half], "scale": [1, 1, 1]},
            "b_jnt": {"translate": [0, 2, 0], "rotate": [0, 0, 0, 1], "scale": [1, 1, 1]},
            "c_jnt": {"translate": [0, 2, 0], "rotate": [0, 0, 0, 1], "scale": [1, 1, 1]},
        }
        clip = {
            "formatVersion": 1, "frameRate": 24.0, "startFrame": 1,
            "joints": [
                {"path": "a_jnt", "markup": markup_of(rig_name("a_jnt")), "rest": transform_of(world(rig_name("a_jnt")))},
                {"path": "a_jnt/b_jnt", "markup": markup_of(rig_name("b_jnt")), "rest": poses["b_jnt"]},
                {"path": "a_jnt/b_jnt/c_jnt", "markup": markup_of(rig_name("c_jnt")), "rest": poses["c_jnt"]},
            ],
            "poses": [[poses["a_jnt"], poses["b_jnt"], poses["c_jnt"]]],
        }
        cmds.poseRigFromJointPose(rig_root(), jointPose=self.write("hand.json", clip))

        # a turns 90 degrees about Z and carries b and c rigidly, which are still at their own rest locals.
        self.assertValues(rig_name("a_ctrl_c"), {"rotateZ": 90, "rotateX": 0, "rotateY": 0, "translateX": 0, "translateY": 0})
        self.assertValues(rig_name("b_ctrl_c"), {"rotateZ": 0, "translateX": 0, "translateY": 0})
        self.assertValues(rig_name("c_ctrl_c"), {"rotateZ": 0, "translateX": 0, "translateY": 0})
        self.assertWorldTranslation(rig_name("b_jnt"), [-2, 0, 0])
        self.assertWorldTranslation(rig_name("c_jnt"), [-4, 0, 0])


class FkChainRoundTrip(SolveTestCase):

    def test_every_control_comes_back(self):
        self.build_standard("fkchain_three", "fk_three")
        posed = {"a_ctrl_c.rotateZ": 30, "a_ctrl_c.translateX": 0.5, "b_ctrl_c.rotateX": 20, "b_ctrl_c.rotateY": -15,
                 "c_ctrl_c.rotateZ": 45, "c_ctrl_c.scaleY": 1.5}
        _, posed_joints, controls = self.round_trip(posed)
        self.assertJointsAt(posed_joints)
        for control, expected in controls.items():
            self.assertValues(control, expected)

    def test_a_pose_at_rest_is_the_rest_pose(self):
        self.build_standard("fkchain_three", "fk_three")
        rest = self.snapshot()
        cmds.setAttr(rig_name("b_ctrl_c.rotateZ"), 40)
        cmds.poseRigFromJointPose(rig_root(), jointPose=self.write_clip(rest, [rest]))
        self.assertValues(rig_name("b_ctrl_c"), {"rotateZ": 0})
        self.assertJointsAt(rest)

    def test_undo_and_redo(self):
        self.build_standard("fkchain_three", "fk_three")
        rest = self.snapshot()
        cmds.setAttr(rig_name("a_ctrl_c.rotateZ"), 60)
        clip = self.write_clip(rest, [self.snapshot()])
        cmds.zeroRigControls(rig_root())
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip)
        self.assertValues(rig_name("a_ctrl_c"), {"rotateZ": 60})
        cmds.undo()
        self.assertValues(rig_name("a_ctrl_c"), {"rotateZ": 0})
        cmds.redo()
        self.assertValues(rig_name("a_ctrl_c"), {"rotateZ": 60})

    def test_controls_in_a_scope_are_the_only_ones_set(self):
        self.build_standard("fkchain_three", "fk_three")
        rest = self.snapshot()
        cmds.setAttr(rig_name("a_ctrl_c.rotateZ"), 30)
        cmds.setAttr(rig_name("b_ctrl_c.rotateZ"), 20)
        clip = self.write_clip(rest, [self.snapshot()])
        cmds.zeroRigControls(rig_root())
        changed = cmds.poseRigFromJointPose(rig_name("b_ctrl_c"), jointPose=clip)
        self.assertEqual([c.split(":")[-1] for c in changed], ["b_ctrl_c"])
        self.assertValues(rig_name("a_ctrl_c"), {"rotateZ": 0})
        # b's control value is relative to a, which stayed at rest, so b matches its joint only through its own value.
        self.assertValues(rig_name("b_ctrl_c"), {"rotateZ": 20})

    def test_a_source_without_the_joints_leaves_the_controls_to_the_sidecar_then_rest(self):
        self.build_standard("fkchain_three", "fk_three")
        rest = self.snapshot()
        cmds.setAttr(rig_name("c_ctrl_c.rotateZ"), 25)
        clip = self.write_clip(rest, [self.snapshot()], skip=("c_jnt",))
        cmds.zeroRigControls(rig_root())
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip)
        self.assertValues(rig_name("c_ctrl_c"), {"rotateZ": 0})

        sidecar = self.write_sidecar({"Center": {"c": {"rotateZ": 25.0}}})
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip, sidecar=sidecar)
        self.assertValues(rig_name("c_ctrl_c"), {"rotateZ": 25})

    def test_a_missing_joint_in_the_middle_keeps_the_controls_above_it(self):
        self.build_standard("fkchain_three", "fk_three")
        rest = self.snapshot()
        cmds.setAttr(rig_name("a_ctrl_c.rotateZ"), 30)
        cmds.setAttr(rig_name("b_ctrl_c.rotateZ"), 20)
        cmds.setAttr(rig_name("c_ctrl_c.rotateZ"), 10)
        clip = self.write_clip(rest, [self.snapshot()], skip=("b_jnt",))
        cmds.zeroRigControls(rig_root())
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip)
        self.assertValues(rig_name("a_ctrl_c"), {"rotateZ": 30})
        self.assertValues(rig_name("b_ctrl_c"), {"rotateZ": 0})
        # c is solved against b at rest, so it takes up the 20 degrees b's joint would have given.
        self.assertValues(rig_name("c_ctrl_c"), {"rotateZ": 30})

    def test_a_sidecar_value_never_beats_a_joint(self):
        self.build_standard("fkchain_three", "fk_three")
        rest = self.snapshot()
        cmds.setAttr(rig_name("a_ctrl_c.rotateZ"), 30)
        clip = self.write_clip(rest, [self.snapshot()])
        cmds.zeroRigControls(rig_root())
        sidecar = self.write_sidecar({"Center": {"a": {"rotateZ": 99.0}}})
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip, sidecar=sidecar)
        self.assertValues(rig_name("a_ctrl_c"), {"rotateZ": 30})

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
        posed = {"eyeTarget_ctrl_r.translateX": 2.0, "eyeTarget_ctrl_r.translateY": 1.0, "eyeUp_ctrl_r.translateX": -1.5}
        _, posed_joints, controls = self.round_trip(posed)
        self.assertJointsAt(posed_joints)
        for control, expected in controls.items():
            self.assertValues(control, expected)

    def test_a_missing_up_joint_leaves_that_control_to_rest(self):
        self.build_standard("aim_basic", "aim_basic")
        rest = self.snapshot()
        cmds.setAttr(rig_name("eyeUp_ctrl_r.translateX"), -1.5)
        clip = self.write_clip(rest, [self.snapshot()], skip=("eyeUp_jnt",))
        cmds.zeroRigControls(rig_root())
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip)
        self.assertValues(rig_name("eyeUp_ctrl_r"), {"translateX": 0})


class LayoutRoundTrip(SolveTestCase):
    """Layout has no joints. Its controls come from the sidecar, and everything below hangs from them, so the sweep
    solves it first and the other modules only absorb what the sidecar doesn't explain."""

    def setUp(self):
        super().setUp()
        self.build_standard("mirror_arms", "mirror_arms")
        self.layout = [c for c in cmds.ls(rig_name("layout_*_ctrl_c"), type="rigControlNode")]
        self.assertTrue(self.layout)

    def test_the_sidecar_restores_the_layout_and_the_modules_below_stay_put(self):
        _, posed_joints, controls = self.round_trip(
            {"layout_0_ctrl_c.translateX": 3.0, "chest_ctrl_c.rotateZ": 15},
            sidecar=self.write_sidecar({"Center": {"layout_0": {"translateX": 3.0}}}))
        self.assertJointsAt(posed_joints)
        self.assertValues(rig_name("layout_0_ctrl_c"), {"translateX": 3.0})
        self.assertValues(rig_name("chest_ctrl_c"), {"rotateZ": 15, "translateX": 0})

    def test_without_a_sidecar_the_layout_is_at_rest_and_the_controls_below_carry_the_move(self):
        _, posed_joints, _ = self.round_trip({"layout_0_ctrl_c.translateX": 3.0})
        self.assertJointsAt(posed_joints)
        self.assertValues(rig_name("layout_0_ctrl_c"), {"translateX": 0.0})
        self.assertValues(rig_name("chest_ctrl_c"), {"translateX": 3.0})


class SpaceRoundTrip(SolveTestCase):
    """The arm's space switch: clavicle (0) or the world (1). The sidecar says which, and the sweep builds the same
    delta the constraint does."""

    ARM = "upperArm_l_ctrl_l"
    POSE = {"clavicle_l_ctrl_l.rotateZ": 25, ARM + ".rotateZ": 35, "lowerArm_l_ctrl_l.rotateZ": -10, "chest_ctrl_c.rotateZ": 10}

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
        controls = {c: values_of(c) for c in cmds.ls(rig_name("*_ctrl_l"), type="rigControlNode")}
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


class OrientConstraintRoundTrip(SolveTestCase):
    """The other constraint types go through the same delta function the constraint node uses (D31)."""

    CONTROL = rig_name("c_ctrl_l")

    def test_an_orient_space_round_trip(self):
        self.build_standard("fkchain_two_chains", "fk_two_chains")
        cmds.createRigConstraint(self.CONTROL, driver=["fkchain_1.end", "layout.end"], type="Orient")
        cmds.setAttr(self.CONTROL + ".space", 1)
        rest = self.snapshot()
        cmds.setAttr(rig_name("a_ctrl_c.rotateZ"), 40)
        cmds.setAttr(self.CONTROL + ".rotateZ", 15)
        posed_joints = self.snapshot()
        clip = self.write_clip(rest, [posed_joints])
        cmds.zeroRigControls(rig_root())
        control = rigmodules.query.get_control(self.CONTROL)
        sidecar = self.write_sidecar({control.side: {control.usage: {"space": 1}}})
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip, sidecar=sidecar)
        self.assertJointsAt(posed_joints)
        self.assertValues(self.CONTROL, {"rotateZ": 15})


class AnimateFromClip(SolveTestCase):
    """animateRigFromJointClip keys every channel it writes on every frame, from the joints' animation."""

    A = rig_name("a_ctrl_c")
    B = rig_name("b_ctrl_c")
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
        self.key("a_ctrl_c.rotateZ", (0, 10, 20, 30, 40))
        self.key("b_ctrl_c.rotateX", (0, 5, 10, 15, 20))
        snapshots = self.capture()
        clip = self.write_clip(self.rest, snapshots)
        self.clear_animation()

        changed = cmds.animateRigFromJointClip(rig_root(), jointClip=clip)

        # Every control, the layout's too: with no sidecar entry they are keyed at rest.
        self.assertEqual(sorted(c.lstrip(":") for c in changed), sorted(c.lstrip(":") for c in cmds.ls(rig_name("*"), type="rigControlNode")))
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
        self.key("a_ctrl_c.rotateZ", (0, 10, 20, 30, 40))
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
        self.key("a_ctrl_c.rotateZ", (0, 10, 20, 30, 40))
        self.key("b_ctrl_c.rotateX", (0, 5, 10, 15, 20))
        clip = self.write_clip(self.rest, self.capture())
        self.clear_animation()
        cmds.animateRigFromJointClip(self.B, jointClip=clip)
        self.assertEqual(self.times(self.B, "rotateX"), [1, 2, 3, 4, 5])
        self.assertEqual(self.times(self.A, "rotateZ"), [])

    def test_the_whole_call_is_one_undo(self):
        self.key("a_ctrl_c.rotateZ", (0, 10, 20, 30, 40))
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
        self.key("a_ctrl_c.rotateZ", (0, 170, 340, 510, 680))
        clip = self.write_clip(self.rest, self.capture())
        self.clear_animation()
        cmds.animateRigFromJointClip(rig_root(), jointClip=clip)
        self.assertCurve(self.A, "rotateZ", [0, 170, 340, 510, 680])

    def test_the_animation_sidecar_gives_what_the_joints_cannot(self):
        cmds.file(new=True, force=True)
        cmds.namespace(setNamespace=":")
        self.build_standard("mirror_arms", "mirror_arms")
        rest = self.snapshot()
        for frame, value in zip((1, 2, 3), (1.0, 2.0, 3.0)):
            cmds.setKeyframe(rig_name("layout_0_ctrl_c.translateX"), time=frame, value=value)
        snapshots = []
        for frame in (1, 2, 3):
            cmds.currentTime(frame)
            snapshots.append(self.snapshot())
        clip = self.write_clip(rest, snapshots)
        cmds.delete(cmds.ls(type="animCurve"))
        cmds.currentTime(1)
        cmds.zeroRigControls(rig_root())
        sidecar = self.write("sidecar.json", {
            "formatVersion": 1, "frameRate": 24.0,
            "frames": {str(f): {"Center": {"layout_0": {"translateX": float(f)}}} for f in (1, 2, 3)}})

        cmds.animateRigFromJointClip(rig_root(), jointClip=clip, sidecar=sidecar)

        self.assertCurve(rig_name("layout_0_ctrl_c"), "translateX", [1, 2, 3])
        self.assertCurve(rig_name("chest_ctrl_c"), "translateX", [0, 0, 0])
        for frame, snapshot in zip((1, 2, 3), snapshots):
            cmds.currentTime(frame)
            self.assertJointsAt(snapshot)

    def test_keys_an_animator_set_on_other_frames_survive(self):
        self.key("a_ctrl_c.rotateZ", (0, 10, 20, 30, 40))
        clip = self.write_clip(self.rest, self.capture(), start_frame=1)
        cmds.setKeyframe(self.A + ".rotateZ", time=50, value=5)
        cmds.animateRigFromJointClip(rig_root(), jointClip=clip)
        self.assertEqual(self.times(self.A, "rotateZ"), [1, 2, 3, 4, 5, 50])
        self.assertCurve(self.A, "rotateZ", [0, 10, 20, 30, 40, 5])
