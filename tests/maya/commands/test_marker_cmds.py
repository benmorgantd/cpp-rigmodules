"""Markers (D73, design/markers.md): rigMarkerMarkup, saveRigMarkers, loadRigMarkers, mirrorRigMarkers, getRigMarkers,
the build with createRigFromTemplate markers=, the markersHash stamps and the stale status on loaded poses.

The skeleton is mirror_arms in 'skel': upperArm_00_L_JNT at (2, 5, 0), upperArm_00_R_JNT at (-2, 5, 0), lowerArm L (4, 5, 0)
and R (-4, 5, 0), with no parents and no rotation, so a marker's offset is its world position minus its joint's, and
every expected number below is worked by hand from those. reference_hash is written independently of core/MarkerFile.cpp,
so a change to what the hash feeds fails here.
"""
import json
import math
import os
import struct

from maya import cmds
import maya.api.OpenMaya as om

from rigtest import config, fixtures
from rigtest.case import RigTestCase, rig_name

SKELETON = "mirror_arms"
TEMPLATE = "mirror_arms"
GRID = 1e-4
NAMESPACE = config.SKELETON_NAMESPACE


def round_half_away(value):
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

    markers = sorted(data["markers"], key=lambda marker: tuple(marker["markup"]))
    feed_uint(len(markers))
    for marker in markers:
        feed_string(marker["markup"][0])
        feed_string(marker["markup"][1])
        feed_string(marker["joint"][0])
        feed_string(marker["joint"][1])
        feed_uint(1 if marker["export"] else 0)
        feed_string(marker["name"])
        for number in marker["offsetLocal"]:
            feed(struct.pack("<q", round_half_away(number / GRID)))
    return "%016x" % value


def capture_warnings():
    """A list that fills with every warning Maya prints until the returned function is called."""
    warnings = []

    def callback(message, messageType, clientData):
        if messageType == om.MCommandMessage.kWarning:
            warnings.append(message)

    callbackId = om.MCommandMessage.addCommandOutputCallback(callback)

    def stop():
        om.MMessage.removeCallback(callbackId)
        return warnings

    return stop


class MarkerTestCase(RigTestCase):

    def setUp(self):
        super().setUp()
        self.joints = self.make_skeleton(SKELETON)
        self.path = os.path.join(config.WORK_DIR, "markers.json").replace("\\", "/")
        if os.path.exists(self.path):
            os.remove(self.path)

    def joint(self, side, usage):
        return ":%s:%s_00_%s_JNT" % (NAMESPACE, usage, side)

    def locator(self, usage, side, joint, position, name=None):
        """A marker locator named for the convention, in the skeleton's namespace, at a world position."""
        short = "%s_00_%s_MRK" % (usage, side[0] if side != "Center" else "M")
        node = cmds.spaceLocator(name=name or (":%s:%s" % (NAMESPACE, short)))[0]
        cmds.xform(node, worldSpace=True, translation=position)
        cmds.rigMarkerMarkup(node, usage=usage, side={"L": "Left", "R": "Right", "M": "Center"}[short.split("_")[2]],
                             joint=joint, export=False)
        return node

    def save(self, *nodes):
        result = cmds.saveRigMarkers(*nodes, file=self.path)
        with open(self.path, encoding="utf-8") as handle:
            return result, json.load(handle)

    def entry(self, data, side, usage):
        return next(marker for marker in data["markers"] if marker["markup"] == [side, usage])


