"""getRigControls, getRigControlInfo, saveRigControlShapes and loadRigControlShapes, plus the control markup the build writes.

Every test builds fkchain_two_chains: fkchain_1 (Center a, b), fkchain_2 (Left c) and the layout (Center layout_0..2).
"""
import json
import os

from maya import cmds

import rigmodules.query
from rigtest import config
from rigtest.case import RigTestCase, rig_name

ALL_CONTROLS = ["a_ctrl_c", "b_ctrl_c", "c_ctrl_l", "layout_0_ctrl_c", "layout_1_ctrl_c", "layout_2_ctrl_c"]
SCALE_TWO = [2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1]
IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


def short(names):
    return [n.split(":")[-1] for n in names]


def markup(control):
    return json.loads(cmds.getAttr(rig_name(control) + ".controlMarkup"))


def shape_transform(control):
    return [round(v, 6) for v in cmds.getAttr(rig_name(control) + ".shapeTransform")]


class RigControlTestCase(RigTestCase):

    def setUp(self):
        super().setUp()
        self.build_standard("fkchain_two_chains", "fk_two_chains")
        self.root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        self.path = os.path.join(config.WORK_DIR, "shapes.json").replace("\\", "/")

    def write_file(self, data):
        with open(self.path, "w", encoding="utf-8") as handle:
            json.dump(data, handle)

    def read_file(self):
        with open(self.path, encoding="utf-8") as handle:
            return json.load(handle)


class ControlMarkup(RigControlTestCase):

    def test_fk_control_takes_its_joint_markup(self):
        self.assertEqual(markup("a_ctrl_c"), {"markupVersion": 1, "side": "Center", "usage": "a"})
        self.assertEqual(markup("c_ctrl_l"), {"markupVersion": 1, "side": "Left", "usage": "c"})

    def test_layout_controls_are_center_layout_n(self):
        for i in range(3):
            self.assertEqual(markup("layout_%d_ctrl_c" % i), {"markupVersion": 1, "side": "Center", "usage": "layout_%d" % i})


class ControlDisplayDefaults(RigControlTestCase):

    def test_new_controls_are_filled_and_xray(self):
        for control in ("a_ctrl_c", "c_ctrl_l"):
            self.assertTrue(cmds.getAttr(rig_name(control) + ".filled"))
            self.assertTrue(cmds.getAttr(rig_name(control) + ".xray"))


class AimControlMarkup(RigTestCase):

    def test_aim_controls_take_target_and_up_markup(self):
        self.build_standard("aim_basic", "aim_basic")
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        self.assertEqual(short(cmds.getRigControls(root, moduleType="Aim")), ["eyeTarget_ctrl_r", "eyeUp_ctrl_r"])
        self.assertEqual(markup("eyeUp_ctrl_r"), {"markupVersion": 1, "side": "Right", "usage": "eyeUp"})


class GetRigControls(RigControlTestCase):

    def test_no_filter_returns_every_control_by_module_name(self):
        self.assertEqual(short(cmds.getRigControls(self.root)), ALL_CONTROLS)

    def test_filter_by_module_type(self):
        self.assertEqual(short(cmds.getRigControls(self.root, moduleType="FkChain")), ["a_ctrl_c", "b_ctrl_c", "c_ctrl_l"])
        self.assertEqual(short(cmds.getRigControls(self.root, moduleType="Layout")), ALL_CONTROLS[3:])

    def test_filter_by_side(self):
        self.assertEqual(short(cmds.getRigControls(self.root, side="Left")), ["c_ctrl_l"])

    def test_values_of_one_flag_are_alternatives(self):
        self.assertEqual(short(cmds.getRigControls(self.root, usage=["a", "c"])), ["a_ctrl_c", "c_ctrl_l"])

    def test_flags_must_all_match(self):
        self.assertEqual(short(cmds.getRigControls(self.root, side="Center", usage=["a", "c"])), ["a_ctrl_c"])
        self.assertEqual(short(cmds.getRigControls(self.root, moduleType="Layout", usage="a")), [])

    def test_no_match_returns_empty(self):
        self.assertEqual(cmds.getRigControls(self.root, usage="nothing") or [], [])

    def test_bad_side_is_an_error(self):
        self.assertMayaError(lambda: cmds.getRigControls(self.root, side="Middle"), "Middle", "Center, Left, Right")

    def test_bad_module_type_is_an_error(self):
        self.assertMayaError(lambda: cmds.getRigControls(self.root, moduleType="RigRoot"), "RigRoot", "Layout, FkChain, Aim")

    def test_object_must_be_a_rig_root(self):
        self.assertMayaError(lambda: cmds.getRigControls(rig_name("fkchain_1")), "Not a rig root")
        self.assertMayaError(lambda: cmds.getRigControls("missing"), "Rig root not found")

    def test_control_without_markup_is_an_error(self):
        cmds.setAttr(rig_name("b_ctrl_c") + ".controlMarkup", "", type="string")
        self.assertMayaError(lambda: cmds.getRigControls(self.root), "b_ctrl_c", "no control markup")

    def test_selection_is_not_used(self):
        cmds.select(self.root)
        self.assertMayaError(lambda: cmds.getRigControls())


