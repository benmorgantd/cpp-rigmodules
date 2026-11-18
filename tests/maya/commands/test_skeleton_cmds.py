"""saveRigSkeleton, loadRigSkeleton and the skeleton hash (D54, design/rigs-and-looks.md section 5).

The skeleton is fk_three_parented in 'skel': a_00_M_JNT at the origin, b_00_M_JNT at y=2 under it, c_00_M_JNT at y=4 under b, all
[Center, a/b/c], exported and skinned, with no rig. reference_hash recomputes the hash from a file's own contents,
written independently of core/SkeletonFile.cpp, so a change to what the hash feeds fails here.
"""
import json
import math
import os
import struct

from maya import cmds

from rigtest import config
from rigtest.case import RigTestCase

SKELETON = "fk_three_parented"
GRID = 1e-4
IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
ROTATE_ORDERS = ["xyz", "yzx", "zxy", "xzy", "yxz", "zyx"]


def round_half_away(value):
    """C++ std::llround: halves round away from zero, unlike Python's round."""
    return int(math.copysign(math.floor(abs(value) + 0.5), value))


def reference_hash(data):
    value = 14695981039346656037

    def feed(raw):
        nonlocal value
        for byte in raw:
            value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF

    def feed_uint(number):
        feed(struct.pack("<I", number))

    def feed_string(text):
        encoded = text.encode("utf-8")
        feed_uint(len(encoded))
        feed(encoded)

    joints = sorted(data["joints"], key=lambda joint: tuple(joint["markup"]))
    feed_uint(len(joints))
    for joint in joints:
        feed_string(joint["markup"][0])
        feed_string(joint["markup"][1])
        feed_uint(1 if joint["export"] else 0)
        feed_uint(1 if joint["skin"] else 0)
        feed_string(joint["name"])
        parent = joint["parent"]
        feed_uint(1 if parent else 0)
        feed_string(parent[0] if parent else "")
        feed_string(parent[1] if parent else "")
        feed_uint(ROTATE_ORDERS.index(joint["rotateOrder"]))
        for number in joint["restLocal"]:
            feed(struct.pack("<q", round_half_away(number / GRID)))
    return "%016x" % value


class SkeletonTestCase(RigTestCase):

    def setUp(self):
        super().setUp()
        self.joints = self.make_skeleton(SKELETON)
        self.path = os.path.join(config.WORK_DIR, "skeleton.json").replace("\\", "/")
        if os.path.exists(self.path):
            os.remove(self.path)

    def save(self, joints=None):
        """Saves and returns (hash, file data)."""
        result = cmds.saveRigSkeleton(*(joints or self.joints), file=self.path)
        with open(self.path, encoding="utf-8") as handle:
            return result, json.load(handle)

    def joint(self, data, usage):
        return next(joint for joint in data["joints"] if joint["markup"][1] == usage)