class Markup(MarkerTestCase):

    def test_create_and_query(self):
        node = cmds.spaceLocator(name=":skel:heel_00_L_MRK")[0]
        cmds.rigMarkerMarkup(node, usage="heel", side="Left", joint=self.joint("L", "upperArm"), export=True)
        self.assertEqual(cmds.rigMarkerMarkup(node, query=True, usage=True), ["heel"])
        self.assertEqual(cmds.rigMarkerMarkup(node, query=True, side=True), ["Left"])
        self.assertEqual(cmds.rigMarkerMarkup(node, query=True, export=True), ["true"])
        self.assertEqual(cmds.rigMarkerMarkup(node, query=True, joint=True), ["[Left, upperArm]"])
        self.assertEqual(json.loads(cmds.rigMarkerMarkup(node, query=True)[0]),
                         {"usage": "heel", "side": "Left", "export": True, "jointSide": "Left", "jointUsage": "upperArm"})

    def test_a_node_with_no_markup_queries_empty(self):
        node = cmds.spaceLocator()[0]
        self.assertEqual(cmds.rigMarkerMarkup(node, query=True, usage=True), [""])

    def test_the_joint_defaults_to_the_nearest_marked_joint_above(self):
        node = cmds.spaceLocator(name=":skel:ball_00_L_MRK")[0]
        node = cmds.parent(node, self.joint("L", "lowerArm"))[0]
        cmds.rigMarkerMarkup(node, usage="ball", side="Left")
        self.assertEqual(cmds.rigMarkerMarkup(node, query=True, joint=True), ["[Left, lowerArm]"])
        self.assertEqual(cmds.rigMarkerMarkup(node, query=True, export=True), ["false"])

    def test_edit_changes_only_the_flags_given(self):
        node = self.locator("heel", "Left", self.joint("L", "upperArm"), (0, 0, 0))
        cmds.rigMarkerMarkup(node, edit=True, export=True)
        self.assertEqual(cmds.rigMarkerMarkup(node, query=True, export=True), ["true"])
        self.assertEqual(cmds.rigMarkerMarkup(node, query=True, usage=True), ["heel"])
        cmds.rigMarkerMarkup(node, edit=True, joint=self.joint("L", "lowerArm"))
        self.assertEqual(cmds.rigMarkerMarkup(node, query=True, joint=True), ["[Left, lowerArm]"])

    def test_remove_and_undo(self):
        node = self.locator("heel", "Left", self.joint("L", "upperArm"), (0, 0, 0))
        cmds.rigMarkerMarkup(node, remove=True)
        self.assertFalse(cmds.attributeQuery("rigMarkerMarkup", node=node, exists=True))
        cmds.undo()
        self.assertTrue(cmds.attributeQuery("rigMarkerMarkup", node=node, exists=True))

    def test_create_is_undoable(self):
        node = cmds.spaceLocator()[0]
        cmds.rigMarkerMarkup(node, usage="heel", side="Left", joint=self.joint("L", "upperArm"))
        cmds.undo()
        self.assertFalse(cmds.attributeQuery("rigMarkerMarkup", node=node, exists=True))
        cmds.redo()
        self.assertEqual(cmds.rigMarkerMarkup(node, query=True, usage=True), ["heel"])

    def test_errors(self):
        node = cmds.spaceLocator()[0]
        self.assertMayaError(lambda: cmds.rigMarkerMarkup(node, usage="heel"), "needs -usage and -side")
        self.assertMayaError(lambda: cmds.rigMarkerMarkup(node, usage="heel", side="Left"), "no marked joint above it")
        self.assertMayaError(lambda: cmds.rigMarkerMarkup(node, usage="heel", side="Up", joint=self.joint("L", "upperArm")),
                             "Invalid marker markup")
        self.assertMayaError(lambda: cmds.rigMarkerMarkup(node, usage="heel", side="Left", joint=node), "not a marked joint")
        self.assertMayaError(lambda: cmds.rigMarkerMarkup(node, edit=True, export=True), "no marker markup to edit")
        cmds.rigMarkerMarkup(node, usage="heel", side="Left", joint=self.joint("L", "upperArm"))
        self.assertMayaError(lambda: cmds.rigMarkerMarkup(node, usage="heel", side="Left", joint=self.joint("L", "upperArm")),
                             "already has marker markup")
        self.assertMayaError(lambda: cmds.rigMarkerMarkup(node, query=True, usage=True, side=True), "at most one")
        self.assertMayaError(lambda: cmds.rigMarkerMarkup("no_such_node", query=True), "no_such_node")
        self.assertMayaError(lambda: cmds.rigMarkerMarkup(node, remove=True, usage="x"), "cannot be combined")


