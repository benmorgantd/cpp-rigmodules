"""Names are public API: golden interface snapshots and control and module naming (D18, D19).
How constraint command ids resolve across namespaces is in commands/test_constraint_cmds.py."""
import json

from maya import cmds

import rigmodules.naming
from rigtest.case import RigTestCase, rig_name
from rigtest.cases import add_golden_cases


class GoldenInterfaces(RigTestCase):
    """Fails when a control, attribute, socket, child plug, joint or constraint label changes name."""


add_golden_cases(GoldenInterfaces)


class ControlNames(RigTestCase):

    def test_control_name_drops_joint_namespace_and_adds_side(self):
        self.build_standard("layout_rig", "layout_rig")
        for name in ("fkchain1_00_M_CTRL", "fkchain4_00_L_CTRL", "up_00_R_CTRL"):
            self.assertExists(rig_name(name))
        self.assertEqual(cmds.ls("*:skel:*") + cmds.ls("rig:skel:*"), [])

    def test_module_nodes_take_their_template_names(self):
        self.build_standard("layout_rig", "layout_rig")
        for name in ("layout_00_M_MOD", "fkchain_01_M_MOD", "fkchain_02_L_MOD", "aim_01_R_MOD", "fkchain2Space_00_M_CON", "aimTargetSpace_00_M_CON"):
            self.assertExists(rig_name(name))
        self.assertNotExists(rig_name("layoutModule"))

    def test_layout_name_already_taken_fails(self):
        cmds.namespace(add=":rig")
        cmds.createNode("transform", name=":rig:layout_00_M_MOD")
        joints = self.make_skeleton("layout_rig")
        self.assertMayaError(lambda: self.build("layout_rig", joints), ":rig:layout_00_M_MOD", "Nothing was built")

    def test_module_name_already_taken_fails(self):
        cmds.namespace(add=":rig")
        cmds.createNode("transform", name=":rig:fkchain_01_M_MOD")
        joints = self.make_skeleton("layout_rig")
        self.assertMayaError(lambda: self.build("layout_rig", joints), ":rig:fkchain_01_M_MOD", "Nothing was built")


class Convention(RigTestCase):
    """The rules of <camelCaseName>_##_<L|R|M>_<JNT|CTRL|MOD|CON|GEO> (D58), read through rigmodules.naming.parse."""

    def test_a_name_gives_its_parts(self):
        parse = rigmodules.naming.parse
        self.assertEqual(parse("upperArm_00_L_JNT"), rigmodules.naming.RigName("upperArm", 0, "Left", "Joint"))
        self.assertEqual(parse("spineIK_12_M_CTRL"), rigmodules.naming.RigName("spineIK", 12, "Center", "Control"))
        self.assertEqual(parse("fkChain_01_R_MOD"), rigmodules.naming.RigName("fkChain", 1, "Right", "Module"))
        self.assertEqual(parse("armSpace_00_L_CON"), rigmodules.naming.RigName("armSpace", 0, "Left", "Constraint"))
        self.assertEqual(parse("arm_100_L_JNT").index, 100)

    def test_a_namespace_is_ignored(self):
        self.assertEqual(rigmodules.naming.parse("skel:hero:upperArm_00_L_JNT").base, "upperArm")

    def test_names_outside_the_convention_do_not_parse(self):
        for name in ("arm_0_L_JNT",           # one digit
                     "arm_007_L_JNT",         # a leading zero past two digits
                     "arm_00_X_JNT",          # not a side
                     "arm_00_l_JNT",          # side letters are uppercase
                     "arm_00_L_jnt",          # so are kinds
                     "arm_00_L_BONE",         # not a kind
                     "Arm_00_L_JNT",          # the base starts lowercase
                     "up_arm_00_L_JNT",       # no underscore in the base
                     "arm_L_JNT", "arm_00_JNT", "arm_00_L_JNT_extra", "arm_jnt", "", "00_00_L_JNT"):
            self.assertIsNone(rigmodules.naming.parse(name), name)

    def test_a_base_may_hold_digits_after_its_first_letter(self):
        self.assertEqual(rigmodules.naming.parse("spine1_00_M_JNT").base, "spine1")
        self.assertIsNone(rigmodules.naming.parse("1spine_00_M_JNT"))


