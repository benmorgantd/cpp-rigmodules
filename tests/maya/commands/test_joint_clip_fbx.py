"""FBX joint files and bone maps (D42): rigmodules.jointclip.write_fbx, read_fbx and write_skeleton_fbx, and
rigmodules.bonemap.

The clips are written by hand. A round trip has to give back the transforms that went in, so each value is worked out
from the clip and never recorded from the adapters. The FBX plugin runs in mayapy (spike, design/control-library.md).
"""
import json
import math
import os
import shutil
import tempfile

from maya import cmds
import maya.mel as mel

import rigmodules.bonemap
import rigmodules.jointclip
from rigtest import config
from rigtest.case import RigTestCase, rig_name
from rigtest.jointclips import rig_root

FRAME_RATE = 30.0
MARKUP_HIPS = {"markupVersion": 1, "side": "Center", "usage": "pelvis", "export": True, "skin": True}
MARKUP_HEAD = {"markupVersion": 1, "side": "Center", "usage": "head", "export": True, "skin": False}


def quaternion(axis, degrees):
    half = math.radians(degrees) / 2.0
    values = [0.0, 0.0, 0.0, math.cos(half)]
    values["xyz".index(axis)] = math.sin(half)
    return values


def transform(translate=(0, 0, 0), rotate=(0, 0, 0, 1), scale=(1, 1, 1)):
    return {"translate": list(translate), "rotate": list(rotate), "scale": list(scale)}


def hand_clip(frames=5, markup=True):
    """hips > spine > head over `frames` frames. The hips turn about Y by 70 degrees a frame, so the last frame is at
    280 degrees and the quaternion's sign has to flip on the way for the curve to stay continuous. The spine is
    scaled (1.5, 1, 1) and leans about X, the head is offset up the spine."""
    poses = []
    for frame in range(frames):
        poses.append([
            transform((frame * 2.0, 1.0, 0.0), quaternion("y", 70.0 * frame)),
            transform((0, 10, 0), quaternion("x", 10.0 * frame), (1.5, 1, 1)),
            transform((0, 4, 3), quaternion("z", -25.0 * frame)),
        ])
    return {
        "formatVersion": 1,
        "frameRate": FRAME_RATE,
        "startFrame": 1,
        "joints": [
            {"path": "hips", "markup": json.dumps(MARKUP_HIPS) if markup else "", "rest": poses[0][0]},
            {"path": "hips/spine", "markup": "", "rest": poses[0][1]},
            {"path": "hips/spine/head", "markup": json.dumps(MARKUP_HEAD) if markup else "", "rest": poses[0][2]},
        ],
        "poses": poses,
    }


class FbxTestCase(RigTestCase):

    def setUp(self):
        super().setUp()
        cmds.currentUnit(time="ntsc")
        self.folder = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.folder, True)

    def path(self, name):
        return os.path.join(self.folder, name)

    def write_json(self, name, data):
        with open(self.path(name), "w", encoding="utf-8") as handle:
            json.dump(data, handle)
        return self.path(name)

    def read_json(self, name):
        with open(self.path(name), encoding="utf-8") as handle:
            return json.load(handle)

    def round_trip(self, clip, bone_map=None):
        clip_path = self.write_json("in.json", clip)
        rigmodules.jointclip.write_fbx(clip_path, self.path("clip.fbx"))
        rigmodules.jointclip.read_fbx(self.path("clip.fbx"), self.path("out.json"), bone_map)
        return self.read_json("out.json")

    def count_anim_curves(self, fbx_path):
        """How many animation curves the file holds, imported into a scratch scene."""
        rigmodules.jointclip._load_fbx_plugin()
        with rigmodules.jointclip._scratch_scene():
            mel.eval("FBXResetImport")
            mel.eval("FBXImportMode -v add")
            mel.eval('FBXImport -f "%s"' % fbx_path.replace("\\", "/"))
            return len(cmds.ls(type="animCurve") or [])

    def assertTransform(self, actual, expected):
        for key in ("translate", "scale"):
            for a, e in zip(actual[key], expected[key]):
                self.assertAlmostEqual(a, e, places=4)
        # q and -q are the same rotation
        dot = sum(a * e for a, e in zip(actual["rotate"], expected["rotate"]))
        self.assertAlmostEqual(abs(dot), 1.0, places=4)


