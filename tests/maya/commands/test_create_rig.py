"""createRigFromTemplate: arguments, namespaces, assets and joint handling (D18, D29). Template content is in
concepts/test_template_validation.py, undo in concepts/test_undo_redo.py."""
from maya import cmds

from rigtest import config, fixtures
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
        self.assertMayaError(lambda: cmds.createRigFromTemplate(template=path, asset=config.ASSET_ID), "Not enough objects")

    def test_asset_flag_required(self):
        joints = self.make_skeleton(SKELETON)
        path = fixtures.template_path(TEMPLATE).replace("\\", "/")
        self.assertMayaError(lambda: cmds.createRigFromTemplate(*joints, template=path), "-asset")
        self.assertNothingBuilt()

    def test_invalid_asset_id_fails(self):
        joints = self.make_skeleton(SKELETON)
        for asset_id in ["", "shooter||hero", "shooter|main chars|hero", "shooter/hero", "hero|"]:
            self.assertMayaError(lambda: self.build(TEMPLATE, joints, asset=asset_id), "Asset id")
        self.assertNothingBuilt()

    def test_asset_keys_in_a_template_are_unknown(self):
        # Templates carry no asset since D29, so one template serves many assets.
        template = fixtures.load_template(TEMPLATE)
        template["assetId"] = "old|style"
        joints = self.make_skeleton(SKELETON)
        self.assertMayaError(lambda: self.build(template, joints), "assetId", "Nothing was built")
        self.assertNothingBuilt()

    def test_missing_joint_name(self):
        self.assertMayaError(lambda: self.build(TEMPLATE, ["skel:nope"]), "No DAG node named skel:nope")
        self.assertNothingBuilt()

    def test_a_named_node_that_is_not_a_joint_fails(self):
        joints = self.make_skeleton(SKELETON)
        node = cmds.createNode("transform", name="skel:notAJoint")
        self.assertMayaError(lambda: self.build(TEMPLATE, joints + [node]), "skel:notAJoint is not a joint")
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
        self.assertMayaError(lambda: cmds.createRigFromTemplate(template=path, asset=config.ASSET_ID), "Not enough objects")
        self.assertNothingBuilt()


class Namespaces(RigTestCase):

    def test_default_namespace_is_the_asset_name(self):
        joints = self.make_skeleton(SKELETON)
        self.build(TEMPLATE, joints, asset="shooter|main|characters|hero")
        self.assertExists(":hero:layout")
        self.assertExists(":hero:fkchain1_ctrl_c")

    def test_namespace_flag_overrides_the_asset_name(self):
        joints = self.make_skeleton(SKELETON)
        self.build(TEMPLATE, joints, asset="shooter|main|characters|hero", namespace="hero_damaged")
        self.assertExists(":hero_damaged:layout")
        self.assertFalse(cmds.namespace(exists=":hero"))

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

    def test_colons_around_the_namespace_are_dropped(self):
        self.build_standard(TEMPLATE, SKELETON, namespace=":char:rig:")
        self.assertExists(rig_name("layout", "char:rig"))
        self.assertExists(rig_name("fkchain1_jnt", "char:rig"))

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


