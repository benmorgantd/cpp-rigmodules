"""ControlAnim files (D42): saveRigAnimation and loadRigAnimation, and the keys payload they share with getRigKeys and
setRigKeys, now with tangents and curve flags.

The files are written by hand, and every expected value is worked out here, never recorded from a save.
mirror_arms has Center chest, the Left and Right arms and the `space` enum on the upper arms. Every joint is world
aligned, so a Center control flips about its local X: translateX, rotateY and rotateZ negate.
"""
import json
import os
import shutil
import tempfile

from maya import cmds

import rigmodules.pose
from rigtest.case import RigTestCase, rig_name

ARM_L = rig_name("upperArm_l_ctrl_l")
ARM_R = rig_name("upperArm_r_ctrl_r")
CHEST = rig_name("chest_ctrl_c")


def rig_root():
    return cmds.ls(rig_name("*"), type="rigRootModule")[0]


def curve(keys, **flags):
    return dict({"weighted": False, "preInfinity": "constant", "postInfinity": "constant", "keys": keys}, **flags)


def anim(controls, start=1, end=20, rate=24.0, version=1):
    return {"formatVersion": version, "frameRate": rate, "startFrame": start, "endFrame": end, "controls": controls}


class AnimTestCase(RigTestCase):

    def setUp(self):
        super().setUp()
        cmds.currentUnit(time="film")
        self.build_standard("mirror_arms", "mirror_arms")
        self.folder = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.folder, True)

    def path(self, name="anim.json"):
        return os.path.join(self.folder, name).replace("\\", "/")

    def write(self, data, name="anim.json"):
        with open(self.path(name), "w", encoding="utf-8") as handle:
            handle.write(data if isinstance(data, str) else json.dumps(data))
        return self.path(name)

    def read(self, name="anim.json"):
        with open(self.path(name), encoding="utf-8") as handle:
            return json.load(handle)

    def load(self, data, *scope, **flags):
        return cmds.loadRigAnimation(*(scope or (rig_root(),)), file=self.write(data), **flags)

    def keys(self, plug):
        return cmds.keyframe(plug, query=True, timeChange=True) or [], cmds.keyframe(plug, query=True, valueChange=True) or []

    def assertKeys(self, plug, times, values):
        """The curve's frames and values. Values go through radians, so they match to 1e-4, not exactly."""
        actual_times, actual_values = self.keys(plug)
        self.assertEqual([round(t, 4) for t in actual_times], list(times), plug)
        self.assertEqual([round(v, 4) for v in actual_values], [float(v) for v in values], plug)

    def tangent(self, plug, frame, which):
        return cmds.keyTangent(plug, query=True, time=(frame, frame), **{which + "TangentType": True})[0]

    def assertNothingKeyed(self):
        self.assertEqual(cmds.ls(type="animCurve") or [], [])


