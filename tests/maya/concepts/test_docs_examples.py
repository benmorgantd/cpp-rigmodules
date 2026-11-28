"""Examples in the user docs work as written.

The template is read from README.md itself, so editing the README example into something that no longer
builds fails here. The commands mirror the README's Python. The skeleton fixture matches the joints the
README describes.
"""
import copy
import json
import math
import os
import re

from maya import cmds

from rigtest import config, fixtures
from rigtest.case import RigTestCase, rig_name

README = os.path.join(config.REPO_DIR, "README.md")
TEMPLATES_DOC = os.path.join(config.REPO_DIR, "docs", "templates.md")

# The README builds with asset="shooter\\characters\\hero" and no namespace, so its rig "body" lands in the
# default namespace "{asset name}_{rigName}".
DOC_ASSET = "shooter\\characters\\hero"
DOC_NAMESPACE = "hero_body"

SPINE3_CTRL = rig_name("spine3_00_M_CTRL", DOC_NAMESPACE)
ARM_CTRL = rig_name("arm_00_L_CTRL", DOC_NAMESPACE)
ARM_JNT = rig_name("arm_00_L_JNT", DOC_NAMESPACE)

# arm_00_L_JNT rests at (2, 4, 0), 2 units along +X from spine3_00_M_JNT at (0, 4, 0).
ARM_REST = (2, 4, 0)
# Rotating spine3 90 degrees about Z turns that +X offset into +Y.
ARM_AFTER_SPINE3_ROTATE_Z_90 = (0, 6, 0)


def readme_quick_start_template():
    """The first ```json block under '## Quick start' in README.md."""
    with open(README, encoding="utf-8") as handle:
        text = handle.read()
    section = text.split("## Quick start", 1)[1]
    match = re.search(r"```json\s*\n(.*?)```", section, re.S)
    if not match:
        raise AssertionError("README.md '## Quick start' has no ```json block")
    return json.loads(match.group(1))


ATTACHMENTS_DOC = os.path.join(config.REPO_DIR, "docs", "attachments.md")


class AttachmentsDoc(RigTestCase):
    """The two template snippets in docs/attachments.md are the entries of the attach_face fixture, which
    commands/test_attachment.py builds and checks."""

    def doc_blocks(self):
        with open(ATTACHMENTS_DOC, encoding="utf-8") as handle:
            return [json.loads(block) for block in re.findall(r"```json\s*\n(.*?)```", handle.read(), re.S)]

    def test_snippets_match_the_tested_fixture(self):
        from rigtest import fixtures
        template = fixtures.read_json(fixtures.template_path("attach_face"))
        module, constraint = self.doc_blocks()
        self.assertEqual(module, template["rigModules"][0])
        self.assertEqual(constraint, template["constraints"][0])


class ReadmeQuickStart(RigTestCase):

    def setUp(self):
        super().setUp()
        self.build(readme_quick_start_template(), self.make_skeleton("readme_quick_start"), asset=DOC_ASSET, namespace=None)

    def rotate_spine3_z(self, degrees):
        cmds.setAttr(SPINE3_CTRL + ".rotateZ", degrees)

    def test_builds_the_names_the_readme_gives(self):
        for module in ("spine_00_M_MOD", "arm_00_L_MOD", "layout_00_M_MOD"):
            self.assertExists(rig_name(module, DOC_NAMESPACE))
        for control in ("spine1_00_M_CTRL", "spine2_00_M_CTRL", "spine3_00_M_CTRL", "arm_00_L_CTRL"):
            self.assertExists(rig_name(control, DOC_NAMESPACE))

    def test_arm_hangs_off_the_spine_end(self):
        self.rotate_spine3_z(90)
        self.assertWorldTranslation(ARM_JNT, ARM_AFTER_SPINE3_ROTATE_Z_90)

    def test_space_switch(self):
        con = cmds.createRigConstraint(ARM_CTRL, driver=["layout_00_M_MOD.end", "spine_00_M_MOD.end"], label=["World", "Chest"])
        self.assertEqual(con.lstrip(":"), "hero_body:arm_00_L_CON")
        self.assertEqual(cmds.attributeQuery("space", node=ARM_CTRL, listEnum=True), ["World:Chest"])

        # World is the first driver, so it starts active and the spine no longer carries the arm.
        self.rotate_spine3_z(90)
        self.assertWorldTranslation(ARM_JNT, ARM_REST)
        self.rotate_spine3_z(0)

        cmds.setRigConstraintSpace(con, label="Chest")
        self.assertEqual(cmds.getAttr(ARM_CTRL + ".space"), 1)
        self.assertWorldTranslation(ARM_JNT, ARM_REST)
        self.rotate_spine3_z(90)
        self.assertWorldTranslation(ARM_JNT, ARM_AFTER_SPINE3_ROTATE_Z_90)

    def test_get_rig_control_space_example(self):
        """The example under '## getRigControlSpace' in docs/commands.md."""
        cmds.createRigConstraint(ARM_CTRL, driver=["layout_00_M_MOD.end", "spine_00_M_MOD.end"], label=["World", "Chest"], initial=1)
        self.assertEqual(json.loads(cmds.getRigControlSpace(ARM_CTRL)),
                         {"constraint": "hero_body:arm_00_L_CON", "module": "", "labels": ["World", "Chest"], "current": 1})
        self.assertEqual(cmds.getRigControlSpace(SPINE3_CTRL), "")

    def test_set_rig_control_space_example(self):
        """The first example under '## setRigControlSpace' in docs/commands.md."""
        cmds.createRigConstraint(ARM_CTRL, driver=["layout_00_M_MOD.end", "spine_00_M_MOD.end"], label=["World", "Chest"])
        cmds.setRigControlSpace(ARM_CTRL, label="Chest")
        self.assertEqual(json.loads(cmds.getRigControlSpace(ARM_CTRL))["current"], 1)


