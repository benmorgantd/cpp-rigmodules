"""getRigControls, saveRigControlShapes and loadRigControlShapes, plus the control markup the build writes.

Every test builds fkchain_two_chains: fkchain_1 (Center a, b), fkchain_2 (Left c) and the layout (Center layout_0..2).
"""
import json
import os

from maya import cmds

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


class SaveShapes(RigControlTestCase):

    def test_returns_saved_controls(self):
        self.assertEqual(short(cmds.saveRigControlShapes(self.root, file=self.path)), ALL_CONTROLS)

    def test_file_is_keyed_by_side_and_usage_without_color(self):
        cmds.setAttr(rig_name("c_ctrl_l") + ".shapeTransform", SCALE_TWO, type="matrix")
        cmds.setAttr(rig_name("c_ctrl_l") + ".shapeType", 13)   # SingleArrow
        cmds.setAttr(rig_name("c_ctrl_l") + ".filled", True)
        cmds.saveRigControlShapes(self.root, file=self.path)

        data = self.read_file()
        self.assertEqual(sorted(data), ["controls", "formatVersion"])
        self.assertEqual(data["formatVersion"], 1)
        self.assertEqual(sorted(data["controls"]), ["Center", "Left"])
        self.assertEqual(sorted(data["controls"]["Center"]), ["a", "b", "layout_0", "layout_1", "layout_2"])
        self.assertEqual(data["controls"]["Left"],
                         {"c": {"shapeType": "SingleArrow", "shapeTransform": SCALE_TWO, "filled": True}})

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
        cases = [
            ({"formatVersion": 2, "controls": {}}, "formatVersion"),
            ({"controls": {}}, "missing required key 'formatVersion'"),
            ({"formatVersion": 1, "controls": {}, "colors": {}}, "unknown key 'colors'"),
            ({"formatVersion": 1, "controls": {"Middle": {}}}, "Middle"),
            ({"formatVersion": 1, "controls": {"Left": {"c": {"shapeType": "Box", "shapeTransform": [1, 0], "filled": False}}}},
             "16 numbers"),
            ({"formatVersion": 1, "controls": {"Left": {"c": {"shapeType": "Box", "shapeTransform": IDENTITY}}}},
             "missing required key 'filled'"),
        ]
        for data, expected in cases:
            with self.subTest(expected=expected):
                self.write_file(data)
                self.assertMayaError(lambda: cmds.loadRigControlShapes(self.root, file=self.path), expected)

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
