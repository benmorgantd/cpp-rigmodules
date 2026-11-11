"""createRigConstraint and setRigConstraintSpace: arguments, space switching, undo, and how
driver ids and constraint names resolve across namespaces (D14, D19). Parent constraints only:
Orient and Point are in test_constraint_types."""
from maya import cmds
import maya.api.OpenMaya as om

from rigtest.case import RigTestCase, rig_name

# up_ctrl_r drives aim_1.up. Its drivers must not depend on aim_1, or the constraint would form a cycle.
CONTROL = rig_name("up_ctrl_r")
OTHER_CONTROL = rig_name("fkchain1_ctrl_c")


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
        kwargs.setdefault("driver", ["layout.end", "fkchain_1.end"])
        return cmds.createRigConstraint(control, **kwargs)

    def assertWorldMatrix(self, node, expected, message):
        self.assertTrue(world_matrix(node).isEquivalent(expected, 1e-4), "%s: %s" % (node, message))


class Create(ConstraintTestCase):

    def test_labels_become_space_enum(self):
        self.create(label=["World", "Arm"])
        self.assertEqual(cmds.attributeQuery("space", node=CONTROL, listEnum=True), ["World:Arm"])

    def test_default_labels_are_the_driver_ids(self):
        self.create()
        self.assertEqual(cmds.attributeQuery("space", node=CONTROL, listEnum=True), ["layout.end:fkchain_1.end"])

    def test_initial_sets_active_driver(self):
        con = self.create(initial=1)
        self.assertEqual(cmds.getAttr(con + ".activeDriver"), 1)
        self.assertEqual(cmds.getAttr(CONTROL + ".space"), 1)

    def test_default_name_is_control_space(self):
        con = self.create()
        self.assertEqual(con.lstrip(":"), "rig:up_ctrl_r_space")

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
        # The control's current parent (fkchain_1.start) is posed and the new driver is not,
        # so a missing offset would show as a jump.
        cmds.setAttr(OTHER_CONTROL + ".rotateZ", 90)
        before = world_matrix(CONTROL)
        self.create(driver=["layout.end"])
        self.assertWorldMatrix(CONTROL, before, "moved when its constraint was created on a posed rig")

    def test_zero_after_create_on_posed_rig_returns_to_rest(self):
        # D27: the compensation lives in the control's values, so zeroing every control gives the rest pose.
        rest = world_matrix(CONTROL)
        cmds.setAttr(OTHER_CONTROL + ".rotateZ", 90)
        self.create(driver=["layout.end"])
        zero_controls(OTHER_CONTROL, CONTROL)
        self.assertWorldMatrix(CONTROL, rest, "not at rest after zeroing every control")

    def test_create_at_rest_leaves_control_values(self):
        self.create(initial=1)
        self.assertEqual(cmds.getAttr(CONTROL + ".translate")[0], (0.0, 0.0, 0.0))
        self.assertEqual(cmds.getAttr(CONTROL + ".rotate")[0], (0.0, 0.0, 0.0))


class CreateRejected(ConstraintTestCase):

    def test_no_control(self):
        self.assertMayaError(lambda: cmds.createRigConstraint(driver=["layout.end"]))

    def test_two_controls(self):
        self.assertMayaError(lambda: cmds.createRigConstraint(CONTROL, OTHER_CONTROL, driver=["layout.end"]))

    def test_no_driver(self):
        self.assertMayaError(lambda: cmds.createRigConstraint(OTHER_CONTROL))

    def test_label_count_mismatch(self):
        self.assertMayaError(lambda: self.create(OTHER_CONTROL, label=["A"]))

    def test_negative_initial(self):
        self.assertMayaError(lambda: self.create(OTHER_CONTROL, initial=-1))

    def test_initial_past_last_driver(self):
        self.assertMayaError(lambda: self.create(OTHER_CONTROL, initial=2))

    def test_unknown_socket(self):
        self.assertMayaError(lambda: self.create(OTHER_CONTROL, driver=["layout.nope"]), "nope", "layout")

    def test_unknown_module(self):
        self.assertMayaError(lambda: self.create(OTHER_CONTROL, driver=["nope.end"]), "nope")

    def test_missing_control(self):
        self.assertMayaError(lambda: self.create(rig_name("nope_ctrl")), "nope_ctrl")

    def test_legacy_semicolon_socket_names_rejected(self):
        # D16: rigs built before socket names became JSON report an error instead of guessing.
        cmds.setAttr(rig_name("fkchain_1") + ".socketNames[0]", "index_0;start;", type="string")
        self.assertMayaError(lambda: self.create(OTHER_CONTROL, driver=["fkchain_1.start"]), "Invalid socketNames[0]")


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
        # D27: the match is in the control's channels, and the constraint stores nothing per switch.
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
        cmds.setAttr(rig_name("fkchain1_ctrl_c") + ".translateX", 3)
        self.assertWorldTranslation(CONTROL, [before[0] + 3, before[1], before[2]])

    def test_undo_switch(self):
        cmds.setRigConstraintSpace(self.con, label="Arm")
        cmds.undo()
        self.assertEqual(cmds.getAttr(self.con + ".activeDriver"), 0)

    def test_template_constraint_by_label(self):
        cmds.setRigConstraintSpace(rig_name("fkchain_2_space"), label="Aim")
        self.assertEqual(cmds.getAttr(rig_name("fkchain_2_space") + ".activeDriver"), 1)