class BuildNames(RigTestCase):

    def test_a_joint_outside_the_convention_fails_the_build_before_any_change(self):
        joints = self.make_skeleton("layout_rig")
        joints[0] = cmds.rename(joints[0], "root_jnt")
        self.assertMayaError(lambda: self.build("layout_rig", joints), "root_jnt", "ending in JNT", "renameRigNodes", "Nothing was built")
        self.assertNothingBuilt()

    def test_every_bad_joint_is_listed(self):
        joints = self.make_skeleton("layout_rig")
        joints[0] = cmds.rename(joints[0], "a_jnt")
        joints[1] = cmds.rename(joints[1], "b_jnt")
        self.assertMayaError(lambda: self.build("layout_rig", joints), "a_jnt", "b_jnt", "with 2 error(s)")

    def test_a_control_with_no_joint_is_named_from_its_usage_and_the_module_side(self):
        self.build_standard("twoboneik_basic", "twoboneik_basic")
        for name in ("armIK_00_L_CTRL", "armPole_00_L_CTRL", "armSettings_00_L_CTRL", "layout_02_M_CTRL"):
            self.assertExists(rig_name(name))

    def test_a_ribbons_ik_controls_take_the_usage_number_as_their_index_and_its_tweakers_their_joints_names(self):
        self.build_standard("ribbon_basic", "ribbon_basic")
        for name in ("tailIK_00_M_CTRL", "tailIK_02_M_CTRL", "tail5_00_M_CTRL"):
            self.assertExists(rig_name(name))


