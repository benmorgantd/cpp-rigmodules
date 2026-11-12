"""rigmodules.jointclip.write_usd and read_usd (D41): a JointClip file to a UsdSkel file and back.

Clips are written by hand, and expected values are worked out by hand. Foreign files (another tool's USD, in other units
or another up axis) are authored with pxr directly, so the reader is tested on files it didn't write. A joint's scale is
half precision in UsdSkel, so the clips use scales that are exact in half precision (1, 2, 0.5).
"""
import json
import math
import os
import shutil
import tempfile

from maya import cmds

import pxr.Gf
import pxr.Sdf
import pxr.Usd
import pxr.UsdGeom
import pxr.UsdSkel
import pxr.Vt

import rigmodules.jointclip
from rigtest.case import RigTestCase, rig_name

HALF_ROOT = math.sqrt(0.5)
MARKUP = '{"markupVersion":1,"usage":"%s","side":"Center","export":true,"skin":true}'
IDENTITY = {"translate": [0, 0, 0], "rotate": [0, 0, 0, 1], "scale": [1, 1, 1]}


def transform(translate=(0, 0, 0), rotate=(0, 0, 0, 1), scale=(1, 1, 1)):
    return {"translate": list(translate), "rotate": list(rotate), "scale": list(scale)}


def hand_clip():
    """hips > spine > head, the head unmarked. Two frames starting at frame 5, at 30 fps."""
    return {
        "formatVersion": 1,
        "frameRate": 30.0,
        "startFrame": 5,
        "joints": [
            {"path": "hips", "markup": MARKUP % "hips", "rest": transform((0, 100, 0))},
            {"path": "hips/spine", "markup": MARKUP % "spine", "rest": transform((0, 10, 0))},
            {"path": "hips/spine/head", "markup": "", "rest": transform((0, 20, 0), scale=(2, 2, 2))},
        ],
        "poses": [
            [transform((0, 100, 0)), transform((0, 10, 0)), transform((0, 20, 0), scale=(2, 2, 2))],
            [transform((1, 101, 0), (0, 0, HALF_ROOT, HALF_ROOT)), transform((0, 10, 0), (HALF_ROOT, 0, 0, HALF_ROOT)),
             transform((0, 20, 0), scale=(0.5, 0.5, 0.5))],
        ],
    }


class UsdTestCase(RigTestCase):

    def setUp(self):
        super().setUp()
        self.folder = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.folder, True)
        self.addCleanup(cmds.upAxis, axis="y")
        cmds.upAxis(axis="y")

    def path(self, name):
        return os.path.join(self.folder, name)

    def write_clip(self, clip, name="clip.json"):
        path = self.path(name)
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(clip, handle)
        return path

    def read_clip(self, usd_path):
        out = self.path("read.json")
        rigmodules.jointclip.read_usd(usd_path, out)
        with open(out, encoding="utf-8") as handle:
            return json.load(handle)

    def round_trip(self, clip, usd_name="clip.usd"):
        rigmodules.jointclip.write_usd(self.write_clip(clip), self.path(usd_name))
        return self.read_clip(self.path(usd_name))

    def author_usd(self, name, joints, rests, anim_joints, samples, meters_per_unit=1.0, up_axis="Y", skeletons=1):
        """A USD file the way another tool might write it: no rigmodules attributes. samples maps a frame to
        (translations, rotations as w x y z, scales) in anim_joints order."""
        path = self.path(name)
        stage = pxr.Usd.Stage.CreateNew(path)
        pxr.UsdGeom.SetStageMetersPerUnit(stage, meters_per_unit)
        pxr.UsdGeom.SetStageUpAxis(stage, up_axis)
        stage.SetTimeCodesPerSecond(24)
        pxr.UsdSkel.Root.Define(stage, "/Root")
        for index in range(skeletons):
            skeleton = pxr.UsdSkel.Skeleton.Define(stage, "/Root/Skeleton%d" % index)
            skeleton.CreateJointsAttr(pxr.Vt.TokenArray(joints))
            skeleton.CreateRestTransformsAttr(pxr.Vt.Matrix4dArray(
                [pxr.Gf.Matrix4d(1.0).SetTranslate(pxr.Gf.Vec3d(*t)) for t in rests]))
        if samples is not None:
            animation = pxr.UsdSkel.Animation.Define(stage, "/Root/Animation")
            pxr.UsdSkel.BindingAPI.Apply(skeleton.GetPrim()).CreateAnimationSourceRel().SetTargets([animation.GetPath()])
            animation.CreateJointsAttr(pxr.Vt.TokenArray(anim_joints))
            translations, rotations, scales = (animation.CreateTranslationsAttr(), animation.CreateRotationsAttr(),
                                               animation.CreateScalesAttr())
            for frame, (t, r, s) in samples.items():
                time = pxr.Usd.TimeCode(frame)
                translations.Set(pxr.Vt.Vec3fArray([pxr.Gf.Vec3f(*v) for v in t]), time)
                rotations.Set(pxr.Vt.QuatfArray([pxr.Gf.Quatf(*q) for q in r]), time)
                scales.Set(pxr.Vt.Vec3hArray([pxr.Gf.Vec3h(*v) for v in s]), time)
        stage.GetRootLayer().Save()
        return path

    def assertVector(self, actual, expected, places=4):
        self.assertEqual(len(actual), len(expected))
        for a, e in zip(actual, expected):
            self.assertAlmostEqual(a, e, places=places)

    def assertTransform(self, actual, expected):
        for key in ("translate", "rotate", "scale"):
            self.assertVector(actual[key], expected[key])


