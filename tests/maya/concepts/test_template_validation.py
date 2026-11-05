"""Read-phase template validation (D17). Template-only checks are data: one case per
fixtures/validation/*.json. Checks that need a scene change before the build are code below."""
from maya import cmds

from rigtest.case import RigTestCase
from rigtest.cases import add_validation_cases


class TemplateCases(RigTestCase):
    pass


add_validation_cases(TemplateCases, expect_build=False)


class SceneChecks(RigTestCase):

    def test_joint_with_driven_offset_parent_matrix_fails(self):
        joints = self.make_skeleton("layout_rig")
        helper = cmds.createNode("transform", name="driver")
        cmds.connectAttr(helper + ".worldMatrix[0]", joints[3] + ".offsetParentMatrix")
        self.assertMayaError(lambda: self.build("layout_rig", joints), "fkchain4_jnt", "offsetParentMatrix", "Nothing was built")
        self.assertNothingBuilt()

    def test_all_errors_reported_with_a_count(self):
        joints = self.make_skeleton("layout_rig")
        cmds.namespace(add=":rig")
        cmds.createNode("transform", name=":rig:fkchain_1")
        cmds.createNode("transform", name=":rig:aim_1")
        message = self.assertMayaError(lambda: self.build("layout_rig", joints), "with 2 error(s)")
        self.assertIn(":rig:fkchain_1", message)
        self.assertIn(":rig:aim_1", message)