class RenameRigNodes(RigTestCase):

    def joints(self, *names):
        return [cmds.createNode("joint", name=name) for name in names]

    def uuid(self, node):
        return cmds.ls(node, uuid=True)[0]

    def name_of(self, uuid):
        return cmds.ls(uuid)[0]

    def test_names_follow_the_order_the_nodes_are_passed_in(self):
        a, b, c = self.joints("a", "b", "c")
        uuids = [self.uuid(n) for n in (c, a, b)]
        result = cmds.renameRigNodes(c, a, b, base="upperArm", side="Left")
        self.assertEqual(result, ["upperArm_00_L_JNT", "upperArm_01_L_JNT", "upperArm_02_L_JNT"])
        self.assertEqual([self.name_of(u) for u in uuids], result)

    def test_start_index_moves_the_first_number(self):
        a, b = self.joints("a", "b")
        self.assertEqual(cmds.renameRigNodes(a, b, base="finger", startIndex=3), ["finger_03_M_JNT", "finger_04_M_JNT"])

    def test_renumbering_keeps_each_nodes_base_and_side_and_may_swap_names(self):
        first, second = self.joints("spine_00_L_JNT", "spine_01_L_JNT")
        uuids = [self.uuid(second), self.uuid(first)]
        result = cmds.renameRigNodes(second, first)
        self.assertEqual(result, ["spine_00_L_JNT", "spine_01_L_JNT"])
        self.assertEqual([self.name_of(u) for u in uuids], result)

    def test_a_node_outside_the_convention_needs_a_base(self):
        (a,) = self.joints("a")
        self.assertMayaError(lambda: cmds.renameRigNodes(a), "pass -base")
        self.assertEqual(cmds.ls(a), ["a"])

    def test_the_kind_comes_from_the_nodes_type(self):
        (joint,) = self.joints("a")
        control = cmds.createNode("rigControlNode", name="b")
        self.assertEqual(cmds.renameRigNodes(joint, base="arm"), ["arm_00_M_JNT"])
        self.assertEqual(cmds.renameRigNodes(control, base="arm"), ["arm_00_M_CTRL"])

    def test_a_mesh_is_named_on_its_transform_with_geo(self):
        cube = cmds.polyCube(name="box")[0]
        self.assertEqual(cmds.renameRigNodes(cube, base="body", side="Left"), ["body_00_L_GEO"])
        self.assertEqual(cmds.listRelatives("body_00_L_GEO", shapes=True), ["body_00_L_GEOShape"])   # Maya names the shape after it
        self.assertEqual(json.loads(cmds.renameRigNodes("body_00_L_GEO", query=True)[0]),
                         {"base": "body", "index": 0, "side": "Left", "kind": "Mesh"})

    def test_meshes_are_numbered_in_order_with_the_other_kinds(self):
        a, b = cmds.polyCube(name="a")[0], cmds.polySphere(name="b")[0]
        (joint,) = self.joints("j")
        self.assertEqual(cmds.renameRigNodes(b, a, base="prop", startIndex=2), ["prop_02_M_GEO", "prop_03_M_GEO"])
        self.assertEqual(cmds.renameRigNodes(joint, base="prop", startIndex=2), ["prop_02_M_JNT"])   # the kind keeps them apart

    def test_a_skinned_meshs_intermediate_shape_does_not_stop_it_being_a_mesh(self):
        (joint,) = self.joints("j")
        cube = cmds.polyCube(name="box")[0]
        cmds.skinCluster(joint, cube)
        self.assertEqual(cmds.renameRigNodes(cube, base="body"), ["body_00_M_GEO"])

    def test_a_mesh_shape_and_an_empty_transform_are_not_meshes(self):
        cube = cmds.polyCube(name="box")[0]
        shape = cmds.listRelatives(cube, shapes=True)[0]
        self.assertMayaError(lambda: cmds.renameRigNodes(shape, base="body"), "is not a joint")
        self.assertMayaError(lambda: cmds.renameRigNodes(cmds.group(empty=True, name="grp"), base="body"), "grp is not a joint")
        self.assertEqual(cmds.ls(cube), ["box"])

    def test_geo_is_a_kind_and_only_the_exact_token(self):
        self.assertEqual(rigmodules.naming.parse("body_00_L_GEO"), rigmodules.naming.RigName("body", 0, "Left", "Mesh"))
        self.assertIsNone(rigmodules.naming.parse("body_00_L_GEOM"))

    def test_modules_and_constraints_are_named_by_their_kind_and_keep_their_namespace(self):
        self.build_standard("layout_rig", "layout_rig")
        module = rig_name("fkchain_01_M_MOD")
        constraint = rig_name("fkchain2Space_00_M_CON")
        self.assertEqual(cmds.renameRigNodes(module, base="chain", side="Left"), ["rig:chain_00_L_MOD"])
        self.assertEqual(cmds.renameRigNodes(constraint, base="space", startIndex=2), ["rig:space_02_M_CON"])

    def test_a_node_that_is_not_a_rig_node_fails_and_nothing_changes(self):
        (joint,) = self.joints("a")
        group = cmds.createNode("transform", name="group")
        self.assertMayaError(lambda: cmds.renameRigNodes(joint, group, base="arm"), "group is not a joint, rig control, rig module, rig constraint or mesh")
        self.assertEqual(cmds.ls(joint, group), ["a", "group"])

    def test_a_name_another_node_holds_fails_and_nothing_changes(self):
        a, b = self.joints("a", "b")
        cmds.createNode("joint", name="arm_01_M_JNT")
        self.assertMayaError(lambda: cmds.renameRigNodes(a, b, base="arm"), "arm_01_M_JNT already exists")
        self.assertEqual(cmds.ls(a, b), ["a", "b"])

    def test_the_same_base_and_index_in_two_namespaces_are_two_names(self):
        a = cmds.createNode("joint", name="a")
        cmds.namespace(add=":other")
        b = cmds.createNode("joint", name=":other:b")
        self.assertEqual(cmds.renameRigNodes(a, b, base="arm"), ["arm_00_M_JNT", "other:arm_01_M_JNT"])

    def test_a_bad_base_side_or_start_index_fails(self):
        (a,) = self.joints("a")
        for kwargs, expected in (({"base": "up_arm"}, "-base must be camelCase"), ({"base": "Arm"}, "-base must be camelCase"),
                                 ({"base": "arm", "side": "left"}, "-side must be"), ({"base": "arm", "startIndex": -1}, "-startIndex must be")):
            self.assertMayaError(lambda: cmds.renameRigNodes(a, **kwargs), expected)
        self.assertEqual(cmds.ls(a), ["a"])

    def test_undo_and_redo(self):
        a, b = self.joints("a", "b")
        cmds.renameRigNodes(a, b, base="arm")
        self.assertEqual(sorted(cmds.ls(type="joint")), ["arm_00_M_JNT", "arm_01_M_JNT"])
        cmds.undo()
        self.assertEqual(sorted(cmds.ls(type="joint")), ["a", "b"])
        cmds.redo()
        self.assertEqual(sorted(cmds.ls(type="joint")), ["arm_00_M_JNT", "arm_01_M_JNT"])

    def test_the_selection_is_used_in_selection_order(self):
        a, b, c = self.joints("a", "b", "c")
        cmds.selectPref(trackSelectionOrder=True)
        cmds.select(c, replace=True)
        cmds.select(a, add=True)
        cmds.select(b, add=True)
        uuids = [self.uuid(n) for n in (c, a, b)]
        result = cmds.renameRigNodes(base="arm")
        self.assertEqual(result, ["arm_00_M_JNT", "arm_01_M_JNT", "arm_02_M_JNT"])
        self.assertEqual([self.name_of(u) for u in uuids], result)

    def test_a_selection_of_several_nodes_fails_while_selection_order_is_not_tracked(self):
        a, b = self.joints("a", "b")
        cmds.selectPref(trackSelectionOrder=False)
        cmds.select(a, b, replace=True)
        self.assertMayaError(lambda: cmds.renameRigNodes(base="arm"), "not tracking selection order")
        self.assertEqual(cmds.ls(a, b), ["a", "b"])

    def test_nothing_selected_and_nothing_passed_fails(self):
        cmds.select(clear=True)
        self.assertMayaError(lambda: cmds.renameRigNodes(base="arm"), "needs nodes")

    def test_query_returns_the_parts_of_each_name(self):
        result = cmds.renameRigNodes("skel:upperArm_00_L_JNT", "arm_0_L_JNT", query=True)
        self.assertEqual(json.loads(result[0]), {"base": "upperArm", "index": 0, "side": "Left", "kind": "Joint"})
        self.assertEqual(result[1], "")
