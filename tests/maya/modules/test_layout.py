"""Layout module, built implicitly by every rig: pose cases from fixtures/poses/layout and its interface."""
import json

from maya import cmds

from rigtest.case import RigTestCase, rig_name
from rigtest.cases import add_pose_cases
from rigtest.module_contract import assert_one_solve_fills_every_output


class LayoutPoses(RigTestCase):
    pass


add_pose_cases(LayoutPoses, "layout")


class LayoutBehaviour(RigTestCase):

    def test_every_rig_gets_one_layout_named_layout(self):
        self.build_standard("fkchain_three", "fk_three")
        self.assertEqual(cmds.ls(rig_name("*"), type="layoutModule"), ["rig:layout"])

    def test_three_stacked_layout_controls(self):
        self.build_standard("fkchain_three", "fk_three")
        parent = lambda n: (cmds.listRelatives(rig_name(n), parent=True) or [None])[0]
        self.assertEqual(parent("layout_0_ctrl_c"), "rig:rig_all", "layout_0_ctrl_c should be the topmost control, under rig_all (D43)")
        self.assertEqual(parent("layout_1_ctrl_c"), "rig:layout_0_ctrl_c")
        self.assertEqual(parent("layout_2_ctrl_c"), "rig:layout_1_ctrl_c")

    def test_last_socket_is_named_end(self):
        self.build_standard("fkchain_three", "fk_three")
        layout = rig_name("layout")
        last = (cmds.getAttr(layout + ".socketNames", multiIndices=True) or [None])[-1]
        self.assertIn("end", json.loads(cmds.getAttr("%s.socketNames[%d]" % (layout, last))))

    def test_module_without_parent_structure_attaches_to_layout_end(self):
        self.build_standard("fkchain_three", "fk_three")
        self.assertSource(rig_name("fkchain_1") + ".childPlugs[0].childPlugParentMatrix",
                          "rig:layout.outputSocketMatrix[2]")

    def test_one_solve_fills_every_output(self):
        self.build_standard("fkchain_three", "fk_three")
        assert_one_solve_fills_every_output(self, rig_name("layout"))
