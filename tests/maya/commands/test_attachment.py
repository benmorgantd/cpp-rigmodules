"""Attachments (D55): a rig built with parentRig follows sockets of another rig through "parent:module.socket" ids.

The body is fkchain_three in 'rig' (fkchain_1: a, b, c at y 0, 2, 4). The face (attach_face) is built into 'face':
face_chain follows fkchain_1.end, and neck_copy's control is driven by fkchain_1.index_1 through a constraint whose only
driver is a parent socket, so the control is hidden, locked and at rest.
"""
import json

from maya import cmds

from rigtest import config, fixtures
from rigtest.case import RigTestCase, rig_name

FACE = "face"


class AttachmentCase(RigTestCase):

    def setUp(self):
        super().setUp()
        self.build_standard("fkchain_three", "fk_three")
        self.body_root = cmds.ls(type="rigRootModule")[0]

    def attach(self, template="attach_face", skeleton="attach_face", parent=None, namespace=FACE):
        joints = self.make_skeleton(skeleton, namespace="face_skel")
        kwargs = {"template": fixtures.template_path(template).replace("\\", "/"), "asset": config.ASSET_ID,
                  "namespace": namespace}
        if parent is not False:
            kwargs["parentRig"] = parent or self.body_root
        return cmds.createRigFromTemplate(*joints, **kwargs)

    def face_root(self):
        return [r for r in cmds.ls(type="rigRootModule") if r != self.body_root][0]

    def info(self, node):
        return json.loads(cmds.getRigInfo(node))


class AttachedRigFollows(AttachmentCase):

    def test_chain_follows_the_parent_socket(self):
        self.attach()
        cmds.setAttr(rig_name("a_ctrl_c") + ".rotateZ", 90)
        # c_ctrl_c's world moves rigidly with the chain, so the joint 1 above c at rest goes from (0,5,0) to (-5,0,0).
        self.assertWorldTranslation(rig_name("f_a_jnt", FACE), (-5, 0, 0))
        cmds.setAttr(rig_name("a_ctrl_c") + ".rotateZ", 0)
        self.assertWorldTranslation(rig_name("f_a_jnt", FACE), (0, 5, 0))

    def test_constraint_driven_control_follows_a_mid_chain_socket(self):
        self.attach()
        cmds.setAttr(rig_name("a_ctrl_c") + ".rotateZ", 90)
        self.assertWorldTranslation(rig_name("n_jnt", FACE), (-2, 0, 0))

    def test_the_attachment_stays_under_its_own_rig(self):
        # Only the matrix connections cross. The face's controls are not reparented under the body's.
        self.attach()
        parent = (cmds.listRelatives(rig_name("f_a_ctrl_c", FACE), parent=True) or [""])[0]
        self.assertTrue(parent.lstrip(":").startswith(FACE + ":"), parent)


