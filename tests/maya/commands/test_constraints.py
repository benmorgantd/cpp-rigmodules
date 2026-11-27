"""Rig constraints: createRigConstraint, setRigConstraintSpace, setRigControlSpace and getRigControlSpace,
the constraint types and constraintWeight.

- Parent constraints and the commands: arguments, space switching, undo, and how driver ids and constraint names resolve across
  namespaces.
- The types: the driver gives only its rotation (Orient), translation (Point), translation and rotation without its twist
  (Swing) or translation and twist (Twist), and the rest comes from the child plug's parentStructure parent.
- constraintWeight: a baked 0..1 blend from "the control follows its parent" (0) to the type's full result (1). A pose that
  turns a_00_M_CTRL moves the parent alone and leaves the driver at rest, so the full constraint and the parent's own motion
  (weight 0) differ, and the answers are worked by hand. The pivot is c's joint, rotated about the origin by the parent's motion.

fk_two_chains (the types and the weight): a_00_M_JNT (0,0,0) and b_00_M_JNT (0,2,0) are fkchain_1, under layout_00_M_MOD.end.
c_00_M_JNT (1,3,0) is fkchain_2, whose start plug is parented to fkchain_01_M_MOD.end. Every constraint there is on that plug, so
its parent is b. The weight's driver is layout_00_M_MOD.end."""
import json
import math

from maya import cmds
import maya.api.OpenMaya as om

import rigmodules.query
from rigtest import fixtures
from rigtest.case import RigTestCase, rig_name


# up_00_R_CTRL drives aim_01_R_MOD.up. Its drivers must not depend on aim_1, or the constraint would form a cycle.
CONTROL = rig_name("up_00_R_CTRL")
OTHER_CONTROL = rig_name("fkchain1_00_M_CTRL")


def world_matrix(node):
    return om.MMatrix(cmds.xform(node, q=True, ws=True, matrix=True))


def zero_controls(*controls):
    for control in controls:
        cmds.setAttr(control + ".translate", 0, 0, 0)
        cmds.setAttr(control + ".rotate", 0, 0, 0)
        cmds.setAttr(control + ".scale", 1, 1, 1)


class ConstraintTestCase(RigTestCase):

    def setUp(self):
        super().setUp()
        self.build_standard("layout_rig", "layout_rig")

    def create(self, control=CONTROL, **kwargs):
        kwargs.setdefault("driver", ["layout_00_M_MOD.end", "fkchain_01_M_MOD.end"])
        return cmds.createRigConstraint(control, **kwargs)

    def assertWorldMatrix(self, node, expected, message):
        self.assertTrue(world_matrix(node).isEquivalent(expected, 1e-4), "%s: %s" % (node, message))


class Create(ConstraintTestCase):

    def test_labels_become_space_enum(self):
        self.create(label=["World", "Arm"])
        self.assertEqual(cmds.attributeQuery("space", node=CONTROL, listEnum=True), ["World:Arm"])

    def test_default_labels_are_the_driver_ids(self):
        self.create()
        self.assertEqual(cmds.attributeQuery("space", node=CONTROL, listEnum=True), ["layout_00_M_MOD.end:fkchain_01_M_MOD.end"])

    def test_initial_sets_active_driver(self):
        con = self.create(initial=1)
        self.assertEqual(cmds.getAttr(con + ".activeDriver"), 1)
        self.assertEqual(cmds.getAttr(CONTROL + ".space"), 1)

    def test_default_name_is_control_space(self):
        con = self.create()
        self.assertEqual(con.lstrip(":"), "rig:up_00_R_CON")

    def test_undo_and_redo_create(self):
        con = self.create()
        cmds.undo()
        self.assertNotExists(con)
        self.assertFalse(cmds.attributeQuery("space", node=CONTROL, exists=True), "undo left the space attribute")
        cmds.redo()
        self.assertExists(con)

    def test_does_not_move_control(self):
        before = cmds.xform(CONTROL, q=True, ws=True, t=True)
        self.create(initial=1)
        self.assertWorldTranslation(CONTROL, before)

    def test_create_on_posed_rig_does_not_move_control(self):
        # The control's current parent (fkchain_01_M_MOD.start) is posed and the new driver is not,
        # so a missing offset would show as a jump.
        cmds.setAttr(OTHER_CONTROL + ".rotateZ", 90)
        before = world_matrix(CONTROL)
        self.create(driver=["layout_00_M_MOD.end"])
        self.assertWorldMatrix(CONTROL, before, "moved when its constraint was created on a posed rig")

    def test_zero_after_create_on_posed_rig_returns_to_rest(self):
        # the compensation lives in the control's values, so zeroing every control gives the rest pose.
        rest = world_matrix(CONTROL)
        cmds.setAttr(OTHER_CONTROL + ".rotateZ", 90)
        self.create(driver=["layout_00_M_MOD.end"])
        zero_controls(OTHER_CONTROL, CONTROL)
        self.assertWorldMatrix(CONTROL, rest, "not at rest after zeroing every control")

    def test_create_at_rest_leaves_control_values(self):
        self.create(initial=1)
        self.assertEqual(cmds.getAttr(CONTROL + ".translate")[0], (0.0, 0.0, 0.0))
        self.assertEqual(cmds.getAttr(CONTROL + ".rotate")[0], (0.0, 0.0, 0.0))


class CreateRejected(ConstraintTestCase):

    def test_no_control(self):
        self.assertMayaError(lambda: cmds.createRigConstraint(driver=["layout_00_M_MOD.end"]))

    def test_two_controls(self):
        self.assertMayaError(lambda: cmds.createRigConstraint(CONTROL, OTHER_CONTROL, driver=["layout_00_M_MOD.end"]))

    def test_no_driver(self):
        self.assertMayaError(lambda: cmds.createRigConstraint(OTHER_CONTROL))

    def test_label_count_mismatch(self):
        self.assertMayaError(lambda: self.create(OTHER_CONTROL, label=["A"]))

    def test_negative_initial(self):
        self.assertMayaError(lambda: self.create(OTHER_CONTROL, initial=-1))

    def test_initial_past_last_driver(self):
        self.assertMayaError(lambda: self.create(OTHER_CONTROL, initial=2))

    def test_unknown_socket(self):
        self.assertMayaError(lambda: self.create(OTHER_CONTROL, driver=["layout_00_M_MOD.nope"]), "nope", "layout_00_M_MOD")

    def test_unknown_module(self):
        self.assertMayaError(lambda: self.create(OTHER_CONTROL, driver=["nope.end"]), "nope")

    def test_missing_control(self):
        self.assertMayaError(lambda: self.create(rig_name("nope_ctrl")), "nope_ctrl")

    def test_legacy_semicolon_socket_names_rejected(self):
        # rigs built before socket names became JSON report an error instead of guessing.
        cmds.setAttr(rig_name("fkchain_01_M_MOD") + ".socketNames[0]", "index_0;start;", type="string")
        self.assertMayaError(lambda: self.create(OTHER_CONTROL, driver=["fkchain_01_M_MOD.start"]), "Invalid socketNames[0]")