def templates_doc_orient_constraint():
    """The first ```json block under '### Constraint types' in docs/templates.md."""
    with open(TEMPLATES_DOC, encoding="utf-8") as handle:
        text = handle.read()
    section = text.split("### Constraint types", 1)[1]
    match = re.search(r"```json\s*\n(.*?)```", section, re.S)
    if not match:
        raise AssertionError("docs/templates.md '### Constraint types' has no ```json block")
    return json.loads(match.group(1))


class OrientConstraintExamples(RigTestCase):
    """docs/templates.md '### Constraint types' and the docs/commands.md Orient example, on the quick start rig.
    Either way the arm's pivot follows spine3 and its rotation stays at rest."""

    def assertPivotFollowsChestRotationDoesNot(self):
        rest_rotation = cmds.xform(ARM_JNT, q=True, ws=True, rotation=True)
        cmds.setAttr(SPINE3_CTRL + ".rotateZ", 90)
        self.assertWorldTranslation(ARM_JNT, ARM_AFTER_SPINE3_ROTATE_Z_90)
        self.assertWorldRotation(ARM_JNT, rest_rotation)

    def test_template_example(self):
        template = readme_quick_start_template()
        template["constraints"] = [templates_doc_orient_constraint()]
        self.build(template, self.make_skeleton("readme_quick_start"), asset=DOC_ASSET, namespace=None)
        self.assertEqual(cmds.getAttr("hero_body:armSpace_00_L_CON.constraintType", asString=True), "Orient")
        self.assertPivotFollowsChestRotationDoesNot()

    def test_command_example(self):
        self.build(readme_quick_start_template(), self.make_skeleton("readme_quick_start"), asset=DOC_ASSET, namespace=None)
        cmds.createRigConstraint("hero_body:arm_00_L_CTRL", driver=["layout_00_M_MOD.end", "spine_00_M_MOD.end"], label=["World", "Chest"], type="Orient")
        self.assertPivotFollowsChestRotationDoesNot()

    def test_constraint_weight_example(self):
        """The docs/commands.md constraintWeight example: half the Orient's effect, so the arm turns halfway with the chest."""
        import maya.api.OpenMaya as om
        self.build(readme_quick_start_template(), self.make_skeleton("readme_quick_start"), asset=DOC_ASSET, namespace=None)
        cmds.createRigConstraint("hero_body:arm_00_L_CTRL", driver=["layout_00_M_MOD.end", "spine_00_M_MOD.end"], label=["World", "Chest"], type="Orient", constraintWeight=0.5)
        rest = om.MMatrix(cmds.xform(ARM_JNT, q=True, ws=True, matrix=True))
        cmds.setAttr(SPINE3_CTRL + ".rotateZ", 90)
        self.assertWorldTranslation(ARM_JNT, ARM_AFTER_SPINE3_ROTATE_Z_90)
        turned = om.MTransformationMatrix(om.MMatrix(cmds.xform(ARM_JNT, q=True, ws=True, matrix=True)) * rest.inverse()).rotation(asQuaternion=True)
        self.assertAlmostEqual(math.degrees(2.0 * math.acos(min(1.0, abs(turned.w)))), 45.0, delta=0.1)


CONTROLS_DOC = os.path.join(config.REPO_DIR, "docs", "controls.md")


def controls_doc_shape_file():
    """The first ```json block under '## Shape file' in docs/controls.md."""
    with open(CONTROLS_DOC, encoding="utf-8") as handle:
        text = handle.read()
    section = text.split("## Shape file", 1)[1]
    match = re.search(r"```json\s*\n(.*?)```", section, re.S)
    if not match:
        raise AssertionError("docs/controls.md '## Shape file' has no ```json block")
    return match.group(1)


class ControlDocExamples(RigTestCase):
    """docs/commands.md getRigControls examples and the docs/controls.md shape file, on the quick start rig."""

    def setUp(self):
        super().setUp()
        self.build(readme_quick_start_template(), self.make_skeleton("readme_quick_start"), asset=DOC_ASSET, namespace=None)
        self.root = "hero_body:rigRootModule1"   # the name the docs examples use

    def test_get_rig_controls_examples(self):
        self.assertEqual(len(cmds.getRigControls(self.root)), 7)
        self.assertEqual([c.split(":")[-1] for c in cmds.getRigControls(self.root, moduleType="FkChain", side="Left")],
                         ["arm_00_L_CTRL"])
        self.assertEqual([c.split(":")[-1] for c in cmds.getRigControls(self.root, usage=["spine1", "spine2"])],
                         ["spine1_00_M_CTRL", "spine2_00_M_CTRL"])

    def test_shape_file_example_loads(self):
        path = os.path.join(config.WORK_DIR, "doc_shapes.json").replace("\\", "/")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(controls_doc_shape_file())
        self.assertEqual([c.split(":")[-1] for c in cmds.loadRigControlShapes(self.root, file=path)], ["arm_00_L_CTRL"])
        self.assertEqual(cmds.getAttr(ARM_CTRL + ".shapeType", asString=True), "Circle")
        self.assertAlmostEqual(cmds.getAttr(ARM_CTRL + ".wireframeAlpha"), 0.5, places=5)


SKINNING_DOC = os.path.join(config.REPO_DIR, "docs", "skinning.md")


def skinning_doc_weight_file():
    """The first ```json block under '## File format' in docs/skinning.md."""
    with open(SKINNING_DOC, encoding="utf-8") as handle:
        text = handle.read()
    section = text.split("## File format", 1)[1]
    match = re.search(r"```json\s*\n(.*?)```", section, re.S)
    if not match:
        raise AssertionError("docs/skinning.md '## File format' has no ```json block")
    return match.group(1)


