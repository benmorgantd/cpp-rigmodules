"""rigmodules.jointclip.capture_joint_clip (D41): sampling a rig's export joints into a JointClip file.

Expected values are worked out by hand from the skeleton fixtures. mirror_arms has flat joints (no DAG parents), and
fk_three_parented has a chain a > b > c. FkChain joints follow their controls exactly, so a control's rotation about its
own joint moves the joints below it rigidly, and their transforms relative to their parent stay at rest.
"""
import json
import math
import os
import shutil
import tempfile

from maya import cmds

import rigmodules.jointclip
from rigtest.case import RigTestCase, rig_name

HALF_ROOT = math.sqrt(0.5)


def rig_root():
    return cmds.ls(rig_name("*"), type="rigRootModule")[0]


class CaptureTestCase(RigTestCase):

    def setUp(self):
        super().setUp()
        self.folder = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.folder, True)
        self.path = os.path.join(self.folder, "clip.json")

    def capture(self, start=1, end=1):
        joints = rigmodules.jointclip.capture_joint_clip(rig_root(), start, end, self.path)
        with open(self.path, encoding="utf-8") as handle:
            return joints, json.load(handle)

    def assertVector(self, actual, expected):
        self.assertEqual(len(actual), len(expected))
        for a, e in zip(actual, expected):
            self.assertAlmostEqual(a, e, places=4)


