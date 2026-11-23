"""FkHierarchy module (D71, design/fk-hierarchy.md): contract tests, pose cases from fixtures/poses/fkhierarchy, and what only this module does.

The pose cases hold hand-worked numbers (each description has the arithmetic). Nothing here is recorded from the rig.
The skeleton is a gun: root_00 with a mag (and its tip), a trigger and a handle (with a grip and its end) under it.
"""
import copy
import json

from maya import cmds

from commands.test_two_bone_ik_inverse import CapturedWarnings
from rigtest import fixtures
from rigtest.case import RigTestCase, rig_name
from rigtest.cases import add_pose_cases
from rigtest.module_contract import ModuleContract

GUN_JOINTS = ["root_00_M_JNT", "mag_00_M_JNT", "magTip_00_M_JNT", "trigger_00_M_JNT", "handle_00_M_JNT", "grip_00_M_JNT", "gripEnd_00_M_JNT"]
GUN_CONTROLS = [name.replace("_JNT", "_CTRL") for name in GUN_JOINTS]


class FkHierarchyContract(ModuleContract, RigTestCase):
    TEMPLATE = "fk_hierarchy"
    SKELETON = "fk_hierarchy"
    MODULE_TYPE = "fkHierarchyModule"
    JOINT_OWNERS = {joint: "gun_00_M_MOD" for joint in GUN_JOINTS}
    MIRROR_TEMPLATE = "fk_hierarchy_mirror"
    MIRROR_SKELETON = "fk_hierarchy_mirror"


class FkHierarchyPoses(RigTestCase):
    pass


add_pose_cases(FkHierarchyPoses, "fkhierarchy")


def _control_names():
    return sorted(n.split(":")[-1] for n in cmds.ls(rig_name("*"), type="rigControlNode") if not n.split(":")[-1].startswith("layout_"))


def _parent_of(control):
    return (cmds.listRelatives(rig_name(control), parent=True) or [""])[0].split(":")[-1]


