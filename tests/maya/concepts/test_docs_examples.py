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

SPINE3_CTRL = rig_name("spine3_ctrl_c")
ARM_CTRL = rig_name("arm_ctrl_l")
ARM_JNT = rig_name("arm_jnt")

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
        self.build_standard(readme_quick_start_template(), "readme_quick_start")

    def rotate_spine3_z(self, degrees):
        cmds.setAttr(SPINE3_CTRL + ".rotateZ", degrees)

    def test_builds_the_names_the_readme_gives(self):
        for module in ("spine", "arm", "layout"):
            self.assertExists(rig_name(module))
        for control in ("spine1_ctrl_c", "spine2_ctrl_c", "spine3_ctrl_c", "arm_ctrl_l"):
            self.assertExists(rig_name(control))

    def test_arm_hangs_off_the_spine_end(self):
        self.rotate_spine3_z(90)
        self.assertWorldTranslation(ARM_JNT, ARM_AFTER_SPINE3_ROTATE_Z_90)

    def test_space_switch(self):
        con = cmds.createRigParentConstraint(ARM_CTRL, driver=["layout.end", "spine.end"], label=["World", "Chest"])
        self.assertEqual(con.lstrip(":"), "rig:arm_ctrl_l_space")
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
        self.build_standard(readme_quick_start_template(), "readme_quick_start")
        self.root = "rig:rigRootModule1"   # the name the docs examples use

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