def info(*controls):
    return json.loads(cmds.getRigControlInfo(*controls))


class GetRigControlInfo(RigControlTestCase):

    def test_reports_module_markup_and_mirror(self):
        found = info(rig_name("c_ctrl_l"), rig_name("a_ctrl_c"))
        self.assertEqual(sorted(found), ["rig:a_ctrl_c", "rig:c_ctrl_l"])
        self.assertEqual(found["rig:a_ctrl_c"]["moduleType"], "FkChain")
        self.assertEqual(found["rig:a_ctrl_c"]["side"], "Center")
        self.assertEqual(found["rig:a_ctrl_c"]["usage"], "a")
        self.assertEqual(found["rig:a_ctrl_c"]["mirror"], "rig:a_ctrl_c")   # a Center control is its own mirror
        self.assertEqual(cmds.nodeType(found["rig:a_ctrl_c"]["module"]), "fkChainModule")

    def test_a_side_control_with_no_mirror_reports_null(self):
        self.assertIsNone(info(rig_name("c_ctrl_l"))["rig:c_ctrl_l"]["mirror"])

    def test_layout_controls_report_the_layout_module(self):
        found = info(rig_name("layout_1_ctrl_c"))["rig:layout_1_ctrl_c"]
        self.assertEqual((found["moduleType"], found["side"], found["usage"]), ("Layout", "Center", "layout_1"))

    def test_object_must_be_a_rig_control(self):
        self.assertMayaError(lambda: cmds.getRigControlInfo(rig_name("fkchain_1")), "Not a rig control")
        self.assertMayaError(lambda: cmds.getRigControlInfo("missing"), "Not a rig control")

    def test_control_without_markup_is_an_error(self):
        cmds.setAttr(rig_name("b_ctrl_c") + ".controlMarkup", "", type="string")
        self.assertMayaError(lambda: cmds.getRigControlInfo(rig_name("b_ctrl_c")), "b_ctrl_c", "no control markup")

    def test_selection_is_not_used(self):
        cmds.select(rig_name("a_ctrl_c"))
        self.assertMayaError(lambda: cmds.getRigControlInfo())


class MirrorControlInfo(RigTestCase):

    def test_a_pair_reports_each_other_as_mirror(self):
        self.build_standard("mirror_arms", "mirror_arms")
        left, right = rig_name("upperArm_l_ctrl_l"), rig_name("upperArm_r_ctrl_r")
        found = info(left, right)
        self.assertEqual(found["rig:upperArm_l_ctrl_l"]["mirror"], "rig:upperArm_r_ctrl_r")
        self.assertEqual(found["rig:upperArm_r_ctrl_r"]["mirror"], "rig:upperArm_l_ctrl_l")
        self.assertEqual(cmds.getRigMirrorControl(left), found["rig:upperArm_l_ctrl_l"]["mirror"])


class PythonControlQuery(RigControlTestCase):

    def test_get_rig_controls_returns_every_control_in_module_order(self):
        controls = rigmodules.query.get_rig_controls(self.root)
        self.assertEqual(short(c.node for c in controls), ALL_CONTROLS)
        self.assertEqual(controls[2], rigmodules.query.RigControl(
            node="rig:c_ctrl_l", module=controls[2].module, module_type="FkChain", side="Left", usage="c", mirror=None))

    def test_get_rig_controls_filters_like_the_command(self):
        self.assertEqual(short(c.node for c in rigmodules.query.get_rig_controls(self.root, module_type="Layout")), ALL_CONTROLS[3:])
        self.assertEqual(short(c.node for c in rigmodules.query.get_rig_controls(self.root, side="Center", usage=["a", "c"])), ["a_ctrl_c"])
        self.assertEqual(rigmodules.query.get_rig_controls(self.root, usage="nothing"), [])

    def test_get_control_and_find_control(self):
        control = rigmodules.query.get_control(rig_name("b_ctrl_c"))
        self.assertEqual((control.module_type, control.side, control.usage, control.mirror), ("FkChain", "Center", "b", "rig:b_ctrl_c"))
        self.assertEqual(rigmodules.query.find_control(rig_name("b_ctrl_c")), control)
        self.assertIsNone(rigmodules.query.find_control(rig_name("fkchain_1")))
        self.assertIsNone(rigmodules.query.find_control(self.root))

    def test_get_control_raises_for_another_node(self):
        with self.assertRaises(RuntimeError):
            rigmodules.query.get_control(self.root)

    def test_get_channels_is_one_call_for_many_controls(self):
        channels = rigmodules.query.get_channels([rig_name("a_ctrl_c"), rig_name("c_ctrl_l")])
        self.assertEqual(sorted(channels), ["rig:a_ctrl_c", "rig:c_ctrl_l"])
        self.assertEqual(channels["rig:a_ctrl_c"], rigmodules.query.get_control_channels(rig_name("a_ctrl_c")))
        self.assertEqual(rigmodules.query.get_channels([]), {})