class FbxRoundTrip(FbxTestCase):

    def test_every_transform_of_every_frame_comes_back(self):
        clip = hand_clip()
        out = self.round_trip(clip)
        self.assertEqual([joint["path"] for joint in out["joints"]], ["hips", "hips/spine", "hips/spine/head"])
        self.assertEqual(len(out["poses"]), 5)
        self.assertEqual(out["startFrame"], 1)
        self.assertAlmostEqual(out["frameRate"], FRAME_RATE, places=3)
        for expected_pose, pose in zip(clip["poses"], out["poses"]):
            for expected, actual in zip(expected_pose, pose):
                self.assertTransform(actual, expected)

    def test_markup_comes_back_without_a_bone_map(self):
        out = self.round_trip(hand_clip())
        markups = [json.loads(joint["markup"]) if joint["markup"] else None for joint in out["joints"]]
        self.assertEqual(markups, [MARKUP_HIPS, None, MARKUP_HEAD])

    def test_the_rest_is_the_first_frame(self):
        clip = hand_clip()
        out = self.round_trip(clip)
        for joint, expected in zip(out["joints"], clip["poses"][0]):
            self.assertTransform(joint["rest"], expected)

    def test_start_frame_is_kept(self):
        clip = hand_clip()
        clip["startFrame"] = 11
        out = self.round_trip(clip)
        self.assertEqual(out["startFrame"], 11)
        self.assertEqual(len(out["poses"]), 5)

    def test_a_single_pose_is_a_one_frame_animation(self):
        clip = hand_clip(frames=3)
        clip["poses"] = clip["poses"][2:]
        out = self.round_trip(clip)
        self.assertEqual(len(out["poses"]), 1)
        for expected, actual in zip(clip["poses"][0], out["poses"][0]):
            self.assertTransform(actual, expected)
        self.assertGreater(self.count_anim_curves(self.path("clip.fbx")), 0, "the file has keys, not only values")

    def test_the_importers_error_muting_is_undone(self):
        cmds.scriptEditorInfo(suppressErrors=False)
        self.round_trip(hand_clip())
        self.assertFalse(cmds.scriptEditorInfo(query=True, suppressErrors=True))

    def test_the_scene_is_left_as_it_was(self):
        cmds.currentTime(7)
        cmds.playbackOptions(minTime=3, maxTime=9)
        cmds.polyCube(name="keepMe")
        nodes = set(cmds.ls(long=True))
        self.round_trip(hand_clip())
        self.assertEqual(set(cmds.ls(long=True)), nodes, "no scratch joint, curve or namespace is left behind")
        self.assertFalse(cmds.namespace(exists="rmfbx0"))
        self.assertEqual(cmds.ls(selection=True), ["keepMe"])
        self.assertEqual(cmds.currentTime(query=True), 7)
        self.assertEqual((cmds.playbackOptions(query=True, minTime=True), cmds.playbackOptions(query=True, maxTime=True)), (3, 9))
        self.assertEqual(cmds.currentUnit(query=True, time=True), "ntsc")

    def test_nothing_goes_on_the_undo_queue(self):
        cmds.polyCube(name="first")
        before = cmds.undoInfo(query=True, undoName=True)
        self.round_trip(hand_clip())
        self.assertEqual(cmds.undoInfo(query=True, undoName=True), before)

    def test_a_file_at_another_frame_rate_reads_at_its_own_and_the_scene_keeps_its_unit(self):
        clip = hand_clip()
        clip_path = self.write_json("in.json", clip)
        rigmodules.jointclip.write_fbx(clip_path, self.path("clip.fbx"))
        cmds.currentUnit(time="film")
        rigmodules.jointclip.read_fbx(self.path("clip.fbx"), self.path("out.json"))
        out = self.read_json("out.json")
        self.assertAlmostEqual(out["frameRate"], FRAME_RATE, places=3)
        self.assertEqual(cmds.currentUnit(query=True, time=True), "film")
        for expected, actual in zip(clip["poses"][4], out["poses"][4]):
            self.assertTransform(actual, expected)