class SkinningDocExample(RigTestCase):
    """The weight file in docs/skinning.md loads onto the cube it describes, on the fk_three joints a and b."""

    def test_weight_file_example_loads(self):
        joints = self.make_skeleton("fk_three")
        box = cmds.polyCube(name=":skel:box_geo", height=2, constructionHistory=False)[0]
        cmds.move(0, 1, 0, box)
        cmds.makeIdentity(box, apply=True, translate=True)
        text = skinning_doc_weight_file()
        cmds.addAttr(box, longName="meshId", dataType="string")
        cmds.setAttr(box + ".meshId", json.loads(text)["meshId"], type="string")
        path = os.path.join(config.WORK_DIR, "doc_weights.json").replace("\\", "/")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
        cluster = cmds.loadRigSkinWeights(box, file=path)
        top = cmds.skinPercent(cluster, box + ".vtx[3]", transform=joints[1], query=True)
        bottom = cmds.skinPercent(cluster, box + ".vtx[0]", transform=joints[0], query=True)
        self.assertEqual((round(bottom, 6), round(top, 6)), (1.0, 0.75))


class LooksDocExample(RigTestCase):
    """The setRigLook example in docs/looks.md and docs/commands.md, with the names it gives: a rig in hero_body, a mesh
    and its weight file made from the rig, and the call as documented."""

    def test_set_rig_look_example(self):
        self.build_standard("fkchain_three", "fk_three_parented", namespace=DOC_NAMESPACE)
        rig_root = cmds.ls(type="rigRootModule")[0]
        self.assertEqual(rig_root, "hero_body:rigRootModule1")
        body = cmds.polyCube(name=":hero_body:body_geo", height=2, constructionHistory=False)[0]
        cmds.addAttr(body, longName="meshId", dataType="string")
        cmds.setAttr(body + ".meshId", "doc-body", type="string")
        cmds.skinCluster(rig_name("a_00_M_JNT", DOC_NAMESPACE), rig_name("b_00_M_JNT", DOC_NAMESPACE), body, toSelectedBones=True)
        path = os.path.join(config.WORK_DIR, "body_geo.json").replace("\\", "/")
        cmds.saveRigSkinWeights(body, file=path)
        cmds.delete(body)
        body = cmds.polyCube(name=":hero_body:body_geo", height=2, constructionHistory=False)[0]
        cmds.addAttr(body, longName="meshId", dataType="string")
        cmds.setAttr(body + ".meshId", "doc-body", type="string")

        result = cmds.setRigLook("hero_body:rigRootModule1",
                                 mesh=["hero_body:body_geo"],
                                 file=[path],
                                 asset="shooter\\characters\\hero", look="red")
        report = json.loads(result)
        self.assertEqual(report["lookRoot"], "hero_body:lookRoot")
        self.assertEqual([(m["meshName"], m["result"], m["reasons"]) for m in report["meshes"]], [("body_geo", "loaded", [])])
        info = json.loads(cmds.getRigInfo(rig_root))
        self.assertEqual(info["look"], {"assetId": "shooter\\characters\\hero", "name": "red", "publishVersion": ""})


def templates_doc_mirroring_template():
    """The first ```json block under '## Mirroring' in docs/templates.md."""
    with open(TEMPLATES_DOC, encoding="utf-8") as handle:
        text = handle.read()
    section = text.split("## Mirroring", 1)[1]
    match = re.search(r"```json\s*\n(.*?)```", section, re.S)
    if not match:
        raise AssertionError("docs/templates.md '## Mirroring' has no ```json block")
    return json.loads(match.group(1))


class TemplatesDocMirroring(RigTestCase):
    """The mirroring example builds on the mirror_arms skeleton, which has the joints it names."""

    def test_example_builds_both_sides(self):
        self.build_standard(templates_doc_mirroring_template(), "mirror_arms")
        for name in ("clavicle_00_R_MOD", "arm_00_R_MOD", "armSpace_00_R_CON"):
            self.assertExists(rig_name(name))
        self.assertOpmDrivenBy(rig_name("upperArm_00_R_JNT"), "arm_00_R_MOD")
        self.assertEqual(cmds.attributeQuery("space", node=rig_name("upperArm_00_R_CTRL"), listEnum=True), ["Clavicle:World"])


PYTHON_DOC = os.path.join(config.REPO_DIR, "docs", "python.md")


def python_doc_joint_clip():
    """The first ```json block under '### File format' in docs/python.md."""
    with open(PYTHON_DOC, encoding="utf-8") as handle:
        text = handle.read()
    section = text.split("### File format", 1)[1]
    match = re.search(r"```json\s*\n(.*?)```", section, re.S)
    if not match:
        raise AssertionError("docs/python.md '### File format' has no ```json block")
    return match.group(1)


class JointClipDocExample(RigTestCase):
    """The JointClip file in docs/python.md passes the plugin's reader and the solve runs on it."""

    def test_joint_clip_example_is_accepted(self):
        self.build_standard("mirror_arms", "mirror_arms")
        path = os.path.join(config.WORK_DIR, "doc_joint_clip.json").replace("\\", "/")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(python_doc_joint_clip())
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        changed = cmds.poseRigFromJointPose(root, jointPose=path) or []
        self.assertIsInstance(changed, list)