class AttachedControlsAreLocked(AttachmentCase):

    def test_parent_driven_control_is_hidden_locked_and_at_rest(self):
        self.attach()
        control = rig_name("n_ctrl_c", FACE)
        self.assertEqual(cmds.getAttr(control + ".visibility"), 0)
        for channel in ("visibility", "tx", "ty", "tz", "rx", "ry", "rz", "sx", "sy", "sz"):
            self.assertTrue(cmds.getAttr("%s.%s" % (control, channel), lock=True), channel)
            self.assertFalse(cmds.getAttr("%s.%s" % (control, channel), keyable=True), channel)
        self.assertEqual(cmds.getAttr(control + ".translate")[0], (0, 0, 0))
        self.assertEqual(cmds.getAttr(control + ".rotate")[0], (0, 0, 0))
        self.assertEqual(cmds.getAttr(control + ".scale")[0], (1, 1, 1))

    def test_controls_the_parent_does_not_drive_stay_free(self):
        self.attach()
        control = rig_name("f_a_ctrl_c", FACE)
        self.assertEqual(cmds.getAttr(control + ".visibility"), 1)
        self.assertFalse(cmds.getAttr(control + ".tx", lock=True))
        self.assertTrue(cmds.getAttr(control + ".tx", keyable=True))


    def test_a_control_with_another_driver_besides_the_parent_stays_free(self):
        # Only a control the body drives alone is locked. Here the animator can still pick the layout.
        template = {"rigVersion": 1, "rigAuthor": "tests", "rigTemplateName": "Mixed", "rigModules": [
            {"moduleType": "FkChain", "name": "neck_copy", "shapeType": "Box", "side": "Center",
             "moduleArgs": {"joints": [["Center", "n"]]}}],
            "constraints": [{"name": "neck_follow", "target": "neck_copy.start", "drivers": [
                {"socket": "parent:fkchain_1.index_1", "weight": 1.0}, {"socket": "layout.end", "label": "World"}]}]}
        joints = self.make_skeleton("attach_face", namespace="face_skel")
        path = fixtures.write_template(template, "mixed").replace("\\", "/")
        cmds.createRigFromTemplate(*joints[:1], template=path, asset=config.ASSET_ID, namespace=FACE, parentRig=self.body_root)
        control = rig_name("n_ctrl_c", FACE)
        self.assertEqual(cmds.getAttr(control + ".visibility"), 1)
        self.assertFalse(cmds.getAttr(control + ".tx", lock=True))
        self.assertEqual(cmds.getAttr(control + ".space"), 0)
        cmds.setAttr(rig_name("a_ctrl_c") + ".rotateZ", 90)
        self.assertWorldTranslation(rig_name("n_jnt", FACE), (-2, 0, 0))
        cmds.setAttr(control + ".space", 1)
        self.assertWorldTranslation(rig_name("n_jnt", FACE), (0, 2, 0))


class AttachedRigIsRecorded(AttachmentCase):

    def test_info_names_the_parent(self):
        self.attach()
        self.assertEqual(self.info(self.face_root())["parentRigRoot"], self.body_root)
        self.assertEqual(self.info(rig_name("f_a_ctrl_c", FACE))["parentRigRoot"], self.body_root)
        self.assertIsNone(self.info(self.body_root)["parentRigRoot"])

    def test_the_parent_lists_its_attachments(self):
        self.attach()
        self.assertEqual(cmds.listConnections(self.body_root + ".attachments", source=False, destination=True),
                         [self.face_root()])


class AttachFailsInTheReadPhase(AttachmentCase):

    def assertFaceNotBuilt(self):
        self.assertEqual(len(cmds.ls(type="rigRootModule")), 1)
        self.assertFalse(cmds.namespace(exists=":" + FACE) and cmds.ls(":%s:*" % FACE))

    def test_a_template_with_parent_ids_needs_a_parent_rig(self):
        message = self.assertMayaError(lambda: self.attach(parent=False), "needs a parentRig", "Nothing was built")
        self.assertIn("fkchain_1.end", message)
        self.assertIn("fkchain_1.index_1", message)
        self.assertFaceNotBuilt()

    def test_every_missing_socket_is_listed(self):
        # A body from another template: its one module is called spine, so both parent sockets are missing.
        cmds.file(new=True, force=True)
        template = {"rigVersion": 1, "rigAuthor": "tests", "rigTemplateName": "Other", "rigModules": [
            {"moduleType": "FkChain", "name": "spine", "shapeType": "Box", "side": "Center",
             "moduleArgs": {"joints": [["Center", "a"], ["Center", "b"], ["Center", "c"]]}}]}
        self.build(template, self.make_skeleton("fk_three"))
        message = self.assertMayaError(lambda: self.attach(parent=cmds.ls(type="rigRootModule")[0]),
                                       "with 2 error(s)", "parent:fkchain_1.end", "parent:fkchain_1.index_1")
        self.assertIn("no module", message)
        self.assertFaceNotBuilt()

    def test_a_missing_socket_on_an_existing_module_is_listed(self):
        template = {"rigVersion": 1, "rigAuthor": "tests", "rigTemplateName": "Face", "rigModules": [
            {"moduleType": "FkChain", "name": "face_chain", "shapeType": "Box", "side": "Center",
             "moduleArgs": {"joints": [["Center", "f_a"]]},
             "parentStructure": {"start": "parent:fkchain_1.nope"}}]}
        joints = self.make_skeleton("attach_face", namespace="face_skel")
        path = fixtures.write_template(template, "face").replace("\\", "/")
        self.assertMayaError(lambda: cmds.createRigFromTemplate(
            *joints, template=path, asset=config.ASSET_ID, namespace=FACE, parentRig=self.body_root),
            "no socket 'nope' on module 'fkchain_1'")

    def test_a_rig_that_is_not_an_attachment_refuses_a_parent(self):
        joints = self.make_skeleton("fk_three", namespace="other_skel")
        path = fixtures.template_path("fkchain_three").replace("\\", "/")
        self.assertMayaError(lambda: cmds.createRigFromTemplate(
            *joints, template=path, asset=config.ASSET_ID, namespace="other", parentRig=self.body_root),
            "does not attach to a rig")

    def test_the_parent_must_be_a_rig_root(self):
        self.assertMayaError(lambda: self.attach(parent=rig_name("a_ctrl_c")), "is not a rig root")
        self.assertFaceNotBuilt()

    def test_an_unknown_parent_fails(self):
        self.assertMayaError(lambda: self.attach(parent="nothing_here"), "parentRig not found")

    def test_a_malformed_parent_id_fails(self):
        template = {"rigVersion": 1, "rigAuthor": "tests", "rigTemplateName": "Face", "rigModules": [
            {"moduleType": "FkChain", "name": "face_chain", "shapeType": "Box", "side": "Center",
             "moduleArgs": {"joints": [["Center", "f_a"]]},
             "parentStructure": {"start": "parent:fkchain_1"}}]}
        joints = self.make_skeleton("attach_face", namespace="face_skel")
        path = fixtures.write_template(template, "face").replace("\\", "/")
        self.assertMayaError(lambda: cmds.createRigFromTemplate(
            *joints, template=path, asset=config.ASSET_ID, namespace=FACE, parentRig=self.body_root),
            "must be 'parent:module.socket'")