class WriteUsd(UsdTestCase):

    def stage(self):
        rigmodules.jointclip.write_usd(self.write_clip(hand_clip()), self.path("clip.usd"))
        return pxr.Usd.Stage.Open(self.path("clip.usd"))

    def test_stage_metadata_is_the_clips_frame_rate_range_and_internal_units(self):
        stage = self.stage()
        self.assertEqual(stage.GetTimeCodesPerSecond(), 30.0)
        self.assertEqual((stage.GetStartTimeCode(), stage.GetEndTimeCode()), (5.0, 6.0))
        self.assertAlmostEqual(pxr.UsdGeom.GetStageMetersPerUnit(stage), 0.01)
        self.assertEqual(pxr.UsdGeom.GetStageUpAxis(stage), "Y")

    def test_the_stage_is_in_the_scenes_up_axis(self):
        cmds.upAxis(axis="z")
        self.assertEqual(pxr.UsdGeom.GetStageUpAxis(self.stage()), "Z")

    def test_the_skeleton_holds_joints_rest_bind_and_markup(self):
        stage = self.stage()   # a prim is invalid once its stage is collected
        skeleton = pxr.UsdSkel.Skeleton(stage.GetPrimAtPath("/Root/Skeleton"))
        self.assertEqual(list(skeleton.GetJointsAttr().Get()), ["hips", "hips/spine", "hips/spine/head"])
        rests = skeleton.GetRestTransformsAttr().Get()
        self.assertVector(list(rests[1].ExtractTranslation()), [0, 10, 0])
        # The head's rest scales by 2 about its own origin.
        self.assertVector([rests[2].GetRow3(row)[row] for row in range(3)], [2, 2, 2])
        # A bind transform is the joint's rest in skeleton space: 100 + 10 + 20 up.
        self.assertVector(list(skeleton.GetBindTransformsAttr().Get()[2].ExtractTranslation()), [0, 130, 0])
        prim = skeleton.GetPrim()
        self.assertEqual(list(prim.GetAttribute("rigmodules:jointMarkup").Get()),
                         [MARKUP % "hips", MARKUP % "spine", ""])
        self.assertEqual(prim.GetAttribute("rigmodules:formatVersion").Get(), 1)

    def test_the_animation_has_a_sample_per_pose_on_the_clips_frame_numbers(self):
        stage = self.stage()
        animation = pxr.UsdSkel.Animation(stage.GetPrimAtPath("/Root/Animation"))
        self.assertEqual(animation.GetTranslationsAttr().GetTimeSamples(), [5.0, 6.0])
        self.assertEqual(animation.GetRotationsAttr().GetTimeSamples(), [5.0, 6.0])
        translations = animation.GetTranslationsAttr().Get(6.0)
        self.assertVector(list(translations[0]), [1, 101, 0])
        rotation = animation.GetRotationsAttr().Get(6.0)[0]
        self.assertVector([rotation.real, *rotation.imaginary], [HALF_ROOT, 0, 0, HALF_ROOT])
        self.assertVector(list(animation.GetScalesAttr().Get(6.0)[2]), [0.5, 0.5, 0.5])

    def test_the_animation_is_bound_to_the_skeleton(self):
        stage = self.stage()
        source = pxr.UsdSkel.BindingAPI(stage.GetPrimAtPath("/Root/Skeleton")).GetAnimationSource()
        self.assertEqual(str(source.GetPath()), "/Root/Animation")

    def test_each_extension_writes_its_format(self):
        for extension in ("usda", "usdc", "usd"):
            path = self.path("clip." + extension)
            rigmodules.jointclip.write_usd(self.write_clip(hand_clip()), path)
            self.assertEqual(len(self.read_clip(path)["poses"]), 2)
        with open(self.path("clip.usda"), encoding="utf-8") as handle:
            self.assertIn("rigmodules:jointMarkup", handle.read())

    def test_writing_again_replaces_the_file_and_leaves_no_temporary(self):
        usd_path = self.path("clip.usd")
        rigmodules.jointclip.write_usd(self.write_clip(hand_clip()), usd_path)
        self.assertEqual(len(self.read_clip(usd_path)["poses"]), 2)
        shorter = hand_clip()
        shorter["poses"] = shorter["poses"][:1]
        rigmodules.jointclip.write_usd(self.write_clip(shorter), usd_path)
        self.assertEqual(len(self.read_clip(usd_path)["poses"]), 1)
        self.assertEqual(sorted(name for name in os.listdir(self.folder) if name.endswith(".usd")), ["clip.usd"])

    def test_a_name_that_is_not_a_usd_identifier_is_an_error(self):
        clip = hand_clip()
        clip["joints"][1]["path"] = "hips/1spine"
        clip["joints"][2]["path"] = "hips/1spine/head"
        with self.assertRaisesRegex(ValueError, "1spine"):
            rigmodules.jointclip.write_usd(self.write_clip(clip), self.path("clip.usd"))
        self.assertFalse(os.path.exists(self.path("clip.usd")))

    def test_a_clip_with_no_poses_is_an_error(self):
        clip = hand_clip()
        clip["poses"] = []
        with self.assertRaisesRegex(ValueError, "no joints or no poses"):
            rigmodules.jointclip.write_usd(self.write_clip(clip), self.path("clip.usd"))

    def test_a_pose_of_the_wrong_length_is_an_error(self):
        clip = hand_clip()
        clip["poses"][1].pop()
        with self.assertRaisesRegex(ValueError, "one transform per joint"):
            rigmodules.jointclip.write_usd(self.write_clip(clip), self.path("clip.usd"))

    def test_an_unknown_format_version_is_an_error(self):
        clip = hand_clip()
        clip["formatVersion"] = 2
        with self.assertRaisesRegex(ValueError, "formatVersion 2"):
            rigmodules.jointclip.write_usd(self.write_clip(clip), self.path("clip.usd"))


