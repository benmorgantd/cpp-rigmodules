"""Undo and redo of a full rig build. Per-module round trips are in the module contract tests."""
from maya import cmds
import maya.api.OpenMaya as om

from rigtest import config
from rigtest.case import RigTestCase, rig_name


class BuildUndoRedo(RigTestCase):

    def test_undo_removes_every_rig_node(self):
        self.build_standard("layout_rig", "layout_rig")
        cmds.undo()
        self.assertNothingBuilt()

    def test_undo_returns_joints_to_their_namespace(self):
        self.build_standard("layout_rig", "layout_rig")
        cmds.undo()
        self.assertExists(":skel:fkchain1_jnt")
        self.assertNotExists(rig_name("fkchain1_jnt"))

    def test_redo_rebuilds_identical_rig(self):
        self.build_standard("layout_rig", "layout_rig")
        before = sorted(cmds.ls(rig_name("*"), long=True))
        cmds.undo()
        cmds.redo()
        self.assertEqual(sorted(cmds.ls(rig_name("*"), long=True)), before)

    def test_redo_ignores_current_namespace(self):
        # D18 regression: redo used to put controls and modules in the user's current namespace.
        # Namespace changes go through MNamespace: cmds.namespace is undoable and would clear the redo queue.
        om.MNamespace.addNamespace(":elsewhere")
        self.build_standard("layout_rig", "layout_rig")
        cmds.undo()
        om.MNamespace.setCurrentNamespace(":elsewhere")
        cmds.redo()
        om.MNamespace.setCurrentNamespace(":")
        self.assertEqual(cmds.ls("elsewhere:*"), [])
        stray = [n for n in cmds.ls(type=config.RIG_NODE_TYPES) if not n.startswith("rig:")]
        self.assertEqual(stray, [], "rig nodes outside the rig namespace after redo")

    def test_redo_restores_working_rig(self):
        self.build_standard("layout_rig", "layout_rig")
        cmds.undo()
        cmds.redo()
        cmds.setAttr(rig_name("fkchain1_ctrl_c") + ".rotateZ", 90)
        self.assertWorldTranslation(rig_name("fkchain2_jnt"), (-2, 0, 0))

    def test_undo_leaves_empty_rig_namespace(self):
        # Documented in D18: namespace creation is not part of the command's undo.
        self.build_standard("layout_rig", "layout_rig")
        cmds.undo()
        self.assertTrue(cmds.namespace(exists=":rig"))
        self.assertEqual(cmds.ls("rig:*"), [])