class SpaceSwitch(ConstraintTestCase):

    def setUp(self):
        super().setUp()
        self.con = self.create(label=["World", "Arm"])

    def test_by_index(self):
        cmds.setRigConstraintSpace(self.con, space=1)
        self.assertEqual(cmds.getAttr(self.con + ".activeDriver"), 1)
        self.assertEqual(cmds.getAttr(CONTROL + ".space"), 1)

    def test_by_label(self):
        cmds.setRigConstraintSpace(self.con, label="Arm")
        self.assertEqual(cmds.getAttr(self.con + ".activeDriver"), 1)

    def test_switch_does_not_move_control(self):
        # Pose the new driver away from rest first, so a missing match would show as a jump.
        cmds.setAttr(OTHER_CONTROL + ".rotateZ", 90)
        before = world_matrix(CONTROL)
        cmds.setRigConstraintSpace(self.con, label="Arm")
        self.assertWorldMatrix(CONTROL, before, "moved when its space was switched on a posed rig")

    def test_switch_on_posed_rig_writes_control_values(self):
        # the match is in the control's channels, and the constraint stores nothing per switch.
        cmds.setAttr(OTHER_CONTROL + ".rotateZ", 90)
        cmds.setRigConstraintSpace(self.con, label="Arm")
        self.assertNotEqual(cmds.getAttr(CONTROL + ".translate")[0], (0.0, 0.0, 0.0))
        self.assertFalse(cmds.attributeQuery("driverOffset", node=self.con, exists=True))

    def test_zero_after_switch_on_posed_rig_returns_to_rest(self):
        rest = world_matrix(CONTROL)
        cmds.setAttr(OTHER_CONTROL + ".rotateZ", 90)
        cmds.setRigConstraintSpace(self.con, label="Arm")
        zero_controls(OTHER_CONTROL, CONTROL)
        self.assertWorldMatrix(CONTROL, rest, "not at rest after zeroing every control")

    def test_every_space_gives_rest_at_rest(self):
        # Setting the enum directly (as a key would) does no matching. At rest every space must agree.
        rest = world_matrix(CONTROL)
        for space in (1, 0):
            cmds.setAttr(CONTROL + ".space", space)
            self.assertWorldMatrix(CONTROL, rest, "space %d does not give the rest pose" % space)

    def test_undo_switch_restores_control_values(self):
        cmds.setAttr(OTHER_CONTROL + ".rotateZ", 90)
        before = world_matrix(CONTROL)
        cmds.setRigConstraintSpace(self.con, label="Arm")
        cmds.undo()
        self.assertEqual(cmds.getAttr(CONTROL + ".space"), 0)
        self.assertEqual(cmds.getAttr(CONTROL + ".translate")[0], (0.0, 0.0, 0.0))
        self.assertWorldMatrix(CONTROL, before, "moved after undoing the switch")

    def test_locked_channel_keeps_its_value(self):
        cmds.setAttr(CONTROL + ".translateX", lock=True)
        cmds.setAttr(OTHER_CONTROL + ".rotateZ", 90)
        cmds.setRigConstraintSpace(self.con, label="Arm")
        self.assertEqual(cmds.getAttr(CONTROL + ".translateX"), 0.0)
        self.assertNotEqual(cmds.getAttr(CONTROL + ".translateY"), 0.0)

    def test_switch_does_not_set_keys(self):
        cmds.setAttr(OTHER_CONTROL + ".rotateZ", 90)
        cmds.setRigConstraintSpace(self.con, label="Arm")
        self.assertFalse(cmds.keyframe(CONTROL, query=True, keyframeCount=True))

    def test_control_follows_new_space_after_switch(self):
        cmds.setRigConstraintSpace(self.con, label="Arm")
        before = cmds.xform(CONTROL, q=True, ws=True, t=True)
        cmds.setAttr(rig_name("fkchain1_00_M_CTRL") + ".translateX", 3)
        self.assertWorldTranslation(CONTROL, [before[0] + 3, before[1], before[2]])

    def test_undo_switch(self):
        cmds.setRigConstraintSpace(self.con, label="Arm")
        cmds.undo()
        self.assertEqual(cmds.getAttr(self.con + ".activeDriver"), 0)

    def test_template_constraint_by_label(self):
        cmds.setRigConstraintSpace(rig_name("fkchain2Space_00_M_CON"), label="Aim")
        self.assertEqual(cmds.getAttr(rig_name("fkchain2Space_00_M_CON") + ".activeDriver"), 1)


class RotatedSpaceSwitch(RigTestCase):
    """The match math with rotated rests, rotated drivers and a non-default rotate order."""

    def setUp(self):
        super().setUp()
        self.build_standard("fkchain_two_chains", "fk_two_chains_rotated")
        self.child = rig_name("c_00_L_CTRL")
        self.parents = (rig_name("a_00_M_CTRL"), rig_name("b_00_M_CTRL"))
        cmds.setAttr(self.child + ".rotateOrder", lock=False)   # the module locks it; this case needs another order
        cmds.setAttr(self.child + ".rotateOrder", 2)   # zxy
        # The child's current parent is fkchain_01_M_MOD.end, so the initial driver needs no match.
        self.con = cmds.createRigConstraint(self.child, driver=["fkchain_01_M_MOD.end", "fkchain_01_M_MOD.start"])
        cmds.setAttr(self.parents[0] + ".rotateY", 40)
        cmds.setAttr(self.parents[1] + ".rotate", 30, 0, -25)
        cmds.setAttr(self.child + ".rotateX", 15)

    def test_switch_does_not_move_control(self):
        before = world_matrix(self.child)
        cmds.setRigConstraintSpace(self.con, space=1)
        self.assertTrue(world_matrix(self.child).isEquivalent(before, 1e-4),
                        "%s moved when its space was switched on a posed rig" % self.child)

    def test_zero_after_switch_returns_to_rest(self):
        cmds.setRigConstraintSpace(self.con, space=1)
        zero_controls(self.child, *self.parents)
        # Rest values come from the fk_two_chains_rotated skeleton.
        self.assertWorldTranslation(rig_name("c_00_M_JNT"), [1, 3, 0])
        self.assertWorldRotation(rig_name("c_00_M_JNT"), [20, -30, 60])


class SpaceSwitchRejected(ConstraintTestCase):

    def setUp(self):
        super().setUp()
        self.con = self.create(label=["World", "Arm"])

    def test_both_space_and_label(self):
        self.assertMayaError(lambda: cmds.setRigConstraintSpace(self.con, space=0, label="World"))

    def test_neither_space_nor_label(self):
        self.assertMayaError(lambda: cmds.setRigConstraintSpace(self.con))

    def test_unknown_label(self):
        self.assertMayaError(lambda: cmds.setRigConstraintSpace(self.con, label="Nope"), "Nope")

    def test_index_past_last_driver(self):
        self.assertMayaError(lambda: cmds.setRigConstraintSpace(self.con, space=7))

    def test_negative_index(self):
        self.assertMayaError(lambda: cmds.setRigConstraintSpace(self.con, space=-1))

    def test_not_a_constraint(self):
        self.assertMayaError(lambda: cmds.setRigConstraintSpace(CONTROL, space=0))

    def test_no_constraint_given(self):
        self.assertMayaError(lambda: cmds.setRigConstraintSpace(space=0))


