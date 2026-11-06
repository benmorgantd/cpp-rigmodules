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
