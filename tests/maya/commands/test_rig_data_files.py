"""Library files (D51): the `meta` block in pose and ControlAnim files, getRigDataFileInfo, setRigDataFileMeta, and
loadRigPose's data-first mirror. Every test builds mirror_arms (Fk chains, a Layout and a space switch) unless it says
otherwise.
"""
import json
import os
import re
import shutil
import tempfile

from maya import cmds

import rigmodules.pose
import rigmodules.query
from rigtest.case import RigTestCase, rig_name

ARM_L = rig_name("upperArm_l_ctrl_l")
ARM_R = rig_name("upperArm_r_ctrl_r")
CHEST = rig_name("chest_ctrl_c")

CALLER_META = {
    "template": "Human", "skeleton": "body", "author": "bmorgan", "description": "Clenched right hand",
    "category": "Expressions", "status": "scratch", "tags": ["hands", "fist"], "rigRegions": ["hands"],
}
RIG_FIELDS = {"assetId", "rigName", "skeletonHash", "pluginVersion", "created"}


def rig_root():
    return cmds.ls(rig_name("*"), type="rigRootModule")[0]


def values(control, names):
    return [round(cmds.getAttr("%s.%s" % (control, name)), 4) for name in names]


class DataFileTestCase(RigTestCase):

    template = "mirror_arms"

    def setUp(self):
        super().setUp()
        cmds.currentUnit(time="film")
        self.build_standard(self.template, self.template)
        self.folder = tempfile.mkdtemp(prefix="rigdata_")
        self.addCleanup(shutil.rmtree, self.folder, True)

    def path(self, name="file.json"):
        return os.path.join(self.folder, name).replace("\\", "/")

    def write(self, name, data):
        path = self.path(name)
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(data, handle)
        return path

    def read(self, path):
        with open(path, encoding="utf-8") as handle:
            return json.load(handle)

    def text(self, path):
        with open(path, encoding="utf-8") as handle:
            return handle.read()

    def save_pose(self, name="pose.json", **flags):
        cmds.setAttr(ARM_L + ".rotateY", 20)
        cmds.setAttr(CHEST + ".translateX", 2)
        path = self.path(name)
        cmds.saveRigPose(rig_root(), file=path, **flags)
        return path

    def save_anim(self, name="anim.json", **flags):
        cmds.setKeyframe(ARM_L + ".rotateZ", time=1, value=0)
        cmds.setKeyframe(ARM_L + ".rotateZ", time=10, value=90)
        path = self.path(name)
        cmds.saveRigAnimation(rig_root(), file=path, startFrame=1, endFrame=10, **flags)
        return path

    def info(self, *files):
        return json.loads(cmds.getRigDataFileInfo(rig_root(), file=list(files)))


