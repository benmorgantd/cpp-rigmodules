"""Names are public API: golden interface snapshots and control and module naming (D18, D19).
How constraint command ids resolve across namespaces is in commands/test_constraint_cmds.py."""
from maya import cmds

from rigtest.case import RigTestCase, rig_name
from rigtest.cases import add_golden_cases


class GoldenInterfaces(RigTestCase):
    """Fails when a control, attribute, socket, child plug, joint or constraint label changes name."""


add_golden_cases(GoldenInterfaces)


class ControlNames(RigTestCase):

    def test_control_name_drops_joint_namespace_and_adds_side(self):
        self.build_standard("layout_rig", "layout_rig")
        for name in ("fkchain1_ctrl_c", "fkchain4_ctrl_l", "up_ctrl_r"):
            self.assertExists(rig_name(name))
        self.assertEqual(cmds.ls("*:skel:*") + cmds.ls("rig:skel:*"), [])

    def test_module_nodes_take_their_template_names(self):
        self.build_standard("layout_rig", "layout_rig")
        for name in ("layout", "fkchain_1", "fkchain_2", "aim_1", "fkchain_2_space", "aim_target_space"):
            self.assertExists(rig_name(name))
        self.assertNotExists(rig_name("layoutModule"))

    def test_layout_name_already_taken_fails(self):
        cmds.namespace(add=":rig")
        cmds.createNode("transform", name=":rig:layout")
        joints = self.make_skeleton("layout_rig")
        self.assertMayaError(lambda: self.build("layout_rig", joints), ":rig:layout", "Nothing was built")

    def test_module_name_already_taken_fails(self):
        cmds.namespace(add=":rig")
        cmds.createNode("transform", name=":rig:fkchain_1")
        joints = self.make_skeleton("layout_rig")
        self.assertMayaError(lambda: self.build("layout_rig", joints), ":rig:fkchain_1", "Nothing was built")