class SetRigControlSpace(ConstraintTestCase):
    """setRigControlSpace on a constrained control: it matches as setRigConstraintSpace does, by control."""

    def setUp(self):
        super().setUp()
        self.con = self.create(label=["World", "Arm"])

    def test_by_index_and_by_label(self):
        cmds.setRigControlSpace(CONTROL, space=1)
        self.assertEqual(cmds.getAttr(self.con + ".activeDriver"), 1)
        self.assertEqual(cmds.getAttr(CONTROL + ".space"), 1)
        cmds.setRigControlSpace(CONTROL, label="World")
        self.assertEqual(cmds.getAttr(CONTROL + ".space"), 0)
        cmds.setRigControlSpace(CONTROL, label="Arm")
        self.assertEqual(cmds.getAttr(CONTROL + ".space"), 1)

    def test_a_posed_rig_keeps_the_control_where_it_was(self):
        cmds.setAttr(OTHER_CONTROL + ".rotateZ", 90)
        before = world_matrix(CONTROL)
        cmds.setRigControlSpace(CONTROL, label="Arm")
        self.assertWorldMatrix(CONTROL, before, "moved when its space was switched by control")
        cmds.setRigControlSpace(CONTROL, label="World")
        self.assertWorldMatrix(CONTROL, before, "moved when its space was switched back")

    def test_it_writes_what_setRigConstraintSpace_writes(self):
        cmds.setAttr(OTHER_CONTROL + ".rotateZ", 90)
        cmds.setAttr(CONTROL + ".translate", 0.5, 1, 0)
        cmds.setRigControlSpace(CONTROL, label="Arm")
        by_control = {a: cmds.getAttr(CONTROL + "." + a) for a in ("translateX", "translateY", "translateZ", "rotateX", "rotateY", "rotateZ")}
        cmds.undo()
        cmds.setRigConstraintSpace(self.con, label="Arm")
        for attribute, value in by_control.items():
            self.assertAlmostEqual(cmds.getAttr(CONTROL + "." + attribute), value, places=5, msg=attribute)

    def test_undo_and_redo(self):
        cmds.undoInfo(state=True, infinity=True)
        cmds.setAttr(OTHER_CONTROL + ".rotateZ", 90)
        before = world_matrix(CONTROL)
        cmds.setRigControlSpace(CONTROL, space=1)
        after = cmds.getAttr(CONTROL + ".translate")[0]
        self.assertNotEqual(after, (0.0, 0.0, 0.0))
        cmds.undo()
        self.assertEqual(cmds.getAttr(CONTROL + ".space"), 0)
        self.assertEqual(cmds.getAttr(CONTROL + ".translate")[0], (0.0, 0.0, 0.0))
        cmds.redo()
        self.assertEqual(cmds.getAttr(CONTROL + ".space"), 1)
        self.assertEqual(cmds.getAttr(CONTROL + ".translate")[0], after)
        self.assertWorldMatrix(CONTROL, before, "moved after redo")

    def test_a_locked_channel_keeps_its_value(self):
        cmds.setAttr(CONTROL + ".translateX", lock=True)
        cmds.setAttr(OTHER_CONTROL + ".rotateZ", 90)
        cmds.setRigControlSpace(CONTROL, label="Arm")
        self.assertEqual(cmds.getAttr(CONTROL + ".translateX"), 0.0)
        self.assertNotEqual(cmds.getAttr(CONTROL + ".translateY"), 0.0)

    def test_it_sets_no_keys(self):
        cmds.setAttr(OTHER_CONTROL + ".rotateZ", 90)
        cmds.setRigControlSpace(CONTROL, label="Arm")
        self.assertFalse(cmds.keyframe(CONTROL, query=True, keyframeCount=True))

    def test_both_space_and_label(self):
        self.assertMayaError(lambda: cmds.setRigControlSpace(CONTROL, space=0, label="World"), "exactly one")

    def test_neither_space_nor_label(self):
        self.assertMayaError(lambda: cmds.setRigControlSpace(CONTROL), "exactly one")

    def test_unknown_label_names_the_ones_it_has(self):
        self.assertMayaError(lambda: cmds.setRigControlSpace(CONTROL, label="Nope"), "Nope", "World, Arm")

    def test_index_past_the_last_space(self):
        self.assertMayaError(lambda: cmds.setRigControlSpace(CONTROL, space=2), "2 spaces")

    def test_negative_index(self):
        self.assertMayaError(lambda: cmds.setRigControlSpace(CONTROL, space=-1), "2 spaces")

    def test_a_control_with_no_space_switch(self):
        self.assertMayaError(lambda: cmds.setRigControlSpace(OTHER_CONTROL, space=0), "no space switch")

    def test_a_single_driver_constraint_has_no_space_switch(self):
        self.create(OTHER_CONTROL, driver=["layout_00_M_MOD.end"])
        self.assertMayaError(lambda: cmds.setRigControlSpace(OTHER_CONTROL, space=0), "no space switch")

    def test_the_node_must_be_a_rig_control(self):
        self.assertMayaError(lambda: cmds.setRigControlSpace(rig_name("fkchain_01_M_MOD"), space=0), "Not a rig control")
        self.assertMayaError(lambda: cmds.setRigControlSpace("missing", space=0), "Not a rig control")

    def test_no_object_is_an_error(self):
        with self.assertRaises((RuntimeError, TypeError)):
            cmds.setRigControlSpace(space=0)

    def test_it_never_reads_the_selection(self):
        cmds.select(CONTROL)
        with self.assertRaises((RuntimeError, TypeError)):
            cmds.setRigControlSpace(space=1)

    def test_a_failed_call_changes_nothing(self):
        with self.assertRaises(RuntimeError):
            cmds.setRigControlSpace(CONTROL, space=5)
        self.assertEqual(cmds.getAttr(CONTROL + ".space"), 0)


class CommandIdResolution(RigTestCase):
    """Driver ids and constraint names resolve in the constrained control's namespace."""

    NAMESPACES = ["rig", "char:rig", ":"]

    def build_in(self, namespace):
        self.build_standard("fkchain_two_chains", "fk_two_chains", namespace=namespace)
        if not cmds.namespace(exists=":elsewhere"):
            cmds.namespace(add=":elsewhere")
        return rig_name("c_00_L_CTRL", namespace)

    def test_drivers_resolve_in_control_namespace(self):
        for namespace in self.NAMESPACES:
            with self.subTest(namespace=namespace):
                self.setUp()
                control = self.build_in(namespace)
                cmds.namespace(setNamespace=":elsewhere")
                con = cmds.createRigConstraint(control, driver=["layout_00_M_MOD.end", "fkchain_01_M_MOD.start"])
                cmds.namespace(setNamespace=":")
                self.assertSource(con + ".drivers[0].driverMatrix", rig_name("layout_00_M_MOD", namespace) + ".outputSocketMatrix[2]")

    def test_constraint_node_goes_in_control_namespace(self):
        for namespace in self.NAMESPACES:
            with self.subTest(namespace=namespace):
                self.setUp()
                control = self.build_in(namespace)
                cmds.namespace(setNamespace=":elsewhere")
                default = cmds.createRigConstraint(control, driver=["layout_00_M_MOD.end"])
                cmds.undo()
                custom = cmds.createRigConstraint(control, driver=["layout_00_M_MOD.end"], name="custom_00_M_CON")
                cmds.undo()
                qualified = cmds.createRigConstraint(control, driver=["layout_00_M_MOD.end"], name=":elsewhere:qualified_00_M_CON")
                cmds.namespace(setNamespace=":")
                self.assertEqual(default.lstrip(":"), rig_name("c_00_L_CON", namespace).lstrip(":"))
                self.assertEqual(custom.lstrip(":"), rig_name("custom_00_M_CON", namespace).lstrip(":"))
                self.assertEqual(qualified.lstrip(":"), "elsewhere:qualified_00_M_CON")

    def test_redo_keeps_constraint_name_under_another_current_namespace(self):
        control = self.build_in("rig")
        con = cmds.createRigConstraint(control, driver=["layout_00_M_MOD.end"])
        cmds.undo()
        # MNamespace, not cmds.namespace: the cmds version is undoable and would clear the redo queue.
        om.MNamespace.setCurrentNamespace(":elsewhere")
        cmds.redo()
        om.MNamespace.setCurrentNamespace(":")
        self.assertExists(con)
        self.assertEqual(cmds.ls("elsewhere:*"), [])

    def test_qualified_driver_used_as_given(self):
        control = self.build_in("rig")
        con = cmds.createRigConstraint(control, driver=["rig:layout_00_M_MOD.end"])
        self.assertSource(con + ".drivers[0].driverMatrix", "rig:layout_00_M_MOD.outputSocketMatrix[2]")

    def test_old_layout_module_name_rejected(self):
        control = self.build_in("rig")
        self.assertMayaError(lambda: cmds.createRigConstraint(control, driver=["layoutModule.end"]), "layoutModule")