class SaveShapes(RigControlTestCase):

    def test_returns_saved_controls(self):
        self.assertEqual(short(cmds.saveRigControlShapes(self.root, file=self.path)), ALL_CONTROLS)

    def test_file_is_keyed_by_side_and_usage_without_color(self):
        cmds.setAttr(rig_name("c_ctrl_l") + ".shapeTransform", SCALE_TWO, type="matrix")
        cmds.setAttr(rig_name("c_ctrl_l") + ".shapeType", 13)   # SingleArrow
        cmds.setAttr(rig_name("c_ctrl_l") + ".filled", True)
        cmds.setAttr(rig_name("c_ctrl_l") + ".wireframeAlpha", 0.8)
        cmds.saveRigControlShapes(self.root, file=self.path)

        data = self.read_file()
        self.assertEqual(sorted(data), ["controls", "formatVersion"])
        self.assertEqual(data["formatVersion"], 2)
        self.assertEqual(sorted(data["controls"]), ["Center", "Left"])
        self.assertEqual(sorted(data["controls"]["Center"]), ["a", "b", "layout_0", "layout_1", "layout_2"])
        self.assertEqual(data["controls"]["Center"]["a"]["wireframeAlpha"], 1.0)
        self.assertEqual(data["controls"]["Left"],
                         {"c": {"shapeType": "SingleArrow", "shapeTransform": SCALE_TWO, "filled": True, "wireframeAlpha": 0.8}})

    def test_matrix_is_one_line(self):
        cmds.saveRigControlShapes(self.root, file=self.path)
        with open(self.path, encoding="utf-8") as handle:
            lines = [line for line in handle if '"shapeTransform"' in line]
        self.assertEqual(len(lines), len(ALL_CONTROLS))
        self.assertEqual(len(json.loads(lines[0].split(":", 1)[1].strip().rstrip(","))), 16)

    def test_needs_file(self):
        self.assertMayaError(lambda: cmds.saveRigControlShapes(self.root), "needs -file")


