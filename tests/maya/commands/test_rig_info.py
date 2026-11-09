"""getRigInfo and its Python wrapper rigmodules.query.get_rig_info (D29, D30): from any rig node to its rig
root, asset root, asset id, variant, namespace and version stamps."""
import json

from maya import cmds

import rigmodules.query
from rigtest import config
from rigtest.case import RigTestCase, rig_name

TEMPLATE = "layout_rig"    # rigName "fktest", rigVersion 1
SKELETON = "layout_rig"


class GetRigInfo(RigTestCase):

    def setUp(self):
        super().setUp()
        self.build_standard(TEMPLATE, SKELETON)
        self.rig_root = cmds.ls(type="rigRootModule")[0]
        self.asset_root = cmds.ls(type="assetRootModule")[0]

    def info(self, node):
        return json.loads(cmds.getRigInfo(node))

    def test_rig_root_reports_its_rig_and_asset(self):
        info = self.info(self.rig_root)
        self.assertEqual(info["rigRoot"], self.rig_root)
        self.assertEqual(info["assetRoot"], self.asset_root)
        self.assertEqual(info["assetId"], config.ASSET_ID)
        self.assertEqual(info["rigName"], "fktest")
        self.assertEqual(info["namespace"], config.RIG_NAMESPACE)
        self.assertEqual(info["rigVersion"], 1)
        self.assertEqual(info["pluginVersion"], cmds.getAttr(self.rig_root + ".pluginVersion"))
        self.assertEqual(info["publishVersion"], "")

    def test_every_control_and_module_reports_the_same_rig(self):
        expected = self.info(self.rig_root)
        nodes = cmds.ls(type="rigControlNode") + cmds.ls(type=["layoutModule", "fkChainModule"])
        self.assertGreater(len(nodes), 2)
        for node in nodes:
            self.assertEqual(self.info(node), expected, node)

    def test_publish_version_is_read_from_the_rig_root(self):
        cmds.setAttr(self.rig_root + ".publishVersion", "20261108T142012Z_bmorgan", type="string")
        self.assertEqual(self.info(rig_name("layout_0_ctrl_c"))["publishVersion"], "20261108T142012Z_bmorgan")

    def test_joint_driven_by_a_module_reports_its_rig(self):
        expected = self.info(self.rig_root)
        driven = [j for j in cmds.ls(type="joint") if cmds.listConnections(j + ".offsetParentMatrix", source=True, destination=False)]
        self.assertTrue(driven, "the build drives its joints' offsetParentMatrix")
        for joint in driven:
            self.assertEqual(self.info(joint), expected, joint)

    def test_other_nodes_give_an_empty_result_without_an_error(self):
        cmds.select(clear=True)
        loose_joint = cmds.joint(name="loose_jnt")
        for node in (loose_joint, self.asset_root, cmds.polyCube()[0]):
            self.assertEqual(cmds.getRigInfo(node), "", node)

    def test_missing_node(self):
        self.assertMayaError(lambda: cmds.getRigInfo("no_such_node"), "Node not found")

    def test_rig_root_without_asset_root(self):
        cmds.disconnectAttr(self.asset_root + ".children", self.rig_root + ".assetRoot")
        self.assertMayaError(lambda: cmds.getRigInfo(self.rig_root), "is not connected to an asset root")

    def test_takes_exactly_one_node(self):
        self.assertMayaError(lambda: cmds.getRigInfo(self.rig_root, self.asset_root))


class PythonQuery(RigTestCase):

    def test_get_rig_info_wraps_the_command(self):
        self.build_standard(TEMPLATE, SKELETON)
        rig_root = cmds.ls(type="rigRootModule")[0]
        info = rigmodules.query.get_rig_info(rig_name("layout_0_ctrl_c"))
        self.assertEqual(info, rigmodules.query.RigInfo(
            rig_root=rig_root,
            asset_root=cmds.ls(type="assetRootModule")[0],
            asset_id=config.ASSET_ID,
            variant="fktest",
            namespace=config.RIG_NAMESPACE,
            rig_version=1,
            plugin_version=cmds.getAttr(rig_root + ".pluginVersion"),
            publish_version="",
        ))

    def test_get_rigs_lists_every_rig_or_one_namespace(self):
        self.build_standard(TEMPLATE, SKELETON)
        self.build_standard(TEMPLATE, SKELETON, namespace="other")
        rigs = rigmodules.query.get_rigs()
        self.assertEqual([info.namespace for info in rigs], ["other", config.RIG_NAMESPACE])
        self.assertEqual(rigmodules.query.get_rigs("other"), rigs[:1])
        self.assertEqual(rigmodules.query.get_rigs(":" + config.RIG_NAMESPACE), rigs[1:])
        self.assertEqual(rigmodules.query.get_rigs("nobody"), [])

    def test_unrigged_joint_is_none_or_an_error(self):
        self.make_skeleton(SKELETON)
        joint = cmds.ls(type="joint")[0]
        self.assertIsNone(rigmodules.query.find_rig_info(joint))
        with self.assertRaisesRegex(RuntimeError, "is not a rig root, rig module, rig control or joint driven by a rig"):
            rigmodules.query.get_rig_info(joint)

    def test_missing_node_raises_from_both(self):
        for query in (rigmodules.query.find_rig_info, rigmodules.query.get_rig_info):
            with self.assertRaises(RuntimeError):
                query("no_such_node")