class GetRigControlSpace(ConstraintTestCase):
    """getRigControlSpace and rigmodules.query.find_control_space: a control's constraint, labels and current space."""

    def test_reports_the_constraint_the_labels_and_the_current_space(self):
        con = self.create(label=["World", "Arm"], initial=1)
        found = json.loads(cmds.getRigControlSpace(CONTROL))
        self.assertEqual(found, {"constraint": con.lstrip(":"), "module": "", "labels": ["World", "Arm"], "current": 1})

    def test_labels_default_to_the_driver_ids(self):
        self.create()
        self.assertEqual(json.loads(cmds.getRigControlSpace(CONTROL))["labels"], ["layout_00_M_MOD.end", "fkchain_01_M_MOD.end"])

    def test_current_follows_a_space_switch(self):
        con = self.create(label=["World", "Arm"])
        cmds.setRigConstraintSpace(con, label="Arm")
        self.assertEqual(json.loads(cmds.getRigControlSpace(CONTROL))["current"], 1)
        cmds.setRigConstraintSpace(con, space=0)
        self.assertEqual(json.loads(cmds.getRigControlSpace(CONTROL))["current"], 0)

    def test_the_constraint_it_returns_is_the_one_setRigConstraintSpace_takes(self):
        self.create(label=["World", "Arm"])
        found = json.loads(cmds.getRigControlSpace(CONTROL))
        cmds.setRigConstraintSpace(found["constraint"], label="Arm")
        self.assertEqual(cmds.getAttr(CONTROL + ".space"), 1)

    def test_a_control_with_no_space_switch_gives_an_empty_string(self):
        self.assertEqual(cmds.getRigControlSpace(OTHER_CONTROL), "")

    def test_a_single_driver_constraint_has_no_space_switch(self):
        """With one driver there is no `space` enum, so the control has nothing to switch."""
        self.create(driver=["layout_00_M_MOD.end"])
        self.assertEqual(cmds.getRigControlSpace(CONTROL), "")

    def test_it_is_not_undoable_and_changes_nothing(self):
        self.create()
        cmds.undoInfo(state=True, infinity=True)
        cmds.getRigControlSpace(CONTROL)
        self.assertNotIn("getRigControlSpace", cmds.undoInfo(query=True, undoName=True) or "")

    def test_the_node_must_be_a_rig_control(self):
        self.assertMayaError(lambda: cmds.getRigControlSpace(rig_name("fkchain_01_M_MOD")), "Not a rig control")
        self.assertMayaError(lambda: cmds.getRigControlSpace("missing"), "Not a rig control")

    def test_it_takes_exactly_one_control(self):
        self.assertMayaError(lambda: cmds.getRigControlSpace(CONTROL, OTHER_CONTROL))
        self.assertMayaError(lambda: cmds.getRigControlSpace())

    def test_the_python_query(self):
        con = self.create(label=["World", "Arm"], initial=1)
        space = rigmodules.query.find_control_space(CONTROL)
        self.assertEqual(space, rigmodules.query.ControlSpace(constraint=con.lstrip(":"), labels=("World", "Arm"), current=1))
        self.assertIsNone(rigmodules.query.find_control_space(OTHER_CONTROL))
        with self.assertRaises(RuntimeError):
            rigmodules.query.find_control_space(rig_name("fkchain_01_M_MOD"))


CHILD_CTRL = rig_name("c_00_L_CTRL")
CHILD_JNT = rig_name("c_00_M_JNT")
A_CTRL = rig_name("a_00_M_CTRL")
LAYOUT_END_CTRL = rig_name("layout_02_M_CTRL")


class ConstraintTypeCase(RigTestCase):

    SKELETON = "fk_two_chains"

    def setUp(self):
        super().setUp()
        self.build_standard("fkchain_two_chains", self.SKELETON)

    def constrain(self, constraint_type, driver=("layout_00_M_MOD.end",)):
        return cmds.createRigConstraint(CHILD_CTRL, driver=list(driver), type=constraint_type)

    def assertWorldMatrix(self, node, expected, message):
        self.assertTrue(world_matrix(node).isEquivalent(expected, 1e-4), "%s: %s" % (node, message))


class Orient(ConstraintTypeCase):

    def test_type_is_stored_on_the_node(self):
        con = self.constrain("Orient")
        self.assertEqual(cmds.getAttr(con + ".constraintType", asString=True), "Orient")

    def test_pivot_follows_parent_rotation_does_not(self):
        # a rotates 90 about Z at the origin: c's pivot swings from (1,3,0) to (-3,1,0), and c keeps the layout's rotation.
        self.constrain("Orient")
        cmds.setAttr(A_CTRL + ".rotateZ", 90)
        self.assertWorldTranslation(CHILD_JNT, [-3, 1, 0])
        self.assertWorldRotation(CHILD_JNT, [0, 0, 0])

    def test_rotation_follows_driver(self):
        # layout_00_M_MOD.end also parents fkchain_1, so its move carries the pivot too: (1,3,0) about Y 90 is (0,3,-1), then +5 in X.
        self.constrain("Orient")
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateY", 90)
        cmds.setAttr(LAYOUT_END_CTRL + ".translateX", 5)
        self.assertWorldTranslation(CHILD_JNT, [5, 3, -1])
        self.assertWorldRotation(CHILD_JNT, [0, 90, 0])

    def test_scale_comes_from_parent(self):
        self.constrain("Orient")
        cmds.setAttr(A_CTRL + ".scale", 2, 2, 2)
        self.assertWorldTranslation(CHILD_JNT, [2, 6, 0])
        self.assertWorldScale(CHILD_JNT, [2, 2, 2])

    def test_template_type(self):
        template = fixtures.load_template("fkchain_two_chains")
        template["constraints"] = [{"name": "cSpace_00_M_CON", "type": "Orient", "target": "fkchain_02_L_MOD.start",
                                    "drivers": [{"socket": "layout_00_M_MOD.end", "label": "World", "weight": 1.0}]}]
        cmds.file(new=True, force=True)
        self.build(template, self.make_skeleton(self.SKELETON))
        self.assertEqual(cmds.getAttr(rig_name("cSpace_00_M_CON") + ".constraintType", asString=True), "Orient")
        cmds.setAttr(A_CTRL + ".rotateZ", 90)
        self.assertWorldTranslation(CHILD_JNT, [-3, 1, 0])
        self.assertWorldRotation(CHILD_JNT, [0, 0, 0])


class Point(ConstraintTypeCase):

    def test_rotation_follows_parent_pivot_does_not(self):
        self.constrain("Point")
        cmds.setAttr(A_CTRL + ".rotateZ", 90)
        self.assertWorldTranslation(CHILD_JNT, [1, 3, 0])
        self.assertWorldRotation(CHILD_JNT, [0, 0, 90])

    def test_translation_follows_driver(self):
        # fkchain_1 rides layout_00_M_MOD.end too, so the parent's rotation is the layout's.
        self.constrain("Point")
        cmds.setAttr(LAYOUT_END_CTRL + ".translateX", 5)
        self.assertWorldTranslation(CHILD_JNT, [6, 3, 0])
        self.assertWorldRotation(CHILD_JNT, [0, 0, 0])

    def test_scale_comes_from_parent(self):
        self.constrain("Point")
        cmds.setAttr(A_CTRL + ".scale", 2, 2, 2)
        self.assertWorldTranslation(CHILD_JNT, [1, 3, 0])
        self.assertWorldScale(CHILD_JNT, [2, 2, 2])


