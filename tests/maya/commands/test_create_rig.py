"""createRigFromTemplate: arguments, namespaces, assets and joint handling (D18, D29). Template content is in
concepts/test_template_validation.py, undo in concepts/test_undo_redo.py."""
from maya import cmds

import rigmodules.displaylayers
import rigmodules.groups

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
        for asset_id in ["", "shooter\\\\hero", "shooter\\main chars\\hero", "shooter/hero", "shooter|hero", "hero\\"]:
            self.assertMayaError(lambda: self.build(TEMPLATE, joints, asset=asset_id), "Asset id")
        self.assertNothingBuilt()

    def test_asset_keys_in_a_template_are_unknown(self):
        # Templates carry no asset since D29, so one template serves many assets.
        template = fixtures.load_template(TEMPLATE)
        template["assetId"] = "old\\style"
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
        self.build(TEMPLATE, joints, asset="shooter\\main\\characters\\hero")
        self.assertExists(":hero:layout")
        self.assertExists(":hero:fkchain1_ctrl_c")

    def test_namespace_flag_overrides_the_asset_name(self):
        joints = self.make_skeleton(SKELETON)
        self.build(TEMPLATE, joints, asset="shooter\\main\\characters\\hero", namespace="hero_damaged")
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
        self.make_asset_root("other\\asset")
        joints = self.make_skeleton(SKELETON)
        self.assertMayaError(lambda: self.build(TEMPLATE, joints), "other\\asset", "owns the namespace", "Nothing was built")
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
        self.make_asset_root("other\\asset")
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


class DisplayLayers(RigTestCase):
    """The build replaces the rig's three display layers (D38)."""

    LAYERS = {"jointDisplayLayer": "jointsLayer", "meshDisplayLayer": "meshesLayer", "controlDisplayLayer": "controlsLayer"}

    def rig_root(self):
        return cmds.ls(type="rigRootModule")[0]

    def layer_of(self, node):
        return cmds.listConnections(node + ".drawOverride", source=True, destination=False) or []

    def members(self, layer):
        return sorted(cmds.ls(cmds.listConnections(layer + ".drawInfo", source=False, destination=True) or [], long=True))

    def test_each_layer_is_on_its_own_attribute(self):
        self.build_standard(TEMPLATE, SKELETON)
        root = self.rig_root()
        for attr, name in self.LAYERS.items():
            self.assertEqual(cmds.listConnections(root + "." + attr, source=True, destination=False), [rig_name(name)[1:]])

    def test_layers_hold_joints_and_controls_and_the_mesh_layer_starts_empty(self):
        # The model is imported after the build, so meshes already in the namespace are not the rig's (D38).
        cmds.namespace(add=":rig")
        body = cmds.polyCube(name=":rig:body")[0]
        self.build_standard(TEMPLATE, SKELETON)
        joints = cmds.ls(rig_name("*"), type="joint", long=True)
        controls = cmds.ls(cmds.getRigControls(self.rig_root()), long=True)
        self.assertTrue(joints and controls)
        self.assertEqual(self.members(rig_name("jointsLayer")), sorted(joints))
        self.assertEqual(self.members(rig_name("meshesLayer")), [])
        self.assertEqual(self.members(rig_name("controlsLayer")), sorted(controls))
        self.assertEqual(self.layer_of(body), [])

    def test_layers_are_in_the_layer_editor(self):
        self.build_standard(TEMPLATE, SKELETON)
        ids = [cmds.getAttr(rig_name(n) + ".identification") for n in self.LAYERS.values()]
        self.assertEqual(len(set(ids)), 3)
        self.assertNotIn(0, ids)
        for name in self.LAYERS.values():
            layer = rig_name(name)
            self.assertEqual(cmds.getAttr(layer + ".displayOrder"), cmds.getAttr(layer + ".identification"))
            self.assertIn("layerManager", cmds.listConnections(layer + ".identification", destination=True))

    def test_existing_layers_in_the_namespace_are_replaced(self):
        cmds.namespace(add=":rig")
        decoy = cmds.createNode("transform", name=":rig:decoy")
        old = cmds.createDisplayLayer(decoy, name=":rig:jointsLayer", noRecurse=True)
        self.assertEqual(old, "rig:jointsLayer")
        keep = cmds.createDisplayLayer(empty=True, name=":rig:animatorLayer")
        self.build_standard(TEMPLATE, SKELETON)
        self.assertEqual(self.layer_of(decoy), [])
        self.assertEqual(cmds.ls("rig:jointsLayer"), ["rig:jointsLayer"])
        self.assertEqual(len(self.members(rig_name("jointsLayer"))), len(cmds.ls(rig_name("*"), type="joint")))
        self.assertExists(":" + keep)

    def test_a_member_of_another_layer_moves_to_the_rig_layer(self):
        joints = self.make_skeleton(SKELETON)
        cmds.createDisplayLayer(joints[0], name="mine", noRecurse=True)
        self.build(TEMPLATE, joints)
        moved = rig_name(joints[0].split(":")[-1])
        self.assertEqual(self.layer_of(moved), ["rig:jointsLayer"])
        self.assertEqual(self.members("mine"), [])

    def test_undo_and_redo(self):
        self.build_standard(TEMPLATE, SKELETON)
        before = {name: self.members(rig_name(name)) for name in self.LAYERS.values()}
        cmds.undo()
        self.assertEqual(cmds.ls(type="displayLayer"), ["defaultLayer"])
        cmds.redo()
        self.assertEqual({name: self.members(rig_name(name)) for name in self.LAYERS.values()}, before)
        for attr, name in self.LAYERS.items():
            self.assertEqual(cmds.listConnections(self.rig_root() + "." + attr, source=True, destination=False), [rig_name(name)[1:]])