class Save(MarkerTestCase):

    def test_offset_is_the_world_position_minus_the_joint(self):
        # heel at world (3, 4, 1) following upperArm L at (2, 5, 0): offset (1, -1, 1) in the joint's unrotated space.
        heel = self.locator("heel", "Left", self.joint("L", "upperArm"), (3, 4, 1))
        result, data = self.save(heel)
        entry = self.entry(data, "Left", "heel")
        self.assertEqual(entry["joint"], ["Left", "upperArm"])
        self.assertEqual(entry["name"], "heel_00_L_MRK")
        self.assertFalse(entry["export"])
        expected = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, -1, 1, 1]
        for got, want in zip(entry["offsetLocal"], expected):
            self.assertAlmostEqual(got, want, places=5)

    def test_offset_follows_a_rotated_joint(self):
        # The joint is turned 90 degrees about Y, so its local X points down world -Z. A marker at world (2, 5, -3) is 3 along local X.
        cmds.xform(self.joint("L", "upperArm"), worldSpace=True, rotation=(0, 90, 0))
        node = self.locator("ball", "Left", self.joint("L", "upperArm"), (2, 5, -3))
        entry = self.entry(self.save(node)[1], "Left", "ball")
        for got, want in zip(entry["offsetLocal"][12:15], (3, 0, 0)):
            self.assertAlmostEqual(got, want, places=5)

    def test_hash_is_returned_stored_and_matches_the_reference(self):
        left = self.locator("heel", "Left", self.joint("L", "upperArm"), (3, 4, 1))
        right = self.locator("heel", "Right", self.joint("R", "upperArm"), (-3, 4, 1))
        result, data = self.save(left, right)
        self.assertRegex(result, r"^[0-9a-f]{16}$")
        self.assertEqual(data["markersHash"], result)
        self.assertEqual(data["formatVersion"], 1)
        self.assertEqual(result, reference_hash(data))
        self.assertEqual([marker["markup"] for marker in data["markers"]], [["Left", "heel"], ["Right", "heel"]])

    def test_hash_does_not_depend_on_the_order_of_the_nodes(self):
        left = self.locator("heel", "Left", self.joint("L", "upperArm"), (3, 4, 1))
        right = self.locator("heel", "Right", self.joint("R", "upperArm"), (-3, 4, 1))
        self.assertEqual(self.save(left, right)[0], self.save(right, left)[0])

    def test_moving_a_marker_changes_the_hash(self):
        node = self.locator("heel", "Left", self.joint("L", "upperArm"), (3, 4, 1))
        before = self.save(node)[0]
        cmds.xform(node, worldSpace=True, translation=(3, 4, 1.5))
        self.assertNotEqual(self.save(node)[0], before)

    def test_the_skeleton_hash_does_not_see_markers(self):
        before = cmds.saveRigSkeleton(*self.joints, file=self.path.replace("markers", "skeleton"))
        self.save(self.locator("heel", "Left", self.joint("L", "upperArm"), (3, 4, 1)))
        self.assertEqual(cmds.saveRigSkeleton(*self.joints, file=self.path.replace("markers", "skeleton")), before)

    def test_errors_write_nothing(self):
        plain = cmds.spaceLocator(name=":skel:heel_00_L_MRK")[0]
        self.assertMayaError(lambda: cmds.saveRigMarkers(plain, file=self.path), "has no rigMarkerMarkup")
        badName = self.locator("heel", "Left", self.joint("L", "upperArm"), (0, 0, 0), name=":skel:locator1")
        self.assertMayaError(lambda: cmds.saveRigMarkers(badName, file=self.path), "must be <camelCaseName>", "MRK")
        orphan = self.locator("toe", "Left", self.joint("L", "upperArm"), (0, 0, 0))
        cmds.rigMarkerMarkup(orphan, edit=True, joint=self.joints[0])
        cmds.rigMarkerMarkup(orphan, edit=True, usage="toe")
        cmds.delete(self.joint("L", "lowerArm"))
        gone = self.locator("tip", "Left", self.joint("L", "upperArm"), (0, 0, 0))
        cmds.rigMarkerMarkup(gone, edit=True, joint=self.joint("R", "upperArm"))
        cmds.rigJointMarkup(self.joint("R", "upperArm"), remove=True)
        self.assertMayaError(lambda: cmds.saveRigMarkers(gone, file=self.path), "which is not a marked joint")
        self.assertMayaError(lambda: cmds.saveRigMarkers(file=self.path), "at least one node")
        self.assertFalse(os.path.exists(self.path))

    def test_two_nodes_with_one_markup_are_refused(self):
        a = self.locator("heel", "Left", self.joint("L", "upperArm"), (3, 4, 1))
        b = self.locator("heel", "Left", self.joint("L", "upperArm"), (3, 4, 2), name=":skel:heel_01_L_MRK")
        self.assertMayaError(lambda: cmds.saveRigMarkers(a, b, file=self.path), "is marked on another node too")