class CaptureFlatRig(CaptureTestCase):

    def setUp(self):
        super().setUp()
        self.build_standard("mirror_arms", "mirror_arms")

    def joint_index(self, clip, path):
        return [joint["path"] for joint in clip["joints"]].index(path)

    def test_captures_the_export_joints_only(self):
        joints, clip = self.capture()
        self.assertEqual(len(joints), 7)
        self.assertEqual(sorted(joint["path"] for joint in clip["joints"]), sorted([
            "chest_jnt", "clavicle_l_jnt", "upperArm_l_jnt", "lowerArm_l_jnt", "clavicle_r_jnt", "upperArm_r_jnt", "lowerArm_r_jnt"]))

    def test_header_holds_version_frame_rate_and_start_frame(self):
        cmds.currentUnit(time="ntsc")
        _, clip = self.capture(start=5, end=6)
        self.assertEqual(clip["formatVersion"], 1)
        self.assertAlmostEqual(clip["frameRate"], 30.0)
        self.assertEqual(clip["startFrame"], 5)
        self.assertEqual(len(clip["poses"]), 2)

    def test_markup_is_the_joints_own_json(self):
        _, clip = self.capture()
        entry = clip["joints"][self.joint_index(clip, "upperArm_l_jnt")]
        self.assertEqual(json.loads(entry["markup"]),
                         {"markupVersion": 1, "usage": "upperArm", "side": "Left", "export": True, "skin": True})

    def test_rest_and_a_pose_at_rest_are_the_skeleton(self):
        _, clip = self.capture()
        index = self.joint_index(clip, "upperArm_l_jnt")
        for transform in (clip["joints"][index]["rest"], clip["poses"][0][index]):
            self.assertVector(transform["translate"], [2, 5, 0])
            self.assertVector(transform["rotate"], [0, 0, 0, 1])
            self.assertVector(transform["scale"], [1, 1, 1])

    def test_posing_a_control_moves_its_joints_and_not_the_rest(self):
        cmds.setAttr(rig_name("upperArm_l_ctrl_l") + ".rotateZ", 90)
        _, clip = self.capture()
        upper = self.joint_index(clip, "upperArm_l_jnt")
        lower = self.joint_index(clip, "lowerArm_l_jnt")
        self.assertVector(clip["poses"][0][upper]["translate"], [2, 5, 0])
        self.assertVector(clip["poses"][0][upper]["rotate"], [0, 0, HALF_ROOT, HALF_ROOT])
        # The lower arm hangs 2 units down +X from the upper arm, and swings to 2 units up +Y.
        self.assertVector(clip["poses"][0][lower]["translate"], [2, 7, 0])
        self.assertVector(clip["poses"][0][lower]["rotate"], [0, 0, HALF_ROOT, HALF_ROOT])
        self.assertVector(clip["joints"][lower]["rest"]["translate"], [4, 5, 0])
        self.assertVector(clip["joints"][lower]["rest"]["rotate"], [0, 0, 0, 1])

    def test_every_frame_is_sampled_without_moving_the_time_slider(self):
        control = rig_name("upperArm_l_ctrl_l")
        cmds.setKeyframe(control + ".rotateZ", time=1, value=0, outTangentType="linear")
        cmds.setKeyframe(control + ".rotateZ", time=3, value=90, inTangentType="linear")
        cmds.currentTime(10)
        _, clip = self.capture(start=1, end=3)
        index = self.joint_index(clip, "upperArm_l_jnt")
        half = math.radians(45) / 2
        self.assertVector(clip["poses"][0][index]["rotate"], [0, 0, 0, 1])
        self.assertVector(clip["poses"][1][index]["rotate"], [0, 0, math.sin(half), math.cos(half)])
        self.assertVector(clip["poses"][2][index]["rotate"], [0, 0, HALF_ROOT, HALF_ROOT])
        self.assertEqual(cmds.currentTime(query=True), 10)

    def test_quaternions_keep_one_sign_through_a_long_turn(self):
        control = rig_name("upperArm_l_ctrl_l")
        for frame, value in enumerate((0, 120, 240, 360), start=1):
            cmds.setKeyframe(control + ".rotateZ", time=frame, value=value, inTangentType="linear", outTangentType="linear")
        _, clip = self.capture(start=1, end=4)
        index = self.joint_index(clip, "upperArm_l_jnt")
        rotations = [pose[index]["rotate"] for pose in clip["poses"]]
        for before, after in zip(rotations, rotations[1:]):
            self.assertGreater(sum(a * b for a, b in zip(before, after)), 0.0)

    def test_the_file_is_accepted_by_the_plugin_reader(self):
        self.capture()
        self.assertMayaError(lambda: cmds.poseRigFromJointPose(rig_root(), jointPose=self.path), "solve is not built yet")

    def test_start_after_end_is_an_error(self):
        with self.assertRaisesRegex(ValueError, "start_frame 5 is after end_frame 2"):
            self.capture(start=5, end=2)

    def test_a_node_that_is_not_a_rig_is_an_error(self):
        node = cmds.createNode("transform")
        with self.assertRaises(RuntimeError):
            rigmodules.jointclip.capture_joint_clip(node, 1, 1, self.path)


class CaptureParentedChain(CaptureTestCase):

    def setUp(self):
        super().setUp()
        self.build_standard("fkchain_three", "fk_three_parented")

    def test_paths_follow_the_joint_hierarchy_parents_first(self):
        _, clip = self.capture()
        self.assertEqual([joint["path"] for joint in clip["joints"]], ["a_jnt", "a_jnt/b_jnt", "a_jnt/b_jnt/c_jnt"])

    def test_transforms_are_relative_to_the_parent(self):
        _, clip = self.capture()
        self.assertVector(clip["poses"][0][0]["translate"], [0, 0, 0])
        self.assertVector(clip["poses"][0][1]["translate"], [0, 2, 0])
        self.assertVector(clip["poses"][0][2]["translate"], [0, 2, 0])

    def test_rotating_the_first_link_leaves_the_children_at_their_local_rest(self):
        cmds.setAttr(cmds.getRigControls(rig_root())[0] + ".rotateZ", 90)
        _, clip = self.capture()
        self.assertVector(clip["poses"][0][0]["rotate"], [0, 0, HALF_ROOT, HALF_ROOT])
        for child in (1, 2):
            self.assertVector(clip["poses"][0][child]["translate"], [0, 2, 0])
            self.assertVector(clip["poses"][0][child]["rotate"], [0, 0, 0, 1])