class Swing(ConstraintTypeCase):
    """The driver's translation and its rotation without the twist about X (design/ribbon-twist-follow.md, phase 3).

    The driver here is layout_00_M_MOD.end, a control at the origin, and c's pivot is at (1,3,0), so as with Point the pivot swings about
    the origin with the driver's whole rotation, twist included (rotateX 90 takes (1,3,0) to (1,0,3), rotateZ 90 to (-3,1,0)).
    Only c's own rotation loses the twist. Its rotate order is xyz, so rotateX is applied first and is the twist, and rotateY and
    rotateZ after it are the swing: twist * swing."""

    def test_type_is_stored_on_the_node(self):
        con = self.constrain("Swing")
        self.assertEqual(cmds.getAttr(con + ".constraintType", asString=True), "Swing")

    def test_a_pure_twist_is_dropped(self):
        # Orient would turn c 90 about X. Swing keeps none of it in c's rotation, and the pivot still swings with the driver.
        self.constrain("Swing")
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateX", 90)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 0])
        self.assertWorldTranslation(CHILD_JNT, [1, 0, 3])

    def test_a_pure_swing_is_kept(self):
        self.constrain("Swing")
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateZ", 90)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 90])
        self.assertWorldTranslation(CHILD_JNT, [-3, 1, 0])

    def test_a_twist_and_a_swing_give_the_swing(self):
        # rotateX 60 then rotateZ 40 is twist 60 * swing Rz(40): the result is the swing alone.
        self.constrain("Swing")
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateX", 60)
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateZ", 40)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 40])

    def test_a_twist_past_half_a_turn_is_the_same_rotation(self):
        # 200 degrees is -160: either way the whole rotation is twist, so no swing is left.
        self.constrain("Swing")
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateX", 200)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 0])

    def test_translation_follows_the_driver(self):
        self.constrain("Swing")
        cmds.setAttr(LAYOUT_END_CTRL + ".translateX", 5)
        self.assertWorldTranslation(CHILD_JNT, [6, 3, 0])

    def test_scale_comes_from_parent(self):
        self.constrain("Swing")
        cmds.setAttr(A_CTRL + ".scale", 2, 2, 2)
        self.assertWorldTranslation(CHILD_JNT, [1, 3, 0])
        self.assertWorldScale(CHILD_JNT, [2, 2, 2])

    def test_template_type(self):
        template = fixtures.load_template("fkchain_two_chains")
        template["constraints"] = [{"name": "cSwing_00_M_CON", "type": "Swing", "target": "fkchain_02_L_MOD.start",
                                    "drivers": [{"socket": "layout_00_M_MOD.end", "label": "World", "weight": 1.0}]}]
        cmds.file(new=True, force=True)
        self.build(template, self.make_skeleton(self.SKELETON))
        self.assertEqual(cmds.getAttr(rig_name("cSwing_00_M_CON") + ".constraintType", asString=True), "Swing")
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateX", 60)
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateZ", 40)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 40])


class Twist(ConstraintTypeCase):
    """The driver's translation and its twist about X, on the swing of the plug's parent (design/limb.md, the wrist).

    The driver is layout_00_M_MOD.end at the origin, which also carries the parent (fkchain_1) with it, and a_00_M_CTRL moves the
    parent alone. c's pivot is at (1,3,0) and its rest rotation is identity, so the child's own X is the world X. The parent's
    motion is its twist about X then its swing; c takes the driver's twist in place of the parent's."""

    def test_type_is_stored_on_the_node(self):
        con = self.constrain("Twist")
        self.assertEqual(cmds.getAttr(con + ".constraintType", asString=True), "Twist")

    def test_a_driver_twist_is_kept_and_the_pivot_swings_with_the_driver(self):
        # The layout carries the parent too, so the parent's twist is the driver's: c turns 90 about X, and its pivot goes from
        # (1,3,0) to (1,0,3) as with Point.
        self.constrain("Twist")
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateX", 90)
        self.assertWorldRotation(CHILD_JNT, [90, 0, 0])
        self.assertWorldTranslation(CHILD_JNT, [1, 0, 3])

    def test_the_swing_comes_from_the_parent_and_not_the_driver(self):
        # a turns the parent alone by 90 about Z. The driver doesn't move, so c's rotation is the parent's swing and its pivot
        # (the driver's) stays at rest.
        self.constrain("Twist")
        cmds.setAttr(A_CTRL + ".rotateZ", 90)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 90])
        self.assertWorldTranslation(CHILD_JNT, [1, 3, 0])

    def test_the_parents_twist_is_dropped(self):
        # a twists the parent by 60 about X. The driver has no twist, so c has none (Point would turn it 60).
        self.constrain("Twist")
        cmds.setAttr(A_CTRL + ".rotateX", 60)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 0])
        self.assertWorldTranslation(CHILD_JNT, [1, 3, 0])

    def test_the_drivers_twist_replaces_the_parents(self):
        # The layout twists driver and parent by 60 and a twists the parent by 30 more: the parent has 90, the driver 60, and c
        # takes 60. The pivot is the driver's: (1,3,0) turned 60 about X is (1, 1.5, 2.598).
        self.constrain("Twist")
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateX", 60)
        cmds.setAttr(A_CTRL + ".rotateX", 30)
        self.assertWorldRotation(CHILD_JNT, [60, 0, 0])
        self.assertWorldTranslation(CHILD_JNT, [1, 1.5, 3 * math.sin(math.radians(60))])

    def test_a_twist_and_a_swing_on_the_parent_with_the_same_twist_are_the_parents_rotation(self):
        # a swings the parent 40 about Z, then the layout twists it (and the driver) 60 about X: the driver's twist is the
        # parent's, so c has the parent's whole rotation, Rz(40) then Rx(60).
        self.constrain("Twist")
        cmds.setAttr(A_CTRL + ".rotateZ", 40)
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateX", 60)
        expected = (om.MEulerRotation(0, 0, math.radians(40)).asMatrix() * om.MEulerRotation(math.radians(60), 0, 0).asMatrix())
        actual = om.MTransformationMatrix(world_matrix(CHILD_JNT)).asRotateMatrix()
        self.assertTrue(actual.isEquivalent(expected, 1e-4), "c's rotation is not the parent's Rz(40) * Rx(60)")

    def test_translation_follows_the_driver(self):
        self.constrain("Twist")
        cmds.setAttr(LAYOUT_END_CTRL + ".translateX", 5)
        self.assertWorldTranslation(CHILD_JNT, [6, 3, 0])

    def test_scale_comes_from_parent(self):
        self.constrain("Twist")
        cmds.setAttr(A_CTRL + ".scale", 2, 2, 2)
        self.assertWorldTranslation(CHILD_JNT, [1, 3, 0])
        self.assertWorldScale(CHILD_JNT, [2, 2, 2])

    def test_template_type(self):
        template = fixtures.load_template("fkchain_two_chains")
        template["constraints"] = [{"name": "cTwist_00_M_CON", "type": "Twist", "target": "fkchain_02_L_MOD.start",
                                    "drivers": [{"socket": "layout_00_M_MOD.end", "label": "World", "weight": 1.0}]}]
        cmds.file(new=True, force=True)
        self.build(template, self.make_skeleton(self.SKELETON))
        self.assertEqual(cmds.getAttr(rig_name("cTwist_00_M_CON") + ".constraintType", asString=True), "Twist")
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateX", 60)
        cmds.setAttr(A_CTRL + ".rotateX", 30)
        self.assertWorldRotation(CHILD_JNT, [60, 0, 0])


