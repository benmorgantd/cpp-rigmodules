"""Undo and redo of a full rig build. Per-module round trips are in the module contract tests."""
import os

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
        self.assertExists(":skel:fkchain1_00_M_JNT")
        self.assertNotExists(rig_name("fkchain1_00_M_JNT"))

    def test_undo_removes_the_namespace_the_build_made(self):
        # D18: the namespace is not part of the modifier, so undo used to leave it behind, empty.
        self.build_standard("layout_rig", "layout_rig")
        cmds.undo()
        self.assertFalse(cmds.namespace(exists=":" + config.RIG_NAMESPACE))
        cmds.redo()
        self.assertTrue(cmds.namespace(exists=":" + config.RIG_NAMESPACE))
        self.assertExists(rig_name("fkchain1_00_M_JNT"))

    def test_undo_keeps_a_namespace_that_existed_before(self):
        om.MNamespace.addNamespace(":" + config.RIG_NAMESPACE)
        self.build_standard("layout_rig", "layout_rig")
        cmds.undo()
        self.assertTrue(cmds.namespace(exists=":" + config.RIG_NAMESPACE))

    def test_undo_removes_nested_namespaces_the_build_made(self):
        joints = self.make_skeleton("layout_rig")
        self.build("layout_rig", joints, namespace="char:rig")
        cmds.undo()
        self.assertFalse(cmds.namespace(exists=":char"))

    def test_undo_of_load_skeleton_removes_its_namespace(self):
        path = os.path.join(config.WORK_DIR, "undo_skeleton.json")
        cmds.saveRigSkeleton(*self.make_skeleton("layout_rig"), file=path)
        cmds.loadRigSkeleton(file=path, namespace="loaded")
        cmds.undo()
        self.assertFalse(cmds.namespace(exists=":loaded"))

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
        cmds.setAttr(rig_name("fkchain1_00_M_CTRL") + ".rotateZ", 90)
        self.assertWorldTranslation(rig_name("fkchain2_00_M_JNT"), (-2, 0, 0))
