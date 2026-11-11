"""The selection sets the build makes (D39) and the skinning selections in rigmodules.selection."""
import json

from maya import cmds

import rigmodules.query
import rigmodules.selection

from rigtest.case import RigTestCase, rig_name

TEMPLATE = "mirror_arms"
SKELETON = "mirror_arms"

# Hand-written from the template: its modules and the joints each one drives.
MODULES = ["arm_l", "arm_r", "clavicle_l", "clavicle_r", "spine"]
DRIVEN_BY = {"arm_l": ["upperArm_l_jnt", "lowerArm_l_jnt"], "arm_r": ["upperArm_r_jnt", "lowerArm_r_jnt"],
             "clavicle_l": ["clavicle_l_jnt"], "clavicle_r": ["clavicle_r_jnt"], "spine": ["chest_jnt"]}
UNCLAIMED_JOINT = "hand_l_jnt"          # no module drives it
SKIN_JOINTS_AFTER_UNMARK = ["chest_jnt", "clavicle_l_jnt", "upperArm_l_jnt", "lowerArm_l_jnt",
                            "clavicle_r_jnt", "upperArm_r_jnt"]


def short(node):
    return node.split("|")[-1].split(":")[-1]


def unmark_skin(joints):
    """lowerArm_r and the unclaimed hand stop skinning."""
    for joint, side, usage in [(joints[7], "Right", "lowerArm"), (joints[4], "Left", "hand")]:
        cmds.rigJointMarkup(joint, usage=usage, side=side, export=True, skin=False, edit=True)


class SelectionSetsCase(RigTestCase):

    def setUp(self):
        super().setUp()
        self.existing_sets = set(cmds.ls(type="objectSet"))

    def build_rig(self, skeleton_edit=None):
        joints = self.make_skeleton(SKELETON)
        if skeleton_edit:
            skeleton_edit(joints)
        self.build(TEMPLATE, joints)
        self.root = cmds.ls(type="rigRootModule")[0]
        return joints

    def rig_joints(self):
        return cmds.ls(rig_name("*"), type="joint", long=True)

    def direct_members(self, set_node):
        return cmds.sets(set_node, query=True) or []

    def direct_names(self, set_name):
        return sorted(short(m) for m in self.direct_members(rig_name(set_name)))

    def long_names(self, nodes):
        return sorted(cmds.ls(nodes, long=True))


class SetLayout(SelectionSetsCase):

    def test_every_set_is_on_the_root_array_and_says_what_it_is(self):
        self.build_rig()
        sets = cmds.listConnections(self.root + ".selectionSets", source=True, destination=False)
        # each kind: the all set, a set per module with members, and 3 side sets. The layout module has controls only.
        expected = (1 + len(MODULES) + 1 + 3) + (1 + len(MODULES) + 3) * 2
        self.assertEqual(len(sets), expected)
        for set_node in sets:
            markup = json.loads(cmds.getAttr(set_node + ".setMarkup"))
            self.assertEqual(sorted(markup), ["kind", "markupVersion", "module", "side"])
            self.assertEqual(markup["markupVersion"], 1)
            self.assertIn(markup["kind"], ["controls", "joints", "skinJoints"])
            self.assertTrue(markup["side"] is None or markup["module"] is None)

    def test_set_names(self):
        self.build_rig()
        for name in ["controlsSet", "jointsSet", "skinJointsSet", "arm_l_controlsSet", "arm_r_jointsSet",
                     "spine_skinJointsSet", "controlsLeftSet", "controlsRightSet", "controlsCenterSet",
                     "skinJointsLeftSet", "skinJointsRightSet", "skinJointsCenterSet", "jointsLeftSet"]:
            self.assertEqual(cmds.ls(rig_name(name)), [rig_name(name)[1:]], name)

    def test_the_layout_module_has_controls_but_no_joint_subsets(self):
        self.build_rig()
        self.assertEqual(cmds.ls(rig_name("layout*Set")), [rig_name("layout_controlsSet")[1:]])