class AttachmentUndoRedo(AttachmentCase):

    def test_undo_removes_the_attachment_and_redo_restores_it(self):
        self.attach()
        cmds.undo()
        self.assertEqual(cmds.ls(type="rigRootModule"), [self.body_root])
        self.assertFalse(cmds.listConnections(self.body_root + ".attachments"))
        cmds.redo()
        self.assertEqual(self.info(self.face_root())["parentRigRoot"], self.body_root)
        cmds.setAttr(rig_name("a_ctrl_c") + ".rotateZ", 90)
        self.assertWorldTranslation(rig_name("f_a_jnt", FACE), (-5, 0, 0))
        self.assertEqual(cmds.getAttr(rig_name("n_ctrl_c", FACE) + ".visibility"), 0)


class AttachmentMirror(RigTestCase):

    def test_a_mirrored_module_follows_the_mirrored_parent_socket(self):
        self.build_standard("mirror_arms", "mirror_arms")
        body_root = cmds.ls(type="rigRootModule")[0]
        joints = self.make_skeleton("attach_ears", namespace="face_skel")
        cmds.createRigFromTemplate(
            *joints, template=fixtures.template_path("attach_ears").replace("\\", "/"), asset=config.ASSET_ID,
            namespace=FACE, parentRig=body_root)
        for side, arm in (("l", "arm_l"), ("r", "arm_r")):
            plug = rig_name("ear_%s" % side, FACE) + ".childPlugs[0].childPlugParentMatrix"
            sources = cmds.listConnections(plug, source=True, destination=False) or []
            self.assertEqual([s.lstrip(":") for s in sources], ["rig:" + arm], side)

    def test_a_mirror_needs_its_parent_socket_too(self):
        # A body with only the left arm: the mirrored ear's arm_r.end is missing, and the report says so.
        self.build_standard("mirror_arms", "mirror_arms")
        body_root = cmds.ls(type="rigRootModule")[0]
        cmds.delete(rig_name("arm_r"))
        joints = self.make_skeleton("attach_ears", namespace="face_skel")
        self.assertMayaError(lambda: cmds.createRigFromTemplate(
            *joints, template=fixtures.template_path("attach_ears").replace("\\", "/"), asset=config.ASSET_ID,
            namespace=FACE, parentRig=body_root), "parent:arm_r.end")