class LoadShapes(RigControlTestCase):

    def save_edited(self):
        """Saves with c_ctrl_l as a scaled SingleArrow, then resets c_ctrl_l to an identity Box."""
        control = rig_name("c_ctrl_l")
        cmds.setAttr(control + ".shapeTransform", SCALE_TWO, type="matrix")
        cmds.setAttr(control + ".shapeType", 13)
        cmds.saveRigControlShapes(self.root, file=self.path)
        cmds.setAttr(control + ".shapeTransform", IDENTITY, type="matrix")
        cmds.setAttr(control + ".shapeType", 0)

    def test_round_trip_restores_shapes(self):
        self.save_edited()
        self.assertEqual(short(cmds.loadRigControlShapes(self.root, file=self.path)), ALL_CONTROLS)
        self.assertEqual(shape_transform("c_ctrl_l"), SCALE_TWO)
        self.assertEqual(cmds.getAttr(rig_name("c_ctrl_l") + ".shapeType"), 13)

    def test_round_trip_restores_alpha(self):
        control = rig_name("c_ctrl_l")
        cmds.setAttr(control + ".wireframeAlpha", 0.4)
        cmds.saveRigControlShapes(self.root, file=self.path)
        cmds.setAttr(control + ".wireframeAlpha", 1.0)
        cmds.loadRigControlShapes(self.root, file=self.path)
        self.assertAlmostEqual(cmds.getAttr(control + ".wireframeAlpha"), 0.4, places=5)
        cmds.undo()
        self.assertAlmostEqual(cmds.getAttr(control + ".wireframeAlpha"), 1.0, places=5)

    def test_version_1_file_loads_with_alpha_1(self):
        # A version 1 file has no wireframeAlpha. The update step gives every entry the default, 1.
        control = rig_name("c_ctrl_l")
        cmds.saveRigControlShapes(self.root, file=self.path)
        data = self.read_file()
        data["formatVersion"] = 1
        for usages in data["controls"].values():
            for entry in usages.values():
                del entry["wireframeAlpha"]
        self.write_file(data)
        cmds.setAttr(control + ".wireframeAlpha", 0.3)
        cmds.loadRigControlShapes(self.root, file=self.path)
        self.assertAlmostEqual(cmds.getAttr(control + ".wireframeAlpha"), 1.0, places=5)

    def test_load_does_not_change_color(self):
        # Color follows the module's side, so the file neither stores nor restores it.
        self.save_edited()
        cmds.setAttr(rig_name("c_ctrl_l") + ".wireframeColor", 0, 1, 0, type="double3")
        cmds.loadRigControlShapes(self.root, file=self.path)
        self.assertEqual([round(v, 5) for v in cmds.getAttr(rig_name("c_ctrl_l") + ".wireframeColor")[0]], [0, 1, 0])

    def test_undo_and_redo(self):
        self.save_edited()
        cmds.loadRigControlShapes(self.root, file=self.path)
        cmds.undo()
        self.assertEqual(shape_transform("c_ctrl_l"), IDENTITY)
        self.assertEqual(cmds.getAttr(rig_name("c_ctrl_l") + ".shapeType"), 0)
        cmds.redo()
        self.assertEqual(shape_transform("c_ctrl_l"), SCALE_TWO)

    def test_missing_entries_are_skipped_and_extra_entries_ignored(self):
        self.save_edited()
        data = self.read_file()
        del data["controls"]["Center"]["a"]
        data["controls"]["Right"] = {"ghost": data["controls"]["Left"]["c"]}
        self.write_file(data)
        applied = short(cmds.loadRigControlShapes(self.root, file=self.path))
        self.assertNotIn("a_ctrl_c", applied)
        self.assertIn("c_ctrl_l", applied)
        self.assertEqual(shape_transform("c_ctrl_l"), SCALE_TWO)

    def test_invalid_file_changes_nothing(self):
        self.save_edited()
        data = self.read_file()
        data["controls"]["Center"]["a"]["shapeType"] = "Blob"
        self.write_file(data)
        self.assertMayaError(lambda: cmds.loadRigControlShapes(self.root, file=self.path),
                             "controls.Center.a.shapeType", "Blob", "nothing was loaded")
        self.assertEqual(shape_transform("c_ctrl_l"), IDENTITY)

    def test_validation_errors(self):
        def entry(**changes):
            fields = {"shapeType": "Box", "shapeTransform": IDENTITY, "filled": False, "wireframeAlpha": 1.0}
            fields.update(changes)
            return {"formatVersion": 2, "controls": {"Left": {"c": fields}}}

        missing_filled = entry()
        del missing_filled["controls"]["Left"]["c"]["filled"]
        missing_alpha = entry()
        del missing_alpha["controls"]["Left"]["c"]["wireframeAlpha"]
        cases = [
            ({"formatVersion": 3, "controls": {}}, "formatVersion"),
            ({"formatVersion": 0, "controls": {}}, "formatVersion"),
            ({"formatVersion": "2", "controls": {}}, "must be an integer"),
            ({"controls": {}}, "missing required key 'formatVersion'"),
            ({"formatVersion": 2, "controls": {}, "colors": {}}, "unknown key 'colors'"),
            ({"formatVersion": 2, "controls": {"Middle": {}}}, "Middle"),
            (entry(shapeTransform=[1, 0]), "16 numbers"),
            (missing_filled, "missing required key 'filled'"),
            (missing_alpha, "missing required key 'wireframeAlpha'"),
            (entry(wireframeAlpha=1.5), "wireframeAlpha", "0 to 1"),
            (entry(wireframeAlpha=-0.1), "wireframeAlpha", "0 to 1"),
            (entry(wireframeAlpha="1"), "wireframeAlpha", "0 to 1"),
        ]
        for data, *expected in cases:
            with self.subTest(expected=expected):
                self.write_file(data)
                self.assertMayaError(lambda: cmds.loadRigControlShapes(self.root, file=self.path), *expected)

    def test_bad_json(self):
        with open(self.path, "w", encoding="utf-8") as handle:
            handle.write("{ not json")
        self.assertMayaError(lambda: cmds.loadRigControlShapes(self.root, file=self.path), "shape file")

    def test_missing_file(self):
        self.assertMayaError(lambda: cmds.loadRigControlShapes(self.root, file=self.path + ".missing"), "Could not read")


class ControlMarkupValidation(RigTestCase):

    def test_joint_usage_clashing_with_layout_control_fails(self):
        joints = self.make_skeleton("fk_three")
        cmds.rigJointMarkup(joints[1], edit=True, usage="layout_0")
        template = {
            "rigVersion": 1, "rigAuthor": "tests",
            "rigTemplateName": "Clash",
            "rigModules": [{"moduleType": "FkChain", "name": "fkchain_1", "shapeType": "Box", "side": "Center",
                            "moduleArgs": {"joints": [["Center", "a"], ["Center", "layout_0"]]}}],
        }
        self.assertMayaError(lambda: self.build(template, joints[:2]), "fkchain_1", "usage layout_0", "Nothing was built")
        self.assertNothingBuilt()