class ControlSets(SelectionSetsCase):

    def test_the_all_set_nests_the_module_sets_and_selects_every_control(self):
        self.build_rig()
        all_set = rig_name("controlsSet")
        self.assertTrue(all(cmds.nodeType(m) == "objectSet" for m in self.direct_members(all_set)))
        self.assertEqual(self.long_names(rigmodules.selection.get_members(all_set)),
                         self.long_names(cmds.getRigControls(self.root)))

    def test_each_module_set_holds_that_modules_controls(self):
        self.build_rig()
        for module in MODULES:
            expected = [c.node for c in rigmodules.query.get_rig_controls(self.root) if short(c.module) == module]
            self.assertTrue(expected, module)
            self.assertEqual(self.long_names(self.direct_members(rig_name(module + "_controlsSet"))),
                             self.long_names(expected))

    def test_side_sets_hold_controls_by_markup(self):
        self.build_rig()
        for side in ["Left", "Right", "Center"]:
            self.assertEqual(self.long_names(self.direct_members(rig_name("controls%sSet" % side))),
                             self.long_names(cmds.getRigControls(self.root, side=[side])), side)


class JointSets(SelectionSetsCase):

    def test_the_all_set_selects_every_joint_and_holds_an_unclaimed_one_directly(self):
        self.build_rig()
        all_set = rig_name("jointsSet")
        self.assertEqual(self.long_names(rigmodules.selection.get_members(all_set)), self.long_names(self.rig_joints()))
        direct = [m for m in self.direct_members(all_set) if cmds.nodeType(m) == "joint"]
        self.assertEqual([short(m) for m in direct], [UNCLAIMED_JOINT])

    def test_module_sets_hold_the_joints_the_module_drives(self):
        self.build_rig()
        for module, names in DRIVEN_BY.items():
            self.assertEqual(self.direct_names(module + "_jointsSet"), sorted(names))

    def test_side_sets(self):
        self.build_rig()
        self.assertEqual(self.direct_names("jointsCenterSet"), ["chest_jnt"])
        self.assertEqual(self.direct_names("jointsRightSet"), ["clavicle_r_jnt", "lowerArm_r_jnt", "upperArm_r_jnt"])


class SkinJointSets(SelectionSetsCase):

    def test_only_joints_marked_to_skin_are_in_the_skin_sets(self):
        self.build_rig(unmark_skin)
        members = rigmodules.selection.get_members(rig_name("skinJointsSet"))
        self.assertEqual(sorted(short(m) for m in members), sorted(SKIN_JOINTS_AFTER_UNMARK))
        self.assertEqual(self.direct_names("arm_r_skinJointsSet"), ["upperArm_r_jnt"])
        # the joint sets still hold the joints that don't skin
        self.assertIn("lowerArm_r_jnt", self.direct_names("arm_r_jointsSet"))

    def test_a_skin_joint_no_module_drives_is_in_the_all_set_directly(self):
        self.build_rig()
        direct = [m for m in self.direct_members(rig_name("skinJointsSet")) if cmds.nodeType(m) == "joint"]
        self.assertEqual([short(m) for m in direct], [UNCLAIMED_JOINT])

    def test_a_module_with_no_skin_joints_has_no_subset(self):
        self.build_rig(lambda joints: cmds.rigJointMarkup(joints[0], usage="chest", side="Center", export=True, skin=False, edit=True))
        self.assertEqual(cmds.ls(rig_name("spine_skinJointsSet")), [])
        self.assertEqual(cmds.ls(rig_name("spine_jointsSet")), [rig_name("spine_jointsSet")[1:]])

    def test_side_sets(self):
        self.build_rig()
        self.assertEqual(self.direct_names("skinJointsLeftSet"),
                         sorted(["clavicle_l_jnt", "upperArm_l_jnt", "lowerArm_l_jnt", UNCLAIMED_JOINT]))
        self.assertEqual(self.direct_names("skinJointsCenterSet"), ["chest_jnt"])


