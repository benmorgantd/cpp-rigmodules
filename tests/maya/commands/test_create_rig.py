"""createRigFromTemplate: arguments, namespaces and joint handling (D18). Template content is in
concepts/test_template_validation.py, undo in concepts/test_undo_redo.py."""
from maya import cmds

from rigtest import fixtures
from rigtest.case import RigTestCase, rig_name

TEMPLATE = "layout_rig"
SKELETON = "layout_rig"


class Arguments(RigTestCase):

    def test_template_flag_required(self):
        joints = self.make_skeleton(SKELETON)
        self.assertMayaError(lambda: cmds.createRigFromTemplate(*joints), "-template")
        self.assertNothingBuilt()

    def test_missing_template_file(self):
        joints = self.make_skeleton(SKELETON)
        self.assertMayaError(lambda: cmds.createRigFromTemplate(*joints, template="C:/no/such/template.json"))
        self.assertNothingBuilt()

    def test_joints_required(self):
        path = fixtures.template_path(TEMPLATE).replace("\\", "/")
        self.assertMayaError(lambda: cmds.createRigFromTemplate(template=path))

    def test_missing_joint_name(self):
        self.assertMayaError(lambda: self.build(TEMPLATE, ["skel:nope"]), "skel:nope")
        self.assertNothingBuilt()

    def test_unmarked_joint_fails_and_builds_nothing(self):
        joints = self.make_skeleton(SKELETON)
        cmds.select(clear=True)
        helper = cmds.joint(name="skel:helper_jnt")
        self.assertMayaError(lambda: self.build(TEMPLATE, joints + [helper]), "helper_jnt", "Nothing was built")
        self.assertNothingBuilt()
        self.assertFalse(cmds.namespace(exists=":rig"), "a failed build must not create the rig namespace")

    def test_invalid_markup_fails_and_builds_nothing(self):
        joints = self.make_skeleton(SKELETON)
        cmds.setAttr(joints[0] + ".rigMarkup", "{bad json", type="string")
        self.assertMayaError(lambda: self.build(TEMPLATE, joints), "fkchain1_jnt")
        self.assertNothingBuilt()

    def test_duplicate_side_and_usage_fails(self):
        joints = self.make_skeleton(SKELETON)
        cmds.rigJointMarkup(joints[1], edit=True, usage="fkchain1")
        self.assertMayaError(lambda: self.build(TEMPLATE, joints), "fkchain1", "Nothing was built")
        self.assertNothingBuilt()

    def test_selection_is_ignored(self):
        joints = self.make_skeleton(SKELETON)
        cmds.select(joints)
        path = fixtures.template_path(TEMPLATE).replace("\\", "/")
        self.assertMayaError(lambda: cmds.createRigFromTemplate(template=path))
        self.assertNothingBuilt()


class Namespaces(RigTestCase):

    def test_default_namespace_is_rig(self):
        self.build_standard(TEMPLATE, SKELETON)
        self.assertExists(rig_name("layout"))
        self.assertExists(rig_name("fkchain1_ctrl_c"))

    def test_joints_move_into_rig_namespace_with_names_unchanged(self):
        self.build_standard(TEMPLATE, SKELETON)
        self.assertEqual(cmds.ls("skel:*", type="joint"), [])
        moved = sorted(j.split(":")[-1] for j in cmds.ls(rig_name("*"), type="joint"))
        expected = sorted(j["name"] for j in fixtures.load_skeleton(SKELETON))
        self.assertEqual(moved, expected)

    def test_nested_namespace(self):
        self.build_standard(TEMPLATE, SKELETON, namespace="char:rig")
        self.assertExists(rig_name("layout", "char:rig"))
        self.assertExists(rig_name("fkchain1_jnt", "char:rig"))
        self.assertExists(rig_name("fkchain1_ctrl_c", "char:rig"))

    def test_root_namespace(self):
        self.build_standard(TEMPLATE, SKELETON, namespace=":")
        self.assertExists(":layout")
        self.assertExists(":fkchain_1")
        self.assertExists(":fkchain1_jnt")

    def test_current_namespace_restored_and_unused(self):
        joints = self.make_skeleton(SKELETON)
        cmds.namespace(add=":other")
        cmds.namespace(setNamespace=":other")
        self.build(TEMPLATE, joints, namespace="char:rig")
        self.assertEqual(cmds.namespaceInfo(currentNamespace=True, absoluteName=True), ":other")
        cmds.namespace(setNamespace=":")
        self.assertEqual(cmds.ls("other:*"), [])

    def test_existing_rig_namespace_is_reused(self):
        cmds.namespace(add=":rig")
        self.build_standard(TEMPLATE, SKELETON)
        self.assertExists(rig_name("layout"))

    def test_joints_already_in_rig_namespace_stay(self):
        joints = self.make_skeleton(SKELETON, namespace="rig")
        self.build(TEMPLATE, joints)
        self.assertExists(rig_name("fkchain1_jnt"))

    def test_second_build_into_same_namespace_fails(self):
        self.build_standard(TEMPLATE, SKELETON)
        joints = self.make_skeleton(SKELETON, namespace="skel2")
        self.assertMayaError(lambda: self.build(TEMPLATE, joints), "already exists", "Nothing was built")
        self.assertEqual(len(cmds.ls(type="rigRootModule")), 1)

    def test_same_short_name_from_two_namespaces_fails(self):
        joints = self.make_skeleton(SKELETON)
        cmds.namespace(add=":b")
        cmds.select(clear=True)
        duplicate = cmds.joint(name=":b:fkchain1_jnt")
        cmds.rigJointMarkup(duplicate, usage="extra", side="Center", export=False, skin=False)
        self.assertMayaError(lambda: self.build(TEMPLATE, joints + [":b:fkchain1_jnt"]), "fkchain1_jnt", "Nothing was built")
        self.assertNothingBuilt()

    def test_markup_outside_passed_joints_is_ignored(self):
        # A marked skeleton in another namespace must not collide with the joints passed in.
        self.make_skeleton(SKELETON, namespace="other")
        self.build_standard(TEMPLATE, SKELETON)
        self.assertEqual(cmds.listConnections("other:fkchain1_jnt.offsetParentMatrix") or [], [])
