"""Module and constraint mirroring (D28): "mirror": true in a template, the checkMirrorPositions build flag,
getRigMirrorControl and mirrorRigControlShapes. Template errors are validation cases (fixtures/validation/mirror_*)."""
from maya import cmds
import maya.api.OpenMaya as om

from rigtest import config, fixtures
from rigtest.case import RigTestCase, rig_name
from rigtest.cases import rig_interface

ARM_L = rig_name("upperArm_l_ctrl_l")
ARM_R = rig_name("upperArm_r_ctrl_r")
FOREARM_L = rig_name("lowerArm_l_ctrl_l")
FOREARM_R = rig_name("lowerArm_r_ctrl_r")
CHEST = rig_name("chest_ctrl_c")
REFLECT_X = om.MMatrix([-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])
SHAPE_POINTS = [om.MPoint(1, 0, 0), om.MPoint(0, 1, 0), om.MPoint(0, 0, 1), om.MPoint(0.5, -0.25, 2)]


def matrix(plug):
    return om.MMatrix(cmds.getAttr(plug))


def rig_root():
    return cmds.ls(rig_name("*"), type="rigRootModule")[0]


def mirror_template_path():
    return fixtures.template_path("mirror_arms").replace("\\", "/")


def shape_world_points(control):
    """Shape points in world space: shape space -> control space (shapeTransform) -> world."""
    to_world = matrix(control + ".shapeTransform") * matrix(control + ".worldMatrix[0]")
    return [p * to_world for p in SHAPE_POINTS]


class MirrorTemplate(RigTestCase):

    def test_mirror_matches_template_written_by_hand(self):
        # "A mirror module's JSON is exactly like its source": the built rigs have the same public names.
        self.build_standard("mirror_arms", "mirror_arms")
        mirrored = rig_interface()
        self.setUp()
        self.build_standard("mirror_arms_explicit", "mirror_arms")
        self.assertEqual(mirrored, rig_interface())

    def test_mirror_module_drives_the_other_side(self):
        self.build_standard("mirror_arms", "mirror_arms")
        self.assertOpmDrivenBy(rig_name("upperArm_r_jnt"), "arm_r")
        self.assertOpmDrivenBy(rig_name("clavicle_r_jnt"), "clavicle_r")
        self.assertEqual(cmds.getAttr(rig_name("arm_r") + ".side"), 2)   # Right

    def test_mirror_constraint_uses_mirrored_sockets(self):
        self.build_standard("mirror_arms", "mirror_arms")
        con = rig_name("arm_space_r")
        sources = cmds.listConnections(con + ".drivers[0].driverMatrix", source=True, destination=False)
        self.assertEqual([s.split(":")[-1] for s in sources], ["clavicle_r"])
        # A label left out defaults to the socket id, so the mirror's is its own socket.
        self.assertEqual(cmds.getAttr(con + ".drivers[0].driverLabel"), "clavicle_r.end")
        self.assertEqual(cmds.getAttr(con + ".drivers[1].driverLabel"), "World")


class CheckMirrorPositions(RigTestCase):

    def test_asymmetric_skeleton_builds_by_default(self):
        # A skeleton can be asymmetric where the rig is not.
        self.build_standard("mirror_arms", "mirror_arms_asymmetric")
        self.assertExists(ARM_R)

    def test_flag_fails_a_mirror_joint_off_its_reflected_source(self):
        joints = self.make_skeleton("mirror_arms_asymmetric")
        build = lambda: cmds.createRigFromTemplate(*joints, template=mirror_template_path(), asset=config.ASSET_ID, checkMirrorPositions=True)
        self.assertMayaError(build, "lowerArm", "checkMirrorPositions")
        self.assertNothingBuilt()

    def test_flag_passes_a_symmetric_skeleton(self):
        joints = self.make_skeleton("mirror_arms")
        cmds.createRigFromTemplate(*joints, template=mirror_template_path(), asset=config.ASSET_ID, checkMirrorPositions=True)
        self.assertExists(ARM_R)