class AssetRoots(RigTestCase):
    """The asset owns the rig namespace: one asset root per namespace, shared by every rig variant (D29)."""

    TEMPLATE_RIG_NAME = "fktest"   # fixtures/templates/layout_rig.json

    def make_asset_root(self, asset_id, name="asset"):
        if not cmds.namespace(exists=":rig"):
            cmds.namespace(add=":rig")
        node = cmds.createNode("assetRootModule", name=rig_name(name))
        cmds.setAttr(node + ".assetId", asset_id, type="string")
        return node

    def rig_roots_of(self, asset_root):
        return cmds.listConnections(asset_root + ".children", source=False, type="rigRootModule") or []

    def test_new_asset_root_takes_the_asset_flag(self):
        self.build_standard(TEMPLATE, SKELETON)
        asset_roots = cmds.ls(type="assetRootModule")
        self.assertEqual(len(asset_roots), 1)
        self.assertEqual(cmds.getAttr(asset_roots[0] + ".assetId"), config.ASSET_ID)
        self.assertEqual(len(self.rig_roots_of(asset_roots[0])), 1)

    def test_existing_asset_root_is_reused_ignoring_case(self):
        asset_root = self.make_asset_root(config.ASSET_ID.upper())
        self.build_standard(TEMPLATE, SKELETON)
        self.assertEqual(cmds.ls(type="assetRootModule"), [asset_root.lstrip(":")])
        self.assertEqual(len(self.rig_roots_of(asset_root)), 1)

    def test_undo_keeps_the_existing_asset_root(self):
        asset_root = self.make_asset_root(config.ASSET_ID)
        cmds.undoInfo(state=True, infinity=True)
        self.build_standard(TEMPLATE, SKELETON)
        cmds.undo()
        self.assertExists(asset_root)
        self.assertEqual(cmds.ls(type="rigRootModule"), [])

    def test_asset_root_with_another_id_fails(self):
        self.make_asset_root("other|asset")
        joints = self.make_skeleton(SKELETON)
        self.assertMayaError(lambda: self.build(TEMPLATE, joints), "other|asset", "owns the namespace", "Nothing was built")
        self.assertEqual(cmds.ls(type="rigRootModule"), [])

    def test_two_asset_roots_in_the_namespace_fail(self):
        self.make_asset_root(config.ASSET_ID, name="asset_a")
        self.make_asset_root(config.ASSET_ID, name="asset_b")
        joints = self.make_skeleton(SKELETON)
        self.assertMayaError(lambda: self.build(TEMPLATE, joints), "2 asset roots", "Nothing was built")
        self.assertEqual(cmds.ls(type="rigRootModule"), [])

    def test_variant_already_built_for_the_asset_fails(self):
        asset_root = self.make_asset_root(config.ASSET_ID)
        rig_root = cmds.createNode("rigRootModule", name=rig_name("other_rig_root"))
        cmds.setAttr(rig_root + ".rigName", self.TEMPLATE_RIG_NAME, type="string")
        cmds.connectAttr(asset_root + ".children", rig_root + ".assetRoot")
        joints = self.make_skeleton(SKELETON)
        self.assertMayaError(lambda: self.build(TEMPLATE, joints), self.TEMPLATE_RIG_NAME, "already built", "Nothing was built")
        self.assertEqual(cmds.ls(type="rigRootModule"), ["rig:other_rig_root"])

    def test_other_namespace_gets_its_own_asset_root(self):
        self.make_asset_root("other|asset")
        self.build_standard(TEMPLATE, SKELETON, namespace="hero")
        self.assertEqual(len(cmds.ls(type="assetRootModule")), 2)


class VersionStamps(RigTestCase):
    """Each rig root records what built it, so tools can find out-of-date imports (D29)."""

    def plugin_version(self):
        # The runner may load a copy of the .mll under another file name, so find it by its node type.
        for plugin in cmds.pluginInfo(query=True, listPlugins=True):
            if "rigRootModule" in (cmds.pluginInfo(plugin, query=True, dependNode=True) or []):
                return cmds.pluginInfo(plugin, query=True, version=True)
        self.fail("no loaded plugin registers rigRootModule")

    def test_rig_root_stamps(self):
        self.build_standard(TEMPLATE, SKELETON)
        rig_root = cmds.ls(type="rigRootModule")[0]
        self.assertEqual(cmds.getAttr(rig_root + ".pluginVersion"), self.plugin_version())
        self.assertEqual(cmds.getAttr(rig_root + ".rigVersion"), 1)
        self.assertFalse(cmds.getAttr(rig_root + ".publishVersion"), "only the publish step sets publishVersion")
        self.assertEqual(cmds.getAttr(rig_root + ".publishVersion", type=True), "string")
        # The publish step writes the publish id, which is a string.
        cmds.setAttr(rig_root + ".publishVersion", "20261108T142012Z_bmorgan", type="string")
        self.assertEqual(cmds.getAttr(rig_root + ".publishVersion"), "20261108T142012Z_bmorgan")
