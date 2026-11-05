"""rigJointMarkup: create, -query, -edit, -remove, undo, and every rejected input (D15)."""
import json

from maya import cmds

from rigtest.case import RigTestCase


def joints(*names):
    created = []
    for name in names:
        cmds.select(clear=True)
        created.append(cmds.joint(name=name))
    cmds.select(clear=True)
    return created


def mark(joint, usage="spine1", side="Center", export=True, skin=True):
    cmds.rigJointMarkup(joint, usage=usage, side=side, export=export, skin=skin)


def has_markup(joint):
    return cmds.attributeQuery("rigMarkup", node=joint, exists=True)


class Query(RigTestCase):

    def test_full_query_returns_markup_json(self):
        (j,) = joints("a")
        mark(j)
        result = cmds.rigJointMarkup(j, query=True)
        self.assertEqual(json.loads(result[0]),
                         {"markupVersion": 1, "usage": "spine1", "side": "Center", "export": True, "skin": True})

    def test_query_one_field(self):
        (j,) = joints("a")
        mark(j, usage="clavicle", side="Left")
        self.assertEqual(cmds.rigJointMarkup(j, query=True, usage=True), ["clavicle"])
        self.assertEqual(cmds.rigJointMarkup(j, query=True, side=True), ["Left"])

    def test_query_bool_returns_text(self):
        (j,) = joints("a")
        mark(j, export=True, skin=False)
        self.assertEqual(cmds.rigJointMarkup(j, query=True, export=True), ["true"])
        self.assertEqual(cmds.rigJointMarkup(j, query=True, skin=True), ["false"])

    def test_query_unmarked_returns_empty(self):
        (j,) = joints("a")
        self.assertEqual(cmds.rigJointMarkup(j, query=True, usage=True), [""])

    def test_query_several_joints_in_order(self):
        a, b = joints("a", "b")
        mark(a, usage="x")
        mark(b, usage="y")
        self.assertEqual(cmds.rigJointMarkup(a, b, query=True, usage=True), ["x", "y"])

    def test_query_two_fields_rejected(self):
        (j,) = joints("a")
        mark(j)
        self.assertMayaError(lambda: cmds.rigJointMarkup(j, query=True, usage=True, side=True), "at most one")

    def test_markup_stored_as_json_attribute(self):
        (j,) = joints("a")
        mark(j, side="Left")
        self.assertEqual(json.loads(cmds.getAttr(j + ".rigMarkup"))["side"], "Left")


class CreateEditRemove(RigTestCase):

    def test_create_on_several_joints(self):
        a, b = joints("a", "b")
        cmds.rigJointMarkup(a, b, usage="x", side="Left", export=True, skin=True)
        self.assertEqual(cmds.rigJointMarkup(a, b, query=True, side=True), ["Left", "Left"])

    def test_undo_create_removes_attribute_and_redo_restores(self):
        a, b = joints("a", "b")
        cmds.rigJointMarkup(a, b, usage="x", side="Left", export=True, skin=True)
        cmds.undo()
        self.assertFalse(has_markup(a) or has_markup(b))
        cmds.redo()
        self.assertEqual(cmds.rigJointMarkup(a, query=True, side=True), ["Left"])

    def test_edit_merges_into_existing_markup(self):
        (j,) = joints("a")
        mark(j, usage="spine1", side="Center", skin=True)
        cmds.rigJointMarkup(j, edit=True, usage="chest", skin=False)
        self.assertEqual(cmds.rigJointMarkup(j, query=True, usage=True), ["chest"])
        self.assertEqual(cmds.rigJointMarkup(j, query=True, skin=True), ["false"])
        self.assertEqual(cmds.rigJointMarkup(j, query=True, side=True), ["Center"])

    def test_undo_and_redo_edit(self):
        (j,) = joints("a")
        mark(j, usage="spine1")
        cmds.rigJointMarkup(j, edit=True, usage="chest")
        cmds.undo()
        self.assertEqual(cmds.rigJointMarkup(j, query=True, usage=True), ["spine1"])
        cmds.redo()
        self.assertEqual(cmds.rigJointMarkup(j, query=True, usage=True), ["chest"])

    def test_remove_and_undo_remove(self):
        (j,) = joints("a")
        mark(j)
        cmds.rigJointMarkup(j, remove=True)
        self.assertFalse(has_markup(j))
        cmds.undo()
        self.assertTrue(has_markup(j))


class Rejected(RigTestCase):

    def test_create_needs_every_field(self):
        (j,) = joints("a")
        self.assertMayaError(lambda: cmds.rigJointMarkup(j, usage="a", side="Left"))
        self.assertFalse(has_markup(j))

    def test_side_is_case_sensitive(self):
        (j,) = joints("a")
        self.assertMayaError(lambda: mark(j, side="left"), "left")
        self.assertFalse(has_markup(j), "a rejected create must not leave an attribute behind")

    def test_create_twice_rejected(self):
        (j,) = joints("a")
        mark(j)
        self.assertMayaError(lambda: mark(j))

    def test_edit_unmarked_rejected(self):
        (j,) = joints("a")
        self.assertMayaError(lambda: cmds.rigJointMarkup(j, edit=True, usage="z"))

    def test_no_joints_rejected(self):
        self.assertMayaError(lambda: cmds.rigJointMarkup(usage="a", side="Left", export=True, skin=True))

    def test_selection_is_ignored(self):
        (j,) = joints("a")
        mark(j)
        cmds.select(j)
        self.assertMayaError(lambda: cmds.rigJointMarkup(query=True))

    def test_non_joint_rejected(self):
        node = cmds.createNode("transform", name="notAJoint")
        self.assertMayaError(lambda: cmds.rigJointMarkup(node, query=True), "notAJoint")

    def test_missing_joint_rejected(self):
        self.assertMayaError(lambda: cmds.rigJointMarkup("nope", query=True), "nope")

    def test_invalid_json_reported(self):
        (j,) = joints("a")
        mark(j)
        cmds.setAttr(j + ".rigMarkup", "{bad json", type="string")
        self.assertMayaError(lambda: cmds.rigJointMarkup(j, query=True), j)

    def test_markup_missing_a_key_reported(self):
        (j,) = joints("a")
        cmds.addAttr(j, longName="rigMarkup", dataType="string")
        cmds.setAttr(j + ".rigMarkup", '{"markupVersion":1,"usage":"x"}', type="string")
        self.assertMayaError(lambda: cmds.rigJointMarkup(j, query=True, usage=True), "side")