class RoundTrip(UsdTestCase):

    def test_a_clip_comes_back_as_it_went(self):
        original = hand_clip()
        clip = self.round_trip(original)
        self.assertEqual(clip["formatVersion"], 1)
        self.assertEqual(clip["frameRate"], 30.0)
        self.assertEqual(clip["startFrame"], 5)
        self.assertEqual([joint["path"] for joint in clip["joints"]], [joint["path"] for joint in original["joints"]])
        for joint, expected in zip(clip["joints"], original["joints"]):
            self.assertEqual(joint["markup"], expected["markup"])
            self.assertTransform(joint["rest"], expected["rest"])
        self.assertEqual(len(clip["poses"]), 2)
        for pose, expected in zip(clip["poses"], original["poses"]):
            for transform_, expected_transform in zip(pose, expected):
                self.assertTransform(transform_, expected_transform)

    def test_an_unmarked_joint_stays_in_the_file(self):
        clip = self.round_trip(hand_clip())
        self.assertEqual([joint["markup"] == "" for joint in clip["joints"]], [False, False, True])

    def test_a_single_pose_is_a_clip_with_one_pose(self):
        original = hand_clip()
        original["poses"] = original["poses"][1:]
        clip = self.round_trip(original)
        self.assertEqual((clip["startFrame"], len(clip["poses"])), (5, 1))
        self.assertTransform(clip["poses"][0][0], original["poses"][0][0])

    def test_a_scene_in_z_up_round_trips_unchanged(self):
        cmds.upAxis(axis="z")
        clip = self.round_trip(hand_clip())
        self.assertTransform(clip["poses"][1][0], hand_clip()["poses"][1][0])

    def test_the_plugin_reads_the_file(self):
        # A clip the plugin rejects would fail here: the file must pass its reader, which checks the unit quaternions
        # and the parents. The joints match no rig, so the error is the plugin's "no joint" one, not a parse error.
        self.build_standard("fkchain_three", "fk_three_parented")
        rigmodules.jointclip.write_usd(self.write_clip(hand_clip()), self.path("clip.usd"))
        out = self.path("read.json")
        rigmodules.jointclip.read_usd(self.path("clip.usd"), out)
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        with self.assertRaises(RuntimeError) as raised:
            cmds.poseRigFromJointPose(root, jointPose=out.replace("\\", "/"))
        self.assertNotIn("formatVersion", str(raised.exception))
        self.assertNotIn("unit quaternion", str(raised.exception))


