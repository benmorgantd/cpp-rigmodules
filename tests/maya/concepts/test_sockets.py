"""Sockets and child plugs: stored names (D16) and how parentStructure wires them (D4, D5)."""
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
        self.assertEqual(socket_names("fkchain_1"), [["index_0", "start"], ["index_1"], ["index_2", "end"]])

    def test_single_joint_fkchain_socket_is_start_and_end(self):
        self.assertEqual(socket_names("fkchain_2"), [["index_0", "start", "end"]])

    def test_aim_sockets(self):
        self.assertEqual(socket_names("aim_1"), [["aim"], ["target"], ["up"]])

    def test_layout_sockets(self):
        self.assertEqual(socket_names("layout"), [["index_0"], ["index_1"], ["index_2", "end"]])


class ParentStructure(RigTestCase):

    def test_child_plugs_connect_to_named_sockets(self):
        self.build_standard("layout_rig", "layout_rig")
        self.assertEqual(child_plug_source("aim_1", "aim"), "rig:fkchain_1.outputSocketMatrix[0]")
        self.assertEqual(child_plug_source("aim_1", "up"), "rig:fkchain_1.outputSocketMatrix[0]")

    def test_constrained_child_plug_is_driven_by_its_constraint(self):
        self.build_standard("layout_rig", "layout_rig")
        self.assertEqual(child_plug_source("fkchain_2", "start"), "rig:fkchain_2_space.outParentMatrix")
        self.assertEqual(child_plug_source("aim_1", "target"), "rig:aim_target_space.outParentMatrix")

    def test_end_socket_resolves_to_last_index(self):
        self.build_standard("fkchain_two_chains", "fk_two_chains")
        self.assertEqual(child_plug_source("fkchain_2", "start"), "rig:fkchain_1.outputSocketMatrix[1]")

    def test_unlisted_child_plugs_default_to_layout_end(self):
        from rigtest import fixtures
        template = fixtures.apply_edits(fixtures.load_template("layout_rig"), [
            {"set": "/constraints", "value": []},
            {"set": "/rigModules/2/parentStructure", "value": {"aim": "fkchain_1.start"}},
        ])
        self.build(template, self.make_skeleton("layout_rig"))
        self.assertEqual(child_plug_source("aim_1", "aim"), "rig:fkchain_1.outputSocketMatrix[0]")
        self.assertEqual(child_plug_source("aim_1", "target"), "rig:layout.outputSocketMatrix[2]")
        self.assertEqual(child_plug_source("aim_1", "up"), "rig:layout.outputSocketMatrix[2]")

    def test_template_constraints_are_built(self):
        self.build_standard("layout_rig", "layout_rig")
        self.assertEqual(sorted(cmds.ls(type="bmParentConstraint")), ["rig:aim_target_space", "rig:fkchain_2_space"])
