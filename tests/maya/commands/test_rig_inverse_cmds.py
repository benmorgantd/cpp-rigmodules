"""poseRigFromJointPose and animateRigFromJointClip (D41): argument handling, every JointClip validation
rule, and what a clip does to a rig it does not match. The solve itself is in test_inverse_solve.py.
"""
import copy
import json
import os
import shutil
import tempfile

from maya import cmds

from rigtest.case import RigTestCase, rig_name

CHEST = rig_name("chest_ctrl_c")

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


def rig_root():
    return cmds.ls(rig_name("*"), type="rigRootModule")[0]


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