class ArmRibbonCase(RigTestCase):
    """An FK arm along X with a ribbon on each bone (the TEMPLATE decides how the ribbons are wired).

    With rotate order xyz, rotateX on a control is the twist about the bone and rotateZ the swing, so a control's rotation is
    Rx(t) * Rz(s) in row order."""

    # The arm drops 0.3 over 4 (a 4.3 degree tilt of the bone from X, which the TwoBoneIk needs for its bend), so a twist about
    # the tangent differs from one about X by a few degrees at the middle joints. The wrong answers differ by tens of degrees.
    TOLERANCE_DEGREES = 5.0
    TEMPLATE = "ribbon_arm_swing"
    JOINTS = ("up1_00_M_JNT", "up2_00_M_JNT", "up3_00_M_JNT", "up4_00_M_JNT", "up5_00_M_JNT", "fo1_00_M_JNT")

    def setUp(self):
        super().setUp()
        self.start("ribbon_arm")

    def start(self, skeleton):
        cmds.file(new=True, force=True)
        self.build_standard(self.TEMPLATE, skeleton)
        cmds.setAttr(rig_name("armSettings_00_L_CTRL.ikFkBlend"), 0)   # FK: the shoulder control's rotation reaches the elbow socket
        self.rest = {joint: world_matrix(rig_name(joint)) for joint in self.JOINTS}

    def pose(self, twist, swing, control="upperArm_00_L_CTRL"):
        cmds.setAttr(rig_name(control + ".rotateX"), twist)
        cmds.setAttr(rig_name(control + ".rotateZ"), swing)

    def motion(self, joint):
        """The joint's motion in its own rest frame, world * rest^-1: what its control's channels would hold. A rotation about the
        joint's own X is a twist about its bone, whichever way the bone points in the world."""
        return world_matrix(rig_name(joint)) * self.rest[joint].inverse()

    def twist_of(self, joint):
        q = om.MTransformationMatrix(self.motion(joint)).rotation(asQuaternion=True)
        return math.degrees(2.0 * math.atan2(q.x, q.w))

    def assertTurnedBy(self, joint, twist, swing):
        expected = om.MEulerRotation(om.MAngle(twist, om.MAngle.kDegrees).asRadians(), 0, om.MAngle(swing, om.MAngle.kDegrees).asRadians()).asMatrix()
        relative = om.MTransformationMatrix(self.motion(joint) * expected.inverse()).rotation(asQuaternion=True)
        angle = math.degrees(2.0 * math.acos(min(1.0, abs(relative.w))))
        self.assertLess(angle, self.TOLERANCE_DEGREES, "%s is %.2f degrees from twist %s then swing %s" % (joint, angle, twist, swing))

    def angle_moved(self, joint):
        """The angle, in degrees, the joint has turned from its rest rotation."""
        q = om.MTransformationMatrix(self.motion(joint)).rotation(asQuaternion=True)
        return math.degrees(2.0 * math.acos(min(1.0, abs(q.w))))


class SwingShoulder(ArmRibbonCase):
    """The shoulder the Swing constraint is for (design/ribbon-twist-follow.md). The upper ribbon hangs from the shoulder's socket
    twice, a Swing constraint on its start (the shoulder's position and swing, no twist) and a Point constraint on its end (the
    elbow's position, and the shoulder's whole rotation from the plug's parent). The shoulder's twist then eases in along the
    upper arm, and the forearm, which starts on the elbow socket, meets it.

    The upper ribbon's first joint should hold Rz(s) and its last Rx(t) * Rz(s)."""

    def test_a_twist_eases_in_from_nothing_at_the_shoulder_to_the_whole_twist_at_the_elbow(self):
        """The twist of each joint about its own X: within a couple of degrees of the twist's share at the joint's place along the
        ribbon."""
        for twist in (60, 170, -170):
            self.pose(twist, 0)
            for joint, share in (("up1_00_M_JNT", 0.0), ("up2_00_M_JNT", 0.25), ("up3_00_M_JNT", 0.5), ("up4_00_M_JNT", 0.75), ("up5_00_M_JNT", 1.0)):
                self.assertAlmostEqual(math.remainder(self.twist_of(joint) - twist * share, 360.0), 0.0, delta=2.0, msg="twist %s, %s" % (twist, joint))

    def test_a_swing_alone_turns_every_joint_by_the_swing(self):
        self.pose(0, 40)
        for joint in ("up1_00_M_JNT", "up3_00_M_JNT", "up5_00_M_JNT", "fo1_00_M_JNT"):
            self.assertTurnedBy(joint, 0, 40)

    def test_the_start_takes_the_swing_and_not_the_twist_and_the_end_takes_both(self):
        self.pose(60, 40)
        self.assertTurnedBy("up1_00_M_JNT", 0, 40)
        self.assertTurnedBy("up3_00_M_JNT", 30, 40)
        self.assertTurnedBy("up5_00_M_JNT", 60, 40)

    def test_the_forearm_meets_the_upper_arm_at_the_elbow(self):
        self.pose(60, 40)
        self.assertTurnedBy("fo1_00_M_JNT", 60, 40)

    def test_a_tilted_arm_is_the_same_whichever_way_its_bone_points(self):
        """The same poses on an arm turned 35 degrees about Z, every joint turned with it (+X down the bone). A delta is a motion
        in world axes, so a twist read about the world X would be wrong here: it must be read about the bone."""
        self.start("ribbon_arm_tilted")
        self.pose(60, 0)
        for joint, share in (("up1_00_M_JNT", 0.0), ("up3_00_M_JNT", 0.5), ("up5_00_M_JNT", 1.0)):
            self.assertAlmostEqual(self.twist_of(joint), 60 * share, delta=2.0, msg=joint)
        self.pose(60, 40)
        self.assertTurnedBy("up1_00_M_JNT", 0, 40)
        self.assertTurnedBy("up5_00_M_JNT", 60, 40)
        self.assertTurnedBy("fo1_00_M_JNT", 60, 40)

    def test_without_the_swing_constraint_the_whole_arm_turns_by_the_twist(self):
        """Parenting the start on the shoulder alone carries its twist rigidly (the probe's 'no ease' wiring): every joint takes it."""
        data = fixtures.load_template("ribbon_arm_swing")
        data["constraints"] = [c for c in data["constraints"] if c["name"] != "upperStartSwing_00_M_CON"]
        cmds.file(new=True, force=True)
        self.build(data, self.make_skeleton("ribbon_arm"), label="ribbon_arm_no_swing")
        cmds.setAttr(rig_name("armSettings_00_L_CTRL.ikFkBlend"), 0)
        self.rest = {joint: world_matrix(rig_name(joint)) for joint in self.JOINTS}
        self.pose(60, 0)
        for joint in ("up1_00_M_JNT", "up3_00_M_JNT", "up5_00_M_JNT"):
            self.assertTurnedBy(joint, 60, 0)


class TwistWrist(ArmRibbonCase):
    """The wrist the Twist constraint is for (design/limb.md): the forearm ribbon's end hangs from the elbow socket and a Twist
    constraint driven by the wrist gives it the wrist's position and its twist. The hand's swing then never reaches the ribbon's
    end, so it can't bend the spline and move the middle control, and the hand's twist eases in along the forearm."""

    TEMPLATE = "ribbon_arm_twist"
    FOREARM = ("fo1_00_M_JNT", "fo2_00_M_JNT", "fo3_00_M_JNT", "fo4_00_M_JNT", "fo5_00_M_JNT")
    JOINTS = ArmRibbonCase.JOINTS + FOREARM[1:]

    def test_the_hands_twist_eases_in_from_the_elbow_to_the_wrist(self):
        for twist in (60, 170, -170):
            self.pose(twist, 0, control="hand_00_L_CTRL")
            for joint, share in zip(self.FOREARM, (0.0, 0.25, 0.5, 0.75, 1.0)):
                self.assertAlmostEqual(math.remainder(self.twist_of(joint) - twist * share, 360.0), 0.0, delta=2.0,
                                       msg="twist %s, %s" % (twist, joint))

    def test_the_hands_swing_does_not_move_the_forearm_ribbon(self):
        self.pose(0, 40, control="hand_00_L_CTRL")
        for joint in self.FOREARM:
            self.assertLess(self.angle_moved(joint), 1.0, joint)
            self.assertTrue(world_matrix(rig_name(joint)).isEquivalent(self.rest[joint], 1e-3), "%s moved with the hand's swing" % joint)

    def test_the_hands_swing_with_a_twist_leaves_only_the_twist(self):
        self.pose(60, 40, control="hand_00_L_CTRL")
        self.assertTurnedBy("fo1_00_M_JNT", 0, 0)
        self.assertTurnedBy("fo3_00_M_JNT", 30, 0)
        self.assertTurnedBy("fo5_00_M_JNT", 60, 0)

    def test_the_elbows_swing_turns_the_whole_forearm(self):
        self.pose(0, 40, control="upperArm_00_L_CTRL")
        for joint in self.FOREARM:
            self.assertTurnedBy(joint, 0, 40)

    def test_the_shoulders_twist_turns_the_whole_forearm_rigidly(self):
        # The wrist's twist about the bone is the shoulder's, and so is the elbow's: no ease between them.
        self.pose(60, 0, control="upperArm_00_L_CTRL")
        for joint in self.FOREARM:
            self.assertTurnedBy(joint, 60, 0)

    def test_without_the_constraint_the_hands_swing_bends_the_forearm_ribbon(self):
        """The wiring this replaces: the end hangs from the wrist socket, so the hand's swing turns the ribbon's end frame."""
        cmds.file(new=True, force=True)
        self.build_standard("ribbon_arm_swing", "ribbon_arm")
        cmds.setAttr(rig_name("armSettings_00_L_CTRL.ikFkBlend"), 0)
        self.rest = {joint: world_matrix(rig_name(joint)) for joint in self.JOINTS}
        self.pose(0, 40, control="hand_00_L_CTRL")
        self.assertGreater(self.angle_moved("fo5_00_M_JNT"), 20.0)


