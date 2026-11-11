"""Plugin registration: every node type, command and long flag is present.

Flags matter because MSyntax::addFlag silently drops a flag whose short name is over 3 letters (D14).
"""
import maya.OpenMaya
from maya import cmds, mel

from rigtest.case import RigTestCase

NODE_TYPES = ["assetRootModule", "rigRootModule", "layoutModule", "fkChainModule", "aimModule",
              "rigConstraint", "rigControlNode"]

COMMAND_FLAGS = {
    "createRigFromTemplate": ["template", "namespace", "checkMirrorPositions"],
    "rigJointMarkup": ["usage", "side", "export", "skin", "remove", "query", "edit"],
    "createRigConstraint": ["name", "driver", "label", "initial", "type"],
    "setRigConstraintSpace": ["space", "label"],
    "getRigControls": ["moduleType", "side", "usage"],
    "saveRigControlShapes": ["file"],
    "loadRigControlShapes": ["file"],
    "getRigMirrorControl": [],
    "mirrorRigControlShapes": ["leftToRight"],
    "getRigInfo": [],
    "setRigMeshId": ["newId"],
    "saveRigSkinWeights": ["file"],
    "loadRigSkinWeights": ["file"],
    "pruneToMaxInfluences": ["maxInfluences"],
}


class Registration(RigTestCase):

    def test_node_types_registered(self):
        registered = set(cmds.allNodeTypes())
        self.assertEqual([t for t in NODE_TYPES if t not in registered], [])

    def test_commands_and_long_flags_registered(self):
        for command, flags in COMMAND_FLAGS.items():
            with self.subTest(command=command):
                self.assertTrue(mel.eval('exists "%s"' % command), "%s is not registered" % command)
                help_text = cmds.help(command)
                missing = [f for f in flags if "-%s" % f not in help_text]
                self.assertEqual(missing, [], "%s is missing flags %s" % (command, missing))

    def test_profiler_category_registered(self):
        # The control draw override's events are filed under it in the Profiler window. MProfiler is API 1 only.
        self.assertGreaterEqual(maya.OpenMaya.MProfiler.getCategoryIndex("RigModules"), 0)


class Reload(RigTestCase):

    def test_selection_changes_after_unload_and_reload(self):
        # The plugin keeps a selection-changed callback for control drawing. One left registered after unload
        # would call into the unloaded .mll on the next selection change and crash Maya.
        path = cmds.pluginInfo("RigModules", query=True, path=True)
        cmds.file(new=True, force=True)
        cmds.flushUndo()
        cmds.unloadPlugin("RigModules")
        try:
            node = cmds.createNode("transform")
            cmds.select(node)
            cmds.select(clear=True)
        finally:
            cmds.loadPlugin(path)
        control = cmds.createNode("rigControlNode")
        cmds.select(control)
        self.assertEqual(cmds.ls(selection=True), [control])