class SaveMeta(DataFileTestCase):
    """saveRigPose and saveRigAnimation write the rig's own fields, and the caller's through the meta flag."""

    def test_a_save_without_the_flag_holds_exactly_the_fields_the_rig_fills(self):
        for path in (self.save_pose(), self.save_anim()):
            data = self.read(path)
            self.assertEqual(data["formatVersion"], 3)
            self.assertEqual(set(data["meta"]), RIG_FIELDS)

    def test_the_rig_fields_come_from_the_rig(self):
        info = rigmodules.query.get_rig_info(rig_root())
        meta = self.read(self.save_pose())["meta"]
        self.assertEqual(meta["assetId"], info.asset_id)
        self.assertEqual(meta["rigName"], info.rig_name)
        self.assertEqual(meta["skeletonHash"], info.skeleton_hash)
        self.assertEqual(meta["pluginVersion"], info.plugin_version)
        self.assertRegex(meta["created"], r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$")

    def test_the_callers_meta_is_stored_in_a_pose(self):
        meta = self.read(self.save_pose(meta=json.dumps(CALLER_META)))["meta"]
        self.assertEqual({key: meta[key] for key in CALLER_META}, CALLER_META)
        self.assertEqual(set(meta), RIG_FIELDS | set(CALLER_META))

    def test_the_callers_meta_is_stored_in_an_animation(self):
        meta = self.read(self.save_anim(meta=json.dumps(CALLER_META)))["meta"]
        self.assertEqual({key: meta[key] for key in CALLER_META}, CALLER_META)

    def test_a_field_the_rig_fills_can_not_be_set(self):
        self.assertMayaError(lambda: cmds.saveRigPose(rig_root(), file=self.path(), meta='{"assetId": "x"}'), "filled from the rig")
        self.assertFalse(os.path.exists(self.path()))

    def test_a_value_of_the_wrong_shape_is_refused_and_nothing_is_written(self):
        for bad in ('{"tags": "hands"}', '{"tags": [1]}', '{"tags": [""]}', '{"status": 3}', '{"mood": "x"}', "[1]", "not json"):
            with self.subTest(meta=bad):
                self.assertMayaError(lambda: cmds.saveRigPose(rig_root(), file=self.path(), meta=bad), "meta")
                self.assertMayaError(lambda: cmds.saveRigAnimation(rig_root(), file=self.path(), meta=bad), "meta")
                self.assertFalse(os.path.exists(self.path()))

    def test_the_vocabulary_is_not_checked(self):
        meta = self.read(self.save_pose(meta='{"status": "anything", "rigRegions": ["tail"]}'))["meta"]
        self.assertEqual((meta["status"], meta["rigRegions"]), ("anything", ["tail"]))

    def test_a_pose_with_meta_loads_as_before(self):
        path = self.save_pose(meta=json.dumps(CALLER_META))
        cmds.zeroRigControls(rig_root())
        cmds.loadRigPose(rig_root(), file=path)
        self.assertEqual(values(ARM_L, ["rotateY"]), [20])


class OlderAndBadFiles(DataFileTestCase):
    """A version 1 file has no meta and still loads (D33). A version 2 file must have one."""

    POSE_V1 = {"formatVersion": 1, "controls": {"Left": {"upperArm": {"rotateY": 20}}}}
    ANIM_V1 = {"formatVersion": 1, "frameRate": 24.0, "startFrame": 1, "endFrame": 10, "controls": {
        "Left": {"upperArm": {"rotateZ": {"weighted": False, "preInfinity": "constant", "postInfinity": "constant",
                                          "keys": [[1, 0.0, "auto", "auto"], [10, 90.0, "auto", "auto"]]}}}}}

    def test_a_version_1_pose_loads(self):
        cmds.loadRigPose(rig_root(), file=self.write("old.json", self.POSE_V1))
        self.assertEqual(values(ARM_L, ["rotateY"]), [20])

    def test_a_version_1_animation_loads(self):
        cmds.loadRigAnimation(rig_root(), file=self.write("old.json", self.ANIM_V1))
        self.assertEqual(cmds.keyframe(ARM_L + ".rotateZ", query=True, valueChange=True), [0, 90])

    def test_the_info_of_a_version_1_file_says_its_stored_version_and_has_empty_meta(self):
        [pose, anim] = self.info(self.write("p.json", self.POSE_V1), self.write("a.json", self.ANIM_V1))
        self.assertEqual((pose["formatVersion"], pose["meta"]), (1, {}))
        self.assertEqual((anim["formatVersion"], anim["meta"]), (1, {}))

    def test_a_version_2_meta_is_renamed(self):
        # D54: variant became rigName and skeletonId became skeleton. A version 2 file has no skeletonHash.
        old_meta = {"assetId": "tests\\rig", "variant": "body", "skeletonId": "biped", "tags": ["a"]}
        new_meta = {"assetId": "tests\\rig", "rigName": "body", "skeleton": "biped", "tags": ["a"]}
        pose = self.write("p.json", dict(self.POSE_V1, formatVersion=2, meta=old_meta))
        anim = self.write("a.json", dict(self.ANIM_V1, formatVersion=2, meta=old_meta))
        for info in self.info(pose, anim):
            self.assertEqual((info["formatVersion"], info["meta"]), (2, new_meta))
        cmds.loadRigPose(rig_root(), file=pose)
        self.assertEqual(values(ARM_L, ["rotateY"]), [20])
        cmds.loadRigAnimation(rig_root(), file=anim)
        self.assertEqual(cmds.keyframe(ARM_L + ".rotateZ", query=True, valueChange=True), [0, 90])

    def test_a_version_2_meta_is_written_at_version_3(self):
        path = self.write("p.json", dict(self.POSE_V1, formatVersion=2, meta={"variant": "body", "skeletonId": "biped"}))
        cmds.setRigDataFileMeta(file=path, meta='{"tags": ["a"]}')
        data = self.read(path)
        self.assertEqual((data["formatVersion"], data["meta"]), (3, {"rigName": "body", "skeleton": "biped", "tags": ["a"]}))

    def test_a_version_3_file_with_a_version_2_key_is_refused(self):
        pose = dict(self.POSE_V1, formatVersion=3, meta={"variant": "body"})
        self.assertMayaError(lambda: cmds.loadRigPose(rig_root(), file=self.write("p.json", pose)), "failed validation")

    def test_a_version_2_file_without_meta_is_refused(self):
        pose = dict(self.POSE_V1, formatVersion=2)
        self.assertMayaError(lambda: cmds.loadRigPose(rig_root(), file=self.write("p.json", pose)), "failed validation")
        anim = dict(self.ANIM_V1, formatVersion=2)
        self.assertMayaError(lambda: cmds.loadRigAnimation(rig_root(), file=self.write("a.json", anim)), "failed validation")

    def test_a_bad_meta_in_a_file_is_refused_and_changes_nothing(self):
        cmds.setAttr(ARM_L + ".rotateY", 5)
        pose = dict(self.POSE_V1, formatVersion=2, meta={"tags": [1]})
        self.assertMayaError(lambda: cmds.loadRigPose(rig_root(), file=self.write("p.json", pose)), "failed validation")
        self.assertEqual(values(ARM_L, ["rotateY"]), [5])

    def test_a_file_with_an_unknown_key_is_still_refused(self):
        pose = dict(self.POSE_V1, formatVersion=2, meta={}, extra=1)
        self.assertMayaError(lambda: cmds.loadRigPose(rig_root(), file=self.write("p.json", pose)), "failed validation")


class GetRigDataFileInfo(DataFileTestCase):

    def test_a_pose(self):
        path = self.save_pose(meta=json.dumps(CALLER_META))
        [info] = self.info(path)
        self.assertEqual(info["file"], path)
        self.assertEqual((info["kind"], info["formatVersion"]), ("pose", 3))
        self.assertEqual(info["meta"]["tags"], ["hands", "fist"])
        self.assertEqual(info["meta"]["assetId"], rigmodules.query.get_rig_info(rig_root()).asset_id)
        data = self.read(path)["controls"]
        self.assertEqual(info["controlCount"], sum(len(usages) for usages in data.values()))
        self.assertEqual(info["channelCount"], sum(len(c) for usages in data.values() for c in usages.values()))
        self.assertEqual(info["unmatchedControls"], [])
        for key in ("frameRate", "startFrame", "endFrame"):
            self.assertNotIn(key, info)

    def test_an_animation_has_its_range_and_frame_rate(self):
        [info] = self.info(self.save_anim())
        self.assertEqual(info["kind"], "animation")
        self.assertEqual((info["frameRate"], info["startFrame"], info["endFrame"]), (24.0, 1, 10))
        self.assertEqual(info["channelCount"], 1)
        self.assertEqual(info["controlCount"], 1)

    def test_a_control_the_rig_lacks_is_listed_as_unmatched(self):
        path = self.write("p.json", {"formatVersion": 2, "meta": {}, "controls": {
            "Left": {"upperArm": {"rotateY": 1}, "noSuchControl": {"rotateY": 1}}, "Right": {"tail": {"rotateY": 1}}}})
        [info] = self.info(path)
        self.assertEqual(info["controlCount"], 3)
        self.assertEqual(info["unmatchedControls"], [["Left", "noSuchControl"], ["Right", "tail"]])

    def test_a_broken_or_missing_file_is_that_files_answer_and_the_others_still_answer(self):
        good = self.save_pose()
        broken = self.write("broken.json", {"formatVersion": 2, "meta": {}, "controls": {"Up": {}}})
        text = self.path("text.json")
        with open(text, "w", encoding="utf-8") as handle:
            handle.write("not json")
        missing = self.path("missing.json")
        results = self.info(broken, good, text, missing)
        self.assertEqual([entry["file"] for entry in results], [broken, good, text, missing])
        self.assertEqual(["error" in entry for entry in results], [True, False, True, True])
        self.assertIn("Up", results[0]["error"])

    def test_no_files_is_an_empty_list(self):
        self.assertEqual(json.loads(cmds.getRigDataFileInfo(rig_root())), [])

    def test_it_changes_nothing(self):
        path = self.save_pose()
        before = self.text(path)
        cmds.setAttr(ARM_L + ".rotateY", 7)
        self.info(path)
        self.assertEqual(self.text(path), before)
        self.assertEqual(values(ARM_L, ["rotateY"]), [7])

    def test_it_needs_a_rig_root(self):
        self.assertMayaError(lambda: cmds.getRigDataFileInfo(ARM_L, file=[self.save_pose()]), "rig root")


class SetRigDataFileMeta(DataFileTestCase):

    def test_keys_replace_fields_and_the_others_stay(self):
        path = self.save_pose(meta=json.dumps(CALLER_META))
        before = self.read(path)["meta"]
        cmds.setRigDataFileMeta(file=path, meta='{"status": "review", "tags": ["hands"]}')
        meta = self.read(path)["meta"]
        self.assertEqual((meta["status"], meta["tags"]), ("review", ["hands"]))
        for key in set(before) - {"status", "tags"}:
            self.assertEqual(meta[key], before[key])

    def test_an_empty_value_clears_a_field(self):
        path = self.save_pose(meta=json.dumps(CALLER_META))
        cmds.setRigDataFileMeta(file=path, meta='{"tags": [], "description": ""}')
        meta = self.read(path)["meta"]
        self.assertNotIn("tags", meta)
        self.assertNotIn("description", meta)
        self.assertEqual(meta["category"], "Expressions")

    def test_the_rest_of_a_pose_is_unchanged(self):
        path = self.save_pose()
        before = self.read(path)
        cmds.setRigDataFileMeta(file=path, meta='{"tags": ["a"]}')
        after = self.read(path)
        self.assertEqual(after["controls"], before["controls"])
        cmds.zeroRigControls(rig_root())
        cmds.loadRigPose(rig_root(), file=path)
        self.assertEqual(values(ARM_L, ["rotateY"]), [20])

    def test_the_rest_of_an_animation_is_unchanged(self):
        path = self.save_anim()
        before = self.read(path)
        cmds.setRigDataFileMeta(file=path, meta='{"tags": ["walk"]}')
        after = self.read(path)
        for key in ("frameRate", "startFrame", "endFrame", "controls"):
            self.assertEqual(after[key], before[key])
        self.assertEqual(after["meta"]["tags"], ["walk"])

    def test_an_older_file_is_written_at_the_current_version(self):
        path = self.write("old.json", OlderAndBadFiles.POSE_V1)
        cmds.setRigDataFileMeta(file=path, meta='{"tags": ["a"]}')
        data = self.read(path)
        self.assertEqual((data["formatVersion"], data["meta"]), (3, {"tags": ["a"]}))
        self.assertEqual(data["controls"], OlderAndBadFiles.POSE_V1["controls"])

    def test_a_field_the_rig_fills_is_refused_and_the_file_is_unchanged(self):
        path = self.save_pose()
        before = self.text(path)
        self.assertMayaError(lambda: cmds.setRigDataFileMeta(file=path, meta='{"created": "now"}'), "filled from the rig")
        self.assertEqual(self.text(path), before)

    def test_a_bad_value_or_a_broken_file_leaves_the_file_as_it_is(self):
        path = self.save_pose()
        before = self.text(path)
        self.assertMayaError(lambda: cmds.setRigDataFileMeta(file=path, meta='{"tags": "x"}'), "unchanged")
        broken = self.write("broken.json", {"formatVersion": 2, "meta": {}, "controls": {"Up": {}}})
        self.assertMayaError(lambda: cmds.setRigDataFileMeta(file=broken, meta='{"tags": ["x"]}'), "unchanged")
        self.assertEqual(self.text(path), before)

    def test_the_flags_are_required(self):
        self.assertMayaError(lambda: cmds.setRigDataFileMeta(meta="{}"), "needs -file")
        self.assertMayaError(lambda: cmds.setRigDataFileMeta(file=self.save_pose()), "needs -meta")

    def test_a_missing_file_is_an_error(self):
        self.assertMayaError(lambda: cmds.setRigDataFileMeta(file=self.path("nope.json"), meta="{}"), "Could not read")


class MirrorOnLoad(DataFileTestCase):
    """loadRigPose(mirror=True) mirrors the file's data before it writes, with the rules flipRigPose uses."""

    CONTROLS = [ARM_L, ARM_R, CHEST]
    NAMES = ["translateX", "translateY", "translateZ", "rotateX", "rotateY", "rotateZ"]

    def pose_values(self):
        return {control: values(control, self.NAMES) for control in self.CONTROLS}

    def set_left_pose(self):
        cmds.setAttr(ARM_L + ".translateX", 1.5)
        cmds.setAttr(ARM_L + ".rotateY", 20)
        cmds.setAttr(ARM_L + ".rotateZ", -10)
        cmds.setAttr(CHEST + ".translateX", 2)
        cmds.setAttr(CHEST + ".rotateY", 30)

    def test_it_gives_the_same_rig_as_loading_and_then_flipping(self):
        self.set_left_pose()
        path = self.path()
        cmds.saveRigPose(rig_root(), file=path)
        cmds.zeroRigControls(rig_root())
        cmds.loadRigPose(rig_root(), file=path)
        cmds.flipRigPose(rig_root())
        flipped = self.pose_values()
        cmds.zeroRigControls(rig_root())
        cmds.loadRigPose(rig_root(), file=path, mirror=True)
        self.assertEqual(self.pose_values(), flipped)
        self.assertNotEqual(flipped[ARM_R], [0] * 6, "the left arm's pose landed on the right arm")

    def test_the_other_side_gets_the_values_and_the_source_side_is_not_kept(self):
        self.set_left_pose()
        path = self.path()
        cmds.saveRigPose(ARM_L, file=path)
        cmds.zeroRigControls(rig_root())
        cmds.loadRigPose(rig_root(), file=path, mirror=True)
        self.assertEqual(values(ARM_L, self.NAMES), [0] * 6)
        self.assertNotEqual(values(ARM_R, self.NAMES), [0] * 6)

    def test_a_selection_scope_writes_only_those_controls(self):
        self.set_left_pose()
        path = self.path()
        cmds.saveRigPose(ARM_L, file=path)
        cmds.zeroRigControls(rig_root())
        changed = cmds.loadRigPose(ARM_R, file=path, mirror=True)
        self.assertEqual(changed, ["rig:upperArm_r_ctrl_r"])
        self.assertEqual(values(ARM_L, self.NAMES), [0] * 6)

    def test_mirror_false_is_the_default(self):
        self.set_left_pose()
        path = self.path()
        cmds.saveRigPose(rig_root(), file=path)
        cmds.zeroRigControls(rig_root())
        cmds.loadRigPose(rig_root(), file=path, mirror=False)
        self.assertEqual(values(ARM_L, ["rotateY"]), [20])

    def test_it_is_one_undo_step(self):
        self.set_left_pose()
        path = self.path()
        cmds.saveRigPose(rig_root(), file=path)
        cmds.zeroRigControls(rig_root())
        cmds.loadRigPose(rig_root(), file=path, mirror=True)
        mirrored = self.pose_values()
        cmds.undo()
        self.assertEqual(self.pose_values(), {control: [0] * 6 for control in self.CONTROLS})
        cmds.redo()
        self.assertEqual(self.pose_values(), mirrored)


def channel_values(controls):
    """(control, channel) -> value, for every writable channel of every control."""
    found = {}
    for control in controls:
        entries = json.loads(cmds.getRigControlChannels(control))[control.lstrip(":")]
        for entry in entries:
            found[(control, entry["name"])] = cmds.getAttr("%s.%s" % (control, entry["name"]))
    return found


def move_every_channel(controls):
    """Sets every writable channel of every control to a value other than its rest, inside its range."""
    for control in controls:
        entries = json.loads(cmds.getRigControlChannels(control))[control.lstrip(":")]
        for entry in entries:
            plug = "%s.%s" % (control, entry["name"])
            rest = entry["rest"]
            if entry["type"] == "bool":
                value = 0 if rest else 1
            elif entry["type"] == "enum":
                labels = cmds.attributeQuery(entry["name"], node=control, listEnum=True)[0].split(":")
                value = len(labels) - 1
            else:
                value = rest + 0.25
                if entry["max"] is not None and value > entry["max"]:
                    value = entry["max"]
                if entry["min"] is not None and value < entry["min"]:
                    value = entry["min"]
            cmds.setAttr(plug, value)


class EveryControlTypeBase:
    """Saves, zeroes and loads every channel of every control, as a pose and as an animation, and mirrors on load."""

    def controls(self):
        return cmds.getRigControls(rig_root())

    def test_a_pose_round_trips_every_channel(self):
        controls = self.controls()
        move_every_channel(controls)
        expected = channel_values(controls)
        path = self.path()
        cmds.saveRigPose(rig_root(), file=path)
        cmds.zeroRigControls(rig_root())
        cmds.loadRigPose(rig_root(), file=path)
        actual = channel_values(controls)
        for key, value in expected.items():
            self.assertAlmostEqual(actual[key], value, places=4, msg=str(key))

    def test_an_animation_round_trips_every_channel(self):
        controls = self.controls()
        move_every_channel(controls)
        expected = channel_values(controls)
        rigmodules.pose.key_rig(rig_root(), time=1)
        cmds.zeroRigControls(rig_root())
        rigmodules.pose.key_rig(rig_root(), time=10)
        path = self.path()
        cmds.saveRigAnimation(rig_root(), file=path, startFrame=1, endFrame=10)
        cmds.cutKey(controls, clear=True)
        cmds.zeroRigControls(rig_root())
        cmds.loadRigAnimation(rig_root(), file=path)
        for (control, channel), value in expected.items():
            keyed = cmds.keyframe("%s.%s" % (control, channel), query=True, valueChange=True, time=(1, 1))
            self.assertIsNotNone(keyed, "%s.%s has its key" % (control, channel))
            self.assertAlmostEqual(keyed[0], value, places=4, msg="%s.%s" % (control, channel))

    def test_mirror_on_load_is_the_flip_for_every_channel(self):
        controls = self.controls()
        move_every_channel(controls)
        path = self.path()
        cmds.saveRigPose(rig_root(), file=path)
        cmds.zeroRigControls(rig_root())
        cmds.loadRigPose(rig_root(), file=path)
        cmds.flipRigPose(rig_root())
        flipped = channel_values(controls)
        cmds.zeroRigControls(rig_root())
        cmds.loadRigPose(rig_root(), file=path, mirror=True)
        actual = channel_values(controls)
        for key, value in flipped.items():
            self.assertAlmostEqual(actual[key], value, places=4, msg=str(key))


class EveryControlTypeArms(EveryControlTypeBase, DataFileTestCase):
    """FkChain, Layout and the space switch (an enum channel)."""

    template = "mirror_arms"


class EveryControlTypeAim(EveryControlTypeBase, DataFileTestCase):
    """The Aim module's controls and channels."""

    template = "aim_mirror"


class PythonLibrary(DataFileTestCase):
    """rigmodules.library wraps the two file commands."""

    def test_get_data_file_info_gives_typed_entries_and_keeps_the_order(self):
        import rigmodules.library
        pose = self.save_pose(meta=json.dumps(CALLER_META))
        anim = self.save_anim()
        missing = self.path("missing.json")
        found = rigmodules.library.get_data_file_info(rig_root(), [anim, missing, pose])
        self.assertEqual([entry.file for entry in found], [anim, missing, pose])
        self.assertEqual((found[0].kind, found[0].start_frame, found[0].end_frame, found[0].frame_rate), ("animation", 1, 10, 24.0))
        self.assertTrue(found[1].error)
        self.assertEqual((found[1].kind, found[1].control_count), ("", 0))
        self.assertEqual((found[2].kind, found[2].meta["tags"], found[2].unmatched_controls), ("pose", ["hands", "fist"], ()))

    def test_no_files_is_an_empty_list(self):
        import rigmodules.library
        self.assertEqual(rigmodules.library.get_data_file_info(rig_root(), []), [])

    def test_set_data_file_meta_edits_the_file(self):
        import rigmodules.library
        path = self.save_pose()
        rigmodules.library.set_data_file_meta(path, {"tags": ["a", "b"], "status": "final"})
        [found] = rigmodules.library.get_data_file_info(rig_root(), [path])
        self.assertEqual((found.meta["tags"], found.meta["status"]), (["a", "b"], "final"))

    def test_set_data_file_meta_raises_for_a_rig_field(self):
        import rigmodules.library
        path = self.save_pose()
        with self.assertRaises(RuntimeError):
            rigmodules.library.set_data_file_meta(path, {"created": "now"})