class TypedSpaceSwitch(ConstraintTypeCase):
    """Space-switch matching with the node's own output for each driver."""

    def pose(self):
        cmds.setAttr(A_CTRL + ".rotate", 10, 20, 30)
        cmds.setAttr(rig_name("b_00_M_CTRL") + ".rotateX", -40)
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateY", 25)

    def test_switch_does_not_move_control(self):
        for constraint_type in ("Orient", "Point", "Swing", "Twist"):
            with self.subTest(type=constraint_type):
                self.setUp()
                con = self.constrain(constraint_type, ["layout_00_M_MOD.end", "fkchain_01_M_MOD.start"])
                self.pose()
                before = world_matrix(CHILD_CTRL)
                cmds.setRigConstraintSpace(con, space=1)
                self.assertWorldMatrix(CHILD_CTRL, before, "moved on a %s space switch" % constraint_type)

    def test_orient_switch_writes_only_rotate(self):
        con = self.constrain("Orient", ["layout_00_M_MOD.end", "fkchain_01_M_MOD.start"])
        self.pose()
        cmds.setRigConstraintSpace(con, space=1)
        for value in cmds.getAttr(CHILD_CTRL + ".translate")[0]:
            self.assertAlmostEqual(value, 0.0, places=4)
        self.assertNotEqual(cmds.getAttr(CHILD_CTRL + ".rotate")[0], (0.0, 0.0, 0.0))

    def test_point_switch_writes_only_translate(self):
        con = self.constrain("Point", ["layout_00_M_MOD.end", "fkchain_01_M_MOD.start"])
        self.pose()
        cmds.setRigConstraintSpace(con, space=1)
        for value in cmds.getAttr(CHILD_CTRL + ".rotate")[0]:
            self.assertAlmostEqual(value, 0.0, places=4)
        self.assertNotEqual(cmds.getAttr(CHILD_CTRL + ".translate")[0], (0.0, 0.0, 0.0))

    def test_create_on_posed_rig_does_not_move_control(self):
        self.pose()
        before = world_matrix(CHILD_CTRL)
        self.constrain("Orient")
        self.assertWorldMatrix(CHILD_CTRL, before, "moved when an Orient constraint was created on a posed rig")

    def test_every_space_gives_rest_at_rest(self):
        rest = world_matrix(CHILD_CTRL)
        self.constrain("Orient", ["layout_00_M_MOD.end", "fkchain_01_M_MOD.start"])
        for space in (1, 0):
            cmds.setAttr(CHILD_CTRL + ".space", space)
            self.assertWorldMatrix(CHILD_CTRL, rest, "space %d does not give the rest pose" % space)


class MatchesMayaConstraints(ConstraintTypeCase):
    """Rotated rests and an animated child, against Maya's own point and orient constraints on a group.

    The point constraint targets a locator parented to the socket at c's rest position, with no offset, so the pivot
    swings with the socket. maintainOffset on a point constraint stores a world offset and would only slide it."""

    SKELETON = "fk_two_chains_rotated"

    def socket_follower(self, socket_plug, name):
        node = cmds.createNode("transform", name=name)
        cmds.connectAttr(socket_plug, node + ".offsetParentMatrix")
        return node

    def reference(self, con, constraint_type):
        """A group that follows c's rest under the same rules, and a child carrying c's control values."""
        parent = self.socket_follower(cmds.listConnections(con + ".parentMatrix", plugs=True)[0], "ref_parent")
        driver = self.socket_follower(cmds.listConnections(con + ".drivers[0].driverMatrix", plugs=True)[0], "ref_driver")
        rest = cmds.xform(CHILD_JNT, q=True, ws=True, matrix=True)
        translate_source, rotate_source = (parent, driver) if constraint_type == "Orient" else (driver, parent)

        pivot = cmds.createNode("transform", name="ref_pivot", parent=translate_source)
        cmds.xform(pivot, ws=True, matrix=rest)
        group = cmds.createNode("transform", name="ref_group")
        cmds.xform(group, ws=True, matrix=rest)
        cmds.pointConstraint(pivot, group)
        cmds.orientConstraint(rotate_source, group, maintainOffset=True)

        child = cmds.createNode("transform", name="ref_child", parent=group)
        cmds.connectAttr(CHILD_CTRL + ".translate", child + ".translate")
        cmds.connectAttr(CHILD_CTRL + ".rotate", child + ".rotate")
        cmds.connectAttr(CHILD_CTRL + ".rotateOrder", child + ".rotateOrder")
        return child

    def test_matches_point_and_orient_constraint_rig(self):
        for constraint_type in ("Orient", "Point"):
            with self.subTest(type=constraint_type):
                self.setUp()
                con = self.constrain(constraint_type)
                reference = self.reference(con, constraint_type)
                cmds.setAttr(A_CTRL + ".rotate", 15, -35, 50)
                cmds.setAttr(rig_name("b_00_M_CTRL") + ".rotate", 70, 10, -20)
                cmds.setAttr(rig_name("b_00_M_CTRL") + ".translate", 0.5, 0, 1)
                cmds.setAttr(LAYOUT_END_CTRL + ".rotate", -30, 60, 5)
                cmds.setAttr(LAYOUT_END_CTRL + ".translate", 2, 1, -3)
                cmds.setAttr(CHILD_CTRL + ".rotate", 5, 40, -15)
                cmds.setAttr(CHILD_CTRL + ".translate", 0.3, -0.2, 0.7)
                self.assertWorldMatrix(CHILD_JNT, world_matrix(reference),
                                       "%s differs from Maya's point and orient constraints" % constraint_type)


class TypeRejected(ConstraintTypeCase):

    def test_unknown_type(self):
        self.assertMayaError(lambda: self.constrain("Scale"), "Scale", "(has: Parent, Orient, Point, Swing, Twist)")

    def test_orient_without_parent(self):
        cmds.disconnectAttr(rig_name("fkchain_01_M_MOD") + ".outputSocketMatrix[1]",
                            rig_name("fkchain_02_L_MOD") + ".childPlugs[0].childPlugParentMatrix")
        self.assertMayaError(lambda: self.constrain("Orient"), "no parent")

    def test_parent_without_parent_is_allowed(self):
        cmds.disconnectAttr(rig_name("fkchain_01_M_MOD") + ".outputSocketMatrix[1]",
                            rig_name("fkchain_02_L_MOD") + ".childPlugs[0].childPlugParentMatrix")
        self.assertExists(self.constrain("Parent"))


SIN60 = 3 * math.sin(math.radians(60))


