"""FkChain module: contract tests, pose cases from fixtures/poses/fkchain, and FkChain-only behaviour."""
import json

from maya import cmds

from rigtest.case import RigTestCase, rig_name
from rigtest.cases import add_pose_cases
from rigtest.module_contract import ModuleContract


class FkChainContract(ModuleContract, RigTestCase):
    TEMPLATE = "fkchain_three"
    SKELETON = "fk_three"
    MODULE_TYPE = "fkChainModule"
    JOINT_OWNERS = {"a_jnt": "fkchain_1", "b_jnt": "fkchain_1", "c_jnt": "fkchain_1"}


class FkChainPoses(RigTestCase):
    pass


add_pose_cases(FkChainPoses, "fkchain")


class FkChainBehaviour(RigTestCase):

    def test_controls_rotate_in_xyz(self):
        # +X down the bone, +Z bend: Y is the middle axis, so gimbal lock needs a 90 degree side swing (D40).
        self.build_standard("fkchain_three", "fk_three")
        for name in ("a_ctrl_c", "b_ctrl_c", "c_ctrl_c"):
            self.assertEqual(cmds.getAttr(rig_name(name) + ".rotateOrder"), 0, name)

    def test_one_control_per_joint(self):
        self.build_standard("fkchain_three", "fk_three")
        controls = sorted(n.split(":")[-1] for n in cmds.ls(rig_name("*"), type="rigControlNode")
                          if not n.split(":")[-1].startswith("layout_"))
        self.assertEqual(controls, ["a_ctrl_c", "b_ctrl_c", "c_ctrl_c"])

    def test_controls_form_a_dag_chain_under_the_layout(self):
        # DAG parenting does not drive transforms (D1), but animators navigate the outliner with it.
        self.build_standard("fkchain_three", "fk_three")
        parent = lambda n: (cmds.listRelatives(rig_name(n), parent=True) or [""])[0].split(":")[-1]
        self.assertEqual(parent("a_ctrl_c"), "layout_2_ctrl_c")
        self.assertEqual(parent("b_ctrl_c"), "a_ctrl_c")
        self.assertEqual(parent("c_ctrl_c"), "b_ctrl_c")

    def test_single_joint_chain_start_and_end_are_one_socket(self):
        self.build_standard("fkchain_two_chains", "fk_two_chains")
        names = json.loads(cmds.getAttr(rig_name("fkchain_2") + ".socketNames[0]"))
        self.assertEqual(names, ["index_0", "start", "end"])