class Load(MarkerTestCase):

    def saved(self):
        left = self.locator("heel", "Left", self.joint("L", "upperArm"), (3, 4, 1))
        right = self.locator("ball", "Right", self.joint("R", "lowerArm"), (-5, 5.5, 2))
        cmds.rigMarkerMarkup(right, edit=True, export=True)
        self.save(left, right)
        cmds.delete(left, right)

    def test_creates_marked_locators_at_their_place(self):
        self.saved()
        nodes = cmds.loadRigMarkers(file=self.path, namespace=NAMESPACE)
        self.assertEqual([n.split("|")[-1] for n in nodes], ["skel:heel_00_L_MRK", "skel:ball_00_R_MRK"])
        self.assertWorldTranslation("skel:heel_00_L_MRK", (3, 4, 1))
        self.assertWorldTranslation("skel:ball_00_R_MRK", (-5, 5.5, 2))
        self.assertEqual(cmds.rigMarkerMarkup("skel:ball_00_R_MRK", query=True, joint=True), ["[Right, lowerArm]"])
        self.assertEqual(cmds.rigMarkerMarkup("skel:ball_00_R_MRK", query=True, export=True), ["true"])

    def test_the_markers_follow_the_joints_they_are_loaded_onto(self):
        self.saved()
        cmds.setAttr(self.joint("L", "upperArm") + ".translateY", 7)   # the joint is 2 higher than when saved
        cmds.loadRigMarkers(file=self.path, namespace=NAMESPACE)
        self.assertWorldTranslation("skel:heel_00_L_MRK", (3, 6, 1))

    def test_save_load_save_gives_the_same_file(self):
        self.saved()
        first = open(self.path, encoding="utf-8").read()
        nodes = cmds.loadRigMarkers(file=self.path, namespace=NAMESPACE)
        cmds.saveRigMarkers(*nodes, file=self.path)
        self.assertEqual(open(self.path, encoding="utf-8").read(), first)

    def test_display_size_scales_the_shape(self):
        self.saved()
        nodes = cmds.loadRigMarkers(file=self.path, namespace=NAMESPACE, displaySize=5.0)
        matrix = cmds.getAttr(nodes[0] + ".shapeTransform")
        self.assertAlmostEqual(matrix[0], 5.0)
        self.assertAlmostEqual(matrix[5], 5.0)
        self.assertAlmostEqual(matrix[10], 5.0)

    def test_undo_and_redo(self):
        self.saved()
        cmds.loadRigMarkers(file=self.path, namespace=NAMESPACE)
        cmds.undo()
        self.assertNotExists("skel:heel_00_L_MRK")
        cmds.redo()
        self.assertExists("skel:heel_00_L_MRK")
        self.assertWorldTranslation("skel:heel_00_L_MRK", (3, 4, 1))

    def test_a_missing_joint_creates_nothing(self):
        self.saved()
        cmds.rigJointMarkup(self.joint("R", "lowerArm"), remove=True)
        self.assertMayaError(lambda: cmds.loadRigMarkers(file=self.path, namespace=NAMESPACE), "nothing was loaded")
        self.assertNotExists("skel:heel_00_L_MRK")

    def test_a_name_in_use_creates_nothing(self):
        self.saved()
        cmds.spaceLocator(name=":skel:heel_00_L_MRK")
        self.assertMayaError(lambda: cmds.loadRigMarkers(file=self.path, namespace=NAMESPACE), "already exists")
        self.assertEqual(len(cmds.ls("skel:*_MRK")), 1)

    def test_a_bad_file_creates_nothing(self):
        with open(self.path, "w", encoding="utf-8") as handle:
            handle.write('{"formatVersion": 1, "markersHash": "x", "markers": [{"markup": ["Up", "a"]}]}')
        self.assertMayaError(lambda: cmds.loadRigMarkers(file=self.path, namespace=NAMESPACE), "failed validation")
        self.assertEqual(cmds.ls("skel:*_MRK"), [])

    def test_errors(self):
        self.assertMayaError(lambda: cmds.loadRigMarkers(namespace=NAMESPACE), "needs -file")
        self.assertMayaError(lambda: cmds.loadRigMarkers(file=self.path, namespace=NAMESPACE), "markers")
        self.saved()
        self.assertMayaError(lambda: cmds.loadRigMarkers(file=self.path, namespace=NAMESPACE, displaySize=0), "displaySize")