class GetRigMirrorControl(RigTestCase):

    def setUp(self):
        super().setUp()
        self.build_standard("mirror_arms", "mirror_arms")

    def test_left_returns_right(self):
        self.assertEqual(cmds.getRigMirrorControl(ARM_L), "rig:upperArm_r_ctrl_r")

    def test_right_returns_left(self):
        self.assertEqual(cmds.getRigMirrorControl(ARM_R), "rig:upperArm_l_ctrl_l")

    def test_center_returns_itself(self):
        self.assertEqual(cmds.getRigMirrorControl(CHEST), "rig:chest_ctrl_c")
        self.assertEqual(cmds.getRigMirrorControl(rig_name("layout_1_ctrl_c")), "rig:layout_1_ctrl_c")

    def test_no_mirror_is_an_error(self):
        RigTestCase.setUp(self)
        self.build_standard("fkchain_two_chains", "fk_two_chains")
        self.assertMayaError(lambda: cmds.getRigMirrorControl(rig_name("c_ctrl_l")), "c_ctrl_l", "Right")

    def test_not_a_control_is_an_error(self):
        self.assertMayaError(lambda: cmds.getRigMirrorControl(rig_name("upperArm_l_jnt")), "Not a rig control")

    def test_needs_one_control(self):
        self.assertMayaError(lambda: cmds.getRigMirrorControl(ARM_L, ARM_R))


class MirrorRigControlShapes(RigTestCase):

    def setUp(self):
        super().setUp()
        self.build_standard("mirror_arms", "mirror_arms")
        # An asymmetric shape: offset, rotated and scaled, so a wrong reflection shows.
        shape = om.MTransformationMatrix()
        shape.setTranslation(om.MVector(0.5, 1, -0.75), om.MSpace.kTransform)
        shape.setRotation(om.MEulerRotation(0.3, -0.6, 0.9))
        shape.setScale([1.5, 0.5, 2], om.MSpace.kTransform)
        for control in (ARM_L, FOREARM_L):
            cmds.setAttr(control + ".shapeTransform", list(shape.asMatrix()), type="matrix")
            cmds.setAttr(control + ".shapeType", 8)
            cmds.setAttr(control + ".filled", True)

    def assertShapesReflect(self, source, mirror):
        expected = [p * REFLECT_X for p in shape_world_points(source)]
        actual = shape_world_points(mirror)
        for want, got in zip(expected, actual):
            self.assertTrue(want.isEquivalent(got, 1e-4), "%s: point %s, expected %s" % (mirror, got, want))

    def test_left_to_right_reflects_shapes_in_world_space(self):
        changed = cmds.mirrorRigControlShapes(rig_root())
        self.assertEqual(sorted(changed), ["rig:clavicle_r_ctrl_r", "rig:lowerArm_r_ctrl_r", "rig:upperArm_r_ctrl_r"])
        self.assertShapesReflect(ARM_L, ARM_R)
        self.assertShapesReflect(FOREARM_L, FOREARM_R)
        self.assertEqual(cmds.getAttr(ARM_R + ".shapeType"), 8)
        self.assertTrue(cmds.getAttr(ARM_R + ".filled"))

    def test_right_to_left(self):
        cmds.mirrorRigControlShapes(rig_root())
        cmds.setAttr(ARM_L + ".shapeTransform", list(om.MMatrix()), type="matrix")
        cmds.mirrorRigControlShapes(rig_root(), leftToRight=False)
        self.assertShapesReflect(ARM_R, ARM_L)

    def test_uses_rest_not_the_current_pose(self):
        cmds.setAttr(rig_name("clavicle_r_ctrl_r") + ".rotateZ", 45)
        cmds.setAttr(ARM_L + ".rotateY", 30)
        cmds.mirrorRigControlShapes(rig_root())
        cmds.setAttr(rig_name("clavicle_r_ctrl_r") + ".rotateZ", 0)
        cmds.setAttr(ARM_L + ".rotateY", 0)
        self.assertShapesReflect(ARM_L, ARM_R)

    def test_center_and_color_untouched(self):
        chest = cmds.getAttr(CHEST + ".shapeTransform")
        color = cmds.getAttr(ARM_R + ".wireframeColor")
        cmds.mirrorRigControlShapes(rig_root())
        self.assertEqual(cmds.getAttr(CHEST + ".shapeTransform"), chest)
        self.assertEqual(cmds.getAttr(ARM_R + ".wireframeColor"), color)

    def test_undo_and_redo(self):
        before = cmds.getAttr(ARM_R + ".shapeTransform")
        cmds.mirrorRigControlShapes(rig_root())
        cmds.undo()
        self.assertEqual(cmds.getAttr(ARM_R + ".shapeTransform"), before)
        cmds.redo()
        self.assertShapesReflect(ARM_L, ARM_R)

    def test_object_must_be_a_rig_root(self):
        self.assertMayaError(lambda: cmds.mirrorRigControlShapes(ARM_L), "Not a rig root")
