"""poseRigFromJointPose and animateRigFromJointClip (D41), the skeleton stage.

The solve and the JointClip reader are not built, so every call that gets past argument checking fails with "not built
yet" and changes nothing. These tests pin the argument handling and that failure. Each "not built yet" test is replaced
by a real round trip test (inverse-solve.md section 11) when its piece is built.
"""
import os
import shutil
import tempfile

from maya import cmds

from rigtest.case import RigTestCase, rig_name

CHEST = rig_name("chest_ctrl_c")


def rig_root():
    return cmds.ls(rig_name("*"), type="rigRootModule")[0]


class InverseCmdsTestCase(RigTestCase):

    def setUp(self):
        super().setUp()
        self.build_standard("mirror_arms", "mirror_arms")
        self.folder = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.folder, True)

    def write(self, name, text="{}"):
        path = os.path.join(self.folder, name)
        with open(path, "w") as handle:
            handle.write(text)
        return path


class PoseRigFromJointPose(InverseCmdsTestCase):

    def test_needs_a_joint_pose_file(self):
        self.assertMayaError(lambda: cmds.poseRigFromJointPose(rig_root()), "poseRigFromJointPose", "jointPose")

    def test_missing_file_is_an_error(self):
        self.assertMayaError(
            lambda: cmds.poseRigFromJointPose(rig_root(), jointPose=os.path.join(self.folder, "none.json")),
            "Could not read the JointClip file")

    def test_unknown_mode_is_an_error(self):
        path = self.write("pose.json")
        self.assertMayaError(lambda: cmds.poseRigFromJointPose(rig_root(), jointPose=path, mode="retarget"), "retarget", "match")

    def test_missing_sidecar_file_is_an_error(self):
        path = self.write("pose.json")
        self.assertMayaError(
            lambda: cmds.poseRigFromJointPose(rig_root(), jointPose=path, sidecar=os.path.join(self.folder, "none.json")),
            "Could not read the pose file")

    def test_a_file_fails_as_not_built_and_changes_nothing(self):
        path = self.write("pose.json")
        before = cmds.getAttr(CHEST + ".rotateX")
        self.assertMayaError(lambda: cmds.poseRigFromJointPose(rig_root(), jointPose=path, mode="match"), "not built yet")
        self.assertEqual(cmds.getAttr(CHEST + ".rotateX"), before)


class AnimateRigFromJointClip(InverseCmdsTestCase):

    def test_needs_a_joint_clip_file(self):
        self.assertMayaError(lambda: cmds.animateRigFromJointClip(rig_root()), "animateRigFromJointClip", "jointClip")

    def test_unknown_mode_is_an_error(self):
        path = self.write("clip.json")
        self.assertMayaError(lambda: cmds.animateRigFromJointClip(rig_root(), jointClip=path, mode="retarget"), "retarget", "match")

    def test_missing_sidecar_file_is_an_error(self):
        path = self.write("clip.json")
        self.assertMayaError(
            lambda: cmds.animateRigFromJointClip(rig_root(), jointClip=path, sidecar=os.path.join(self.folder, "none.json")),
            "Could not read the animation sidecar file")

    def test_start_after_end_is_an_error(self):
        path = self.write("clip.json")
        self.assertMayaError(
            lambda: cmds.animateRigFromJointClip(rig_root(), jointClip=path, startFrame=10, endFrame=5), "startFrame 10", "endFrame 5")

    def test_a_file_fails_as_not_built_and_keys_nothing(self):
        path = self.write("clip.json")
        self.assertMayaError(lambda: cmds.animateRigFromJointClip(rig_root(), jointClip=path), "not built yet")
        self.assertFalse(cmds.keyframe(CHEST, query=True, keyframeCount=True))
