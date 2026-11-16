"""saveRigSkeleton and the skeleton hash (D54, design/rigs-and-looks.md section 5).

The skeleton is fk_three_parented in 'skel': a_jnt at the origin, b_jnt at y=2 under it, c_jnt at y=4 under b, all
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
        self.assertEqual(a["name"], "a_jnt")
        self.assertIsNone(a["parent"])
        self.assertEqual(a["rotateOrder"], "xyz")
        self.assertTrue(a["export"])
        self.assertTrue(a["skin"])
        self.assertEqual(a["restLocal"], IDENTITY)
        b = self.joint(data, "b")
        self.assertEqual(b["name"], "b_jnt")
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
            "rename": lambda joints: cmds.rename(joints[2], "tip_jnt"),
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
        helper = cmds.joint(name="skel:helper_jnt")
        self.assertMayaError(lambda: cmds.saveRigSkeleton(*(self.joints + [helper]), file=self.path),
                             "helper_jnt", "no rigMarkup", "nothing was written")
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