class RotatedSpaceSwitch(RigTestCase):
    """The match math with rotated rests, rotated drivers and a non-default rotate order (D27)."""

    def setUp(self):
        super().setUp()
        self.build_standard("fkchain_two_chains", "fk_two_chains_rotated")
        self.child = rig_name("c_ctrl_l")
        self.parents = (rig_name("a_ctrl_c"), rig_name("b_ctrl_c"))
        cmds.setAttr(self.child + ".rotateOrder", lock=False)   # the module locks it (D40); this case needs another order
        cmds.setAttr(self.child + ".rotateOrder", 2)   # zxy
        # The child's current parent is fkchain_1.end, so the initial driver needs no match.
        self.con = cmds.createRigConstraint(self.child, driver=["fkchain_1.end", "fkchain_1.start"])
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
        self.assertWorldTranslation(rig_name("c_jnt"), [1, 3, 0])
        self.assertWorldRotation(rig_name("c_jnt"), [20, -30, 60])


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


class CommandIdResolution(RigTestCase):
    """D19: driver ids and constraint names resolve in the constrained control's namespace."""

    NAMESPACES = ["rig", "char:rig", ":"]

    def build_in(self, namespace):
        self.build_standard("fkchain_two_chains", "fk_two_chains", namespace=namespace)
        if not cmds.namespace(exists=":elsewhere"):
            cmds.namespace(add=":elsewhere")
        return rig_name("c_ctrl_l", namespace)

    def test_drivers_resolve_in_control_namespace(self):
        for namespace in self.NAMESPACES:
            with self.subTest(namespace=namespace):
                self.setUp()
                control = self.build_in(namespace)
                cmds.namespace(setNamespace=":elsewhere")
                con = cmds.createRigConstraint(control, driver=["layout.end", "fkchain_1.start"])
                cmds.namespace(setNamespace=":")
                self.assertSource(con + ".drivers[0].driverMatrix", rig_name("layout", namespace) + ".outputSocketMatrix[2]")

    def test_constraint_node_goes_in_control_namespace(self):
        for namespace in self.NAMESPACES:
            with self.subTest(namespace=namespace):
                self.setUp()
                control = self.build_in(namespace)
                cmds.namespace(setNamespace=":elsewhere")
                default = cmds.createRigConstraint(control, driver=["layout.end"])
                cmds.undo()
                custom = cmds.createRigConstraint(control, driver=["layout.end"], name="custom")
                cmds.undo()
                qualified = cmds.createRigConstraint(control, driver=["layout.end"], name=":elsewhere:qualified")
                cmds.namespace(setNamespace=":")
                self.assertEqual(default.lstrip(":"), rig_name("c_ctrl_l_space", namespace).lstrip(":"))
                self.assertEqual(custom.lstrip(":"), rig_name("custom", namespace).lstrip(":"))
                self.assertEqual(qualified.lstrip(":"), "elsewhere:qualified")

    def test_redo_keeps_constraint_name_under_another_current_namespace(self):
        control = self.build_in("rig")
        con = cmds.createRigConstraint(control, driver=["layout.end"])
        cmds.undo()
        # MNamespace, not cmds.namespace: the cmds version is undoable and would clear the redo queue.
        om.MNamespace.setCurrentNamespace(":elsewhere")
        cmds.redo()
        om.MNamespace.setCurrentNamespace(":")
        self.assertExists(con)
        self.assertEqual(cmds.ls("elsewhere:*"), [])

    def test_qualified_driver_used_as_given(self):
        control = self.build_in("rig")
        con = cmds.createRigConstraint(control, driver=["rig:layout.end"])
        self.assertSource(con + ".drivers[0].driverMatrix", "rig:layout.outputSocketMatrix[2]")

    def test_old_layout_module_name_rejected(self):
        control = self.build_in("rig")
        self.assertMayaError(lambda: cmds.createRigConstraint(control, driver=["layoutModule.end"]), "layoutModule")