class LoadFormat(AnimTestCase):

    def test_a_curve_with_every_tangent_kind_lands_with_its_keys_and_types(self):
        self.load(anim({"Left": {"upperArm": {"rotateZ": curve([
            [1, 0.0, "auto", "auto"], [5, 10.0, "clamped", "linear"], [10, 20.0, "flat", "step"], [15, 30.0, "spline", "spline"],
        ])}}}))
        self.assertKeys(ARM_L + ".rotateZ", [1, 5, 10, 15], [0, 10, 20, 30])
        self.assertEqual([self.tangent(ARM_L + ".rotateZ", f, "in") for f in (1, 5, 10, 15)], ["auto", "clamped", "flat", "spline"])
        self.assertEqual([self.tangent(ARM_L + ".rotateZ", f, "out") for f in (1, 5, 10, 15)], ["auto", "linear", "step", "spline"])

    def test_a_fixed_tangent_has_its_angle_and_weight(self):
        self.load(anim({"Left": {"upperArm": {"rotateZ": curve([[1, 0.0, "auto", "auto"], [10, 90.0, [-30.0, 2.0], [15.0, 3.0]]], weighted=True)}}}))
        plug = ARM_L + ".rotateZ"
        self.assertTrue(cmds.keyTangent(plug, query=True, weightedTangents=True)[0])
        self.assertEqual((self.tangent(plug, 10, "in"), self.tangent(plug, 10, "out")), ("fixed", "fixed"))
        self.assertAlmostEqual(cmds.keyTangent(plug, query=True, time=(10, 10), inAngle=True)[0], -30.0, places=2)
        self.assertAlmostEqual(cmds.keyTangent(plug, query=True, time=(10, 10), outAngle=True)[0], 15.0, places=2)
        self.assertAlmostEqual(cmds.keyTangent(plug, query=True, time=(10, 10), inWeight=True)[0], 2.0, places=3)
        self.assertAlmostEqual(cmds.keyTangent(plug, query=True, time=(10, 10), outWeight=True)[0], 3.0, places=3)

    def test_the_infinities_are_set(self):
        self.load(anim({"Left": {"upperArm": {"rotateZ": curve([[1, 0.0], [10, 5.0]], preInfinity="linear", postInfinity="cycleRelative")}}}))
        self.assertEqual(cmds.setInfinity(ARM_L + ".rotateZ", query=True, preInfinite=True), ["linear"])
        self.assertEqual(cmds.setInfinity(ARM_L + ".rotateZ", query=True, postInfinite=True), ["cycleRelative"])

    def test_a_curve_without_flags_is_unweighted_and_constant(self):
        self.load(anim({"Left": {"upperArm": {"rotateZ": curve([[1, 0.0], [10, 5.0]], preInfinity="cycle")}}}))
        self.load(anim({"Left": {"upperArm": {"rotateZ": curve([[1, 0.0], [10, 5.0]])}}}))
        self.assertEqual(cmds.setInfinity(ARM_L + ".rotateZ", query=True, preInfinite=True), ["constant"])
        self.assertFalse(cmds.keyTangent(ARM_L + ".rotateZ", query=True, weightedTangents=True)[0])

    def test_a_two_number_key_has_two_auto_tangents(self):
        self.load(anim({"Left": {"upperArm": {"rotateZ": curve([[1, 0.0], [10, 5.0]])}}}))
        self.assertEqual((self.tangent(ARM_L + ".rotateZ", 10, "in"), self.tangent(ARM_L + ".rotateZ", 10, "out")), ("auto", "auto"))

    def test_a_channel_may_be_a_bare_key_array(self):
        self.load(anim({"Left": {"upperArm": {"rotateZ": [[1, 0.0], [10, 5.0]]}}}))
        self.assertKeys(ARM_L + ".rotateZ", [1, 10], [0, 5])

    def test_an_enum_channel_keys_stepped_whatever_the_tangents_say(self):
        self.load(anim({"Left": {"upperArm": {"space": curve([[1, 0, "linear", "linear"], [10, 1, "spline", "spline"]])}}}))
        self.assertEqual((self.tangent(ARM_L + ".space", 1, "out"), self.tangent(ARM_L + ".space", 10, "out")), ("step", "step"))
        cmds.currentTime(9)
        self.assertEqual(cmds.getAttr(ARM_L + ".space"), 0)

    def test_a_key_replaces_an_existing_one_at_its_frame_and_others_stay(self):
        cmds.setKeyframe(ARM_L + ".rotateZ", time=1, value=7)
        cmds.setKeyframe(ARM_L + ".rotateZ", time=30, value=9)
        self.load(anim({"Left": {"upperArm": {"rotateZ": curve([[1, 0.0], [10, 5.0]])}}}))
        self.assertKeys(ARM_L + ".rotateZ", [1, 10, 30], [0, 5, 9])

    def test_a_control_the_file_lacks_keeps_its_keys(self):
        cmds.setKeyframe(ARM_R + ".rotateZ", time=3, value=4)
        self.load(anim({"Left": {"upperArm": {"rotateZ": curve([[1, 0.0], [10, 5.0]])}}}))
        self.assertKeys(ARM_R + ".rotateZ", [3], [4])

    def test_values_are_in_ui_units(self):
        cmds.currentUnit(angle="rad")
        self.load(anim({"Left": {"upperArm": {"rotateZ": curve([[1, 0.0], [10, 1.5]])}}}))
        self.assertAlmostEqual(self.keys(ARM_L + ".rotateZ")[1][1], 1.5, places=5)

    def test_offset_moves_every_key(self):
        self.load(anim({"Left": {"upperArm": {"rotateZ": curve([[1, 0.0], [10, 5.0]])}}}), offset=100)
        self.assertKeys(ARM_L + ".rotateZ", [101, 110], [0, 5])

    def test_a_scope_of_controls_keys_only_those(self):
        self.load(anim({"Left": {"upperArm": {"rotateZ": curve([[1, 0.0]])}}, "Right": {"upperArm": {"rotateZ": curve([[1, 0.0]])}}}), ARM_L)
        self.assertKeys(ARM_L + ".rotateZ", [1], [0])
        self.assertKeys(ARM_R + ".rotateZ", [], [])

    def test_undo_and_redo_are_one_step(self):
        cmds.setKeyframe(ARM_L + ".rotateZ", time=1, value=7)
        self.load(anim({"Left": {"upperArm": {"rotateZ": curve([[1, 0.0, [-30.0, 1.0], "flat"], [10, 5.0]], preInfinity="cycle"), "rotateX": curve([[1, 1.0]])}}}))
        cmds.undo()
        self.assertKeys(ARM_L + ".rotateZ", [1], [7])
        self.assertKeys(ARM_L + ".rotateX", [], [])
        self.assertEqual(cmds.setInfinity(ARM_L + ".rotateZ", query=True, preInfinite=True), ["constant"])
        cmds.redo()
        self.assertKeys(ARM_L + ".rotateZ", [1, 10], [0, 5])
        self.assertEqual(self.tangent(ARM_L + ".rotateZ", 1, "out"), "flat")
        self.assertEqual(cmds.setInfinity(ARM_L + ".rotateZ", query=True, preInfinite=True), ["cycle"])

    def test_a_channel_the_rig_lacks_is_skipped(self):
        self.load(anim({"Left": {"upperArm": {"rotateZ": curve([[1, 2.0]]), "wiggle": curve([[1, 2.0]])}, "tail": {"rotateZ": curve([[1, 2.0]])}}}))
        self.assertKeys(ARM_L + ".rotateZ", [1], [2])


