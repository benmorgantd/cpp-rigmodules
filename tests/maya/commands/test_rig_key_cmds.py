"""getRigKeys and setRigKeys (D34): every key of a call written at once through MFnAnimCurve, undoable as one step, and
read back as plain data keyed by control markup.

fkchain_three has Center controls a, b and c. mirror_arms has the arm's `space` enum, which keys stepped.
"""
import json

from maya import cmds

from rigtest.case import RigTestCase, rig_name

A = rig_name("a_ctrl_c")
B = rig_name("b_ctrl_c")


def rig_root():
    return cmds.ls(rig_name("*"), type="rigRootModule")[0]


def curves():
    return cmds.ls(type="animCurve") or []


class KeyCmdsTestCase(RigTestCase):

    def setUp(self):
        super().setUp()
        self.build_standard("fkchain_three", "fk_three")

    def set_keys(self, keys, *scope):
        return cmds.setRigKeys(*(scope or (rig_root(),)), keys=json.dumps(keys))

    def get_keys(self, *scope):
        return json.loads(cmds.getRigKeys(*(scope or (rig_root(),))))

    def values(self, plug):
        return cmds.keyframe(plug, query=True, valueChange=True) or []

    def times(self, plug):
        return cmds.keyframe(plug, query=True, timeChange=True) or []


class SetRigKeys(KeyCmdsTestCase):

    def test_keys_land_on_the_channels_by_markup(self):
        changed = self.set_keys({"Center": {"a": {"rotateZ": [[1, 0], [10, 90], [20, 45]], "translateX": [[1, 0], [20, 2.5]]}}})
        self.assertEqual([c.split(":")[-1] for c in changed], ["a_ctrl_c"])
        self.assertEqual(self.times(A + ".rotateZ"), [1, 10, 20])
        self.assertEqual(self.values(A + ".rotateZ"), [0, 90, 45])
        self.assertEqual(self.times(A + ".translateX"), [1, 20])
        self.assertEqual(self.values(A + ".translateX"), [0, 2.5])
        self.assertFalse(cmds.keyframe(B + ".rotateZ", query=True, keyframeCount=True))

    def test_the_curves_drive_the_rig(self):
        self.set_keys({"Center": {"a": {"rotateZ": [[1, 0], [11, 90]]}}})
        cmds.currentTime(6)
        self.assertAlmostEqual(cmds.getAttr(A + ".rotateZ"), 45.0, places=0)

    def test_values_are_in_ui_units(self):
        cmds.currentUnit(angle="rad")
        self.set_keys({"Center": {"a": {"rotateZ": [[1, 0.0], [2, 1.5]]}}})
        self.assertAlmostEqual(self.values(A + ".rotateZ")[1], 1.5, places=5)
        self.assertAlmostEqual(self.get_keys()["Center"]["a"]["rotateZ"][1][1], 1.5, places=5)

    def test_a_new_key_replaces_one_on_the_same_frame_and_keeps_the_others(self):
        self.set_keys({"Center": {"a": {"rotateZ": [[1, 0], [10, 90], [20, 45]]}}})
        self.set_keys({"Center": {"a": {"rotateZ": [[10, 10]]}}})
        self.assertEqual(self.times(A + ".rotateZ"), [1, 10, 20])
        self.assertEqual(self.values(A + ".rotateZ"), [0, 10, 45])

    def test_the_new_keys_replace_what_lies_between_them(self):
        # A channel's keys cover the frames from its first new key to its last. Keys inside that range go, keys outside stay.
        self.set_keys({"Center": {"a": {"rotateZ": [[1, 0], [10, 90], [20, 45], [30, 7]]}}})
        self.set_keys({"Center": {"a": {"rotateZ": [[1, 5], [20, 6]]}}})
        self.assertEqual(self.times(A + ".rotateZ"), [1, 20, 30])
        for actual, expected in zip(self.values(A + ".rotateZ"), [5, 6, 7]):
            self.assertAlmostEqual(actual, expected, places=6)

    def test_only_the_controls_in_the_scope_are_keyed(self):
        keys = {"Center": {"a": {"rotateZ": [[1, 0], [5, 10]]}, "b": {"rotateZ": [[1, 0], [5, 20]]}}}
        changed = self.set_keys(keys, B)
        self.assertEqual([c.split(":")[-1] for c in changed], ["b_ctrl_c"])
        self.assertTrue(cmds.keyframe(B + ".rotateZ", query=True, keyframeCount=True))
        self.assertFalse(cmds.keyframe(A + ".rotateZ", query=True, keyframeCount=True))

    def test_a_whole_call_is_one_undo(self):
        self.set_keys({"Center": {"a": {"rotateZ": [[1, 0], [10, 90]], "translateX": [[1, 0], [10, 1]]}, "b": {"rotateX": [[1, 0], [10, 5]]}}})
        self.assertEqual(len(curves()), 3)
        cmds.undo()
        self.assertEqual(curves(), [])
        cmds.redo()
        self.assertEqual(len(curves()), 3)
        self.assertEqual(self.values(A + ".rotateZ"), [0, 90])
        self.assertEqual(self.values(B + ".rotateX"), [0, 5])

    def test_undo_removes_only_the_keys_it_added_to_an_existing_curve(self):
        self.set_keys({"Center": {"a": {"rotateZ": [[1, 0], [10, 90]]}}})
        self.set_keys({"Center": {"a": {"rotateZ": [[20, 45]]}}})
        self.assertEqual(self.times(A + ".rotateZ"), [1, 10, 20])
        cmds.undo()
        self.assertEqual(self.times(A + ".rotateZ"), [1, 10])
        cmds.redo()
        self.assertEqual(self.times(A + ".rotateZ"), [1, 10, 20])

    def test_keys_may_come_in_any_order(self):
        self.set_keys({"Center": {"a": {"rotateZ": [[20, 45], [1, 0], [10, 90]]}}})
        self.assertEqual(self.times(A + ".rotateZ"), [1, 10, 20])
        self.assertEqual(self.values(A + ".rotateZ"), [0, 90, 45])

    def test_a_control_or_channel_the_rig_lacks_is_skipped(self):
        changed = self.set_keys({"Center": {"zzz": {"rotateZ": [[1, 0]]}, "a": {"nope": [[1, 0]], "rotateZ": [[1, 5]]}}, "Left": {"a": {"rotateZ": [[1, 0]]}}})
        self.assertEqual([c.split(":")[-1] for c in changed], ["a_ctrl_c"])
        self.assertEqual(self.values(A + ".rotateZ"), [5])

    def test_a_bad_string_changes_nothing(self):
        bad = {
            "not json": "{ nope",
            "a side": json.dumps({"Middle": {}}),
            "a key": json.dumps({"Center": {"a": {"rotateZ": [[1]]}}}),
            "a repeated frame": json.dumps({"Center": {"a": {"rotateZ": [[1, 0], [1, 5]]}}}),
            "an empty usage": json.dumps({"Center": {"": {}}}),
        }
        for label, text in bad.items():
            self.assertMayaError(lambda: cmds.setRigKeys(rig_root(), keys=text), "keys failed validation")
        self.assertEqual(curves(), [])

    def test_the_keys_flag_is_required(self):
        self.assertMayaError(lambda: cmds.setRigKeys(rig_root()), "setRigKeys", "keys")

    def test_a_node_that_is_not_a_control_is_an_error(self):
        node = cmds.createNode("transform")
        self.assertMayaError(lambda: cmds.setRigKeys(node, keys="{}"), "Not a rig root or rig control")