class FileContents(SkeletonTestCase):

    def test_hash_is_returned_and_stored(self):
        result, data = self.save()
        self.assertRegex(result, r"^[0-9a-f]{16}$")
        self.assertEqual(data["skeletonHash"], result)
        self.assertEqual(data["formatVersion"], 1)

    def test_hash_matches_the_reference(self):
        result, data = self.save()
        self.assertEqual(reference_hash(data), result)

    def test_joints_are_sorted_by_side_then_usage(self):
        _, data = self.save(list(reversed(self.joints)))
        self.assertEqual([joint["markup"] for joint in data["joints"]], [["Center", "a"], ["Center", "b"], ["Center", "c"]])

    def test_each_joint(self):
        _, data = self.save()
        a = self.joint(data, "a")
        self.assertEqual(a["name"], "a_00_M_JNT")
        self.assertIsNone(a["parent"])
        self.assertEqual(a["rotateOrder"], "xyz")
        self.assertTrue(a["export"])
        self.assertTrue(a["skin"])
        self.assertEqual(a["restLocal"], IDENTITY)
        b = self.joint(data, "b")
        self.assertEqual(b["name"], "b_00_M_JNT")
        self.assertEqual(b["parent"], ["Center", "a"])
        self.assertEqual(b["restLocal"][12:15], [0, 2, 0])

    def test_rest_is_local_to_the_parent(self):
        # Moving the root moves its children in world space, but their entries stay the same.
        _, before = self.save()
        cmds.setAttr(self.joints[0] + ".translateX", 5)
        _, after = self.save()
        self.assertEqual(self.joint(after, "a")["restLocal"][12:15], [5, 0, 0])
        for usage in ("b", "c"):
            self.assertEqual(self.joint(after, usage)["restLocal"], self.joint(before, usage)["restLocal"])
        self.assertNotEqual(after["skeletonHash"], before["skeletonHash"])

    def test_group_between_joints_folds_into_rest(self):
        # The parent is the nearest ancestor among the joints, so a group in between moves into restLocal.
        group = cmds.group(empty=True, name="skel:offset_grp", parent=self.joints[0])
        cmds.setAttr(group + ".translateX", 3)
        b = cmds.parent(self.joints[1], group)[0]
        _, data = self.save([self.joints[0], b, self.joints[2]])
        entry = self.joint(data, "b")
        self.assertEqual(entry["parent"], ["Center", "a"])
        # b's own translateX is -3 under the group; relative to a it is 0.
        for actual, expected in zip(entry["restLocal"][12:15], [0, 2, 0]):
            self.assertAlmostEqual(actual, expected, places=9)

    def test_marker_joints_count(self):
        # Every marked joint is in the skeleton, export or not (author, 2026-11-16).
        cmds.rigJointMarkup(self.joints[2], edit=True, export=False)
        result, data = self.save()
        self.assertFalse(self.joint(data, "c")["export"])
        self.assertEqual(reference_hash(data), result)


class Determinism(SkeletonTestCase):

    def test_same_skeleton_same_hash(self):
        first, _ = self.save()
        second, _ = self.save(list(reversed(self.joints)))
        self.assertEqual(first, second)

    def test_reverted_change_gives_the_old_hash(self):
        original, _ = self.save()
        cmds.setAttr(self.joints[1] + ".translateY", 3)
        moved, _ = self.save()
        cmds.setAttr(self.joints[1] + ".translateY", 2)
        reverted, _ = self.save()
        self.assertNotEqual(moved, original)
        self.assertEqual(reverted, original)

    def test_noise_below_the_grid_is_ignored(self):
        original, _ = self.save()
        cmds.setAttr(self.joints[1] + ".translateY", 2 + 1e-6)
        self.assertEqual(self.save()[0], original)

    def test_changes_that_make_another_skeleton(self):
        changes = {
            "move past the grid": lambda joints: cmds.setAttr(joints[1] + ".translateY", 2.001),
            "rotate": lambda joints: cmds.setAttr(joints[1] + ".rotateZ", 10),
            "rename": lambda joints: cmds.rename(joints[2], "tip_00_M_JNT"),
            "rotate order": lambda joints: cmds.setAttr(joints[1] + ".rotateOrder", 3),
            "usage": lambda joints: cmds.rigJointMarkup(joints[2], edit=True, usage="tip"),
            "side": lambda joints: cmds.rigJointMarkup(joints[2], edit=True, side="Left"),
            "skin flag": lambda joints: cmds.rigJointMarkup(joints[2], edit=True, skin=False),
            "export flag": lambda joints: cmds.rigJointMarkup(joints[2], edit=True, export=False),
            "reparent": lambda joints: cmds.parent(joints[2], joints[0]),
        }
        original, _ = self.save()
        for label, change in changes.items():
            with self.subTest(label):
                cmds.file(new=True, force=True)
                joints = self.make_skeleton(SKELETON)
                change(joints)
                joints = cmds.ls("skel:*", type="joint", long=True)
                self.assertNotEqual(self.save(joints)[0], original)