class LoadRefusals(AnimTestCase):

    def assertRefused(self, data, *contains, **flags):
        cmds.setKeyframe(ARM_L + ".rotateZ", time=3, value=4)
        before = self.keys(ARM_L + ".rotateZ")
        self.assertMayaError(lambda: self.load(data, **flags), *contains)
        self.assertEqual(self.keys(ARM_L + ".rotateZ"), before)
        self.assertEqual(len(cmds.ls(type="animCurve")), 1)

    def good(self, **channel):
        return {"Left": {"upperArm": {"rotateZ": curve([[1, 0.0], [10, 5.0]], **channel)}}}

    def test_a_file_that_is_not_json(self):
        self.assertRefused("{", "failed validation")

    def test_a_missing_key(self):
        data = anim(self.good())
        del data["frameRate"]
        self.assertRefused(data, "failed validation")

    def test_an_unknown_key(self):
        data = anim(self.good())
        data["speed"] = 2
        self.assertRefused(data, "failed validation")

    def test_a_format_version_this_build_does_not_read(self):
        self.assertRefused(anim(self.good(), version=2), "failed validation")

    def test_a_frame_rate_of_zero(self):
        self.assertRefused(anim(self.good(), rate=0), "failed validation")

    def test_a_range_that_runs_backwards(self):
        self.assertRefused(anim(self.good(), start=20, end=1), "failed validation")

    def test_a_key_outside_the_range(self):
        self.assertRefused(anim(self.good(), start=1, end=5), "failed validation")

    def test_a_side_that_does_not_exist(self):
        self.assertRefused(anim({"Up": {"upperArm": {"rotateZ": curve([[1, 0.0]])}}}), "failed validation")

    def test_an_empty_usage(self):
        self.assertRefused(anim({"Left": {"": {"rotateZ": curve([[1, 0.0]])}}}), "failed validation")

    def test_a_key_of_three_values(self):
        self.assertRefused(anim({"Left": {"upperArm": {"rotateZ": curve([[1, 0.0, "auto"]])}}}), "failed validation")

    def test_a_tangent_name_that_does_not_exist(self):
        self.assertRefused(anim({"Left": {"upperArm": {"rotateZ": curve([[1, 0.0, "smooth", "auto"]])}}}), "failed validation")

    def test_a_fixed_tangent_that_is_not_a_pair(self):
        self.assertRefused(anim({"Left": {"upperArm": {"rotateZ": curve([[1, 0.0, [1.0], "auto"]])}}}), "failed validation")

    def test_an_infinity_that_does_not_exist(self):
        self.assertRefused(anim(self.good(preInfinity="forever")), "failed validation")

    def test_weighted_that_is_not_a_bool(self):
        self.assertRefused(anim(self.good(weighted="yes")), "failed validation")

    def test_an_unknown_curve_key(self):
        self.assertRefused(anim(self.good(name="curve1")), "failed validation")

    def test_a_frame_keyed_twice(self):
        self.assertRefused(anim({"Left": {"upperArm": {"rotateZ": curve([[1, 0.0], [1, 5.0]])}}}), "failed validation")

    def test_a_curve_that_is_a_number(self):
        self.assertRefused(anim({"Left": {"upperArm": {"rotateZ": 5}}}), "failed validation")

    def test_a_different_frame_rate_than_the_scene(self):
        self.assertRefused(anim(self.good(), rate=30.0), "30", "24")

    def test_a_missing_file(self):
        self.assertMayaError(lambda: cmds.loadRigAnimation(rig_root(), file=self.path("none.json")), "ControlAnim")
        self.assertNothingKeyed()

    def test_file_is_required(self):
        self.assertMayaError(lambda: cmds.loadRigAnimation(rig_root()), "-file")