class FbxDocExample(RigTestCase):
    """The FBX and bone map sections of docs/python.md name what exists, and their examples run."""

    def section(self, heading, next_heading):
        with open(PYTHON_DOC, encoding="utf-8") as handle:
            return handle.read().split(heading, 1)[1].split(next_heading, 1)[0]

    def test_the_bone_map_example_is_accepted_and_every_function_is_documented(self):
        import rigmodules.bonemap
        import rigmodules.jointclip

        section = self.section("### Bone maps", "### File format")
        path = os.path.join(config.WORK_DIR, "doc_bone_map.bonemap.json")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(re.search(r"```json\s*\n(.*?)```", section, re.S).group(1))
        self.assertEqual(rigmodules.bonemap.read_bone_map(path)["skeletonId"], "shooter\\skeletons\\human")
        everything = self.section("## rigmodules.jointclip", "### File format") + section
        for function in ("write_fbx", "read_fbx", "write_skeleton_fbx", "write_bone_map", "read_bone_map", "apply_bone_map"):
            self.assertIn(function + "(", everything)
            self.assertTrue(hasattr(rigmodules.jointclip, function) or hasattr(rigmodules.bonemap, function), function)

    def test_the_fbx_example_runs(self):
        import rigmodules.jointclip

        section = self.section("### FBX files", "### Bone maps")
        for call in ("capture_joint_clip", "write_fbx", "read_fbx", "animateRigFromJointClip"):
            self.assertIn(call, section)
        self.build_standard("mirror_arms", "mirror_arms")
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        control = rig_name("upperArm_00_L_CTRL")
        cmds.setKeyframe(control + ".rotateZ", time=1, value=0)
        cmds.setKeyframe(control + ".rotateZ", time=3, value=60)
        clip = os.path.join(config.WORK_DIR, "doc_fbx_clip.json")
        fbx = os.path.join(config.WORK_DIR, "doc_fbx_clip.fbx")
        rigmodules.jointclip.capture_joint_clip(root, 1, 3, clip)
        rigmodules.jointclip.write_fbx(clip, fbx)
        cmds.cutKey(control, attribute="rotateZ", clear=True)
        rigmodules.jointclip.read_fbx(fbx, clip)
        cmds.animateRigFromJointClip(root, jointClip=clip.replace("\\", "/"))
        self.assertAlmostEqual(cmds.getAttr(control + ".rotateZ", time=3), 60, places=2)


class ControlAnimDocExample(RigTestCase):
    """The ControlAnim file in docs/library.md loads, and the keys format example in docs/commands.md is accepted."""

    def first_json(self, doc, heading, next_heading=None):
        with open(os.path.join(config.REPO_DIR, "docs", doc), encoding="utf-8") as handle:
            section = handle.read().split(heading, 1)[1]
        if next_heading:
            section = section.split(next_heading, 1)[0]
        return re.search(r"```json\s*\n(.*?)```", section, re.S).group(1)

    def test_the_library_example_loads_and_a_save_has_the_same_shape(self):
        cmds.currentUnit(time="film")
        self.build_standard("mirror_arms", "mirror_arms")
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        path = os.path.join(config.WORK_DIR, "doc_control_anim.json").replace("\\", "/")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(self.first_json("library.md", "## ControlAnim files"))
        cmds.loadRigAnimation(root, file=path)
        plug = rig_name("upperArm_00_L_CTRL") + ".rotateZ"
        self.assertEqual(cmds.keyframe(plug, query=True, timeChange=True), [1, 10, 20])
        self.assertAlmostEqual(cmds.keyframe(plug, query=True, valueChange=True)[1], 90.0, places=3)
        saved = os.path.join(config.WORK_DIR, "doc_control_anim_saved.json").replace("\\", "/")
        cmds.saveRigAnimation(root, file=saved, startFrame=1, endFrame=20)
        with open(saved, encoding="utf-8") as handle:
            written = json.load(handle)
        documented = json.loads(self.first_json("library.md", "## ControlAnim files"))
        self.assertEqual(sorted(written), sorted(documented), "the documented file has the keys a save writes")

    def test_the_keys_format_example_is_accepted(self):
        self.build_standard("mirror_arms", "mirror_arms")
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        changed = cmds.setRigKeys(root, keys=self.first_json("commands.md", "### Keys format"))
        self.assertEqual(changed, ["rig:upperArm_00_L_CTRL"])


COMMANDS_DOC = os.path.join(config.REPO_DIR, "docs", "commands.md")


def commands_doc_json(heading):
    """The first ```json block after a heading of docs/commands.md."""
    with open(COMMANDS_DOC, encoding="utf-8") as handle:
        text = handle.read()
    section = text.split(heading, 1)[1]
    match = re.search(r"```json\s*\n(.*?)```", section, re.S)
    if not match:
        raise AssertionError("docs/commands.md %r has no ```json block" % heading)
    return match.group(1)


class KeysDocExample(RigTestCase):
    """The setRigKeys string in docs/commands.md keys the mirror_arms rig, and getRigKeys reads it back."""

    def test_set_rig_keys_example(self):
        self.build_standard("mirror_arms", "mirror_arms")
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        keys = commands_doc_json("## setRigKeys")
        changed = cmds.setRigKeys(root, keys=keys)
        self.assertEqual([c.split(":")[-1] for c in changed], ["upperArm_00_L_CTRL"])
        arm = rig_name("upperArm_00_L_CTRL")
        self.assertEqual(cmds.keyframe(arm + ".rotateZ", query=True, valueChange=True), [0, 90, 45])
        self.assertEqual(cmds.keyframe(arm + ".translateX", query=True, valueChange=True), [0, 2.5])
        found = json.loads(cmds.getRigKeys(root))
        self.assertEqual(found["Left"]["upperArm"]["rotateZ"], [[1, 0], [10, 90], [20, 45]])


class AnimationSidecarDocExample(RigTestCase):
    """The sidecar in docs/commands.md is a valid ControlAnim file, and animateRigFromJointClip keys what it gives."""

    def test_sidecar_example_is_accepted(self):
        import rigtest.jointclips as jointclips
        self.build_standard("mirror_arms", "mirror_arms")
        root = jointclips.rig_root()
        rest = jointclips.snapshot()
        folder = config.WORK_DIR
        clip = jointclips.write_json(os.path.join(folder, "doc_anim_clip.json"), jointclips.clip_data(rest, [rest, rest, rest]))
        sidecar = jointclips.write_json(os.path.join(folder, "doc_anim_sidecar.json"), commands_doc_json("### The sidecar"))
        cmds.animateRigFromJointClip(root, jointClip=clip.replace("\\", "/"), sidecar=sidecar.replace("\\", "/"))
        layout = rig_name("layout_00_M_CTRL")
        self.assertEqual(cmds.keyframe(layout + ".translateX", query=True, timeChange=True), [1, 3])
        self.assertEqual(cmds.keyframe(layout + ".translateX", query=True, valueChange=True), [0, 1.0])
        self.assertEqual(cmds.keyframe(rig_name("upperArm_00_L_CTRL") + ".space", query=True, valueChange=True), [0])


