"""Sockets and child plugs: stored names and how parentStructure wires them (D4, D5)."""
import json

from maya import cmds

from rigtest.case import RigTestCase, rig_name


def socket_names(module):
    node = rig_name(module)
    return [json.loads(cmds.getAttr("%s.socketNames[%d]" % (node, i)))
            for i in cmds.getAttr(node + ".socketNames", multiIndices=True) or []]


def child_plug_source(module, child_plug):
    node = rig_name(module)
    for index in cmds.getAttr(node + ".childPlugs", multiIndices=True) or []:
        if cmds.getAttr("%s.childPlugs[%d].childPlugName" % (node, index)) == child_plug:
            sources = cmds.listConnections("%s.childPlugs[%d].childPlugParentMatrix" % (node, index),
                                           plugs=True, source=True, destination=False) or []
            return sources[0] if sources else None
    raise AssertionError("%s has no child plug %r" % (module, child_plug))


class SocketNames(RigTestCase):

    def setUp(self):
        super().setUp()
        self.build_standard("layout_rig", "layout_rig")

    def test_fkchain_sockets_are_index_plus_start_and_end(self):
        self.assertEqual(socket_names("fkchain_01_M_MOD"), [["index_0", "start"], ["index_1"], ["index_2", "end"]])

    def test_single_joint_fkchain_socket_is_start_and_end(self):
        self.assertEqual(socket_names("fkchain_02_L_MOD"), [["index_0", "start", "end"]])

    def test_aim_sockets(self):
        self.assertEqual(socket_names("aim_01_R_MOD"), [["aim"], ["target"], ["up"]])

    def test_layout_sockets(self):
        self.assertEqual(socket_names("layout_00_M_MOD"), [["index_0"], ["index_1"], ["index_2", "end"]])


class ParentStructure(RigTestCase):

    def test_child_plugs_connect_to_named_sockets(self):
        self.build_standard("layout_rig", "layout_rig")
        self.assertEqual(child_plug_source("aim_01_R_MOD", "aim"), "rig:fkchain_01_M_MOD.outputSocketMatrix[0]")
        self.assertEqual(child_plug_source("aim_01_R_MOD", "up"), "rig:fkchain_01_M_MOD.outputSocketMatrix[0]")

    def test_constrained_child_plug_is_driven_by_its_constraint(self):
        self.build_standard("layout_rig", "layout_rig")
        self.assertEqual(child_plug_source("fkchain_02_L_MOD", "start"), "rig:fkchain2Space_00_M_CON.outParentMatrix")
        self.assertEqual(child_plug_source("aim_01_R_MOD", "target"), "rig:aimTargetSpace_00_M_CON.outParentMatrix")

    def test_end_socket_resolves_to_last_index(self):
        self.build_standard("fkchain_two_chains", "fk_two_chains")
        self.assertEqual(child_plug_source("fkchain_02_L_MOD", "start"), "rig:fkchain_01_M_MOD.outputSocketMatrix[1]")

    def test_unlisted_child_plugs_default_to_layout_end(self):
        from rigtest import fixtures
        template = fixtures.apply_edits(fixtures.load_template("layout_rig"), [
            {"set": "/constraints", "value": []},
            {"set": "/rigModules/2/parentStructure", "value": {"aim": "fkchain_01_M_MOD.start"}},
        ])
        self.build(template, self.make_skeleton("layout_rig"))
        self.assertEqual(child_plug_source("aim_01_R_MOD", "aim"), "rig:fkchain_01_M_MOD.outputSocketMatrix[0]")
        self.assertEqual(child_plug_source("aim_01_R_MOD", "target"), "rig:layout_00_M_MOD.outputSocketMatrix[2]")
        self.assertEqual(child_plug_source("aim_01_R_MOD", "up"), "rig:layout_00_M_MOD.outputSocketMatrix[2]")

    def test_template_constraints_are_built(self):
        self.build_standard("layout_rig", "layout_rig")
        self.assertEqual(sorted(cmds.ls(type="rigConstraint")), ["rig:aimTargetSpace_00_M_CON", "rig:fkchain2Space_00_M_CON"])
