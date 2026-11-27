"""The TwoBoneIk inverse (design/two-bone-ik.md section 6): a limb's controls from its joints.

Round trips as in test_inverse_solve: pose the rig, write its joints as a JointClip, zero the controls and restore. The
control values the tests expect are hand-worked from design/two-bone-ik.md sections 4.2, 4.2a and 6 (the limb is
twoboneik_basic: root (0,0,0), mid (1.8,2.4,0), end (5,0,0), pole (2.5,5,0), bones 3 and 4), never read off the rig.
Where a value can't be recovered (the pole's place along the axis with no pin, an FK control the blend hides), the test
says so and checks the joints instead.
"""
from maya import cmds
import maya.api.OpenMaya as om

from commands.test_inverse_solve import SolveTestCase
from rigtest.case import rig_name
from rigtest.jointclips import rig_root

UPPER = "upperArm_00_L_CTRL"
LOWER = "lowerArm_00_L_CTRL"
END = "hand_00_L_CTRL"
HANDLE = "armIK_00_L_CTRL"
POLE = "armPole_00_L_CTRL"
SETTINGS = "armSettings_00_L_CTRL"
LIMB_JOINTS = ("upperArm_00_M_JNT", "lowerArm_00_M_JNT", "hand_00_M_JNT")
SETTINGS_USAGE = "armSettings"
HANDLE_USAGE = "armIK"
POLE_USAGE = "armPole"


class CapturedWarnings:
    """The warnings Maya prints while it is open."""

    def __init__(self):
        self.messages = []

    def _on(self, message, message_type, _client_data):
        if message_type == om.MCommandMessage.kWarning:
            self.messages.append(message)

    def __enter__(self):
        self._callback = om.MCommandMessage.addCommandOutputCallback(self._on)
        return self

    def __exit__(self, *_args):
        om.MMessage.removeCallback(self._callback)

    def contain(self, text):
        return any(text in message for message in self.messages)