class RigToUsdAndBack(UsdTestCase):

    def test_a_posed_rig_survives_capture_usd_and_restore(self):
        self.build_standard("mirror_arms", "mirror_arms")
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        upper = rig_name("upperArm_l_ctrl_l")
        cmds.setAttr(upper + ".rotateZ", 90)
        rigmodules.jointclip.capture_joint_clip(root, 1, 1, self.path("captured.json"))
        rigmodules.jointclip.write_usd(self.path("captured.json"), self.path("pose.usd"))

        cmds.zeroRigControls(root)
        self.assertAlmostEqual(cmds.getAttr(upper + ".rotateZ"), 0, places=3)
        restored = self.read_clip(self.path("pose.usd"))
        self.assertEqual(restored["startFrame"], 1)
        cmds.poseRigFromJointPose(root, jointPose=self.path("read.json").replace("\\", "/"))
        self.assertAlmostEqual(cmds.getAttr(upper + ".rotateZ"), 90, places=2)
        self.assertAlmostEqual(cmds.getAttr(rig_name("lowerArm_l_ctrl_l") + ".rotateZ"), 0, places=2)

    def test_an_animation_survives_capture_usd_and_restore(self):
        self.build_standard("mirror_arms", "mirror_arms")
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        upper = rig_name("upperArm_l_ctrl_l")
        cmds.setKeyframe(upper + ".rotateZ", time=1, value=0, inTangentType="linear", outTangentType="linear")
        cmds.setKeyframe(upper + ".rotateZ", time=5, value=80, inTangentType="linear", outTangentType="linear")
        rigmodules.jointclip.capture_joint_clip(root, 1, 5, self.path("captured.json"))
        rigmodules.jointclip.write_usd(self.path("captured.json"), self.path("walk.usd"))

        cmds.cutKey(upper, attribute="rotateZ", clear=True)
        cmds.zeroRigControls(root)
        self.read_clip(self.path("walk.usd"))
        cmds.animateRigFromJointClip(root, jointClip=self.path("read.json").replace("\\", "/"))
        for frame, value in ((1, 0), (3, 40), (5, 80)):
            self.assertAlmostEqual(cmds.getAttr(upper + ".rotateZ", time=frame), value, places=2)