class FbxBoneMap(FbxTestCase):

    def bone_map(self, joints):
        path = self.path("map.bonemap.json")
        rigmodules.bonemap.write_bone_map(path, "tests\\hero", "tests\\human", joints)
        return path

    def test_a_bone_map_marks_a_file_with_no_markup(self):
        out = self.round_trip(hand_clip(markup=False), self.bone_map({"hips": json.dumps(MARKUP_HIPS), "head": json.dumps(MARKUP_HEAD)}))
        markups = [json.loads(joint["markup"]) if joint["markup"] else None for joint in out["joints"]]
        self.assertEqual(markups, [MARKUP_HIPS, None, MARKUP_HEAD])

    def test_a_bone_map_replaces_markup_the_file_carries(self):
        replaced = dict(MARKUP_HIPS, usage="root")
        out = self.round_trip(hand_clip(), self.bone_map({"hips": json.dumps(replaced)}))
        markups = [json.loads(joint["markup"]) if joint["markup"] else None for joint in out["joints"]]
        self.assertEqual(markups, [replaced, None, MARKUP_HEAD])

    def test_a_joint_the_file_lacks_is_a_warning_and_not_an_error(self):
        path = self.bone_map({"hips": json.dumps(MARKUP_HIPS), "tail": json.dumps(dict(MARKUP_HEAD, usage="tail"))})
        out = self.round_trip(hand_clip(markup=False), path)
        self.assertEqual(len(out["joints"]), 3)

    def test_a_bad_bone_map_is_refused_before_anything_is_imported(self):
        clip_path = self.write_json("in.json", hand_clip())
        rigmodules.jointclip.write_fbx(clip_path, self.path("clip.fbx"))
        nodes = set(cmds.ls(long=True))
        bad = self.write_json("bad.bonemap.json", {"formatVersion": 1})
        with self.assertRaises(ValueError):
            rigmodules.jointclip.read_fbx(self.path("clip.fbx"), self.path("out.json"), bad)
        self.assertEqual(set(cmds.ls(long=True)), nodes)
        self.assertFalse(os.path.exists(self.path("out.json")))


class BoneMapFile(FbxTestCase):

    def valid(self):
        return {"formatVersion": 1, "assetId": "tests\\hero", "skeletonId": "tests\\human", "joints": {"Hips": dict(MARKUP_HIPS)}}

    def check(self, data, *contains):
        path = self.write_json("m.bonemap.json", data)
        with self.assertRaises(ValueError) as caught:
            rigmodules.bonemap.read_bone_map(path)
        for text in contains:
            self.assertIn(text, str(caught.exception))

    def test_a_valid_file_reads_back_as_written(self):
        path = self.write_json("m.bonemap.json", self.valid())
        self.assertEqual(rigmodules.bonemap.read_bone_map(path), self.valid())

    def test_write_then_read(self):
        path = self.path("m.bonemap.json")
        rigmodules.bonemap.write_bone_map(path, "tests\\hero", "tests\\human", {"Hips": json.dumps(MARKUP_HIPS), "Spine": ""})
        data = rigmodules.bonemap.read_bone_map(path)
        self.assertEqual(data["joints"], {"Hips": MARKUP_HIPS}, "an unmarked joint is left out")
        self.assertEqual((data["assetId"], data["skeletonId"]), ("tests\\hero", "tests\\human"))

    def test_a_missing_file_is_file_not_found(self):
        with self.assertRaises(FileNotFoundError):
            rigmodules.bonemap.read_bone_map(self.path("none.json"))

    def test_not_json(self):
        with open(self.path("m.bonemap.json"), "w") as handle:
            handle.write("{")
        with self.assertRaises(ValueError):
            rigmodules.bonemap.read_bone_map(self.path("m.bonemap.json"))

    def test_every_problem_is_reported_at_once(self):
        data = self.valid()
        data["formatVersion"] = 2
        data["assetId"] = "tests\\\\hero"
        del data["skeletonId"]
        self.check(data, "formatVersion 2", "assetId", "skeletonId")

    def test_a_newer_markup_version(self):
        data = self.valid()
        data["joints"]["Hips"]["markupVersion"] = 2
        self.check(data, "joints.Hips.markupVersion 2")

    def test_missing_and_mistyped_markup_keys(self):
        data = self.valid()
        del data["joints"]["Hips"]["skin"]
        data["joints"]["Hips"]["export"] = "yes"
        self.check(data, "joints.Hips.skin", "joints.Hips.export")

    def test_an_empty_usage(self):
        data = self.valid()
        data["joints"]["Hips"]["usage"] = ""
        self.check(data, "usage is empty")

    def test_two_joints_with_the_same_side_and_usage(self):
        data = self.valid()
        data["joints"]["Pelvis"] = dict(MARKUP_HIPS)
        self.check(data, "share the markup")

    def test_a_failed_write_keeps_the_old_file(self):
        path = self.path("m.bonemap.json")
        rigmodules.bonemap.write_bone_map(path, "tests\\hero", "tests\\human", {"Hips": json.dumps(MARKUP_HIPS)})
        with open(path, encoding="utf-8") as handle:
            before = handle.read()
        with self.assertRaises(ValueError):
            rigmodules.bonemap.write_bone_map(path, "tests\\hero", "", {"Hips": json.dumps(MARKUP_HIPS)})
        with open(path, encoding="utf-8") as handle:
            self.assertEqual(handle.read(), before)