class RigGroups(RigTestCase):
    """The build makes rig_all over the rig's topmost joints and controls, and mesh_grp under it (D43)."""

    CHANNELS = ["tx", "ty", "tz", "rx", "ry", "rz", "sx", "sy", "sz"]

    def setUp(self):
        super().setUp()
        self.build_standard(TEMPLATE, SKELETON)
        self.rig_root = cmds.ls(type="rigRootModule")[0]

    def parent_of(self, node):
        return (cmds.listRelatives(node, parent=True, fullPath=True) or [None])[0]

    def test_groups_are_named_in_the_namespace_and_on_the_rig_root(self):
        self.assertEqual(cmds.listConnections(self.rig_root + ".allGroup", source=True, destination=False), ["rig:rig_all"])
        self.assertEqual(cmds.listConnections(self.rig_root + ".meshGroup", source=True, destination=False), ["rig:mesh_grp"])
        self.assertEqual(rigmodules.groups.get_group(self.rig_root, "all"), "|rig:rig_all")
        self.assertEqual(rigmodules.groups.get_group(self.rig_root, "mesh"), "|rig:rig_all|rig:mesh_grp")

    def test_rig_all_is_at_the_top_and_holds_mesh_grp(self):
        all_group = cmds.ls("rig:rig_all", long=True)[0]
        self.assertIsNone(self.parent_of(all_group))
        self.assertEqual(self.parent_of("rig:mesh_grp"), all_group)
        self.assertEqual(cmds.nodeType(all_group), "transform")

    def test_topmost_joints_and_controls_are_under_rig_all_and_the_rest_keep_their_parents(self):
        all_group = cmds.ls("rig:rig_all", long=True)[0]
        joints = cmds.ls(rig_name("*"), type="joint", long=True)
        controls = cmds.ls(cmds.getRigControls(self.rig_root), long=True)
        self.assertTrue(joints and controls)
        for nodes in (joints, controls):
            for node in nodes:
                self.assertTrue(node.startswith(all_group + "|"))
                # Directly under rig_all exactly when nothing in the set is above it.
                self.assertEqual(self.parent_of(node) == all_group, self.parent_of(node) not in nodes)
            self.assertTrue([n for n in nodes if self.parent_of(n) == all_group])

    def test_a_parented_skeleton_has_one_topmost_joint(self):
        cmds.file(new=True, force=True)
        self.build_standard("fkchain_three", "fk_three_parented")
        all_group = cmds.ls("rig:rig_all", long=True)[0]
        joints = cmds.ls(rig_name("*"), type="joint", long=True)
        topmost = [n for n in joints if self.parent_of(n) == all_group]
        self.assertEqual(len(joints), 3)
        self.assertEqual(len(topmost), 1)
        for joint in joints:
            if joint not in topmost:
                self.assertIn(self.parent_of(joint), joints)

    def test_transforms_are_locked_and_hidden_but_visibility_is_not(self):
        for group in ("rig:rig_all", "rig:mesh_grp"):
            for channel in self.CHANNELS:
                plug = f"{group}.{channel}"
                self.assertTrue(cmds.getAttr(plug, lock=True), plug)
                self.assertFalse(cmds.getAttr(plug, keyable=True), plug)
                self.assertFalse(cmds.getAttr(plug, channelBox=True), plug)
            self.assertFalse(cmds.getAttr(group + ".visibility", lock=True))
            self.assertTrue(cmds.getAttr(group + ".visibility", keyable=True))

    def test_a_name_already_in_the_namespace_fails_the_build(self):
        cmds.file(new=True, force=True)
        cmds.namespace(add=":rig")
        cmds.createNode("transform", name=":rig:rig_all")
        joints = self.make_skeleton(SKELETON)
        self.assertMayaError(lambda: self.build(TEMPLATE, joints), "rig_all", "already exists")

    def test_undo_and_redo(self):
        cmds.undo()
        self.assertEqual(cmds.ls("rig:rig_all", "rig:mesh_grp"), [])
        cmds.redo()
        self.assertEqual(cmds.ls("rig:rig_all", "rig:mesh_grp"), ["rig:rig_all", "rig:mesh_grp"])
        self.assertTrue(cmds.getAttr("rig:rig_all.tx", lock=True))
        self.assertEqual(self.parent_of("rig:mesh_grp"), cmds.ls("rig:rig_all", long=True)[0])