class Mirror(MarkerTestCase):

    def test_writes_the_other_side_at_the_reflected_place(self):
        left = self.locator("heel", "Left", self.joint("L", "upperArm"), (3, 4, 1))
        cmds.rigMarkerMarkup(left, edit=True, export=True)
        result = cmds.mirrorRigMarkers(left)
        self.assertEqual([n.split("|")[-1] for n in result], ["skel:heel_00_R_MRK"])
        self.assertWorldTranslation("skel:heel_00_R_MRK", (-3, 4, 1))
        self.assertEqual(cmds.rigMarkerMarkup("skel:heel_00_R_MRK", query=True, side=True), ["Right"])
        self.assertEqual(cmds.rigMarkerMarkup("skel:heel_00_R_MRK", query=True, joint=True), ["[Right, upperArm]"])
        self.assertEqual(cmds.rigMarkerMarkup("skel:heel_00_R_MRK", query=True, export=True), ["true"])
        # Saved, its offset is in the right joint's frame: (-3, 4, 1) - (-2, 5, 0).
        entry = self.entry(self.save(left, "skel:heel_00_R_MRK")[1], "Right", "heel")
        for got, want in zip(entry["offsetLocal"][12:15], (-1, -1, 1)):
            self.assertAlmostEqual(got, want, places=5)

    def test_the_orientation_is_reflected_not_flipped(self):
        # Turned 30 degrees about Y, the reflected marker is turned -30: a right-handed frame stays right-handed.
        left = self.locator("heel", "Left", self.joint("L", "upperArm"), (3, 4, 1))
        cmds.xform(left, worldSpace=True, rotation=(0, 30, 0))
        right = cmds.mirrorRigMarkers(left)[0]
        self.assertWorldRotation(right, (0, -30, 0))

    def test_an_existing_counterpart_moves(self):
        left = self.locator("heel", "Left", self.joint("L", "upperArm"), (3, 4, 1))
        cmds.mirrorRigMarkers(left)
        cmds.xform(left, worldSpace=True, translation=(3, 4, 2))
        cmds.mirrorRigMarkers(left)
        self.assertEqual(len(cmds.ls("skel:*_MRK")), 2)
        self.assertWorldTranslation("skel:heel_00_R_MRK", (-3, 4, 2))

    def test_undo_and_redo(self):
        left = self.locator("heel", "Left", self.joint("L", "upperArm"), (3, 4, 1))
        cmds.mirrorRigMarkers(left)
        cmds.undo()
        self.assertNotExists("skel:heel_00_R_MRK")
        cmds.redo()
        self.assertWorldTranslation("skel:heel_00_R_MRK", (-3, 4, 1))

    def test_errors_change_nothing(self):
        center = cmds.spaceLocator(name=":skel:mid_00_M_MRK")[0]
        cmds.rigMarkerMarkup(center, usage="mid", side="Center", joint=":skel:chest_00_M_JNT")
        self.assertMayaError(lambda: cmds.mirrorRigMarkers(center), "no other side")
        lonely = self.locator("toe", "Left", self.joint("L", "lowerArm"), (5, 5, 0))
        cmds.delete(self.joint("R", "lowerArm"))
        self.assertMayaError(lambda: cmds.mirrorRigMarkers(lonely), "other side's joint")
        self.assertEqual(len(cmds.ls("skel:*_MRK")), 2)