class LibraryMetaDocExample(RigTestCase):
    """The meta object in docs/library.md is accepted by the save commands, and getRigDataFileInfo's entry has the keys
    docs/commands.md lists."""

    def meta_text(self):
        with open(os.path.join(config.REPO_DIR, "docs", "library.md"), encoding="utf-8") as handle:
            section = handle.read().split("## File meta", 1)[1]
        return re.search(r"```json\s*\n(.*?)```", section, re.S).group(1)

    def documented_info_keys(self):
        with open(COMMANDS_DOC, encoding="utf-8") as handle:
            section = handle.read().split("## getRigDataFileInfo", 1)[1].split("## setRigDataFileMeta", 1)[0]
        table = section.split("Each entry has:", 1)[1].split("\n\n", 2)[1]
        return {key for line in table.splitlines()[2:] for key in re.findall(r"`(\w+)`", line.split("|")[1])}

    def test_the_documented_meta_saves_and_reads_back(self):
        cmds.currentUnit(time="film")
        self.build_standard("mirror_arms", "mirror_arms")
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        meta = json.loads(self.meta_text())
        pose = os.path.join(config.WORK_DIR, "doc_meta_pose.json").replace("\\", "/")
        anim = os.path.join(config.WORK_DIR, "doc_meta_anim.json").replace("\\", "/")
        cmds.setKeyframe(rig_name("upperArm_00_L_CTRL") + ".rotateZ", time=1, value=0)
        cmds.saveRigPose(root, file=pose, meta=self.meta_text())
        cmds.saveRigAnimation(root, file=anim, startFrame=1, endFrame=1, meta=self.meta_text())
        entries = json.loads(cmds.getRigDataFileInfo(root, file=[pose, anim]))
        for entry in entries:
            self.assertEqual({key: entry["meta"][key] for key in meta}, meta)
            self.assertEqual(set(entry["meta"]) - set(meta), {"assetId", "rigName", "skeletonHash", "pluginVersion", "created"})
        self.assertEqual({key for entry in entries for key in entry} - {"file"}, self.documented_info_keys() - {"file"})
        cmds.setRigDataFileMeta(file=pose, meta=json.dumps({"tags": ["hands"], "status": "review"}))
        self.assertEqual(json.loads(cmds.getRigDataFileInfo(root, file=[pose]))[0]["meta"]["status"], "review")


MODULES_DOC = os.path.join(config.REPO_DIR, "docs", "modules.md")


def doc_code_block(path, heading, language, index=0):
    """The index-th fenced block of a language under a heading, up to the next heading of the same level."""
    with open(path, encoding="utf-8") as handle:
        section = handle.read().split(heading, 1)[1]
    blocks = re.findall(r"```%s\s*\n(.*?)```" % language, section, re.S)
    if len(blocks) <= index:
        raise AssertionError("%s '%s' has no %s block number %d" % (path, heading, language, index))
    return blocks[index]


class TwoBoneIkDocExamples(RigTestCase):
    """The TwoBoneIk entry and snippet in docs/modules.md and the mirrored limb in docs/templates.md."""

    def build_entry(self, template):
        self.build(template, self.make_skeleton("twoboneik_mirror"))

    def test_modules_doc_entry_builds_a_mirrored_limb_at_rest(self):
        entry = json.loads(doc_code_block(MODULES_DOC, "## TwoBoneIk", "json"))
        self.build_entry({"rigVersion": 1, "rigTemplateName": "Doc", "rigAuthor": "you", "rigModules": [entry]})
        for module in ("arm_00_L_MOD", "arm_00_R_MOD", "layout_00_M_MOD"):
            self.assertExists(rig_name(module))
        self.assertWorldTranslation(rig_name("hand_00_L_JNT"), [6, 0, 0])
        self.assertWorldTranslation(rig_name("lowerArm_00_R_JNT"), [-2.8, 2.4, 0])
        # The pole is the mid joint plus poleOffset [0, 2, 0], the settings control the end joint plus [-1, 3, 0] (X negated on the right).
        self.assertWorldTranslation(rig_name("armPole_00_L_CTRL"), [2.8, 4.4, 0])
        self.assertWorldTranslation(rig_name("armPole_00_R_CTRL"), [-2.8, 4.4, 0])
        self.assertWorldTranslation(rig_name("armSettings_00_L_CTRL"), [5, 3, 0])
        self.assertWorldTranslation(rig_name("armSettings_00_R_CTRL"), [-5, 3, 0])

    def test_modules_doc_snippet(self):
        entry = json.loads(doc_code_block(MODULES_DOC, "## TwoBoneIk", "json"))
        self.build_entry({"rigVersion": 1, "rigTemplateName": "Doc", "rigAuthor": "you", "rigModules": [entry]})
        cmds.setAttr("rig:armIK_00_L_CTRL.translateY", 3)
        cmds.setAttr("rig:armSettings_00_L_CTRL.stretch", 1)
        cmds.setAttr("rig:armPole_00_L_CTRL.space", 1)
        # The hand follows the handle: the root is at (1, 0, 0) and the handle at (6, 3, 0), within reach.
        self.assertWorldTranslation(rig_name("hand_00_L_JNT"), [6, 3, 0])
        self.assertEqual(cmds.attributeQuery("space", node="rig:armPole_00_L_CTRL", listEnum=True)[0], "Parent:Ik")

    def test_templates_doc_mirrored_limb(self):
        template = json.loads(doc_code_block(TEMPLATES_DOC, "## Mirroring", "json", index=1))
        self.build_entry(template)
        self.assertExists(rig_name("armIkSpace_00_R_CON"))
        for control in ("armIK_00_L_CTRL", "armIK_00_R_CTRL"):
            self.assertEqual(cmds.attributeQuery("space", node=rig_name(control), listEnum=True)[0], "World:Hips")