class Errors(SkeletonTestCase):

    def assertNothingWritten(self):
        self.assertFalse(os.path.exists(self.path), "a failed save must not write the file")

    def test_unmarked_joint_fails(self):
        cmds.select(clear=True)
        helper = cmds.joint(name="skel:helper_00_M_JNT")
        self.assertMayaError(lambda: cmds.saveRigSkeleton(*(self.joints + [helper]), file=self.path),
                             "helper_00_M_JNT", "no rigMarkup", "nothing was written")
        self.assertNothingWritten()

    def test_duplicate_markup_fails(self):
        cmds.rigJointMarkup(self.joints[2], edit=True, usage="b")
        self.assertMayaError(lambda: cmds.saveRigSkeleton(*self.joints, file=self.path), "nothing was written")
        self.assertNothingWritten()

    def test_node_that_is_not_a_joint_fails(self):
        node = cmds.createNode("transform", name="skel:notAJoint")
        self.assertMayaError(lambda: cmds.saveRigSkeleton(node, file=self.path), "skel:notAJoint is not a joint")
        self.assertNothingWritten()

    def test_no_joints_fails(self):
        self.assertMayaError(lambda: cmds.saveRigSkeleton(file=self.path), "needs at least one joint name")
        self.assertNothingWritten()

    def test_file_flag_is_required(self):
        self.assertMayaError(lambda: cmds.saveRigSkeleton(*self.joints), "needs -file")


# ---- loadRigSkeleton ---------------------------------------------------------------------------
# The skeleton file is a skeleton's only stored form (D54, author 2026-11-16), so a load must give back exactly what
# a save wrote.

LOADED = "loaded"


def hand_written_skeleton():
    """Written by hand, not saved from a scene. root_00_M_JNT is at (1, 2, 3), turned 90 degrees about Z, with rotate
    order zxy. tip_00_M_JNT is 2 along its parent's Y, a marker joint. Rows are Maya's: a 90 degree turn about Z has the
    X row (0, 1, 0) and the Y row (-1, 0, 0), so tip_00_M_JNT's world position is (1, 2, 3) + 2 * (-1, 0, 0) = (-1, 2, 3)."""
    data = {
        "formatVersion": 1,
        "skeletonHash": "",
        "joints": [
            {"markup": ["Center", "root"], "export": True, "skin": True, "name": "root_00_M_JNT", "parent": None,
             "rotateOrder": "zxy", "restLocal": [0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 1, 2, 3, 1]},
            {"markup": ["Left", "tip"], "export": False, "skin": False, "name": "tip_00_M_JNT", "parent": ["Center", "root"],
             "rotateOrder": "xyz", "restLocal": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 2, 0, 1]},
        ],
    }
    data["skeletonHash"] = reference_hash(data)
    return data


class LoadTestCase(SkeletonTestCase):

    def write(self, data):
        with open(self.path, "w", encoding="utf-8") as handle:
            if isinstance(data, str):
                handle.write(data)
            else:
                json.dump(data, handle)

    def load(self, namespace=LOADED):
        return cmds.loadRigSkeleton(file=self.path, namespace=namespace)

    def read_text(self, path=None):
        with open(path or self.path, encoding="utf-8") as handle:
            return handle.read()

    def assertNothingLoaded(self, before):
        self.assertEqual(sorted(cmds.ls(type="joint", long=True)), sorted(before), "a failed load must create nothing")