class Build(MarkerTestCase):

    def write_markers(self, *specs):
        """Marker locators saved to the file, then deleted, so the build is the only thing that sees them. specs are
        (usage, side letter, joint selector side and usage, position)."""
        nodes = [self.locator(usage, side, self.joint(joint[0], joint[1]), position) for usage, side, joint, position in specs]
        result = self.save(*nodes)[0]
        cmds.delete(nodes)
        return result

    def build_with(self, markers=None):
        kwargs = {"template": fixtures.template_path(TEMPLATE).replace("\\", "/"),
                  "asset": config.ASSET_ID, "namespace": config.RIG_NAMESPACE}
        if markers:
            kwargs["markers"] = markers
        cmds.createRigFromTemplate(*self.joints, **kwargs)
        return cmds.ls(type="rigRootModule")[0]

    def test_without_the_flag_nothing_is_stamped(self):
        root = self.build_with()
        self.assertEqual(cmds.getAttr(root + ".markersHash"), "")
        self.assertEqual(json.loads(cmds.getRigInfo(root))["markersHash"], "")
        self.assertEqual(json.loads(cmds.getRigMarkers(root)), [])

    def test_with_the_flag_the_hash_is_stamped_and_the_markers_listed(self):
        digest = self.write_markers(("heel", "L", ("L", "upperArm"), (3, 4, 1)), ("heel", "R", ("R", "upperArm"), (-3, 4, 1)))
        root = self.build_with(self.path)
        self.assertEqual(cmds.getAttr(root + ".markersHash"), digest)
        self.assertEqual(json.loads(cmds.getRigInfo(root))["markersHash"], digest)
        listed = json.loads(cmds.getRigMarkers(root))
        self.assertEqual([marker["markup"] for marker in listed], [["Left", "heel"], ["Right", "heel"]])
        left = listed[0]
        self.assertEqual(left["joint"], ["Left", "upperArm"])
        self.assertEqual(left["name"], "heel_00_L_MRK")
        self.assertFalse(left["export"])
        for got, want in zip(left["world"][12:15], (3, 4, 1)):
            self.assertAlmostEqual(got, want, places=5)

    def test_getRigMarkers_works_from_any_rig_node(self):
        self.write_markers(("heel", "L", ("L", "upperArm"), (3, 4, 1)))
        root = self.build_with(self.path)
        module = rig_name("layout_00_M_MOD")
        self.assertEqual(cmds.getRigMarkers(module), cmds.getRigMarkers(root))
        self.assertMayaError(lambda: cmds.getRigMarkers(self.joints[0]), "not a node of a rig")

    def test_the_world_is_the_joints_rest_world_times_the_offset(self):
        # Saved with the joint turned 90 degrees about Y: the offset (3, 0, 0) is along local X, which is world -Z.
        cmds.xform(self.joint("L", "upperArm"), worldSpace=True, rotation=(0, 90, 0))
        self.write_markers(("ball", "L", ("L", "upperArm"), (2, 5, -3)))
        listed = json.loads(cmds.getRigMarkers(self.build_with(self.path)))
        for got, want in zip(listed[0]["world"][12:15], (2, 5, -3)):
            self.assertAlmostEqual(got, want, places=5)

    def test_markers_leave_the_skeleton_hash_and_the_rig_alone(self):
        plain = self.build_with()
        skeletonHash = cmds.getAttr(plain + ".skeletonHash")
        controls = sorted(cmds.ls(type="rigControlNode"))
        cmds.file(new=True, force=True)
        self.joints = self.make_skeleton(SKELETON)
        self.write_markers(("heel", "L", ("L", "upperArm"), (3, 4, 1)))
        root = self.build_with(self.path)
        self.assertEqual(cmds.getAttr(root + ".skeletonHash"), skeletonHash)
        self.assertEqual(sorted(cmds.ls(type="rigControlNode")), controls)

    def test_a_marker_whose_joint_is_not_in_the_build_is_left_out_with_a_warning(self):
        # tip_00_M_JNT is marked only while the markers are written, so the build is not given it.
        cmds.select(clear=True)
        tip = cmds.joint(name=":skel:tip_00_M_JNT", position=(0, 9, 0))
        cmds.rigJointMarkup(tip, usage="tip", side="Center", export=False, skin=False)
        extra = self.locator("tip", "Center", tip, (0, 10, 0))
        heel = self.locator("heel", "Left", self.joint("L", "upperArm"), (3, 4, 1))
        digest = self.save(extra, heel)[0]
        cmds.delete(extra, heel, tip)
        stop = capture_warnings()
        root = self.build_with(self.path)
        warnings = stop()
        self.assertTrue(any("tip" in text and "is not among the joints" in text for text in warnings), warnings)
        listed = json.loads(cmds.getRigMarkers(root))
        self.assertEqual([marker["markup"] for marker in listed], [["Left", "heel"]])
        self.assertEqual(cmds.getAttr(root + ".markersHash"), digest)   # the hash is of the whole file

    def test_a_missing_file_builds_nothing(self):
        self.assertMayaError(lambda: self.build_with(os.path.join(config.WORK_DIR, "no_such_markers.json").replace("\\", "/")),
                             "Markers file does not exist")
        self.assertNothingBuilt()

    def test_a_bad_file_builds_nothing(self):
        with open(self.path, "w", encoding="utf-8") as handle:
            handle.write('{"formatVersion": 1, "markersHash": "x", "markers": [{"markup": ["Up", "a"]}]}')
        self.assertMayaError(lambda: self.build_with(self.path), "failed validation")
        self.assertNothingBuilt()