class AnchoredRibbonDocExample(RigTestCase):
    """The anchored Ribbon's arm in docs/modules.md (D80) is the ribbon_anchored_arm fixture, which modules/test_ribbon_anchored.py builds
    and poses. It builds as written."""

    def test_the_doc_arm_is_the_tested_fixture_and_builds(self):
        modules = json.loads(doc_code_block(MODULES_DOC, "### The anchored form", "json"))
        self.assertEqual(modules, fixtures.read_json(fixtures.template_path("ribbon_anchored_arm"))["rigModules"])
        self.build({"rigVersion": 1, "rigTemplateName": "Doc", "rigAuthor": "you", "rigModules": modules}, self.make_skeleton("limb_arm"))
        self.assertWorldTranslation(rig_name("armRibbonIK_02_L_CTRL"), [4, 0, 0])
        self.assertIn("bendSoftness", cmds.listAttr(rig_name("armSettings_00_L_CTRL"), keyable=True))


SKELETONS_DOC = os.path.join(config.REPO_DIR, "docs", "skeletons.md")
COMMANDS_DOC = os.path.join(config.REPO_DIR, "docs", "commands.md")


class SkeletonDocExamples(RigTestCase):
    """The skeleton file in docs/skeletons.md is what saveRigSkeleton writes for the chain it describes, the
    fk_three_parented fixture, and the hash both pages quote is that file's hash."""

    def test_file_example_is_what_a_save_writes(self):
        joints = self.make_skeleton("fk_three_parented")
        path = os.path.join(config.WORK_DIR, "doc_skeleton.json").replace("\\", "/")
        result = cmds.saveRigSkeleton(*joints, file=path)
        with open(path, encoding="utf-8") as handle:
            saved = handle.read()
        documented = doc_code_block(SKELETONS_DOC, "## File format", "json")
        self.assertEqual(json.loads(documented), json.loads(saved))
        self.assertEqual(documented.strip(), saved.strip(), "the doc should show the file's exact layout")
        for doc, heading in ((SKELETONS_DOC, "## Saving"), (COMMANDS_DOC, "## saveRigSkeleton")):
            self.assertIn('Returns "%s"' % result, doc_code_block(doc, heading, "python"))

    def test_load_example_returns_the_documented_joints(self):
        # The documented file, loaded as both pages show, gives the joints they quote.
        path = os.path.join(config.WORK_DIR, "doc_skeleton_load.json").replace("\\", "/")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(doc_code_block(SKELETONS_DOC, "## File format", "json"))
        joints = cmds.loadRigSkeleton(file=path, namespace="hero_body")
        self.assertEqual(joints, ["hero_body:a_00_M_JNT", "hero_body:b_00_M_JNT", "hero_body:c_00_M_JNT"])
        for doc, heading in ((SKELETONS_DOC, "## Loading"), (COMMANDS_DOC, "## loadRigSkeleton")):
            self.assertIn("Returns %s" % json.dumps(joints), doc_code_block(doc, heading, "python"))
        self.assertWorldTranslation("hero_body:c_00_M_JNT", [0, 4, 0])