class ParentMeshes(RigTestCase):
    """rigmodules.groups.parent_meshes: what a tool does with the new nodes of a model import."""

    def setUp(self):
        super().setUp()
        self.build_standard(TEMPLATE, SKELETON)
        self.rig_root = cmds.ls(type="rigRootModule")[0]
        self.mesh_grp = "|rig:rig_all|rig:mesh_grp"

    def test_only_the_topmost_imported_transforms_move_and_keep_their_world(self):
        group = cmds.group(empty=True, name="rig:model_grp")
        body = cmds.parent(cmds.polyCube(name="rig:body")[0], group)[0]
        loose = cmds.polyCube(name="rig:loose")[0]
        cmds.xform(loose, translation=(3, 4, 5), worldSpace=True)
        outside = cmds.polyCube(name="outside")[0]
        imported = [group, body, cmds.listRelatives(body, shapes=True)[0], loose]
        moved = rigmodules.groups.parent_meshes(self.rig_root, imported)
        self.assertEqual(sorted(moved), sorted(cmds.ls("rig:model_grp", "rig:loose", long=True)))
        for node in moved:
            self.assertEqual(cmds.listRelatives(node, parent=True, fullPath=True), [self.mesh_grp])
        self.assertEqual(cmds.listRelatives("rig:body", parent=True, fullPath=True), cmds.ls("rig:model_grp", long=True))
        self.assertEqual(cmds.xform("rig:loose", query=True, translation=True, worldSpace=True), [3, 4, 5])
        self.assertIsNone(cmds.listRelatives(outside, parent=True))

    def test_an_imported_group_named_like_the_mesh_group_goes_under_it(self):
        # A model file's own mesh_grp is a root node, so it shares the rig's short name without Maya renaming it.
        imported = cmds.ls(cmds.group(empty=True, name="rig:mesh_grp"), long=True)[0]
        self.assertEqual(imported, "|rig:mesh_grp")
        body = cmds.ls(cmds.parent(cmds.polyCube(name="rig:body")[0], imported)[0], long=True)[0]
        moved = rigmodules.groups.parent_meshes(self.rig_root, [imported, body])
        self.assertEqual(moved, [self.mesh_grp + "|rig:mesh_grp"])
        self.assertEqual(cmds.listRelatives(self.mesh_grp + "|rig:mesh_grp", children=True, fullPath=True),
                         [self.mesh_grp + "|rig:mesh_grp|rig:body"])
        self.assertEqual(cmds.listRelatives(self.mesh_grp, parent=True, fullPath=True), ["|rig:rig_all"])
        self.assertEqual(cmds.listRelatives("|rig:rig_all", children=True, fullPath=True)[0:1], [self.mesh_grp])

    def test_a_shape_alone_moves_its_transform_and_joints_are_skipped(self):
        body = cmds.polyCube(name="rig:body")[0]
        joint = cmds.createNode("joint", name="rig:stray_jnt")
        moved = rigmodules.groups.parent_meshes(self.rig_root, [cmds.listRelatives(body, shapes=True)[0], joint])
        self.assertEqual(moved, cmds.ls("rig:body", long=True))
        self.assertIsNone(cmds.listRelatives(joint, parent=True))

    def test_nothing_to_move_and_a_second_call_move_nothing(self):
        self.assertEqual(rigmodules.groups.parent_meshes(self.rig_root, []), [])
        body = cmds.polyCube(name="rig:body")[0]
        rigmodules.groups.parent_meshes(self.rig_root, [body])
        self.assertEqual(rigmodules.groups.parent_meshes(self.rig_root, cmds.ls("rig:body", long=True)), [])

    def test_undo(self):
        body = cmds.polyCube(name="rig:body")[0]
        rigmodules.groups.parent_meshes(self.rig_root, [body])
        cmds.undo()
        self.assertIsNone(cmds.listRelatives("rig:body", parent=True))

    def test_a_rig_without_the_group_fails(self):
        root = cmds.createNode("transform", name="notARig")
        with self.assertRaises(RuntimeError):
            rigmodules.groups.parent_meshes(root, [])


