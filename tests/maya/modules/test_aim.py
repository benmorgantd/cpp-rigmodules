"""Aim module: contract tests, pose cases from fixtures/poses/aim, and Aim-only behaviour."""
from maya import cmds

from rigtest.case import RigTestCase, rig_name
from rigtest.cases import add_pose_cases
from rigtest.module_contract import ModuleContract


class AimContract(ModuleContract, RigTestCase):
    TEMPLATE = "aim_basic"
    SKELETON = "aim_basic"
    MODULE_TYPE = "aimModule"
    JOINT_OWNERS = {"eye_jnt": "aim_1", "eyeTarget_jnt": "aim_1", "eyeUp_jnt": "aim_1"}


class AimPoses(RigTestCase):
    pass


add_pose_cases(AimPoses, "aim")


class AimBehaviour(RigTestCase):

    def test_target_and_up_get_controls_aim_joint_does_not(self):
        self.build_standard("aim_basic", "aim_basic")
        controls = sorted(n.split(":")[-1] for n in cmds.ls(rig_name("*"), type="rigControlNode")
                          if not n.split(":")[-1].startswith("layout_"))
        self.assertEqual(controls, ["eyeTarget_ctrl_r", "eyeUp_ctrl_r"])

    def test_up_on_aim_line_fails_validation(self):
        joints = self.make_skeleton("aim_basic")
        cmds.xform(joints[2], worldSpace=True, translation=(0, 0, 10))
        self.assertMayaError(lambda: self.build("aim_basic", joints), "rigModules[0] 'aim_1'", "Nothing was built")
        self.assertNothingBuilt()

    def test_target_on_aim_joint_fails_validation(self):
        joints = self.make_skeleton("aim_basic")
        cmds.xform(joints[1], worldSpace=True, translation=(0, 0, 0))
        self.assertMayaError(lambda: self.build("aim_basic", joints), "rigModules[0] 'aim_1'", "Nothing was built")
        self.assertNothingBuilt()
