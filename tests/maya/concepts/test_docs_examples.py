"""Examples in the user docs work as written (D21).

The template is read from README.md itself, so editing the README example into something that no longer
builds fails here. The commands mirror the README's Python. The skeleton fixture matches the joints the
README describes.
"""
import json
import os
import re

from maya import cmds

from rigtest import config
from rigtest.case import RigTestCase, rig_name

README = os.path.join(config.REPO_DIR, "README.md")
TEMPLATES_DOC = os.path.join(config.REPO_DIR, "docs", "templates.md")

# The README builds with asset="shooter|characters|hero", so the rig lands in the asset's namespace, hero.
DOC_ASSET = "shooter|characters|hero"
DOC_NAMESPACE = "hero"

SPINE3_CTRL = rig_name("spine3_ctrl_c", DOC_NAMESPACE)
ARM_CTRL = rig_name("arm_ctrl_l", DOC_NAMESPACE)
ARM_JNT = rig_name("arm_jnt", DOC_NAMESPACE)

# arm_jnt rests at (2, 4, 0), 2 units along +X from spine3_jnt at (0, 4, 0).
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


class ReadmeQuickStart(RigTestCase):

    def setUp(self):
        super().setUp()
        self.build(readme_quick_start_template(), self.make_skeleton("readme_quick_start"), asset=DOC_ASSET)

    def rotate_spine3_z(self, degrees):
        cmds.setAttr(SPINE3_CTRL + ".rotateZ", degrees)

    def test_builds_the_names_the_readme_gives(self):
        for module in ("spine", "arm", "layout"):
            self.assertExists(rig_name(module, DOC_NAMESPACE))
        for control in ("spine1_ctrl_c", "spine2_ctrl_c", "spine3_ctrl_c", "arm_ctrl_l"):
            self.assertExists(rig_name(control, DOC_NAMESPACE))

    def test_arm_hangs_off_the_spine_end(self):
        self.rotate_spine3_z(90)
        self.assertWorldTranslation(ARM_JNT, ARM_AFTER_SPINE3_ROTATE_Z_90)

    def test_space_switch(self):
        con = cmds.createRigConstraint(ARM_CTRL, driver=["layout.end", "spine.end"], label=["World", "Chest"])
        self.assertEqual(con.lstrip(":"), "hero:arm_ctrl_l_space")
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
    """docs/templates.md '### Constraint types' and the docs/commands.md Orient example, on the quick start rig (D31).
    Either way the arm's pivot follows spine3 and its rotation stays at rest."""

    def assertPivotFollowsChestRotationDoesNot(self):
        rest_rotation = cmds.xform(ARM_JNT, q=True, ws=True, rotation=True)
        cmds.setAttr(SPINE3_CTRL + ".rotateZ", 90)
        self.assertWorldTranslation(ARM_JNT, ARM_AFTER_SPINE3_ROTATE_Z_90)
        self.assertWorldRotation(ARM_JNT, rest_rotation)

    def test_template_example(self):
        template = readme_quick_start_template()
        template["constraints"] = [templates_doc_orient_constraint()]
        self.build(template, self.make_skeleton("readme_quick_start"), asset=DOC_ASSET)
        self.assertEqual(cmds.getAttr("hero:arm_space.constraintType", asString=True), "Orient")
        self.assertPivotFollowsChestRotationDoesNot()

    def test_command_example(self):
        self.build(readme_quick_start_template(), self.make_skeleton("readme_quick_start"), asset=DOC_ASSET)
        cmds.createRigConstraint("hero:arm_ctrl_l", driver=["layout.end", "spine.end"], label=["World", "Chest"], type="Orient")
        self.assertPivotFollowsChestRotationDoesNot()


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
        self.build(readme_quick_start_template(), self.make_skeleton("readme_quick_start"), asset=DOC_ASSET)
        self.root = "hero:rigRootModule1"   # the name the docs examples use

    def test_get_rig_controls_examples(self):
        self.assertEqual(len(cmds.getRigControls(self.root)), 7)
        self.assertEqual([c.split(":")[-1] for c in cmds.getRigControls(self.root, moduleType="FkChain", side="Left")],
                         ["arm_ctrl_l"])
        self.assertEqual([c.split(":")[-1] for c in cmds.getRigControls(self.root, usage=["spine1", "spine2"])],
                         ["spine1_ctrl_c", "spine2_ctrl_c"])

    def test_shape_file_example_loads(self):
        path = os.path.join(config.WORK_DIR, "doc_shapes.json").replace("\\", "/")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(controls_doc_shape_file())
        self.assertEqual([c.split(":")[-1] for c in cmds.loadRigControlShapes(self.root, file=path)], ["arm_ctrl_l"])
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
        for name in ("clavicle_r", "arm_r", "arm_space_r"):
            self.assertExists(rig_name(name))
        self.assertOpmDrivenBy(rig_name("upperArm_r_jnt"), "arm_r")
        self.assertEqual(cmds.attributeQuery("space", node=rig_name("upperArm_r_ctrl_r"), listEnum=True), ["Clavicle:World"])


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
        self.assertEqual(rigmodules.bonemap.read_bone_map(path)["skeletonId"], "shooter|skeletons|human")
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
        control = rig_name("upperArm_l_ctrl_l")
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
        plug = rig_name("upperArm_l_ctrl_l") + ".rotateZ"
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
        self.assertEqual(changed, ["rig:upperArm_l_ctrl_l"])


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
        self.assertEqual([c.split(":")[-1] for c in changed], ["upperArm_l_ctrl_l"])
        arm = rig_name("upperArm_l_ctrl_l")
        self.assertEqual(cmds.keyframe(arm + ".rotateZ", query=True, valueChange=True), [0, 90, 45])
        self.assertEqual(cmds.keyframe(arm + ".translateX", query=True, valueChange=True), [0, 2.5])
        found = json.loads(cmds.getRigKeys(root))
        self.assertEqual(found["Left"]["upperArm"]["rotateZ"], [[1, 0], [10, 90], [20, 45]])


class AnimationSidecarDocExample(RigTestCase):
    """The animation sidecar in docs/commands.md passes the plugin's reader, and animateRigFromJointClip uses it."""

    def test_sidecar_example_is_accepted(self):
        import rigtest.jointclips as jointclips
        self.build_standard("mirror_arms", "mirror_arms")
        root = jointclips.rig_root()
        rest = jointclips.snapshot()
        folder = config.WORK_DIR
        clip = jointclips.write_json(os.path.join(folder, "doc_anim_clip.json"), jointclips.clip_data(rest, [rest, rest, rest]))
        sidecar = jointclips.write_json(os.path.join(folder, "doc_anim_sidecar.json"), commands_doc_json("### Animation sidecar format"))
        cmds.animateRigFromJointClip(root, jointClip=clip.replace("\\", "/"), sidecar=sidecar.replace("\\", "/"))
        layout = rig_name("layout_0_ctrl_c")
        self.assertEqual(cmds.keyframe(layout + ".translateX", query=True, valueChange=True), [0, 0.5, 1.0])
        self.assertEqual(cmds.keyframe(rig_name("upperArm_l_ctrl_l") + ".space", query=True, valueChange=True), [0, 0, 0])