class AddMeshes(RigTestCase):
    """rigmodules.displaylayers.add_meshes: what a tool does with the new nodes of a model import."""

    def setUp(self):
        super().setUp()
        self.build_standard(TEMPLATE, SKELETON)
        self.rig_root = cmds.ls(type="rigRootModule")[0]

    def test_get_layer_finds_each_role(self):
        for kind, name in {"joint": "jointsLayer", "mesh": "meshesLayer", "control": "controlsLayer"}.items():
            self.assertEqual(rigmodules.displaylayers.get_layer(self.rig_root, kind), rig_name(name)[1:])

    def test_only_mesh_objects_of_the_import_are_added(self):
        body = cmds.polyCube(name="rig:body")[0]
        other = cmds.polyCube(name="outside")[0]
        extra = cmds.createNode("transform", name="rig:extra_grp")
        imported = [body, cmds.listRelatives(body, shapes=True)[0], extra]
        added = rigmodules.displaylayers.add_meshes(self.rig_root, imported)
        self.assertEqual(added, cmds.ls(body, long=True))
        self.assertEqual(cmds.listConnections(body + ".drawOverride", source=True, destination=False), ["rig:meshesLayer"])
        self.assertEqual(cmds.listConnections(extra + ".drawOverride", source=True, destination=False), None)
        self.assertEqual(cmds.listConnections(other + ".drawOverride", source=True, destination=False), None)

    def test_a_group_adds_the_meshes_under_it(self):
        group = cmds.group(empty=True, name="rig:model_grp")
        body = cmds.parent(cmds.polyCube(name="rig:body")[0], group)[0]
        added = rigmodules.displaylayers.add_meshes(self.rig_root, [group])
        self.assertEqual(added, cmds.ls(body, long=True))

    def test_intermediate_shapes_are_skipped_and_a_mesh_in_another_layer_moves(self):
        body = cmds.polyCube(name="rig:body")[0]
        shape = cmds.listRelatives(body, shapes=True)[0]
        orig = cmds.createNode("mesh", name="rig:bodyShapeOrig", parent=body)
        cmds.setAttr(orig + ".intermediateObject", True)
        cmds.createDisplayLayer(body, name="mine", noRecurse=True)
        rigmodules.displaylayers.add_meshes(self.rig_root, [shape, orig])
        self.assertEqual(cmds.listConnections(body + ".drawOverride", source=True, destination=False), ["rig:meshesLayer"])
        self.assertEqual(cmds.editDisplayLayerMembers("mine", query=True) or [], [])

    def test_no_meshes_adds_nothing_and_undo_removes_them(self):
        self.assertEqual(rigmodules.displaylayers.add_meshes(self.rig_root, []), [])
        body = cmds.polyCube(name="rig:body")[0]
        rigmodules.displaylayers.add_meshes(self.rig_root, [body])
        cmds.undo()
        self.assertEqual(cmds.listConnections(body + ".drawOverride", source=True, destination=False), None)

    def test_a_rig_without_the_layer_fails(self):
        root = cmds.createNode("transform", name="notARig")
        with self.assertRaises(RuntimeError):
            rigmodules.displaylayers.add_meshes(root, [])