class Naming(MarkerTestCase):

    def test_renameRigNodes_puts_a_marker_into_the_convention(self):
        node = cmds.spaceLocator(name=":skel:oddName")[0]
        cmds.rigMarkerMarkup(node, usage="heel", side="Left", joint=self.joint("L", "upperArm"))
        self.assertEqual(cmds.renameRigNodes(node, base="heel", side="Left"), ["skel:heel_00_L_MRK"])
        parsed = json.loads(cmds.renameRigNodes("skel:heel_00_L_MRK", query=True)[0])
        self.assertEqual(parsed, {"base": "heel", "index": 0, "side": "Left", "kind": "Marker"})


class LoadedLocatorNaming(MarkerTestCase):
    """loadRigMarkers makes rigControlNodes, which must still resolve as markers and not as controls."""

    def test_a_loaded_marker_is_a_marker_for_the_naming_convention(self):
        node = self.locator("heel", "Left", self.joint("L", "upperArm"), (3, 4, 1))
        self.save(node)
        cmds.delete(node)
        loaded = cmds.loadRigMarkers(file=self.path, namespace=NAMESPACE)[0]
        parsed = json.loads(cmds.renameRigNodes(loaded, query=True)[0])
        self.assertEqual(parsed["kind"], "Marker")
        self.assertEqual(cmds.renameRigNodes(loaded, base="toe", side="Left"), ["skel:toe_00_L_MRK"])
        self.assertEqual(cmds.rigMarkerMarkup("skel:toe_00_L_MRK", query=True, usage=True), ["heel"])