class RibbonDocExamples(RigTestCase):
    """The Ribbon entry and snippet in docs/modules.md."""

    def build_entry(self):
        entry = json.loads(doc_code_block(MODULES_DOC, "## Ribbon", "json"))
        self.build({"rigVersion": 1, "rigTemplateName": "Doc", "rigAuthor": "you", "rigModules": [entry]}, self.make_skeleton("ribbon_basic"))

    def test_modules_doc_entry_builds_a_tail_at_rest(self):
        self.build_entry()
        self.assertExists(rig_name("tail_01_M_MOD"))
        self.assertWorldTranslation(rig_name("tail3_00_M_JNT"), [4, 0, 0])
        self.assertWorldTranslation(rig_name("tail5_00_M_JNT"), [8, 0, 0])

    def test_modules_doc_snippet(self):
        self.build_entry()
        cmds.setAttr("rig:tailIK_01_M_CTRL.translateY", 2)
        cmds.setAttr("rig:tailIK_02_M_CTRL.rotateX", 200)
        cmds.setAttr("rig:tailIK_00_M_CTRL.endTwist", 200)
        cmds.setAttr("rig:tailIK_00_M_CTRL.volumePreserve", 1)
        # The end's 200 degree roll ramps along the ribbon, so the middle control's frame is rolled 100 degrees and its 2 units
        # of local Y point (0, cos 100, sin 100) * 2. The ends don't move.
        self.assertWorldTranslation(rig_name("tail3_00_M_JNT"), [4, 2 * math.cos(math.radians(100)), 2 * math.sin(math.radians(100))])
        self.assertWorldTranslation(rig_name("tail5_00_M_JNT"), [8, 0, 0])

    def test_modules_doc_shoulder_example_sheds_the_twist_at_the_start(self):
        """The 'A shoulder that sheds its twist' snippet: the upper ribbon entry and its two constraints go on the fixture's
        TwoBoneIk and forearm ribbon. With the shoulder turned 60 about X (FK) the upper ribbon's first joint holds no twist and
        its last holds all of it."""
        snippet = json.loads(doc_code_block(MODULES_DOC, "**A shoulder that sheds its twist.**", "json"))
        fixture = fixtures.load_template("ribbon_arm_swing")
        modules = [fixture["rigModules"][0]] + snippet["rigModules"] + [fixture["rigModules"][2]]
        self.build({"rigVersion": 1, "rigTemplateName": "Doc", "rigAuthor": "you", "rigModules": modules, "constraints": snippet["constraints"]},
                   self.make_skeleton("ribbon_arm"))
        cmds.setAttr("rig:armSettings_00_L_CTRL.ikFkBlend", 0)
        cmds.setAttr("rig:upperArm_00_L_CTRL.rotateX", 60)
        rolls = {}
        for joint in ("up1_00_M_JNT", "up5_00_M_JNT"):
            m = cmds.xform(rig_name(joint), query=True, worldSpace=True, matrix=True)
            rolls[joint] = math.degrees(math.atan2(m[6], m[5]))
        self.assertAlmostEqual(rolls["up1_00_M_JNT"], 0.0, delta=1.0)
        self.assertAlmostEqual(rolls["up5_00_M_JNT"], 60.0, delta=1.0)

    def test_modules_doc_twist_ramp_example_gives_the_numbers_it_quotes(self):
        """The 'twistRamp' snippet's ramp on a three-control fk ribbon: a 30 degree roll of the first fk control rolls the ik controls
        0, 10.5 and 30 degrees, and the fk control itself the whole 30."""
        snippet = json.loads("{" + doc_code_block(MODULES_DOC, "**`twistRamp`: how much of the fk roll the ik controls take.**", "json") + "}")
        template = copy.deepcopy(fixtures.load_template("ribbon_fk"))
        template["rigModules"][0]["moduleArgs"]["twistRamp"] = snippet["moduleArgs"]["twistRamp"]
        self.build(template, self.make_skeleton("ribbon_basic"))
        cmds.setAttr("rig:tailFK_00_M_CTRL.rotateX", 30)
        self.assertWorldRotation(rig_name("tailFK_00_M_CTRL"), [30, 0, 0], 1e-4)
        for control, roll in zip(("tailIK_00_M_CTRL", "tailIK_01_M_CTRL", "tailIK_02_M_CTRL"), (0.0, 10.5, 30.0)):
            self.assertWorldRotation(rig_name(control), [roll, 0, 0], 1e-4)

    def test_modules_doc_wrist_example_sends_up_only_the_hands_twist(self):
        """The 'A wrist that sends up only its twist' snippet goes on the fixture's TwoBoneIk. The hand turned 60 about X (FK)
        rolls the forearm's last joint 60 and its first none, and the hand's swing moves no joint of the ribbon."""
        snippet = json.loads(doc_code_block(MODULES_DOC, "**A wrist that sends up only its twist.**", "json"))
        arm = json.loads(json.dumps(fixtures.load_template("ribbon_arm_twist")["rigModules"][0]))
        self.build({"rigVersion": 1, "rigTemplateName": "Doc", "rigAuthor": "you", "rigModules": [arm] + snippet["rigModules"],
                    "constraints": snippet["constraints"]}, self.make_skeleton("ribbon_arm"))
        cmds.setAttr("rig:armSettings_00_L_CTRL.ikFkBlend", 0)
        rest = {joint: cmds.xform(rig_name(joint), query=True, worldSpace=True, matrix=True) for joint in ("fo1_00_M_JNT", "fo3_00_M_JNT", "fo5_00_M_JNT")}
        cmds.setAttr("rig:hand_00_L_CTRL.rotateZ", 40)
        for joint, matrix in rest.items():
            now = cmds.xform(rig_name(joint), query=True, worldSpace=True, matrix=True)
            for a, b in zip(now, matrix):
                self.assertAlmostEqual(a, b, delta=1e-3, msg=joint)
        cmds.setAttr("rig:hand_00_L_CTRL.rotateZ", 0)
        cmds.setAttr("rig:hand_00_L_CTRL.rotateX", 60)
        rolls = {}
        for joint in ("fo1_00_M_JNT", "fo5_00_M_JNT"):
            m = cmds.xform(rig_name(joint), query=True, worldSpace=True, matrix=True)
            rolls[joint] = math.degrees(math.atan2(m[6], m[5]))
        self.assertAlmostEqual(rolls["fo1_00_M_JNT"], 0.0, delta=1.0)
        self.assertAlmostEqual(rolls["fo5_00_M_JNT"], 60.0, delta=1.0)

    def test_modules_doc_arm_example_builds_with_its_aim_keys_and_meets_at_the_elbow(self):
        """The two ribbon entries under 'An arm' go on the fixture's TwoBoneIk. They build at rest with both ends aimed, and with
        the shoulder turned 60 about X (FK) the upper arm's last joint and the forearm's first both roll 60."""
        ribbons = json.loads(doc_code_block(MODULES_DOC, "**An arm**", "json"))
        arm = json.loads(json.dumps(fixtures.load_template("ribbon_arm")["rigModules"][0]))
        self.build({"rigVersion": 1, "rigTemplateName": "Doc", "rigAuthor": "you", "rigModules": [arm] + ribbons}, self.make_skeleton("ribbon_arm"))
        for control in ("upIK", "foIK"):
            self.assertAlmostEqual(cmds.getAttr("rig:%s_00_M_CTRL.startAimBlend" % control), 1.0, places=6)
            self.assertAlmostEqual(cmds.getAttr("rig:%s_00_M_CTRL.endAimBlend" % control), 1.0, places=6)
        cmds.setAttr("rig:armSettings_00_L_CTRL.ikFkBlend", 0)
        cmds.setAttr("rig:upperArm_00_L_CTRL.rotateX", 60)
        for joint in ("up5_00_M_JNT", "fo1_00_M_JNT"):
            m = cmds.xform(rig_name(joint), query=True, worldSpace=True, matrix=True)
            self.assertAlmostEqual(math.degrees(math.atan2(m[6], m[5])), 60.0, delta=1.0, msg=joint)