class Load(LoadTestCase):

    def test_hand_written_file(self):
        self.write(hand_written_skeleton())
        self.assertEqual(self.load(), ["loaded:root_00_M_JNT", "loaded:tip_00_M_JNT"])

        self.assertWorldTranslation("loaded:root_00_M_JNT", [1, 2, 3])
        self.assertWorldRotation("loaded:root_00_M_JNT", [0, 0, 90])
        self.assertWorldTranslation("loaded:tip_00_M_JNT", [-1, 2, 3])
        self.assertEqual(cmds.listRelatives("loaded:tip_00_M_JNT", parent=True), ["loaded:root_00_M_JNT"])
        self.assertIsNone(cmds.listRelatives("loaded:root_00_M_JNT", parent=True))
        self.assertEqual(cmds.getAttr("loaded:root_00_M_JNT.rotateOrder"), 2)   # zxy
        self.assertEqual(cmds.getAttr("loaded:tip_00_M_JNT.rotateOrder"), 0)

    def test_rest_goes_into_joint_orient_and_rotate_stays_zero(self):
        self.write(hand_written_skeleton())
        self.load()
        self.assertEqual(cmds.getAttr("loaded:root_00_M_JNT.rotate")[0], (0.0, 0.0, 0.0))
        for actual, expected in zip(cmds.getAttr("loaded:root_00_M_JNT.jointOrient")[0], (0, 0, 90)):
            self.assertAlmostEqual(actual, expected, places=6)
        self.assertEqual(cmds.getAttr("loaded:root_00_M_JNT.translate")[0], (1.0, 2.0, 3.0))
        for joint in ("loaded:root_00_M_JNT", "loaded:tip_00_M_JNT"):
            self.assertFalse(cmds.getAttr(joint + ".segmentScaleCompensate"))

    def test_markup_is_loaded(self):
        self.write(hand_written_skeleton())
        self.load()
        root = json.loads(cmds.rigJointMarkup("loaded:root_00_M_JNT", query=True)[0])
        tip = json.loads(cmds.rigJointMarkup("loaded:tip_00_M_JNT", query=True)[0])
        self.assertEqual((root["side"], root["usage"], root["export"], root["skin"]), ("Center", "root", True, True))
        self.assertEqual((tip["side"], tip["usage"], tip["export"], tip["skin"]), ("Left", "tip", False, False))

    def test_save_load_save_gives_the_same_file(self):
        saved_hash, _ = self.save()
        first = self.read_text()
        loaded = self.load()
        second_path = os.path.join(config.WORK_DIR, "skeleton_again.json").replace("\\", "/")
        self.assertEqual(cmds.saveRigSkeleton(*loaded, file=second_path), saved_hash)
        self.assertEqual(self.read_text(second_path), first)

    def test_scaled_and_rotated_joints_round_trip(self):
        cmds.setAttr(self.joints[0] + ".scale", 2, 1, 0.5)
        cmds.setAttr(self.joints[1] + ".rotate", 20, -35, 50)
        cmds.setAttr(self.joints[1] + ".rotateOrder", 4)
        cmds.setAttr(self.joints[2] + ".jointOrient", 10, 0, 45)
        saved_hash, _ = self.save()
        loaded = self.load()
        # b's rotate moves into jointOrient: the same rest, with rotate at zero.
        self.assertEqual(cmds.getAttr("loaded:b_00_M_JNT.rotate")[0], (0.0, 0.0, 0.0))
        self.assertEqual(cmds.saveRigSkeleton(*loaded, file=self.path), saved_hash)
        for source, copy in zip(self.joints, loaded):
            actual = cmds.xform(copy, query=True, worldSpace=True, matrix=True)
            expected = cmds.xform(source, query=True, worldSpace=True, matrix=True)
            for a, e in zip(actual, expected):
                self.assertAlmostEqual(a, e, places=9)

    def test_group_between_joints_is_flattened(self):
        group = cmds.group(empty=True, name="skel:offset_grp", parent=self.joints[0])
        cmds.setAttr(group + ".translateX", 3)
        b = cmds.parent(self.joints[1], group)[0]
        self.save([self.joints[0], b, self.joints[2]])
        self.load()
        self.assertEqual(cmds.listRelatives("loaded:b_00_M_JNT", parent=True), ["loaded:a_00_M_JNT"])
        self.assertWorldTranslation("loaded:b_00_M_JNT", [0, 2, 0])
        self.assertEqual(cmds.ls("loaded:*", exactType="transform"), [])

    def test_namespace_defaults_to_the_current_one(self):
        self.save()
        self.assertEqual(cmds.loadRigSkeleton(file=self.path), ["a_00_M_JNT", "b_00_M_JNT", "c_00_M_JNT"])
        cmds.namespace(add=":other")
        cmds.namespace(setNamespace=":other")
        self.assertEqual(cmds.loadRigSkeleton(file=self.path), ["other:a_00_M_JNT", "other:b_00_M_JNT", "other:c_00_M_JNT"])
        self.assertEqual(cmds.namespaceInfo(currentNamespace=True), "other")

    def test_namespace_colons_are_trimmed_and_the_namespace_is_made(self):
        self.save()
        self.assertFalse(cmds.namespace(exists=":fresh"))
        self.assertEqual(self.load(":fresh:"), ["fresh:a_00_M_JNT", "fresh:b_00_M_JNT", "fresh:c_00_M_JNT"])
        self.assertEqual(cmds.namespaceInfo(currentNamespace=True), ":")

    def test_undo_and_redo(self):
        saved_hash, _ = self.save()
        loaded = self.load()
        cmds.undo()
        self.assertEqual(cmds.ls("loaded:*", type="joint"), [])
        cmds.redo()
        self.assertEqual(sorted(cmds.ls("loaded:*", type="joint")), sorted(loaded))
        self.assertEqual(cmds.saveRigSkeleton(*loaded, file=self.path), saved_hash)

    def test_rig_built_on_loaded_joints_stamps_the_file_hash(self):
        saved_hash, _ = self.save()
        self.build("fkchain_three", self.load())
        (rig_root,) = cmds.ls(type="rigRootModule")
        self.assertEqual(cmds.getAttr(rig_root + ".skeletonHash"), saved_hash)

    def test_stale_stored_hash_still_loads(self):
        # A hand edit or a merge leaves the stored hash behind. The joints are used and the next save fixes it.
        saved_hash, data = self.save()
        data["skeletonHash"] = "0000000000000000"
        self.write(data)
        loaded = self.load()
        self.assertEqual(cmds.saveRigSkeleton(*loaded, file=self.path), saved_hash)
        self.assertEqual(json.loads(self.read_text())["skeletonHash"], saved_hash)