class ConstraintWeightCase(RigTestCase):

    SKELETON = "fk_two_chains"

    def setUp(self):
        super().setUp()
        self.build_standard("fkchain_two_chains", self.SKELETON)

    def constrain(self, constraint_type, weight=None, driver=("layout_00_M_MOD.end",)):
        flags = {} if weight is None else {"constraintWeight": weight}
        return cmds.createRigConstraint(CHILD_CTRL, driver=list(driver), type=constraint_type, **flags)

    def world(self):
        return om.MMatrix(cmds.xform(CHILD_JNT, q=True, ws=True, matrix=True))


class Weight(ConstraintWeightCase):

    def test_default_is_one_and_is_stored_on_the_node(self):
        self.assertAlmostEqual(cmds.getAttr(self.constrain("Orient") + ".constraintWeight"), 1.0)
        self.setUp()
        self.assertAlmostEqual(cmds.getAttr(self.constrain("Orient", 0.25) + ".constraintWeight"), 0.25)

    def test_it_is_not_an_animator_channel(self):
        con = self.constrain("Twist", 0.5)
        self.assertFalse(cmds.getAttr(con + ".constraintWeight", keyable=True))

    def test_weight_one_is_the_full_constraint(self):
        self.constrain("Twist", 1.0)
        cmds.setAttr(A_CTRL + ".rotateX", 60)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 0])
        self.assertWorldTranslation(CHILD_JNT, [1, 3, 0])

    def test_weight_zero_follows_the_parent_for_every_type(self):
        """The same pose with no constraint and with a weight of 0 gives the same world matrix, for every type, Parent included."""
        pose = {A_CTRL + ".rotate": (15, -35, 50), LAYOUT_END_CTRL + ".rotateY": 25, LAYOUT_END_CTRL + ".translate": (2, 1, -3)}

        def apply():
            for plug, value in pose.items():
                if isinstance(value, tuple):
                    cmds.setAttr(plug, *value)
                else:
                    cmds.setAttr(plug, value)

        apply()
        expected = self.world()
        for constraint_type in ("Parent", "Orient", "Point", "Swing", "Twist"):
            with self.subTest(type=constraint_type):
                self.setUp()
                self.constrain(constraint_type, 0.0)
                apply()
                self.assertTrue(self.world().isEquivalent(expected, 1e-4), "%s at weight 0 is not the unconstrained pose" % constraint_type)

    def test_twist_halfway_takes_half_the_parents_twist(self):
        # a twists the parent 60 about X, the driver has none. Full Twist: no rotation, pivot at rest. Weight 0: the parent's 60
        # and its pivot (1, 1.5, 2.598). Halfway: 30 about X and the pivot between.
        self.constrain("Twist", 0.5)
        cmds.setAttr(A_CTRL + ".rotateX", 60)
        self.assertWorldRotation(CHILD_JNT, [30, 0, 0])
        self.assertWorldTranslation(CHILD_JNT, [1, 2.25, SIN60 / 2])

    def test_orient_halfway(self):
        # a turns the parent 90 about Z. Orient keeps the driver's rotation (none) and the parent's pivot (-3, 1, 0).
        self.constrain("Orient", 0.5)
        cmds.setAttr(A_CTRL + ".rotateZ", 90)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 45])
        self.assertWorldTranslation(CHILD_JNT, [-3, 1, 0])

    def test_point_halfway(self):
        # Point keeps the parent's rotation and the driver's pivot (1, 3, 0): halfway to the parent's pivot (-3, 1, 0).
        self.constrain("Point", 0.5)
        cmds.setAttr(A_CTRL + ".rotateZ", 90)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 90])
        self.assertWorldTranslation(CHILD_JNT, [-1, 2, 0])

    def test_parent_halfway_reads_the_parent(self):
        # A full Parent constraint takes the driver's motion alone (none here). Weight 0.5 is halfway to the parent's.
        self.constrain("Parent", 0.5)
        cmds.setAttr(A_CTRL + ".rotateZ", 90)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 45])
        self.assertWorldTranslation(CHILD_JNT, [-1, 2, 0])

    def test_weight_can_be_changed_after_the_build(self):
        con = self.constrain("Twist")
        cmds.setAttr(A_CTRL + ".rotateX", 60)
        self.assertWorldRotation(CHILD_JNT, [0, 0, 0])
        cmds.setAttr(con + ".constraintWeight", 0.0)
        self.assertWorldRotation(CHILD_JNT, [60, 0, 0])
        self.assertWorldTranslation(CHILD_JNT, [1, 1.5, SIN60])

    def test_template_key(self):
        template = fixtures.load_template("fkchain_two_chains")
        template["constraints"] = [{"name": "cTwist_00_M_CON", "type": "Twist", "target": "fkchain_02_L_MOD.start", "constraintWeight": 0.5,
                                    "drivers": [{"socket": "layout_00_M_MOD.end", "label": "World", "weight": 1.0}]}]
        cmds.file(new=True, force=True)
        self.build(template, self.make_skeleton(self.SKELETON))
        self.assertAlmostEqual(cmds.getAttr(rig_name("cTwist_00_M_CON") + ".constraintWeight"), 0.5)
        cmds.setAttr(A_CTRL + ".rotateX", 60)
        self.assertWorldRotation(CHILD_JNT, [30, 0, 0])


class WeightSpaceSwitch(ConstraintWeightCase):

    def pose(self):
        cmds.setAttr(A_CTRL + ".rotate", 10, 20, 30)
        cmds.setAttr(rig_name("b_00_M_CTRL") + ".rotateX", -40)
        cmds.setAttr(LAYOUT_END_CTRL + ".rotateY", 25)

    def test_a_switch_at_partial_weight_does_not_move_the_control(self):
        for constraint_type in ("Parent", "Orient", "Point", "Swing", "Twist"):
            with self.subTest(type=constraint_type):
                self.setUp()
                con = self.constrain(constraint_type, 0.4, ["layout_00_M_MOD.end", "fkchain_01_M_MOD.start"])
                self.pose()
                before = self.world()
                cmds.setRigConstraintSpace(con, space=1)
                self.assertTrue(self.world().isEquivalent(before, 1e-4), "moved on a %s space switch at weight 0.4" % constraint_type)

    def test_creating_on_a_posed_rig_does_not_move_the_control(self):
        self.pose()
        before = self.world()
        self.constrain("Twist", 0.3)
        self.assertTrue(self.world().isEquivalent(before, 1e-4))

    def test_a_switch_at_weight_zero_changes_nothing(self):
        con = self.constrain("Orient", 0.0, ["layout_00_M_MOD.end", "fkchain_01_M_MOD.start"])
        self.pose()
        before = self.world()
        values = cmds.getAttr(CHILD_CTRL + ".rotate")[0]
        cmds.setRigConstraintSpace(con, space=1)
        self.assertTrue(self.world().isEquivalent(before, 1e-4))
        self.assertEqual(cmds.getAttr(CHILD_CTRL + ".rotate")[0], values)


class WeightRejected(ConstraintWeightCase):

    def test_weight_above_one(self):
        self.assertMayaError(lambda: self.constrain("Orient", 1.5), "constraintWeight", "between 0 and 1")

    def test_negative_weight(self):
        self.assertMayaError(lambda: self.constrain("Orient", -0.1), "constraintWeight", "between 0 and 1")

    def test_parent_below_one_needs_a_parent(self):
        cmds.disconnectAttr(rig_name("fkchain_01_M_MOD") + ".outputSocketMatrix[1]",
                            rig_name("fkchain_02_L_MOD") + ".childPlugs[0].childPlugParentMatrix")
        self.assertMayaError(lambda: self.constrain("Parent", 0.5), "no parent")

    def test_parent_at_one_without_a_parent_is_still_allowed(self):
        cmds.disconnectAttr(rig_name("fkchain_01_M_MOD") + ".outputSocketMatrix[1]",
                            rig_name("fkchain_02_L_MOD") + ".childPlugs[0].childPlugParentMatrix")
        self.assertExists(self.constrain("Parent", 1.0))