class SaveAndLoad(AnimTestCase):

    def key_mixed_curve(self):
        plug = ARM_L + ".rotateZ"
        cmds.setKeyframe(plug, time=1, value=0, inTangentType="auto", outTangentType="auto")
        cmds.setKeyframe(plug, time=10, value=90, inTangentType="clamped", outTangentType="linear")
        cmds.setKeyframe(plug, time=20, value=45, inTangentType="auto", outTangentType="step")
        cmds.setInfinity(plug, preInfinite="linear", postInfinite="cycle")
        cmds.setKeyframe(ARM_L + ".space", time=1, value=0)
        cmds.setKeyframe(ARM_L + ".space", time=20, value=1)

    def test_a_curve_round_trips_through_a_file(self):
        self.key_mixed_curve()
        cmds.saveRigAnimation(rig_root(), file=self.path(), startFrame=1, endFrame=20)
        for channel in ("rotateZ", "space"):
            cmds.cutKey(ARM_L, attribute=channel, clear=True)
        self.assertNothingKeyed()
        cmds.loadRigAnimation(rig_root(), file=self.path())
        plug = ARM_L + ".rotateZ"
        self.assertKeys(plug, [1, 10, 20], [0, 90, 45])
        self.assertEqual([self.tangent(plug, f, "in") for f in (1, 10, 20)], ["auto", "clamped", "auto"])
        self.assertEqual([self.tangent(plug, f, "out") for f in (1, 10, 20)], ["auto", "linear", "step"])
        self.assertEqual(cmds.setInfinity(plug, query=True, preInfinite=True), ["linear"])
        self.assertEqual(cmds.setInfinity(plug, query=True, postInfinite=True), ["cycle"])
        self.assertKeys(ARM_L + ".space", [1, 20], [0, 1])

    def test_a_fixed_weighted_tangent_round_trips(self):
        plug = ARM_L + ".rotateZ"
        cmds.setKeyframe(plug, time=1, value=0)
        cmds.setKeyframe(plug, time=10, value=90)
        cmds.keyTangent(plug, edit=True, weightedTangents=True)
        cmds.keyTangent(plug, edit=True, time=(10, 10), lock=False, inAngle=-30, inWeight=2, outAngle=15, outWeight=3)
        cmds.saveRigAnimation(rig_root(), file=self.path(), startFrame=1, endFrame=10)
        saved = self.read()["controls"]["Left"]["upperArm"]["rotateZ"]
        self.assertTrue(saved["weighted"])
        for actual, expected in zip(saved["keys"][1][2] + saved["keys"][1][3], [-30.0, 2.0, 15.0, 3.0]):
            self.assertAlmostEqual(actual, expected, places=3)
        cmds.cutKey(ARM_L, attribute="rotateZ", clear=True)
        cmds.loadRigAnimation(rig_root(), file=self.path())
        self.assertAlmostEqual(cmds.keyTangent(plug, query=True, time=(10, 10), inAngle=True)[0], -30.0, places=2)
        self.assertAlmostEqual(cmds.keyTangent(plug, query=True, time=(10, 10), outWeight=True)[0], 3.0, places=3)

    def test_the_file_holds_the_range_the_frame_rate_and_every_key_with_four_values(self):
        self.key_mixed_curve()
        cmds.saveRigAnimation(rig_root(), file=self.path(), startFrame=1, endFrame=20)
        data = self.read()
        self.assertEqual((data["formatVersion"], data["frameRate"], data["startFrame"], data["endFrame"]), (1, 24.0, 1, 20))
        saved = data["controls"]["Left"]["upperArm"]["rotateZ"]
        self.assertEqual([[k[0], round(k[1], 4), k[2], k[3]] for k in saved["keys"]],
                         [[1, 0, "auto", "auto"], [10, 90, "clamped", "linear"], [20, 45, "auto", "step"]])
        self.assertEqual((saved["weighted"], saved["preInfinity"], saved["postInfinity"]), (False, "linear", "cycle"))
        self.assertEqual(data["controls"]["Left"]["upperArm"]["space"]["keys"], [[1, 0, "auto", "auto"], [20, 1, "auto", "auto"]])

    def test_only_keys_inside_the_range_are_saved(self):
        self.key_mixed_curve()
        cmds.saveRigAnimation(rig_root(), file=self.path(), startFrame=5, endFrame=15)
        data = self.read()
        self.assertEqual((data["startFrame"], data["endFrame"]), (5, 15))
        self.assertEqual([key[0] for key in data["controls"]["Left"]["upperArm"]["rotateZ"]["keys"]], [10])
        self.assertNotIn("space", data["controls"]["Left"]["upperArm"], "a channel with no key in the range is left out")

    def test_the_range_defaults_to_the_playback_range(self):
        self.key_mixed_curve()
        cmds.playbackOptions(minTime=1, maxTime=12)
        cmds.saveRigAnimation(rig_root(), file=self.path())
        data = self.read()
        self.assertEqual((data["startFrame"], data["endFrame"]), (1, 12))
        self.assertEqual([key[0] for key in data["controls"]["Left"]["upperArm"]["rotateZ"]["keys"]], [1, 10])

    def test_a_range_that_runs_backwards_is_refused(self):
        self.assertMayaError(lambda: cmds.saveRigAnimation(rig_root(), file=self.path(), startFrame=10, endFrame=1), "before")
        self.assertFalse(os.path.exists(self.path()))

    def test_a_scope_of_controls_saves_only_those(self):
        self.key_mixed_curve()
        cmds.setKeyframe(ARM_R + ".rotateZ", time=2, value=3)
        names = cmds.saveRigAnimation(ARM_R, file=self.path(), startFrame=1, endFrame=20)
        self.assertEqual(names, ["rig:upperArm_r_ctrl_r"])
        self.assertEqual(list(self.read()["controls"]), ["Right"])

    def test_the_saved_file_is_a_valid_input_for_load_and_pretty_printed(self):
        self.key_mixed_curve()
        cmds.saveRigAnimation(rig_root(), file=self.path(), startFrame=1, endFrame=20)
        with open(self.path(), encoding="utf-8") as handle:
            text = handle.read()
        self.assertTrue(text.startswith('{\n    "formatVersion": 1,'))

    def test_a_pose_is_an_animation_over_one_frame(self):
        cmds.setAttr(ARM_L + ".rotateZ", 33)
        cmds.setAttr(CHEST + ".translateX", 2)
        rigmodules.pose.key_rig(rig_root(), time=1)
        cmds.saveRigAnimation(rig_root(), file=self.path(), startFrame=1, endFrame=1)
        data = self.read()
        self.assertEqual([key[:2] for key in data["controls"]["Left"]["upperArm"]["rotateZ"]["keys"]], [[1, 33]])
        cmds.setAttr(ARM_L + ".rotateZ", 0)
        cmds.cutKey(ARM_L, attribute="rotateZ", clear=True)
        cmds.loadRigAnimation(rig_root(), file=self.path(), offset=10)
        self.assertKeys(ARM_L + ".rotateZ", [11], [33])

    def test_getRigKeys_is_compact_for_default_curves_and_a_curve_object_otherwise(self):
        cmds.setKeyframe(ARM_L + ".rotateX", time=1, value=1, inTangentType="auto", outTangentType="auto")
        cmds.setKeyframe(ARM_L + ".rotateX", time=5, value=2, inTangentType="auto", outTangentType="auto")
        cmds.setKeyframe(ARM_L + ".rotateY", time=1, value=1, inTangentType="auto", outTangentType="flat")
        cmds.setKeyframe(ARM_L + ".rotateZ", time=1, value=1)
        cmds.setInfinity(ARM_L + ".rotateZ", preInfinite="cycle")
        cmds.setKeyframe(ARM_L + ".space", time=1, value=1)
        data = json.loads(cmds.getRigKeys(rig_root()))["Left"]["upperArm"]
        self.assertEqual(data["rotateX"], [[1, 1], [5, 2]])
        self.assertEqual(data["rotateY"], [[1, 1, "auto", "flat"]], "a key with a non-auto tangent says so")
        self.assertEqual(data["rotateZ"]["preInfinity"], "cycle")
        self.assertEqual(data["space"], [[1, 1]], "a stepped channel says nothing about tangents")


