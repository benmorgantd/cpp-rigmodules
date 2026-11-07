"""Plugin registration: every node type, command and long flag is present.

Flags matter because MSyntax::addFlag silently drops a flag whose short name is over 3 letters (D14).
"""
from maya import cmds, mel

from rigtest.case import RigTestCase

NODE_TYPES = ["assetRootModule", "rigRootModule", "layoutModule", "fkChainModule", "aimModule",
              "bmParentConstraint", "rigControlNode"]

COMMAND_FLAGS = {
    "createRigFromTemplate": ["template", "namespace"],
    "rigJointMarkup": ["usage", "side", "export", "skin", "remove", "query", "edit"],
    "createRigParentConstraint": ["name", "driver", "label", "initial"],
    "setRigConstraintSpace": ["space", "label"],
    "getRigControls": ["moduleType", "side", "usage"],
    "saveRigControlShapes": ["file"],
    "loadRigControlShapes": ["file"],
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