class FkHierarchyDocExample(RigTestCase):
    """The FkHierarchy entry in docs/modules.md builds on the fk_hierarchy skeleton, and what the section says about it holds."""

    def test_modules_doc_entry_builds_the_controls_and_sockets_it_describes(self):
        entry = json.loads(doc_code_block(MODULES_DOC, "## FkHierarchy", "json"))
        self.build({"rigVersion": 1, "rigTemplateName": "Doc", "rigAuthor": "you", "rigModules": [entry]}, self.make_skeleton("fk_hierarchy"))
        controls = sorted(n.split(":")[-1] for n in cmds.ls(rig_name("*"), type="rigControlNode") if not n.split(":")[-1].startswith("layout_"))
        # The handle is a terminator: it has a control, and the grip and gripEnd below it do not.
        self.assertEqual(controls, ["handle_00_M_CTRL", "magTip_00_M_CTRL", "mag_00_M_CTRL", "root_00_M_CTRL", "trigger_00_M_CTRL"])
        module = rig_name(entry["name"])
        names = [json.loads(cmds.getAttr("%s.socketNames[%d]" % (module, i))) for i in cmds.getAttr(module + ".socketNames", multiIndices=True)]
        self.assertIn(["center_handle"], names)
        self.assertEqual(names[0], ["center_root", "start"])


MARKERS_DOC = os.path.join(config.REPO_DIR, "docs", "markers.md")


def markers_doc_file_text():
    """The first ```json block under '## The file' in docs/markers.md."""
    with open(MARKERS_DOC, encoding="utf-8") as handle:
        section = handle.read().split("## The file", 1)[1]
    match = re.search(r"```json\s*\n(.*?)```", section, re.S)
    if not match:
        raise AssertionError("docs/markers.md '## The file' has no ```json block")
    return match.group(1)


class MarkersDocExample(RigTestCase):
    """The markers file in docs/markers.md loads onto the Left ankle it follows, and saves back to the same hash."""

    def test_markers_file_example_loads_and_its_hash_is_right(self):
        cmds.namespace(add=":skel")
        cmds.select(clear=True)
        ankle = cmds.joint(name=":skel:ankle_00_L_JNT", position=(0, 0, 0))
        cmds.rigJointMarkup(ankle, usage="ankle", side="Left", export=True, skin=True)
        path = os.path.join(config.WORK_DIR, "doc_markers.json").replace("\\", "/")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(markers_doc_file_text())
        nodes = cmds.loadRigMarkers(file=path, namespace="skel")
        self.assertEqual([node.split("|")[-1] for node in nodes], ["skel:ball_00_L_MRK", "skel:heel_00_L_MRK"])
        self.assertWorldTranslation("skel:ball_00_L_MRK", (0, 10, -8.7))
        self.assertWorldTranslation("skel:heel_00_L_MRK", (0, -9.5, -1.2))
        saved = cmds.saveRigMarkers(*nodes, file=path.replace("doc_markers", "doc_markers_saved"))
        self.assertEqual(json.loads(markers_doc_file_text())["markersHash"], saved)


MODULES_DOC = os.path.join(config.REPO_DIR, "docs", "modules.md")


def modules_doc_foot_module():
    """The first ```json block under '## ReverseFoot' in docs/modules.md: the foot's template entry."""
    with open(MODULES_DOC, encoding="utf-8") as handle:
        section = handle.read().split("## ReverseFoot", 1)[1]
    match = re.search(r"```json\s*\n(.*?)```", section, re.S)
    if not match:
        raise AssertionError("docs/modules.md '## ReverseFoot' has no ```json block")
    return json.loads(match.group(1))


PUSH_OUT_DOC = os.path.join(config.REPO_DIR, "docs", "push-out.md")


def push_out_doc_collisions():
    """The collisions list of docs/push-out.md: its first ```json block, a fragment with the key and the array."""
    with open(PUSH_OUT_DOC, encoding="utf-8") as handle:
        text = handle.read()
    blocks = re.findall(r"```json\s*\n(.*?)```", text, re.S)
    if not blocks:
        raise AssertionError("docs/push-out.md needs its collisions block")
    return json.loads("{" + blocks[0] + "}")["collisions"]


class PushOutDocExample(RigTestCase):
    """docs/push-out.md: the collisions entry builds as written on the Left chain of fk_pushout (its mirror needs the Right
    one, so the doc's entry is built without `mirror`), and the capsule pushes the control. With joint a moved to x = 2 the
    capsule's origin is (2, 0.5, 0) and its straight part runs x from 1 to 3, so the control at (3, 0, 0) is 0.5 from the
    segment's end (3, 0.5, 0). Radius 2 and margin 0.1 reach 2.1, so it is pushed 1.6 along (0, -1, 0)."""

    def test_the_documented_collision_builds_and_pushes(self):
        collisions = push_out_doc_collisions()
        collisions[0].pop("mirror")
        template = fixtures.load_template("push_out_basic")
        template["collisions"] = collisions
        template_path = fixtures.write_template(template, "doc_push_out")
        cmds.createRigFromTemplate(*self.make_skeleton("fk_pushout"), template=template_path, asset=config.ASSET_ID,
                                   namespace=config.RIG_NAMESPACE)
        cmds.setAttr(rig_name("a_00_M_CTRL.translateX"), 2)
        self.assertWorldTranslation(rig_name("c_00_L_CTRL"), [3, -1.6, 0], 1e-4)


class ReverseFootDocExample(RigTestCase):
    """The ReverseFoot entry in docs/modules.md builds on the TwoBoneIk it names, with the markers it selects, and a roll of 20 puts the
    arm's hand where the pose case ball_regime_roll_20 works it out: 20 about the toe pivot (4, 3.3, 1.8), so (4, 4.573421, 0.347967)."""

    def test_foot_module_example_builds_and_rolls(self):
        template = fixtures.load_template("reverse_foot")
        template["rigModules"][1] = modules_doc_foot_module()
        joints = self.make_skeleton("reverse_foot")
        self.build(template, joints, markers="reverse_foot", label="doc_reverse_foot")
        cmds.setAttr(rig_name("foot_00_L_CTRL.footRoll"), 20)
        self.assertWorldTranslation(rig_name("hand_00_L_JNT"), [4.0, 4.573421, 0.347967], 1e-4)