class Lifecycle(SelectionSetsCase):

    def test_existing_sets_of_those_names_are_replaced_and_other_sets_stay(self):
        cmds.namespace(add=":rig")
        decoy = cmds.createNode("transform", name=":rig:decoy")
        cmds.sets(decoy, name=":rig:controlsSet")
        keep = cmds.sets(decoy, name=":rig:animatorSet")
        self.build_rig()
        self.assertEqual(cmds.ls("rig:controlsSet"), ["rig:controlsSet"])
        self.assertNotIn(decoy, cmds.sets("rig:controlsSet", query=True))
        self.assertTrue(cmds.objExists(":" + keep))

    def test_undo_and_redo(self):
        self.build_rig()
        rig_sets = cmds.ls(rig_name("*Set"), type="objectSet")
        before = {s: self.long_names(rigmodules.selection.get_members(s)) for s in rig_sets}
        self.assertTrue(before)
        cmds.undo()
        self.assertEqual(set(cmds.ls(type="objectSet")), self.existing_sets)
        cmds.redo()
        rig_sets = cmds.ls(rig_name("*Set"), type="objectSet")
        self.assertEqual({s: self.long_names(rigmodules.selection.get_members(s)) for s in rig_sets}, before)
        self.assertEqual(len(cmds.listConnections(self.root + ".selectionSets", source=True, destination=False)), len(before))


class SelectionHelpers(SelectionSetsCase):

    def setUp(self):
        super().setUp()
        self.build_rig(unmark_skin)

    def names(self, joints):
        return sorted(short(j) for j in joints)

    def test_find_set_uses_markup(self):
        find = rigmodules.selection.find_set
        self.assertEqual(find(self.root, "controls"), "rig:controlsSet")
        self.assertEqual(find(self.root, "skinJoints", side="Left"), "rig:skinJointsLeftSet")
        self.assertEqual(find(self.root, "joints", module="arm_r"), "rig:arm_r_jointsSet")
        self.assertIsNone(find(self.root, "skinJoints", module="layout"))
        with self.assertRaises(RuntimeError):
            rigmodules.selection.get_set(self.root, "skinJoints", module="layout")

    def test_get_skin_joints_narrows_by_side_module_and_usage(self):
        get = rigmodules.selection.get_skin_joints
        self.assertEqual(self.names(get(self.root)), sorted(SKIN_JOINTS_AFTER_UNMARK))
        self.assertEqual(self.names(get(self.root, side="Right")), ["clavicle_r_jnt", "upperArm_r_jnt"])
        self.assertEqual(self.names(get(self.root, module="arm_l")), ["lowerArm_l_jnt", "upperArm_l_jnt"])
        self.assertEqual(self.names(get(self.root, side="Left", module="clavicle_l")), ["clavicle_l_jnt"])
        self.assertEqual(self.names(get(self.root, side="Right", module="clavicle_l")), [])
        self.assertEqual(self.names(get(self.root, usage="upperArm")), ["upperArm_l_jnt", "upperArm_r_jnt"])
        self.assertEqual(get(self.root, module="layout"), [])

    def test_select_skin_joints_replaces_or_adds(self):
        select = rigmodules.selection.select_skin_joints
        select(self.root, module="arm_l")
        self.assertEqual(self.names(cmds.ls(selection=True)), ["lowerArm_l_jnt", "upperArm_l_jnt"])
        select(self.root, module="spine", add=True)
        self.assertEqual(self.names(cmds.ls(selection=True)), ["chest_jnt", "lowerArm_l_jnt", "upperArm_l_jnt"])
        select(self.root, side="Right")
        self.assertEqual(self.names(cmds.ls(selection=True)), ["clavicle_r_jnt", "upperArm_r_jnt"])

    def test_influences_of_a_mesh(self):
        mesh = cmds.polyCube(name="body")[0]
        # nothing bound yet: every skin joint is missing and none is extra
        self.assertEqual(self.names(rigmodules.selection.get_missing_influences(self.root, mesh)),
                         sorted(SKIN_JOINTS_AFTER_UNMARK))
        self.assertEqual(rigmodules.selection.get_extra_influences(self.root, mesh), [])
        # bound to a skin joint and a joint that does not skin
        cmds.skinCluster(rig_name("chest_jnt"), rig_name("lowerArm_r_jnt"), mesh)
        self.assertEqual(self.names(rigmodules.selection.get_missing_influences(self.root, mesh)),
                         ["clavicle_l_jnt", "clavicle_r_jnt", "lowerArm_l_jnt", "upperArm_l_jnt", "upperArm_r_jnt"])
        self.assertEqual(self.names(rigmodules.selection.get_extra_influences(self.root, mesh)), ["lowerArm_r_jnt"])

    def test_a_node_that_is_not_a_rig_has_no_sets(self):
        node = cmds.createNode("transform", name="notARig")
        with self.assertRaises(RuntimeError):
            rigmodules.selection.find_set(node, "controls")