class FbxErrors(FbxTestCase):

    def assertRefused(self, clip, exception, *contains):
        clip_path = self.write_json("in.json", clip)
        with open(self.path("clip.fbx"), "w") as handle:
            handle.write("old")
        nodes = set(cmds.ls(long=True))
        with self.assertRaises(exception) as caught:
            rigmodules.jointclip.write_fbx(clip_path, self.path("clip.fbx"))
        for text in contains:
            self.assertIn(text, str(caught.exception))
        with open(self.path("clip.fbx")) as handle:
            self.assertEqual(handle.read(), "old", "a refused write leaves the old file")
        self.assertEqual(set(cmds.ls(long=True)), nodes)
        self.assertEqual(os.listdir(self.folder).count("clip.tmp.fbx"), 0)

    def test_an_unknown_format_version(self):
        clip = hand_clip()
        clip["formatVersion"] = 2
        self.assertRefused(clip, ValueError, "formatVersion")

    def test_no_poses(self):
        clip = hand_clip()
        clip["poses"] = []
        self.assertRefused(clip, ValueError, "no joints or no poses")

    def test_a_pose_with_the_wrong_joint_count(self):
        clip = hand_clip()
        clip["poses"][1] = clip["poses"][1][:2]
        self.assertRefused(clip, ValueError, "one transform per joint")

    def test_a_name_maya_cannot_use(self):
        clip = hand_clip()
        clip["joints"][1]["path"] = "hips/spine 1"
        self.assertRefused(clip, ValueError, "spine 1")

    def test_two_joints_with_one_name(self):
        clip = hand_clip()
        clip["joints"][2]["path"] = "hips/spine/hips"
        self.assertRefused(clip, ValueError, "unique", "hips")

    def test_a_different_frame_rate(self):
        clip = hand_clip()
        clip["frameRate"] = 24.0
        self.assertRefused(clip, ValueError, "frame rate")

    def test_a_scene_node_with_the_same_name(self):
        cmds.polyCube(name="spine")
        self.assertRefused(hand_clip(), RuntimeError, "spine")

    def test_read_a_missing_file(self):
        with self.assertRaises(FileNotFoundError):
            rigmodules.jointclip.read_fbx(self.path("none.fbx"), self.path("out.json"))


class SkeletonMatch(FbxTestCase):
    """check_skeleton_match compares joint names and parents, never rests, so an animation made on other proportions
    still matches."""

    def write_fbx(self, name, clip):
        rigmodules.jointclip.write_fbx(self.write_json(name + ".json", clip), self.path(name + ".fbx"))
        return self.path(name + ".fbx")

    def skeleton(self):
        clip = hand_clip(frames=1)
        return self.write_fbx("skeleton", clip)

    def assertMismatch(self, clip, *contains):
        skeleton, animation = self.skeleton(), self.write_fbx("anim", clip)
        with self.assertRaises(ValueError) as caught:
            rigmodules.jointclip.check_skeleton_match(animation, skeleton)
        for text in contains:
            self.assertIn(text, str(caught.exception))

    def test_an_animation_on_the_skeleton_matches(self):
        rigmodules.jointclip.check_skeleton_match(self.write_fbx("anim", hand_clip()), self.skeleton())

    def test_other_proportions_still_match(self):
        clip = hand_clip()
        for joint in clip["joints"]:
            joint["rest"] = transform((5, 5, 5))
        rigmodules.jointclip.check_skeleton_match(self.write_fbx("anim", clip), self.skeleton())

    def test_a_joint_the_animation_lacks(self):
        clip = hand_clip()
        clip["joints"] = clip["joints"][:2]
        clip["poses"] = [pose[:2] for pose in clip["poses"]]
        self.assertMismatch(clip, "the animation lacks head")

    def test_a_joint_the_skeleton_lacks(self):
        clip = hand_clip()
        clip["joints"][2]["path"] = "hips/spine/neck"
        self.assertMismatch(clip, "the animation lacks head", "the skeleton lacks neck")

    def test_a_joint_under_another_parent(self):
        clip = hand_clip()
        clip["joints"][2]["path"] = "hips/head"
        self.assertMismatch(clip, "head is under hips but the skeleton has it under spine")

    def test_the_scene_is_left_as_it_was(self):
        skeleton, animation = self.skeleton(), self.write_fbx("anim", hand_clip())
        nodes = set(cmds.ls(long=True))
        rigmodules.jointclip.check_skeleton_match(animation, skeleton)
        self.assertEqual(set(cmds.ls(long=True)), nodes)

    def test_a_missing_file(self):
        with self.assertRaises(FileNotFoundError):
            rigmodules.jointclip.check_skeleton_match(self.path("none.fbx"), self.skeleton())