class SteppedChannels(RigTestCase):

    def test_an_enum_keys_stepped(self):
        self.build_standard("mirror_arms", "mirror_arms")
        plug = rig_name("upperArm_l_ctrl_l.space")
        cmds.setRigKeys(rig_root(), keys=json.dumps({"Left": {"upperArm": {"space": [[1, 0], [10, 1]]}}}))
        self.assertEqual(cmds.keyTangent(plug, query=True, outTangentType=True), ["step", "step"])
        self.assertEqual(cmds.keyframe(plug, query=True, valueChange=True), [0, 1])


class GetRigKeys(KeyCmdsTestCase):

    def test_a_rig_without_animation_has_no_keys(self):
        self.assertEqual(self.get_keys(), {})

    def test_what_was_set_is_what_comes_back(self):
        keys = {"Center": {"a": {"rotateZ": [[1.0, 0.0], [10.0, 90.0], [20.0, 45.0]], "translateX": [[1.0, 0.0], [20.0, 2.5]]},
                           "b": {"rotateX": [[3.0, 12.0]]}}}
        self.set_keys(keys)
        found = self.get_keys()
        self.assertEqual(found["Center"]["a"], keys["Center"]["a"])
        self.assertAlmostEqual(found["Center"]["b"]["rotateX"][0][1], 12.0, places=9)

    def test_only_channels_with_a_curve_are_listed(self):
        cmds.setKeyframe(A + ".rotateZ", time=4, value=7)
        cmds.setAttr(B + ".rotateZ", 30)
        self.assertEqual(self.get_keys(), {"Center": {"a": {"rotateZ": [[4.0, 7.0]]}}})

    def test_a_scope_of_controls(self):
        self.set_keys({"Center": {"a": {"rotateZ": [[1, 0]]}, "b": {"rotateZ": [[1, 0]]}}})
        self.assertEqual(list(self.get_keys(B)["Center"]), ["b"])

    def test_keys_set_by_setkeyframe_are_read(self):
        for frame, value in ((1, 0), (5, 20), (9, 10)):
            cmds.setKeyframe(A + ".rotateZ", time=frame, value=value)
        self.assertEqual(self.get_keys()["Center"]["a"]["rotateZ"], [[1.0, 0.0], [5.0, 20.0], [9.0, 10.0]])
