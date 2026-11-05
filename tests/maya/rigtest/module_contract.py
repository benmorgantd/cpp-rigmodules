"""Tests every module must pass. A module's test file supplies data, the mixin supplies the tests.

    class FkChainContract(ModuleContract, RigTestCase):
        TEMPLATE = "fkchain_three"
        SKELETON = "fk_three"
        MODULE_TYPE = "fkChainModule"
        JOINT_OWNERS = {"a_jnt": "fkchain_1", ...}
"""
from maya import cmds
import maya.api.OpenMaya as om

from rigtest import config
from rigtest.case import rig_name


def _world_matrix(node):
    return om.MSelectionList().add(node).getDagPath(0).inclusiveMatrix()


class ModuleContract:
    TEMPLATE = None       # fixture template that builds the module
    SKELETON = None       # fixture skeleton the template needs
    MODULE_TYPE = None    # Maya node type of the module
    JOINT_OWNERS = {}     # joint short name -> module name that must drive its offsetParentMatrix

    def _build(self):
        return self.build_standard(self.TEMPLATE, self.SKELETON)

    def test_contract_builds_module(self):
        self._build()
        modules = sorted(set(self.JOINT_OWNERS.values()))
        built = sorted(n.split(":")[-1] for n in cmds.ls(rig_name("*"), type=self.MODULE_TYPE) or [])
        self.assertEqual(built, modules, "%s nodes built: expected %s, got %s" % (self.MODULE_TYPE, modules, built))

    def test_contract_joints_keep_rest_pose(self):
        joints = self.make_skeleton(self.SKELETON)
        before = {j.split(":")[-1]: _world_matrix(j) for j in joints}
        self.build(self.TEMPLATE, joints)
        for short, matrix in before.items():
            after = _world_matrix(rig_name(short))
            if not after.isEquivalent(matrix, 1e-4):
                self.fail("%s moved when the rig was built\n  before %s\n  after  %s" % (short, list(matrix), list(after)))

    def test_contract_module_drives_its_joints(self):
        self._build()
        for joint, module in self.JOINT_OWNERS.items():
            self.assertOpmDrivenBy(rig_name(joint), rig_name(module))

    def test_contract_nothing_inherits_transform(self):
        self._build()
        nodes = (cmds.ls(rig_name("*"), type="rigControlNode") or []) + (cmds.ls(rig_name("*"), type="joint") or [])
        inheriting = [n for n in nodes if cmds.getAttr(n + ".inheritsTransform")]
        self.assertEqual(inheriting, [], "inheritsTransform must be off on every control and joint: %s" % inheriting)

    def test_contract_undo_redo_round_trip(self):
        self._build()
        built = sorted(cmds.ls(rig_name("*"), long=True))
        positions = {j: cmds.xform(j, q=True, ws=True, t=True) for j in cmds.ls(rig_name("*"), type="joint")}
        cmds.undo()
        self.assertEqual(cmds.ls(type=config.RIG_NODE_TYPES), [], "undo left rig nodes behind")
        cmds.redo()
        self.assertEqual(sorted(cmds.ls(rig_name("*"), long=True)), built, "redo did not rebuild the same nodes")
        for joint, position in positions.items():
            self.assertWorldTranslation(joint, position)