class FkHierarchyBehaviour(RigTestCase):

    def test_one_control_per_joint_under_the_root(self):
        self.build_standard("fk_hierarchy", "fk_hierarchy")
        self.assertEqual(_control_names(), sorted(GUN_CONTROLS))

    def test_controls_form_the_joint_tree_under_the_layout(self):
        # DAG parenting does not drive transforms (D1), but animators navigate the outliner with it.
        self.build_standard("fk_hierarchy", "fk_hierarchy")
        expected = {"root_00_M_CTRL": "layout_02_M_CTRL", "mag_00_M_CTRL": "root_00_M_CTRL", "magTip_00_M_CTRL": "mag_00_M_CTRL",
                    "trigger_00_M_CTRL": "root_00_M_CTRL", "handle_00_M_CTRL": "root_00_M_CTRL",
                    "grip_00_M_CTRL": "handle_00_M_CTRL", "gripEnd_00_M_CTRL": "grip_00_M_CTRL"}
        for control, parent in expected.items():
            self.assertEqual(_parent_of(control), parent, control)

    def test_controls_are_in_depth_first_order(self):
        # Slot i is the i-th joint in depth-first order, and a parent's slot is always lower.
        self.build_standard("fk_hierarchy", "fk_hierarchy")
        module = rig_name("gun_00_M_MOD")
        slots = cmds.getAttr(module + ".controlParentIndex", multiIndices=True)
        self.assertEqual([cmds.getAttr("%s.controlParentIndex[%d]" % (module, i)) for i in slots], [-1, 0, 1, 0, 0, 4, 5])
        controls = [(cmds.listConnections("%s.controlMessage[%d]" % (module, i), source=False, destination=True) or [""])[0].split(":")[-1] for i in slots]
        self.assertEqual(controls, GUN_CONTROLS)

    def test_controls_rotate_in_xyz(self):
        self.build_standard("fk_hierarchy", "fk_hierarchy")
        for name in GUN_CONTROLS:
            self.assertEqual(cmds.getAttr(rig_name(name) + ".rotateOrder"), 0, name)

    def test_sockets_are_named_from_markup_and_the_root_is_start(self):
        self.build_standard("fk_hierarchy", "fk_hierarchy")
        module = rig_name("gun_00_M_MOD")
        names = [json.loads(cmds.getAttr("%s.socketNames[%d]" % (module, i))) for i in cmds.getAttr(module + ".socketNames", multiIndices=True)]
        self.assertEqual(names, [["center_root", "start"], ["center_mag"], ["center_magTip"], ["center_trigger"],
                                 ["center_handle"], ["center_grip"], ["center_gripEnd"]])
        plugs = [cmds.getAttr("%s.childPlugs[%d].childPlugName" % (module, i)) for i in cmds.getAttr(module + ".childPlugs", multiIndices=True)]
        self.assertEqual(plugs, ["start"])

    def test_terminators_end_their_branches(self):
        # handle and mag end the walk: they get controls, the joints below them (magTip, grip, gripEnd) do not.
        joints = self.build_standard("fk_hierarchy_terminated", "fk_hierarchy")
        self.assertEqual(_control_names(), ["handle_00_M_CTRL", "mag_00_M_CTRL", "root_00_M_CTRL", "trigger_00_M_CTRL"])
        for joint in ("magTip_00_M_JNT", "grip_00_M_JNT", "gripEnd_00_M_JNT"):
            self.assertFalse(cmds.listConnections(rig_name(joint) + ".offsetParentMatrix", source=True, destination=False),
                             "%s is below a terminator, so nothing drives it" % joint)
        self.assertEqual(len(joints), 7)

    def test_a_second_hierarchy_takes_the_branch_below_a_terminator(self):
        self.build_standard("fk_hierarchy_split", "fk_hierarchy")
        self.assertEqual(_control_names(), sorted(GUN_CONTROLS))
        self.assertEqual(_parent_of("grip_00_M_CTRL"), "handle_00_M_CTRL")

    def test_a_joint_added_to_the_skeleton_builds_and_follows_its_parent(self):
        # The template does not change: a new joint under the trigger gets a control and inherits the trigger's motion.
        joints = self.make_skeleton("fk_hierarchy")
        cmds.select(":skel:trigger_00_M_JNT")
        node = cmds.joint(name=":skel:tip_00_M_JNT", position=[3, 0, 0])
        tip = ":skel:" + node.split(":")[-1].split("|")[-1]
        cmds.rigJointMarkup(tip, usage="tip", side="Center", export=True, skin=True)
        cmds.select(clear=True)
        self.build("fk_hierarchy", joints + [tip])

        self.assertEqual(_control_names(), sorted(GUN_CONTROLS + ["tip_00_M_CTRL"]))
        self.assertEqual(_parent_of("tip_00_M_CTRL"), "trigger_00_M_CTRL")
        module = rig_name("gun_00_M_MOD")
        sockets = [json.loads(cmds.getAttr("%s.socketNames[%d]" % (module, i))) for i in cmds.getAttr(module + ".socketNames", multiIndices=True)]
        self.assertIn(["center_tip"], sockets)
        # The trigger is at (1,0,0) and the tip 2 along X from it. A quarter turn about Z puts the tip 2 along Y from the trigger.
        cmds.setAttr(rig_name("trigger_00_M_CTRL.rotateZ"), 90)
        self.assertWorldTranslation(rig_name("tip_00_M_JNT"), [1, 2, 0])

    def test_joint_without_markup_below_the_root_is_an_error(self):
        joints = self.make_skeleton("fk_hierarchy")
        cmds.select(":skel:mag_00_M_JNT")
        cmds.joint(name=":skel:unmarked_00_M_JNT", position=[0, -3, 1])
        cmds.select(clear=True)
        self.assertMayaError(lambda: self.build("fk_hierarchy", joints), "unmarked_00_M_JNT", "no joint markup")
        self.assertNothingBuilt()

    def test_unmarked_joint_below_a_terminator_is_ignored(self):
        joints = self.make_skeleton("fk_hierarchy")
        cmds.select(":skel:mag_00_M_JNT")
        cmds.joint(name=":skel:unmarked_00_M_JNT", position=[0, -3, 1])
        cmds.select(clear=True)
        self.build("fk_hierarchy_terminated", joints)
        self.assertEqual(_control_names(), ["handle_00_M_CTRL", "mag_00_M_CTRL", "root_00_M_CTRL", "trigger_00_M_CTRL"])

    def test_a_joint_under_a_group_between_two_joints_is_not_reached(self):
        # Only joint-to-joint edges count (design section 4): a joint under a locator or group under a joint is skipped.
        joints = self.make_skeleton("fk_hierarchy")
        group = cmds.group(empty=True, name=":skel:propGroup_00_M_GRP")
        cmds.parent(group, ":skel:trigger_00_M_JNT")
        cmds.select(clear=True)
        node = cmds.joint(name=":skel:hidden_00_M_JNT", position=[3, 0, 0])
        hidden = ":skel:" + node.split(":")[-1].split("|")[-1]
        cmds.parent(hidden, group)
        cmds.rigJointMarkup(hidden, usage="hidden", side="Center", export=True, skin=True)
        cmds.select(clear=True)
        self.build("fk_hierarchy", joints + [hidden])
        self.assertEqual(_control_names(), sorted(GUN_CONTROLS))

    def test_a_mirror_may_have_a_different_hierarchy(self):
        # The right side has no shoulder pad. The mirror walks what is there.
        self.build_standard("fk_hierarchy_mirror", "fk_hierarchy_asymmetric")
        controls = _control_names()
        self.assertIn("shoulderPad_00_L_CTRL", controls)
        self.assertNotIn("shoulderPad_00_R_CTRL", controls)
        for side in "LR":
            for joint in ("clavicle", "upperArm", "lowerArm"):
                self.assertIn("%s_00_%s_CTRL" % (joint, side), controls)

    def test_a_mirrored_terminator_with_no_match_is_dropped_with_a_warning(self):
        template = fixtures.load_template("fk_hierarchy_mirror")
        template["rigModules"][1]["moduleArgs"]["terminators"] = [["Left", "shoulderPad"]]
        joints = self.make_skeleton("fk_hierarchy_asymmetric")
        with CapturedWarnings() as warnings:
            self.build(template, joints)
        self.assertTrue(warnings.contain("shoulderPad"), "expected a warning naming the dropped terminator: %s" % warnings.messages)
        self.assertTrue(warnings.contain("dropped"), warnings.messages)
        controls = _control_names()
        for joint in ("clavicle", "upperArm", "lowerArm"):
            self.assertIn("%s_00_R_CTRL" % joint, controls)

    def test_a_mirror_module_written_out_takes_its_own_terminators(self):
        self.build_standard("fk_hierarchy_mirror_explicit", "fk_hierarchy_mirror")
        controls = _control_names()
        # Left stops at lowerArm and shoulderPad, so it has everything. Right stops at upperArm, so it has no lowerArm.
        for joint in ("clavicle", "upperArm", "lowerArm", "shoulderPad"):
            self.assertIn("%s_00_L_CTRL" % joint, controls)
        for joint in ("clavicle", "upperArm", "shoulderPad"):
            self.assertIn("%s_00_R_CTRL" % joint, controls)
        self.assertNotIn("lowerArm_00_R_CTRL", controls)

    def test_mirrored_sockets_come_from_the_mirrors_own_markup(self):
        self.build_standard("fk_hierarchy_mirror", "fk_hierarchy_mirror")
        module = rig_name("clavicle_00_R_MOD")
        names = [json.loads(cmds.getAttr("%s.socketNames[%d]" % (module, i))) for i in cmds.getAttr(module + ".socketNames", multiIndices=True)]
        self.assertEqual(names[0], ["right_clavicle", "start"])
        self.assertIn(["right_upperArm"], names)

    def test_every_joint_is_driven_by_the_module(self):
        self.build_standard("fk_hierarchy", "fk_hierarchy")
        for joint in GUN_JOINTS:
            self.assertOpmDrivenBy(rig_name(joint), rig_name("gun_00_M_MOD"))

    def test_a_root_alone_is_a_one_control_module(self):
        template = copy.deepcopy(fixtures.load_template("fk_hierarchy"))
        template["rigModules"][0]["moduleArgs"]["terminators"] = [["Center", "root"]]
        joints = self.make_skeleton("fk_hierarchy")
        self.build(template, joints)
        self.assertEqual(_control_names(), ["root_00_M_CTRL"])
        module = rig_name("gun_00_M_MOD")
        self.assertEqual(json.loads(cmds.getAttr(module + ".socketNames[0]")), ["center_root", "start"])

    def test_a_mirrored_module_attaches_to_the_other_sides_markup_named_socket(self):
        # The chain hangs from the hierarchy's left_upperArm socket. Its mirror hangs from right_upperArm, not from a socket named for the wrong side.
        template = {
            "rigVersion": 1, "rigAuthor": "tests", "rigTemplateName": "FkHierarchySocketMirror",
            "rigModules": [
                {"moduleType": "FkChain", "name": "spine_00_M_MOD", "side": "Center", "moduleArgs": {"joints": [["Center", "chest"]]}},
                {"moduleType": "FkHierarchy", "name": "clavicle_00_L_MOD", "side": "Left", "mirror": True,
                 "moduleArgs": {"root": ["Left", "clavicle"], "terminators": [["Left", "upperArm"]]},
                 "parentStructure": {"start": "spine_00_M_MOD.end"}},
                {"moduleType": "FkChain", "name": "forearm_00_L_MOD", "side": "Left", "mirror": True,
                 "moduleArgs": {"joints": [["Left", "lowerArm"]]},
                 "parentStructure": {"start": "clavicle_00_L_MOD.left_upperArm"}},
            ],
        }
        self.build(template, self.make_skeleton("fk_hierarchy_mirror"))
        # A quarter turn about Z sends (x, y) to (-y, x), so lowerArm, 2 along X from its upper arm, ends up 2 along Y.
        cmds.setAttr(rig_name("upperArm_00_L_CTRL.rotateZ"), 90)
        cmds.setAttr(rig_name("upperArm_00_R_CTRL.rotateZ"), 90)
        self.assertWorldTranslation(rig_name("lowerArm_00_L_JNT"), [2, 7, 0])
        self.assertWorldTranslation(rig_name("lowerArm_00_R_JNT"), [-2, 3, 0])