class SkeletonExport(FbxTestCase):

    def setUp(self):
        super().setUp()
        self.build_standard("mirror_arms", "mirror_arms")
        self.fbx = self.path("hero_skeleton.fbx")
        self.map = self.path("hero_skeleton.bonemap.json")
        self.root = rig_root()

    def test_writes_the_fbx_and_a_bone_map_with_the_asset_and_skeleton_ids(self):
        rigmodules.jointclip.write_skeleton_fbx(self.root, "tests\\human", self.fbx, self.map)
        self.assertTrue(os.path.isfile(self.fbx))
        data = rigmodules.bonemap.read_bone_map(self.map)
        self.assertEqual((data["assetId"], data["skeletonId"]), (config.ASSET_ID, "tests\\human"))
        self.assertEqual(sorted(data["joints"]), sorted([
            "chest_jnt", "clavicle_l_jnt", "upperArm_l_jnt", "lowerArm_l_jnt", "clavicle_r_jnt", "upperArm_r_jnt", "lowerArm_r_jnt"]))
        self.assertEqual(data["joints"]["upperArm_l_jnt"],
                         {"markupVersion": 1, "side": "Left", "usage": "upperArm", "export": True, "skin": True})

    def test_the_skeleton_is_at_rest_with_no_animation(self):
        rigmodules.jointclip.write_skeleton_fbx(self.root, "tests\\human", self.fbx, self.map)
        rigmodules.jointclip.read_fbx(self.fbx, self.path("out.json"), self.map)
        out = self.read_json("out.json")
        self.assertEqual(len(out["poses"]), 1)
        self.assertEqual(self.count_anim_curves(self.fbx), 0, "the skeleton export carries no animation")
        by_path = {joint["path"]: pose for joint, pose in zip(out["joints"], out["poses"][0])}
        # the fixture's joints are flat (every joint a root), at these world positions
        for path, position in (("chest_jnt", (0, 4, 0)), ("upperArm_l_jnt", (2, 5, 0)), ("lowerArm_r_jnt", (-4, 5, 0))):
            for actual, expected in zip(by_path[path]["translate"], position):
                self.assertAlmostEqual(actual, expected, places=4)

    def test_a_posed_rig_still_exports_its_rest(self):
        cmds.setAttr(rig_name("upperArm_l_ctrl_l") + ".rotateZ", 60)
        rigmodules.jointclip.write_skeleton_fbx(self.root, "tests\\human", self.fbx, self.map)
        rigmodules.jointclip.read_fbx(self.fbx, self.path("out.json"), self.map)
        out = self.read_json("out.json")
        pose = {joint["path"]: transform for joint, transform in zip(out["joints"], out["poses"][0])}["upperArm_l_jnt"]
        self.assertAlmostEqual(abs(pose["rotate"][3]), 1.0, places=4, msg="the rest has no rotation")

    def test_a_rig_animation_goes_through_fbx_and_restores(self):
        control = rig_name("upperArm_l_ctrl_l")
        cmds.setKeyframe(control + ".rotateZ", time=1, value=0)
        cmds.setKeyframe(control + ".rotateZ", time=3, value=60)
        rigmodules.jointclip.capture_joint_clip(self.root, 1, 3, self.path("captured.json"))
        rigmodules.jointclip.write_fbx(self.path("captured.json"), self.fbx)
        cmds.cutKey(control, attribute="rotateZ", clear=True)
        cmds.setAttr(control + ".rotateZ", 0)
        rigmodules.jointclip.read_fbx(self.fbx, self.path("out.json"))
        cmds.animateRigFromJointClip(self.root, jointClip=self.path("out.json").replace("\\", "/"))
        self.assertAlmostEqual(cmds.getAttr(control + ".rotateZ", time=3), 60, places=2)
        self.assertAlmostEqual(cmds.getAttr(control + ".rotateZ", time=1), 0, places=2)