class ReadForeignUsd(UsdTestCase):

    def test_meters_per_unit_scales_translations_to_centimeters(self):
        path = self.author_usd(
            "meters.usd", ["hips", "hips/spine"], [(0, 1, 0), (0, 0.5, 0)], ["hips", "hips/spine"],
            {1: ([(0, 1, 0), (0, 0.5, 0)], [(1, 0, 0, 0)] * 2, [(1, 1, 1)] * 2)}, meters_per_unit=1.0)
        clip = self.read_clip(path)
        self.assertVector(clip["joints"][0]["rest"]["translate"], [0, 100, 0])
        self.assertVector(clip["joints"][1]["rest"]["translate"], [0, 50, 0])
        self.assertVector(clip["poses"][0][1]["translate"], [0, 50, 0])

    def test_a_centimeter_stage_is_not_scaled(self):
        path = self.author_usd(
            "cm.usd", ["hips"], [(0, 100, 0)], ["hips"], {1: ([(0, 100, 0)], [(1, 0, 0, 0)], [(1, 1, 1)])},
            meters_per_unit=0.01)
        self.assertVector(self.read_clip(path)["poses"][0][0]["translate"], [0, 100, 0])

    def test_a_y_up_stage_is_rotated_into_a_z_up_scene_at_the_roots_only(self):
        cmds.upAxis(axis="z")
        path = self.author_usd(
            "yup.usd", ["hips", "hips/spine"], [(0, 100, 0), (0, 10, 0)], ["hips", "hips/spine"],
            {1: ([(0, 100, 0), (0, 10, 0)], [(1, 0, 0, 0)] * 2, [(1, 1, 1)] * 2)}, meters_per_unit=0.01)
        clip = self.read_clip(path)
        # +Y becomes +Z. The root's rotation turns 90 degrees about X, and the child, local to it, is unchanged.
        self.assertTransform(clip["poses"][0][0], transform((0, 0, 100), (HALF_ROOT, 0, 0, HALF_ROOT)))
        self.assertTransform(clip["joints"][0]["rest"], transform((0, 0, 100), (HALF_ROOT, 0, 0, HALF_ROOT)))
        self.assertTransform(clip["poses"][0][1], transform((0, 10, 0)))

    def test_a_z_up_stage_is_rotated_into_a_y_up_scene(self):
        path = self.author_usd(
            "zup.usd", ["hips"], [(0, 0, 100)], ["hips"], {1: ([(0, 0, 100)], [(1, 0, 0, 0)], [(1, 1, 1)])},
            meters_per_unit=0.01, up_axis="Z")
        self.assertTransform(self.read_clip(path)["poses"][0][0], transform((0, 100, 0), (-HALF_ROOT, 0, 0, HALF_ROOT)))

    def test_a_file_with_no_rigmodules_attributes_gives_unmarked_joints(self):
        path = self.author_usd("plain.usd", ["hips"], [(0, 0, 0)], ["hips"], {1: ([(0, 0, 0)], [(1, 0, 0, 0)], [(1, 1, 1)])})
        self.assertEqual(self.read_clip(path)["joints"][0]["markup"], "")

    def test_frames_between_sparse_samples_are_interpolated(self):
        samples = {1: ([(0, 0, 0)], [(1, 0, 0, 0)], [(1, 1, 1)]), 5: ([(0, 8, 0)], [(1, 0, 0, 0)], [(1, 1, 1)])}
        clip = self.read_clip(self.author_usd("sparse.usd", ["hips"], [(0, 0, 0)], ["hips"], samples, meters_per_unit=0.01))
        self.assertEqual((clip["startFrame"], len(clip["poses"])), (1, 5))
        self.assertVector(clip["poses"][2][0]["translate"], [0, 4, 0])

    def test_a_clip_that_starts_at_frame_ten_starts_there(self):
        samples = {10: ([(0, 0, 0)], [(1, 0, 0, 0)], [(1, 1, 1)]), 11: ([(0, 1, 0)], [(1, 0, 0, 0)], [(1, 1, 1)])}
        clip = self.read_clip(self.author_usd("ten.usd", ["hips"], [(0, 0, 0)], ["hips"], samples, meters_per_unit=0.01))
        self.assertEqual(clip["startFrame"], 10)

    def test_the_frame_rate_is_the_files(self):
        path = self.author_usd("fps.usd", ["hips"], [(0, 0, 0)], ["hips"], {1: ([(0, 0, 0)], [(1, 0, 0, 0)], [(1, 1, 1)])})
        self.assertEqual(self.read_clip(path)["frameRate"], 24.0)

    def test_a_joint_the_animation_lacks_stays_at_its_rest_and_order_is_by_name(self):
        # The animation lists only the spine, and lists nothing for the hips.
        path = self.author_usd(
            "subset.usd", ["hips", "hips/spine"], [(0, 100, 0), (0, 10, 0)], ["hips/spine"],
            {1: ([(0, 12, 0)], [(1, 0, 0, 0)], [(1, 1, 1)])}, meters_per_unit=0.01)
        clip = self.read_clip(path)
        self.assertVector(clip["poses"][0][0]["translate"], [0, 100, 0])
        self.assertVector(clip["poses"][0][1]["translate"], [0, 12, 0])

    def test_joints_listed_children_first_come_out_parents_first(self):
        path = self.author_usd(
            "order.usd", ["hips/spine", "hips"], [(0, 10, 0), (0, 100, 0)], ["hips/spine", "hips"],
            {1: ([(0, 11, 0), (0, 101, 0)], [(1, 0, 0, 0)] * 2, [(1, 1, 1)] * 2)}, meters_per_unit=0.01)
        clip = self.read_clip(path)
        self.assertEqual([joint["path"] for joint in clip["joints"]], ["hips", "hips/spine"])
        self.assertVector(clip["poses"][0][0]["translate"], [0, 101, 0])
        self.assertVector(clip["poses"][0][1]["translate"], [0, 11, 0])

    def test_quaternions_keep_one_sign_across_frames(self):
        # Frame 2 stores the same rotation as frame 1 with every component negated, a valid and common USD value.
        samples = {
            1: ([(0, 0, 0)], [(HALF_ROOT, 0, 0, HALF_ROOT)], [(1, 1, 1)]),
            2: ([(0, 0, 0)], [(-HALF_ROOT, 0, 0, -HALF_ROOT)], [(1, 1, 1)]),
        }
        clip = self.read_clip(self.author_usd("sign.usd", ["hips"], [(0, 0, 0)], ["hips"], samples, meters_per_unit=0.01))
        self.assertVector(clip["poses"][1][0]["rotate"], clip["poses"][0][0]["rotate"])

    def test_a_missing_file_is_an_error(self):
        with self.assertRaises(FileNotFoundError):
            rigmodules.jointclip.read_usd(self.path("missing.usd"), self.path("out.json"))

    def test_a_file_that_is_not_usd_is_an_error(self):
        path = self.path("junk.usd")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("not usd")
        with self.assertRaises(Exception):
            rigmodules.jointclip.read_usd(path, self.path("out.json"))

    def test_no_skeleton_is_an_error(self):
        path = self.path("empty.usda")
        stage = pxr.Usd.Stage.CreateNew(path)
        stage.GetRootLayer().Save()
        with self.assertRaisesRegex(ValueError, "exactly one Skeleton, and has 0"):
            rigmodules.jointclip.read_usd(path, self.path("out.json"))

    def test_two_skeletons_are_an_error(self):
        path = self.author_usd(
            "two.usd", ["hips"], [(0, 0, 0)], ["hips"], {1: ([(0, 0, 0)], [(1, 0, 0, 0)], [(1, 1, 1)])}, skeletons=2)
        with self.assertRaisesRegex(ValueError, "exactly one Skeleton, and has 2"):
            rigmodules.jointclip.read_usd(path, self.path("out.json"))

    def test_a_skeleton_with_no_animation_is_an_error(self):
        path = self.author_usd("static.usd", ["hips"], [(0, 0, 0)], None, None)
        with self.assertRaisesRegex(ValueError, "no SkelAnimation"):
            rigmodules.jointclip.read_usd(path, self.path("out.json"))

    def test_a_newer_format_version_is_an_error(self):
        usd_path = self.path("newer.usd")
        rigmodules.jointclip.write_usd(self.write_clip(hand_clip()), usd_path)
        stage = pxr.Usd.Stage.Open(usd_path)
        stage.GetPrimAtPath("/Root/Skeleton").GetAttribute("rigmodules:formatVersion").Set(2)
        stage.GetRootLayer().Save()
        del stage
        with self.assertRaisesRegex(ValueError, "newer than this module's 1"):
            rigmodules.jointclip.read_usd(usd_path, self.path("out.json"))

    def test_a_markup_list_of_the_wrong_length_is_an_error(self):
        usd_path = self.path("short.usd")
        rigmodules.jointclip.write_usd(self.write_clip(hand_clip()), usd_path)
        stage = pxr.Usd.Stage.Open(usd_path)
        stage.GetPrimAtPath("/Root/Skeleton").GetAttribute("rigmodules:jointMarkup").Set(pxr.Vt.StringArray(["", ""]))
        stage.GetRootLayer().Save()
        del stage
        with self.assertRaisesRegex(ValueError, "2 entries for 3 joints"):
            rigmodules.jointclip.read_usd(usd_path, self.path("out.json"))