class LoadErrors(LoadTestCase):

    def test_name_taken_in_the_namespace_fails_and_creates_nothing(self):
        self.save()
        before = cmds.ls(type="joint", long=True)
        self.assertMayaError(lambda: self.load(config.SKELETON_NAMESPACE), "a_00_M_JNT", "already exists in the namespace")
        self.assertNothingLoaded(before)

    def test_file_flag_is_required(self):
        self.assertMayaError(lambda: cmds.loadRigSkeleton(namespace=LOADED), "needs -file")

    def test_missing_file_fails(self):
        self.assertMayaError(self.load, "Could not read the skeleton file")

    def test_invalid_files_fail_and_create_nothing(self):
        def joints(data):
            return data["joints"]

        def set_key(index, key, value):
            return lambda data: joints(data)[index].__setitem__(key, value)

        cases = {
            "not JSON": (lambda data: None, "parse error"),
            "missing key": (lambda data: data.pop("joints"), "joints"),
            "unknown key": (lambda data: data.__setitem__("extra", 1), "extra"),
            "newer version": (lambda data: data.__setitem__("formatVersion", 2), "formatVersion"),
            "version not a number": (lambda data: data.__setitem__("formatVersion", "1"), "must be a whole number"),
            "joints not a list": (lambda data: data.__setitem__("joints", {}), "must be a list of joints"),
            "joint missing key": (lambda data: joints(data)[1].pop("name"), "joints[1]", "name"),
            "joint unknown key": (set_key(1, "radius", 1), "joints[1]", "radius"),
            "bad side": (set_key(0, "markup", ["Middle", "root"]), "'Middle' is not one of"),
            "empty usage": (set_key(0, "markup", ["Center", ""]), "usage is empty"),
            "markup not a pair": (set_key(0, "markup", "root"), "must be [side, usage]"),
            "export not a bool": (set_key(0, "export", 1), "joints[0].export", "must be true or false"),
            "empty name": (set_key(0, "name", ""), "joints[0].name", "must be a non-empty string"),
            "unknown rotate order": (set_key(0, "rotateOrder", "abc"), "joints[0].rotateOrder", "must be one of"),
            "short matrix": (set_key(0, "restLocal", [1] * 15), "joints[0].restLocal", "must be 16 numbers"),
            "markup listed twice": (set_key(1, "markup", ["Center", "root"]), "[Center, root] is listed twice"),
            "parent not in the file": (set_key(1, "parent", ["Center", "hips"]), "tip_00_M_JNT", "[Center, hips] is not a joint in the file"),
            "parents in a loop": (set_key(0, "parent", ["Left", "tip"]), "parents form a loop", "root_00_M_JNT", "tip_00_M_JNT"),
        }
        for label, (change, *contains) in cases.items():
            with self.subTest(label):
                data = hand_written_skeleton()
                change(data)
                self.write("{ not json" if label == "not JSON" else data)
                before = cmds.ls(type="joint", long=True)
                self.assertMayaError(self.load, "nothing was loaded", *contains)
                self.assertNothingLoaded(before)
                self.assertFalse(cmds.namespace(exists=":" + LOADED), "a failed read must not make the namespace")