class TwoBoneIkInverse(SolveTestCase):

    def setUp(self):
        super().setUp()
        self.build_standard("twoboneik_basic", "twoboneik_basic")

    def limb(self, joints):
        """The limb's joints out of a snapshot: the marker joints follow controls the inverse sets its own way."""
        return {j: m for j, m in joints.items() if j.split(":")[-1] in LIMB_JOINTS}

    def pose(self, plugs):
        """Sets plugs ({'control.channel': value}), and returns the rest and the posed snapshots."""
        rest = self.snapshot()
        for plug, value in plugs.items():
            cmds.setAttr(rig_name(plug), value)
        return rest, self.snapshot()

    def restore(self, rest, posed, sidecar=None, keep=()):
        """Writes the posed joints, zeroes the controls (except the plugs in keep) and restores from the clip."""
        clip = self.write_clip(rest, [posed])
        kept = {plug: cmds.getAttr(rig_name(plug)) for plug in keep}
        cmds.zeroRigControls(rig_root())
        for plug, value in kept.items():
            cmds.setAttr(rig_name(plug), value)
        kwargs = {"sidecar": self.write_sidecar(sidecar)} if sidecar else {}
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip, **kwargs)

    def left(self, usage, **channels):
        return {"Left": {usage: channels}}

    # ---- IK, blend 1 ----------------------------------------------------------------------------

    def test_a_moved_handle_comes_back_and_the_joints_follow(self):
        rest, posed = self.pose({HANDLE + ".translateX": -1, HANDLE + ".translateY": 2})
        self.assertWorldTranslation(rig_name("hand_00_M_JNT"), [4, 2, 0])
        self.restore(rest, posed)
        self.assertValues(rig_name(HANDLE), {"translateX": -1, "translateY": 2, "translateZ": 0})
        self.assertJointsAt(self.limb(posed))

    def test_the_handle_takes_the_hand_joints_rotation(self):
        rest, posed = self.pose({HANDLE + ".rotateZ": 40, HANDLE + ".translateX": -1})
        self.restore(rest, posed)
        self.assertValues(rig_name(HANDLE), {"rotateZ": 40, "translateX": -1})
        self.assertJointsAt(self.limb(posed))

    def test_a_moved_start_control_moves_the_ik_root(self):
        rest, posed = self.pose({UPPER + ".translateY": 2})
        self.restore(rest, posed)
        self.assertValues(rig_name(UPPER), {"translateY": 2})
        self.assertValues(rig_name(HANDLE), {"translateX": 0, "translateY": 0})
        self.assertJointsAt(self.limb(posed))

    def test_the_pole_comes_back_on_the_side_the_elbow_bends(self):
        # The pole goes under the limb. With no pin its place along the axis isn't in the joints: it comes back at the
        # mid joint's x (1.8) at its rest height (5), on the -Y side: world (1.8,-5,0), local (-0.7,-10,0).
        rest, posed = self.pose({POLE + ".translateY": -10})
        self.restore(rest, posed)
        self.assertValues(rig_name(POLE), {"translateX": -0.7, "translateY": -10, "translateZ": 0})
        self.assertJointsAt(self.limb(posed))

    # ---- pin, roll, soft, stretch: the sidecar's values and the rig's own ---------------------------

    def test_pin_half_gives_the_pole_back_exactly(self):
        # design 4.2: handle at rest, pole at (2,3,0), pin 0.5: the mid joint is at (1.9,2.7,0). Pin is what ties the pole
        # to the joints: along = (1.9-0.5*1.8)/0.5 = 2, height = (2.7^2-1.2^2)/(0.5*(2.7+1.2)) = 3.
        rest, posed = self.pose({POLE + ".translateX": -0.5, POLE + ".translateY": -2, POLE + ".pin": 0.5})
        self.assertWorldTranslation(rig_name("lowerArm_00_M_JNT"), [1.9, 2.7, 0])
        self.restore(rest, posed, self.left(POLE_USAGE, pin=0.5))
        self.assertValues(rig_name(POLE), {"translateX": -0.5, "translateY": -2, "translateZ": 0})
        self.assertAlmostEqual(cmds.getAttr(rig_name(POLE + ".pin")), 0.5)
        self.assertJointsAt(posed)

    def test_pin_full_puts_the_pole_on_the_elbow(self):
        rest, posed = self.pose({POLE + ".translateX": -0.5, POLE + ".translateY": -2, POLE + ".pin": 1})
        self.assertWorldTranslation(rig_name("lowerArm_00_M_JNT"), [2, 3, 0])
        self.restore(rest, posed, self.left(POLE_USAGE, pin=1))
        self.assertValues(rig_name(POLE), {"translateX": -0.5, "translateY": -2})
        self.assertJointsAt(posed)

    def test_roll_from_the_sidecar_is_undone_when_placing_the_pole(self):
        # design 4.2: roll 90 turns the bend from +Y to +Z, mid (1.8,0,2.4). The pole is placed so that rolling by 90
        # puts the elbow there: back on +Y, world (1.8,5,0), local (-0.7,0,0).
        rest, posed = self.pose({HANDLE + ".roll": 90})
        self.assertWorldTranslation(rig_name("lowerArm_00_M_JNT"), [1.8, 0, 2.4])
        self.restore(rest, posed, self.left(HANDLE_USAGE, roll=90))
        self.assertValues(rig_name(POLE), {"translateX": -0.7, "translateY": 0, "translateZ": 0})
        self.assertAlmostEqual(cmds.getAttr(rig_name(HANDLE + ".roll")), 90, places=3)
        self.assertJointsAt(self.limb(posed))

    def test_without_the_roll_the_pole_goes_to_the_elbow_side_and_the_joints_still_match(self):
        # No roll in the sidecar and none in the scene: the plane is the pole's, so the pole is on +Z at its rest
        # height, world (1.8,0,5), local (-0.7,-5,5).
        rest, posed = self.pose({HANDLE + ".roll": 90})
        self.restore(rest, posed)
        self.assertValues(rig_name(POLE), {"translateX": -0.7, "translateY": -5, "translateZ": 5})
        self.assertJointsAt(self.limb(posed))

    def test_soft_alone_recovers_the_handle_past_the_wrist(self):
        # design 4.2a: soft 1/7, handle at distance 7: the hand stops at 6.632121 and the handle is at 7.
        rest, posed = self.pose({SETTINGS + ".soft": 1.0 / 7.0, HANDLE + ".translateX": 2})
        self.assertWorldTranslation(rig_name("hand_00_M_JNT"), [6.632121, 0, 0], 1e-5)
        self.restore(rest, posed, self.left(SETTINGS_USAGE, soft=1.0 / 7.0))
        self.assertValues(rig_name(HANDLE), {"translateX": 2.0}, places=4)
        self.assertJointsAt(self.limb(posed))

    def test_soft_with_stretch_reaches_the_handle(self):
        # design 4.2a: with stretch 1 the hand is at 7, the mid joint at (2.942992,1.168305,0).
        rest, posed = self.pose({SETTINGS + ".soft": 1.0 / 7.0, SETTINGS + ".stretch": 1, HANDLE + ".translateX": 2})
        self.assertWorldTranslation(rig_name("lowerArm_00_M_JNT"), [2.942992, 1.168305, 0], 1e-5)
        self.restore(rest, posed, self.left(SETTINGS_USAGE, soft=1.0 / 7.0, stretch=1))
        self.assertValues(rig_name(HANDLE), {"translateX": 2.0}, places=4)
        self.assertJointsAt(self.limb(posed))

    def test_a_stretched_source_comes_back_with_stretch_in_the_sidecar(self):
        # Handle at 8, stretch 1: bones 3*8/7 and 4*8/7, a straight limb, hand at (8,0,0).
        rest, posed = self.pose({SETTINGS + ".stretch": 1, HANDLE + ".translateX": 3})
        self.assertWorldTranslation(rig_name("hand_00_M_JNT"), [8, 0, 0])
        self.assertWorldTranslation(rig_name("lowerArm_00_M_JNT"), [24.0 / 7.0, 0, 0])
        self.restore(rest, posed, self.left(SETTINGS_USAGE, stretch=1))
        self.assertValues(rig_name(HANDLE), {"translateX": 3.0}, places=4)
        self.assertJointsAt(self.limb(posed))

    def test_a_limb_extended_near_full_reach(self):
        # Handle at 6.9 of 7, no soft: the hand is at (6.9,0,0) and the handle is recovered exactly.
        rest, posed = self.pose({HANDLE + ".translateX": 1.9})
        self.assertWorldTranslation(rig_name("hand_00_M_JNT"), [6.9, 0, 0])
        self.restore(rest, posed)
        self.assertValues(rig_name(HANDLE), {"translateX": 1.9}, places=4)
        self.assertJointsAt(self.limb(posed))

    def test_a_handle_past_full_reach_comes_back_at_the_wrist(self):
        # Handle at 8 with no stretch: the hand stops at (7,0,0). Any handle from there on gives that pose, so the
        # handle comes back at the wrist, translate 2, and the joints match.
        rest, posed = self.pose({HANDLE + ".translateX": 3})
        self.assertWorldTranslation(rig_name("hand_00_M_JNT"), [7, 0, 0])
        self.restore(rest, posed)
        self.assertValues(rig_name(HANDLE), {"translateX": 2.0}, places=4)
        self.assertJointsAt(self.limb(posed))

    def test_a_channel_the_sidecar_lacks_is_the_rigs_current_one(self):
        # Soft is set in the scene and not zeroed, and not in the sidecar: the forward solve will use it, so does the inverse.
        rest, posed = self.pose({SETTINGS + ".soft": 1.0 / 7.0, HANDLE + ".translateX": 2})
        self.restore(rest, posed, keep=(SETTINGS + ".soft",))
        self.assertValues(rig_name(HANDLE), {"translateX": 2.0}, places=4)
        self.assertJointsAt(self.limb(posed))

    def test_the_sidecar_wins_over_the_rigs_current_channel(self):
        rest, posed = self.pose({SETTINGS + ".soft": 1.0 / 7.0, HANDLE + ".translateX": 2})
        cmds.setAttr(rig_name(SETTINGS + ".soft"), 0.5)
        self.restore(rest, posed, self.left(SETTINGS_USAGE, soft=1.0 / 7.0), keep=(SETTINGS + ".soft",))
        self.assertValues(rig_name(HANDLE), {"translateX": 2.0}, places=4)
        self.assertAlmostEqual(cmds.getAttr(rig_name(SETTINGS + ".soft")), 1.0 / 7.0, places=5)
        self.assertJointsAt(self.limb(posed))

    # ---- the pole's space -----------------------------------------------------------------------

    def test_a_pole_in_ik_space_is_placed_against_the_matched_handle(self):
        # Handle moved -1 in x, pole in Ik space so it rides along: the mid joint is at x = 1.125 (law of cosines at
        # distance 4: (16+9-16)/8). The pole is at its rest height above that, world (1.125,5,0). Its Ik space moved
        # by (-1,0,0), so its local is 1.125 - (2.5 - 1) = -0.375.
        rest, posed = self.pose({POLE + ".space": 1, HANDLE + ".translateX": -1})
        self.restore(rest, posed, self.left(POLE_USAGE, space=1))
        self.assertValues(rig_name(HANDLE), {"translateX": -1})
        self.assertValues(rig_name(POLE), {"translateX": -0.375, "translateY": 0, "translateZ": 0})
        self.assertEqual(cmds.getAttr(rig_name(POLE + ".space")), 1)
        self.assertJointsAt(self.limb(posed))

    # ---- FK and the blend -----------------------------------------------------------------------

    def test_fk_comes_back_at_blend_0_and_ik_is_matched_to_it(self):
        rest, posed = self.pose({SETTINGS + ".ikFkBlend": 0, UPPER + ".rotateZ": 20, LOWER + ".rotateZ": 40})
        self.restore(rest, posed, self.left(SETTINGS_USAGE, ikFkBlend=0))
        self.assertValues(rig_name(UPPER), {"rotateZ": 20})
        self.assertValues(rig_name(LOWER), {"rotateZ": 40})
        self.assertValues(rig_name(END), {"rotateZ": 0})
        self.assertEqual(cmds.getAttr(rig_name(SETTINGS + ".ikFkBlend")), 0)
        self.assertJointsAt(self.limb(posed))
        # The IK handle and pole were matched to the FK pose, so switching to IK leaves the joints where they were.
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 1)
        self.assertJointsAt(self.limb(posed))

    def test_fk_scale_is_reproduced_at_blend_0(self):
        rest, posed = self.pose({SETTINGS + ".ikFkBlend": 0, LOWER + ".scaleX": 1.5, END + ".translateY": 0.5})
        self.restore(rest, posed, self.left(SETTINGS_USAGE, ikFkBlend=0))
        self.assertValues(rig_name(LOWER), {"scaleX": 1.5})
        self.assertValues(rig_name(END), {"translateY": 0.5})
        self.assertJointsAt(self.limb(posed))

    def test_a_blend_between_warns_that_ik_is_approximate_and_fk_matches_the_joints(self):
        # At 0.5 the joints are a mix of the two chains, so the FK controls aren't the posed FK values: they are the ones
        # that reproduce the joints. Showing FK alone proves it.
        rest, posed = self.pose({SETTINGS + ".ikFkBlend": 0.5, UPPER + ".rotateZ": 10, LOWER + ".rotateZ": 25, HANDLE + ".translateX": -1})
        with CapturedWarnings() as warnings:
            self.restore(rest, posed, self.left(SETTINGS_USAGE, ikFkBlend=0.5))
        self.assertTrue(warnings.contain("between 0 and 1"), warnings.messages)
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        self.assertJointsAt(self.limb(posed))

    def test_no_warning_for_a_pose_ik_can_make(self):
        rest, posed = self.pose({HANDLE + ".translateX": -1, POLE + ".translateY": 2})
        with CapturedWarnings() as warnings:
            self.restore(rest, posed)
        self.assertFalse(warnings.contain("IK can't"), warnings.messages)
        self.assertFalse(warnings.contain("between 0 and 1"), warnings.messages)

    # ---- sources IK can't match, and missing joints ------------------------------------------------

    def test_a_source_with_a_longer_arm_and_no_stretch_warns_and_fk_matches(self):
        # The hand is 0.5 further out than the rig's bones can reach with stretch and pin off.
        rest = self.snapshot()
        data = self.clip_data(rest, [rest])
        names = [entry["path"].split("/")[-1] for entry in data["joints"]]
        hand = names.index("hand_00_M_JNT")
        data["poses"][0][hand]["translate"][0] += 0.5
        with CapturedWarnings() as warnings:
            cmds.poseRigFromJointPose(rig_root(), jointPose=self.write("long_arm.json", data))
        self.assertTrue(warnings.contain("IK can't make"), warnings.messages)
        self.assertValues(rig_name(END), {"translateX": 0.5})

    def test_a_missing_limb_joint_takes_the_controls_from_the_sidecar(self):
        rest, posed = self.pose({HANDLE + ".translateX": -1})
        clip = self.write_clip(rest, [posed], skip=("lowerArm_00_M_JNT",))
        cmds.zeroRigControls(rig_root())
        sidecar = self.write_sidecar(self.left(HANDLE_USAGE, translateX=-1))
        with CapturedWarnings() as warnings:
            cmds.poseRigFromJointPose(rig_root(), jointPose=clip, sidecar=sidecar)
        self.assertTrue(warnings.contain("the source lacks [Left, lowerArm]"), warnings.messages)
        self.assertValues(rig_name(HANDLE), {"translateX": -1})
        self.assertValues(rig_name(UPPER), {"rotateZ": 0, "translateX": 0})

    def test_the_settings_control_comes_from_the_sidecar_only(self):
        rest, posed = self.pose({SETTINGS + ".stretch": 1, HANDLE + ".translateX": 3})
        self.restore(rest, posed, self.left(SETTINGS_USAGE, stretch=1))
        self.assertAlmostEqual(cmds.getAttr(rig_name(SETTINGS + ".stretch")), 1.0)
        self.assertValues(rig_name(SETTINGS), {"translateX": 0, "translateY": 0, "translateZ": 0})

    # ---- keys -----------------------------------------------------------------------------------

    def test_a_clip_animates_the_limb(self):
        rest = self.snapshot()
        frames = []
        for x in (-1.0, 0.0, 1.0):
            cmds.setAttr(rig_name(HANDLE + ".translateX"), x)
            frames.append(self.snapshot())
        clip = self.write_clip(rest, frames)
        cmds.zeroRigControls(rig_root())
        cmds.animateRigFromJointClip(rig_root(), jointClip=clip)
        for frame, x in zip((1, 2, 3), (-1.0, 0.0, 1.0)):
            self.assertAlmostEqual(cmds.getAttr(rig_name(HANDLE + ".translateX"), time=frame), x, places=3)
