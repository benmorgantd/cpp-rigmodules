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

# The README builds with asset="shooter\\characters\\hero" and no namespace, so its rig "body" lands in the
# default namespace "{asset name}_{rigName}" (D54).
DOC_ASSET = "shooter\\characters\\hero"
DOC_NAMESPACE = "hero_body"

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
        self.build(readme_quick_start_template(), self.make_skeleton("readme_quick_start"), asset=DOC_ASSET, namespace=None)

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
        self.assertEqual(con.lstrip(":"), "hero_body:arm_ctrl_l_space")
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
        cmds.createRigConstraint(ARM_CTRL, driver=["layout.end", "spine.end"], label=["World", "Chest"], initial=1)
        self.assertEqual(json.loads(cmds.getRigControlSpace(ARM_CTRL)),
                         {"constraint": "hero_body:arm_ctrl_l_space", "module": "", "labels": ["World", "Chest"], "current": 1})
        self.assertEqual(cmds.getRigControlSpace(SPINE3_CTRL), "")

    def test_set_rig_control_space_example(self):
        """The first example under '## setRigControlSpace' in docs/commands.md."""
        cmds.createRigConstraint(ARM_CTRL, driver=["layout.end", "spine.end"], label=["World", "Chest"])
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
        self.build(template, self.make_skeleton("readme_quick_start"), asset=DOC_ASSET, namespace=None)
        self.assertEqual(cmds.getAttr("hero_body:arm_space.constraintType", asString=True), "Orient")
        self.assertPivotFollowsChestRotationDoesNot()

    def test_command_example(self):
        self.build(readme_quick_start_template(), self.make_skeleton("readme_quick_start"), asset=DOC_ASSET, namespace=None)
        cmds.createRigConstraint("hero_body:arm_ctrl_l", driver=["layout.end", "spine.end"], label=["World", "Chest"], type="Orient")
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
        self.build(readme_quick_start_template(), self.make_skeleton("readme_quick_start"), asset=DOC_ASSET, namespace=None)
        self.root = "hero_body:rigRootModule1"   # the name the docs examples use

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
        layout = rig_name("layout_0_ctrl_c")
        self.assertEqual(cmds.keyframe(layout + ".translateX", query=True, timeChange=True), [1, 3])
        self.assertEqual(cmds.keyframe(layout + ".translateX", query=True, valueChange=True), [0, 1.0])
        self.assertEqual(cmds.keyframe(rig_name("upperArm_l_ctrl_l") + ".space", query=True, valueChange=True), [0])


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
        cmds.setKeyframe(rig_name("upperArm_l_ctrl_l") + ".rotateZ", time=1, value=0)
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
    """The TwoBoneIk entry and snippet in docs/modules.md and the mirrored limb in docs/templates.md (D52)."""

    def build_entry(self, template):
        self.build(template, self.make_skeleton("twoboneik_mirror"))

    def test_modules_doc_entry_builds_a_mirrored_limb_at_rest(self):
        entry = json.loads(doc_code_block(MODULES_DOC, "## TwoBoneIk", "json"))
        self.build_entry({"rigVersion": 1, "rigTemplateName": "Doc", "rigAuthor": "you", "rigModules": [entry]})
        for module in ("arm_l", "arm_r", "layout"):
            self.assertExists(rig_name(module))
        self.assertWorldTranslation(rig_name("hand_l_jnt"), [6, 0, 0])
        self.assertWorldTranslation(rig_name("lowerArm_r_jnt"), [-2.8, 2.4, 0])

    def test_modules_doc_snippet(self):
        entry = json.loads(doc_code_block(MODULES_DOC, "## TwoBoneIk", "json"))
        self.build_entry({"rigVersion": 1, "rigTemplateName": "Doc", "rigAuthor": "you", "rigModules": [entry]})
        cmds.setAttr("rig:handIk_l_ctrl_l.translateY", 3)
        cmds.setAttr("rig:armSettings_l_ctrl_l.stretch", 1)
        cmds.setAttr("rig:armPole_l_ctrl_l.space", 1)
        # The hand follows the handle: the root is at (1, 0, 0) and the handle at (6, 3, 0), within reach.
        self.assertWorldTranslation(rig_name("hand_l_jnt"), [6, 3, 0])
        self.assertEqual(cmds.attributeQuery("space", node="rig:armPole_l_ctrl_l", listEnum=True)[0], "Parent:Ik")

    def test_templates_doc_mirrored_limb(self):
        template = json.loads(doc_code_block(TEMPLATES_DOC, "## Mirroring", "json", index=1))
        self.build_entry(template)
        self.assertExists(rig_name("arm_ik_space_r"))
        for control in ("handIk_l_ctrl_l", "handIk_r_ctrl_r"):
            self.assertEqual(cmds.attributeQuery("space", node=rig_name(control), listEnum=True)[0], "World:Hips")


SKELETONS_DOC = os.path.join(config.REPO_DIR, "docs", "skeletons.md")
COMMANDS_DOC = os.path.join(config.REPO_DIR, "docs", "commands.md")


class SkeletonDocExamples(RigTestCase):
    """The skeleton file in docs/skeletons.md is what saveRigSkeleton writes for the chain it describes, the
    fk_three_parented fixture, and the hash both pages quote is that file's hash (D54)."""

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
        self.assertEqual(joints, ["hero_body:a_jnt", "hero_body:b_jnt", "hero_body:c_jnt"])
        for doc, heading in ((SKELETONS_DOC, "## Loading"), (COMMANDS_DOC, "## loadRigSkeleton")):
            self.assertIn("Returns %s" % json.dumps(joints), doc_code_block(doc, heading, "python"))
        self.assertWorldTranslation("hero_body:c_jnt", [0, 4, 0])