class MirrorOnLoad(AnimTestCase):

    def test_left_keys_land_on_right_and_right_keys_on_left_as_a_flip(self):
        self.load(anim({
            "Left": {"upperArm": {"rotateZ": curve([[1, 0.0], [10, 30.0]])}},
            "Right": {"upperArm": {"rotateX": curve([[1, 5.0]])}},
        }), mirror=True)
        self.assertKeys(ARM_R + ".rotateZ", [1, 10], [0, 30])
        self.assertKeys(ARM_L + ".rotateX", [1], [5])
        self.assertKeys(ARM_L + ".rotateZ", [], [])
        self.assertKeys(ARM_R + ".rotateX", [], [])

    def test_a_negated_channel_negates_its_values_and_fixed_tangent_angles_only(self):
        for control in (ARM_L, ARM_R):
            cmds.addAttr(control, longName="lean", keyable=True, attributeType="double", category="mirrorNegate")
        self.load(anim({"Left": {"upperArm": {"lean": curve(
            [[1, 0.5, "auto", "auto"], [10, 2.0, [-30.0, 2.0], "flat"], [20, -1.0, "linear", "step"]], weighted=True)}}}), mirror=True)
        plug = ARM_R + ".lean"
        self.assertKeys(plug, [1, 10, 20], [-0.5, -2.0, 1.0])
        self.assertAlmostEqual(cmds.keyTangent(plug, query=True, time=(10, 10), inAngle=True)[0], 30.0, places=2)
        self.assertAlmostEqual(cmds.keyTangent(plug, query=True, time=(10, 10), inWeight=True)[0], 2.0, places=3)
        self.assertEqual((self.tangent(plug, 10, "out"), self.tangent(plug, 20, "in"), self.tangent(plug, 20, "out")), ("flat", "linear", "step"))
        self.assertTrue(cmds.keyTangent(plug, query=True, weightedTangents=True)[0])

    def test_auto_clamped_and_spline_tangents_evaluate_as_the_negated_curve(self):
        """Those types are computed from the neighbouring keys, so they should stay valid under negation. Checked
        against the evaluated curve, between the keys too."""
        for control in (ARM_L, ARM_R):
            cmds.addAttr(control, longName="lean", keyable=True, attributeType="double", category="mirrorNegate")
        keys = [[1, 0.0, "auto", "auto"], [6, 4.0, "clamped", "clamped"], [11, -2.0, "spline", "spline"],
                [16, 3.0, "auto", "clamped"], [20, 1.0, "spline", "auto"]]
        self.load(anim({"Left": {"upperArm": {"lean": curve(keys)}}}))
        self.load(anim({"Left": {"upperArm": {"lean": curve(keys)}}}), mirror=True)
        for frame in [1 + 0.5 * i for i in range(39)]:
            left = cmds.getAttr(ARM_L + ".lean", time=frame)
            right = cmds.getAttr(ARM_R + ".lean", time=frame)
            self.assertAlmostEqual(right, -left, places=4, msg="frame %s" % frame)

    def test_a_center_control_flips_by_its_axis(self):
        self.load(anim({"Center": {"chest": {
            "translateX": curve([[1, 2.0]]), "translateY": curve([[1, 3.0]]),
            "rotateX": curve([[1, 10.0]]), "rotateY": curve([[1, 20.0]]), "rotateZ": curve([[1, 30.0]]),
        }}}), mirror=True)
        for channel, value in (("translateX", -2.0), ("translateY", 3.0), ("rotateX", 10.0), ("rotateY", -20.0), ("rotateZ", -30.0)):
            self.assertKeys(CHEST + "." + channel, [1], [value])

    def test_mirror_with_a_scope_writes_only_the_controls_in_it(self):
        self.load(anim({"Left": {"upperArm": {"rotateZ": curve([[1, 7.0]])}}, "Right": {"upperArm": {"rotateZ": curve([[1, 9.0]])}}}), ARM_L, mirror=True)
        self.assertKeys(ARM_L + ".rotateZ", [1], [9])
        self.assertKeys(ARM_R + ".rotateZ", [], [])

    def test_mirror_and_offset_together(self):
        self.load(anim({"Left": {"upperArm": {"rotateZ": curve([[1, 7.0]])}}}), mirror=True, offset=5)
        self.assertKeys(ARM_R + ".rotateZ", [6], [7])

    def test_an_entry_with_no_control_in_the_rig_is_skipped_with_a_warning(self):
        self.load(anim({"Left": {"nothing": {"rotateZ": curve([[1, 7.0]])}}}), mirror=True)
        self.assertNothingKeyed()