class StaleStatus(MarkerTestCase):

    def build_with_markers(self, y):
        cmds.file(new=True, force=True)
        self.joints = self.make_skeleton(SKELETON)
        node = self.locator("heel", "Left", self.joint("L", "upperArm"), (3, y, 1))
        digest = self.save(node)[0]
        cmds.delete(node)
        cmds.createRigFromTemplate(*self.joints, template=fixtures.template_path(TEMPLATE).replace("\\", "/"),
                                   asset=config.ASSET_ID, namespace=config.RIG_NAMESPACE, markers=self.path)
        return digest

    def root(self):
        return cmds.ls(type="rigRootModule")[0]

    def pose_file(self, name):
        return os.path.join(config.WORK_DIR, name).replace("\\", "/")

    def test_a_pose_from_other_markers_loads_with_a_warning(self):
        first = self.build_with_markers(4)
        poseFile = self.pose_file("stale_pose.json")
        cmds.saveRigPose(self.root(), file=poseFile)
        self.assertEqual(json.load(open(poseFile, encoding="utf-8"))["meta"]["markersHash"], first)
        second = self.build_with_markers(4.5)
        self.assertNotEqual(first, second)
        stop = capture_warnings()
        cmds.loadRigPose(self.root(), file=poseFile)
        warnings = stop()
        self.assertTrue(any("markers " + first in text and "has " + second in text for text in warnings), warnings)

    def test_the_same_markers_load_silently(self):
        self.build_with_markers(4)
        poseFile = self.pose_file("same_pose.json")
        cmds.saveRigPose(self.root(), file=poseFile)
        stop = capture_warnings()
        cmds.loadRigPose(self.root(), file=poseFile)
        self.assertFalse([text for text in stop() if "markers" in text])

    def test_a_file_with_no_markers_hash_reads_as_unknown(self):
        self.build_with_markers(4)
        poseFile = self.pose_file("old_pose.json")
        cmds.saveRigPose(self.root(), file=poseFile)
        data = json.load(open(poseFile, encoding="utf-8"))
        del data["meta"]["markersHash"]
        json.dump(data, open(poseFile, "w", encoding="utf-8"))
        cmds.file(new=True, force=True)
        self.joints = self.make_skeleton(SKELETON)
        self.build(TEMPLATE, self.joints)
        stop = capture_warnings()
        cmds.loadRigPose(self.root(), file=poseFile)
        self.assertFalse([text for text in stop() if "markers" in text])

    def test_a_pose_from_a_rig_with_markers_loads_on_one_with_none_with_a_warning(self):
        first = self.build_with_markers(4)
        poseFile = self.pose_file("markers_pose.json")
        cmds.saveRigPose(self.root(), file=poseFile)
        cmds.file(new=True, force=True)
        self.joints = self.make_skeleton(SKELETON)
        self.build(TEMPLATE, self.joints)
        stop = capture_warnings()
        cmds.loadRigPose(self.root(), file=poseFile)
        self.assertTrue(any("was built with none" in text for text in stop()))

    def test_a_pose_made_with_no_markers_loads_silently_on_a_rig_with_markers(self):
        cmds.file(new=True, force=True)
        self.build(TEMPLATE, self.make_skeleton(SKELETON))
        poseFile = self.pose_file("plain_pose.json")
        cmds.saveRigPose(self.root(), file=poseFile)
        self.assertNotIn("markersHash", json.load(open(poseFile, encoding="utf-8"))["meta"])
        self.build_with_markers(4)
        stop = capture_warnings()
        cmds.loadRigPose(self.root(), file=poseFile)
        self.assertFalse([text for text in stop() if "markers" in text])
