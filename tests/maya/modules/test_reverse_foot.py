"""ReverseFoot (design/reverse-foot.md): contract tests with their inverse round trips, pose cases from fixtures/poses/reverse_foot,
and what only the foot does: its channels and their mirror modes, the foot control, the plugs and the ankle socket, the markers it
resolves, the loop check, the graph through the Limb's ikTarget, a foot at rest equal to the Limb alone, and the inverse (phase 5): the
gauge, the handle's space, and the match (setRigIkFkBlend): no pop in the ankle with a raised foot, and the toe held both ways. Then
ReverseFootToes (phase 4): the toe and toe tip joints, the toe FK control, the blend the foot takes from its limb (ikFkDriver), the
visibility it drives and the pivot sockets.

The foot is the core test foot (tests/core/FootSolveTests.cpp) at a tenth of its size on the limb_arm hand at (4, 4, 0): a core point
(x, y, z) is the world point (4 + x/10, 3.2 + y/10, 0.6 + z/10), except the toe pivot, which sits at the toe joint (4, 3.3, 1.8), 0.1
above the sole: the roll below the break and the ball channels turn about it, so those numbers are worked here from it, not scaled from
the core table. The toe tip joint is at the toe tip pivot (4, 3.2, 2.4). Both joints have the hand's rest rotation, Rz 90, because they
are its children in the fixture skeleton. The roll below the break turns about the toe pivot, so the hand keeps its rest distance (the
bone, 1.931321) to the planted toe. The pose cases hold the arithmetic in their descriptions, and every other expected number is worked
by hand in the test it is used in. Nothing here is recorded from the rig."""
import copy
import json
import math
import os
import tempfile

from maya import cmds

import modules.test_limb
import rigmodules.query
from rigtest import config, fixtures, jointclips
from rigtest.case import CapturedWarnings, RigTestCase, rig_name
from rigtest.cases import add_pose_cases
from rigtest.module_contract import ModuleContract, _compute_count


FOOT = "foot_00_L_CTRL"
FOOT_MODULE = "foot_00_L_MOD"
LIMB = "arm_01_L_MOD"
HAND = "hand_00_L_JNT"
DUMMY = "dummy_00_L_JNT"
HANDLE = "armIK_00_L_CTRL"
TOE = "toe_00_L_JNT"
TOE_TIP = "toeTip_00_L_JNT"
TOE_FK = "toeFK_00_L_CTRL"
# name -> (rest in degrees, mirror mode): the 11 channels of design section 8, the animator's names.
CHANNELS = {
    "footRoll": (0.0, "copy"), "breakAngle": (30.0, "copy"), "heelRaise": (0.0, "copy"), "heelSwivel": (0.0, "negate"),
    "ballRaise": (0.0, "copy"), "ballSwivel": (0.0, "negate"), "ballRoll": (0.0, "negate"), "bank": (0.0, "negate"),
    "toeRaise": (0.0, "copy"), "toeSwivel": (0.0, "negate"), "toeTap": (0.0, "copy"),
}


def translation(node):
    return cmds.xform(rig_name(node), query=True, worldSpace=True, translation=True)


def world_matrix(node):
    return cmds.xform(rig_name(node), query=True, worldSpace=True, matrix=True)


class ReverseFootContract(modules.test_limb.LimbRoundTripRules, ModuleContract, RigTestCase):
    TEMPLATE = "reverse_foot"
    SKELETON = "reverse_foot"
    MARKERS = "reverse_foot"
    MODULE_TYPE = "reverseFootModule"
    JOINT_OWNERS = {"toe_00_L_JNT": FOOT_MODULE, "toeTip_00_L_JNT": FOOT_MODULE}   # the ankle is the Limb's
    MIRROR_TEMPLATE = "reverse_foot_mirror"
    MIRROR_SKELETON = "reverse_foot_mirror"
    MIRROR_MARKERS = "reverse_foot_mirror"

    def test_contract_builds_module(self):
        self._build()
        built = sorted(n.split(":")[-1] for n in cmds.ls(rig_name("*"), type=self.MODULE_TYPE) or [])
        self.assertEqual(built, [FOOT_MODULE])

    def test_contract_one_solve_fills_every_output(self):
        """Each output computes for what it reads (no output is solved for another). A channel changes the ankle socket by itself, and
        the controls' offsets and the toe only when the Limb's end moves in answer (they read the ankle plug), so pulling the ankle socket
        computes once. The handle moving changes the control and the ankle socket, so pulling both computes twice."""
        self._build()
        module = rig_name(FOOT_MODULE)
        cmds.evaluationManager(mode="off")
        cmds.getAttr(module + ".controlOPM[0]")
        cmds.getAttr(module + ".ankleSocketMatrix[0]")
        cmds.setAttr(rig_name(FOOT + ".footRoll"), 7.0)
        cmds.dgtimer(on=True, reset=True)
        cmds.getAttr(module + ".ankleSocketMatrix[0]")
        cmds.dgtimer(off=True)
        self.assertEqual(_compute_count(module), 1, "a channel recomputes the ankle socket and nothing else")

        cmds.setAttr(rig_name(HANDLE + ".translateY"), 0.5)
        cmds.dgtimer(on=True, reset=True)
        cmds.getAttr(module + ".controlOPM[0]")
        cmds.getAttr(module + ".ankleSocketMatrix[0]")
        cmds.dgtimer(off=True)
        self.assertEqual(_compute_count(module), 2, "the handle moves the control and the ankle socket, one compute each")

    def _pose_channels(self, control):
        """The foot's handle is moved, not turned. The gauge (design/reverse-foot.md section 10) puts a handle's pitch and roll in
        footRoll and bank, and the handle takes only the heading, so a turned handle comes back with different channels and a toe the
        channels can't make exactly (about 6e-4 off for a quarter degree on each axis). A Layout's turn stays in the Layout, so the Layout
        is posed whole."""
        posed = super()._pose_channels(control)
        if rigmodules.query.get_control(control).usage == "armIK":
            for name in [n for n in posed if n.startswith("rotate")]:
                cmds.setAttr("%s.%s" % (control, name), 0.0)
                posed.pop(name)
        return posed

    def joints_cannot_give(self, control, channel):
        """The round trip poses every channel with the limb at blend 1. The Limb's are test_limb's rules (its ribbon ik
        controls are not posed, its FK controls are hidden). The toe FK control is hidden too: the toe joint is the IK toe, and the inverse
        gives the FK control that matches it. Every foot channel comes back: footRoll, bank and toeTap from the joints (given channels that
        already make the turn are kept), the rest from the sidecar."""
        return control.split(":")[-1].startswith("toeFK") or super().joints_cannot_give(control, channel)


class ReverseFootPoses(RigTestCase):
    pass


add_pose_cases(ReverseFootPoses, "reverse_foot")


class ReverseFootBehaviour(RigTestCase):

    def _build(self):
        self.build_standard("reverse_foot", "reverse_foot", markers="reverse_foot")

    def test_the_foot_control_has_the_eleven_channels_with_their_rests_and_mirror_modes(self):
        self._build()
        channels = {c.name: (c.rest, c.mirror) for c in rigmodules.query.get_control_channels(rig_name(FOOT)) if c.name != "visibility"}
        self.assertEqual(set(channels), set(CHANNELS), "the control's keyable channels are the foot's channels and nothing else")
        for name, expected in CHANNELS.items():
            self.assertAlmostEqual(channels[name][0], expected[0], places=4, msg=name)
            self.assertEqual(channels[name][1], expected[1], name)

    def test_the_foot_control_is_named_from_controlName_and_marked(self):
        self._build()
        control = rigmodules.query.get_control(rig_name(FOOT))
        self.assertEqual((control.side, control.usage), ("Left", "foot"))

    def test_a_controlName_gives_the_whole_usage(self):
        data = copy.deepcopy(fixtures.load_template("reverse_foot"))
        data["rigModules"][1]["moduleArgs"]["controlName"] = "sole"
        self.build(data, self.make_skeleton("reverse_foot"), markers="reverse_foot")
        control = rigmodules.query.get_control(rig_name("sole_00_L_CTRL"))
        self.assertEqual((control.side, control.usage), ("Left", "sole"))

    def test_the_foot_control_sits_at_the_toe_marker_and_is_world_aligned(self):
        """The toe marker, the ball of the foot, is 1.8 ahead of and 0.7 below the hand at (4, 4, 0). The control keeps the world's axes."""
        self._build()
        self.assertEqual([round(v, 4) for v in translation(FOOT)], [4.0, 3.3, 1.8])
        matrix = world_matrix(FOOT)
        for got, want in zip(matrix[:12], [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0]):
            self.assertAlmostEqual(got, want, places=5)

    def test_the_foot_control_carries_channels_only(self):
        self._build()
        control = rig_name(FOOT)
        self.assertEqual(sorted(a for a in cmds.listAttr(control, keyable=True) if a != "visibility"), sorted(CHANNELS))
        for axis in "XYZ":
            for channel in ("translate", "rotate", "scale"):
                self.assertTrue(cmds.getAttr("%s.%s%s" % (control, channel, axis), lock=True), channel + axis)

    def test_the_foot_control_follows_the_handle(self):
        self._build()
        cmds.setAttr(rig_name(HANDLE + ".translateY"), 1.0)
        # The handle's control frame is the hand's, turned 90 about Z, so its local Y is world -X.
        handle = translation(HANDLE)
        self.assertAlmostEqual(handle[0], 3.0, places=4)
        foot = translation(FOOT)
        self.assertAlmostEqual(foot[0], 3.0, places=4)
        self.assertAlmostEqual(foot[1], 3.3, places=4)
        self.assertAlmostEqual(foot[2], 1.8, places=4)

    def test_a_roll_of_20_turns_the_hand_20_degrees_about_the_worlds_x(self):
        """The hand is turned 90 about Z at rest: its axes are (0, 1, 0), (-1, 0, 0), (0, 0, 1). The foot's roll turns them 20 about
        world X: y' = y cos - z sin, z' = y sin + z cos, so the rows become (0, c, s), (-1, 0, 0), (0, -s, c)."""
        self._build()
        cmds.setAttr(rig_name(FOOT + ".footRoll"), 20)
        c, s = math.cos(math.radians(20)), math.sin(math.radians(20))
        expected = [0, c, s, 0, -1, 0, 0, 0, 0, -s, c, 0]
        for got, want in zip(world_matrix(HAND)[:12], expected):
            self.assertAlmostEqual(got, want, places=4)

    def test_the_dummy_hung_from_the_end_socket_rides_the_hand(self):
        self._build()
        cmds.setAttr(rig_name(FOOT + ".footRoll"), 20)
        hand = translation(HAND)
        dummy = translation(DUMMY)
        for axis in range(3):
            self.assertAlmostEqual(dummy[axis], hand[axis], places=4)

    def test_the_ik_plug_comes_from_the_limbs_handle_sockets_and_the_ankle_plug_from_its_end_socket(self):
        self._build()
        module = rig_name(FOOT_MODULE)
        ik = cmds.listConnections(module + ".childPlugs[0].childPlugParentMatrix", source=True, destination=False, plugs=True)
        self.assertEqual([p.split(":")[-1].split("[")[0] for p in ik], [LIMB + ".handleSocketMatrix"])
        ankle = cmds.listConnections(module + ".anklePlugMatrix", source=True, destination=False, plugs=True)
        self.assertEqual([p.split(":")[-1].split("[")[0] for p in ankle], [LIMB + ".outputSocketMatrix"])
        self.assertTrue(cmds.getAttr(module + ".anklePlugConnected"))
        target = cmds.listConnections(rig_name(LIMB + ".ikTargetMatrix"), source=True, destination=False, plugs=True)
        self.assertEqual([p.split(":")[-1] for p in target], [FOOT_MODULE + ".ankleSocketMatrix[0]"])

    def test_the_foot_module_lists_its_plugs_and_its_socket(self):
        self._build()
        module = rig_name(FOOT_MODULE)
        self.assertEqual(cmds.getAttr(module + ".childPlugs[0].childPlugName"), "ik")
        self.assertEqual(json.loads(cmds.getAttr(module + ".socketNames[0]")), ["ankle"])
        self.assertEqual(cmds.getAttr(module + ".ikFkDriver"), LIMB)

    def test_the_markers_resolve_to_the_pivots_worked_by_hand(self):
        """The hand is the ankle at (4, 4, 0). The pivots are a tenth of the core foot's offsets from it, in the hand's own frame
        (Rz 90: local = (wy, -wx, wz)), so the world position of each is the hand plus its world offset."""
        self._build()
        listed = {tuple(m["markup"]): m["world"][12:15] for m in json.loads(cmds.getRigMarkers(rig_name("layout_00_M_MOD")))}
        expected = {
            ("Left", "heel"): (4, 3.2, -0.4), ("Left", "bankOut"): (4.4, 3.2, 1.6),
            ("Left", "bankIn"): (3.6, 3.2, 1.6), ("Left", "toeTip"): (4, 3.2, 2.4), ("Left", "toe"): (4, 3.3, 1.8),
        }
        for markup, position in expected.items():
            for got, want in zip(listed[markup], position):
                self.assertAlmostEqual(got, want, places=5, msg=str(markup))

    def test_rotated_pivot_markers_are_a_warning_because_only_their_positions_are_read(self):
        """The fixture's markers have an identity offset on the hand, which is turned Rz 90, so none is world aligned. The foot reads
        positions only (its axes come from heel to toe tip and world up), so the build warns once, naming every rotated marker."""
        with CapturedWarnings() as warnings:
            self._build()
        found = [m for m in warnings.messages if "not world aligned" in m]
        self.assertEqual(len(found), 1, warnings.messages)
        for role in ("heel", "bankOut", "bankIn", "toeTip", "toe"):
            self.assertIn("[Left, %s]" % role, found[0])

    def test_world_aligned_pivot_markers_build_without_the_warning(self):
        """The same markers with the offset rotation Rz -90 (rows (0, -1, 0), (1, 0, 0), (0, 0, 1)) undo the hand's Rz 90, so each marker is
        world aligned. The positions do not change: the offset's translation row is what places a marker."""
        data = fixtures.read_json(fixtures.markers_path("reverse_foot"))
        for marker in data["markers"]:
            marker["offsetLocal"][0:3] = [0, -1, 0]
            marker["offsetLocal"][4:7] = [1, 0, 0]
        path = os.path.join(config.WORK_DIR, "reverse_foot_world_aligned_markers.json")
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(data, handle)
        joints = self.make_skeleton("reverse_foot")
        with CapturedWarnings() as warnings:
            cmds.createRigFromTemplate(*joints, template=fixtures.template_path("reverse_foot").replace("\\", "/"), asset=config.ASSET_ID,
                                       namespace=config.RIG_NAMESPACE, markers=path.replace("\\", "/"))
        self.assertFalse(warnings.contain("not world aligned"), warnings.messages)
        self.assertAlmostEqual(translation(FOOT)[2], 1.8, places=5)   # the foot control still sits at the toe marker

    def test_the_mirrored_right_foots_markers_resolve_through_the_turned_right_hand(self):
        """The right hand is Rx(180) Rz(90): local = (wy, wx, -wz). The right foot's pivots are the left's mirrored across X = 0."""
        self.build_standard("reverse_foot_mirror", "reverse_foot_mirror", markers="reverse_foot_mirror")
        listed = {tuple(m["markup"]): m["world"][12:15] for m in json.loads(cmds.getRigMarkers(rig_name("layout_00_M_MOD")))}
        expected = {
            ("Right", "heel"): (-4, 3.2, -0.4), ("Right", "bankOut"): (-4.4, 3.2, 1.6),
            ("Right", "bankIn"): (-3.6, 3.2, 1.6), ("Right", "toeTip"): (-4, 3.2, 2.4), ("Right", "toe"): (-4, 3.3, 1.8),
        }
        for markup, position in expected.items():
            for got, want in zip(listed[markup], position):
                self.assertAlmostEqual(got, want, places=5, msg=str(markup))

    def test_the_mirrored_foot_follows_the_other_sides_limb(self):
        self.build_standard("reverse_foot_mirror", "reverse_foot_mirror", markers="reverse_foot_mirror")
        driver = cmds.getAttr(rig_name("foot_00_R_MOD.ikFkDriver"))
        self.assertEqual(driver, "arm_00_R_MOD")

    def test_the_graph_has_no_cycle_in_either_evaluation_mode(self):
        """Limb to foot to Limb is acyclic at the plug level: the ik socket never reads ikTarget, and the foot's ankle socket never reads
        its ankle plug (the end socket, which follows the target). Maya reports a real cycle as a warning when it evaluates."""
        self._build()
        mode = cmds.evaluationManager(query=True, mode=True)[0]
        self.addCleanup(cmds.evaluationManager, mode=mode)
        for evaluation in ("off", "parallel"):
            with self.subTest(mode=evaluation):
                cmds.evaluationManager(mode=evaluation)
                with CapturedWarnings() as warnings:
                    cmds.setAttr(rig_name(FOOT + ".footRoll"), 20 if evaluation == "off" else 25)
                    cmds.dgdirty(allPlugs=True)
                    hand = translation(HAND)
                    cmds.getAttr(rig_name(DUMMY + ".worldMatrix[0]"))
                self.assertFalse(warnings.contain("ycle"), warnings.messages)
                self.assertTrue(abs(hand[1] - 4.0) > 0.1, "the foot's roll moved the hand")

    def test_the_foot_claims_the_toe_joints_and_leaves_the_ankle_to_the_limb(self):
        """The foot's plan claims the toe and the toe tip (a joint two modules drive is an error) and only reads the ankle, which the
        Limb drives. Nothing drives the toe joints until stage 2."""
        self._build()
        hand_sources = cmds.listConnections(rig_name(HAND + ".offsetParentMatrix"), source=True, destination=False) or []
        self.assertEqual([s.split(":")[-1] for s in hand_sources], [LIMB])

    def test_undo_and_redo_rebuild_the_foot(self):
        self._build()
        cmds.undo()
        self.assertEqual(cmds.ls(type="reverseFootModule"), [])
        cmds.redo()
        self.assertTrue(cmds.objExists(rig_name(FOOT)))
        self.assertTrue(cmds.getAttr(rig_name(FOOT_MODULE + ".anklePlugConnected")))
        cmds.setAttr(rig_name(FOOT + ".footRoll"), 20)
        self.assertWorldTranslation(rig_name(HAND), [4.0, 4.573421, 0.347967], 1e-4)   # about the toe pivot, as worked above


class ReverseFootMatchesTheLimb(RigTestCase):
    """The pair stays checked (design/reverse-foot.md section 13): a Limb with a foot at rest gives the joints of the same Limb alone, so the foot
    adds nothing until a channel moves. The foot hangs from the handle, so the handle's poses carry it with the limb."""

    LIMB_AND_BINDS = ["upperArm_00_L_JNT", "lowerArm_00_L_JNT", "hand_00_L_JNT"] + ["bind%d_00_L_JNT" % i for i in range(1, 10)]
    POSES = [
        [(HANDLE, "translateY", 1.0)],
        [(HANDLE, "translateY", 2.0), (HANDLE, "rotateZ", 20.0)],
        [(HANDLE, "translateY", 4.0), ("armPole_00_L_CTRL", "translateX", 2.0)],
    ]

    def _worlds(self):
        return {joint: cmds.xform(rig_name(joint), query=True, worldSpace=True, matrix=True) for joint in self.LIMB_AND_BINDS}

    def test_a_foot_at_rest_leaves_the_limb_as_it_is_alone(self):
        results = {}
        for label, template, markers in (("alone", "limb_arm", None), ("foot", "reverse_foot", "reverse_foot")):
            cmds.file(new=True, force=True)
            self.build(template, self.make_skeleton("reverse_foot" if markers else "limb_arm"), markers=markers)
            for pose in self.POSES:
                for control, channel, value in pose:
                    cmds.setAttr("%s.%s" % (rig_name(control), channel), value)
                results.setdefault(label, []).append(self._worlds())
                for control, channel, value in pose:
                    default = cmds.attributeQuery(channel, node=rig_name(control), listDefault=True)[0]
                    cmds.setAttr("%s.%s" % (rig_name(control), channel), default)
        for index in range(len(self.POSES)):
            for joint in self.LIMB_AND_BINDS:
                for x, y in zip(results["alone"][index][joint], results["foot"][index][joint]):
                    self.assertAlmostEqual(x, y, places=5, msg="pose %d %s: the foot at rest changed the limb" % (index, joint))


class ReverseFootInverse(RigTestCase):
    """The foot from the joints (design/reverse-foot.md section 10): the author's gauge for footRoll and bank, the pre-pass
    that gives the Limb's handle the foot's motion, and the match that puts no pop in the ankle. Every expected value is the posed one:
    the ankle and toe come back exactly whatever the channels, so what is checked is which channels the inverse chose."""

    def _build_and_pose(self, channels, handle=None):
        self.build_standard("reverse_foot", "reverse_foot", markers="reverse_foot")
        for name, value in channels.items():
            cmds.setAttr(rig_name(FOOT + "." + name), value)
        for name, value in (handle or {}).items():
            cmds.setAttr(rig_name(HANDLE + "." + name), value)
        return jointclips.snapshot()

    def _restore(self, posed, sidecar_controls=None):
        """Zeroes the controls and restores them from the posed joints, with a sidecar holding sidecar_controls when given."""
        root = jointclips.rig_root()
        folder = tempfile.mkdtemp()
        cmds.zeroRigControls(root)
        rest_snapshot = jointclips.snapshot()
        clip = jointclips.write_json(os.path.join(folder, "clip.json"), jointclips.clip_data(rest_snapshot, [posed]))
        flags = {"jointPose": clip.replace("\\", "/")}
        if sidecar_controls is not None:
            sidecar = jointclips.write_json(os.path.join(folder, "sidecar.json"), {"formatVersion": 1, "controls": sidecar_controls})
            flags["sidecar"] = sidecar.replace("\\", "/")
        cmds.poseRigFromJointPose(root, **flags)

    def _assert_joints(self, posed):
        for joint, matrix in posed.items():
            after = jointclips.world(joint)
            self.assertTrue(after.isEquivalent(matrix, 1e-4), "%s is off after the restore\n  posed %s\n  after %s" % (joint, list(matrix), list(after)))

    def test_a_clip_without_a_sidecar_gives_footRoll_and_bank_and_the_handle(self):
        # A heel roll with a bank decomposes exactly (the bank turns the ankle before the heel), so the pose comes back as it was made.
        # The handle is only moved: its rest follows the hand, so a turn of it would have a pitch the gauge rightly puts in footRoll (the
        # heading's own case is footGaugePutsPitchInFootRollAndRollInBank in tests/core).
        posed = self._build_and_pose({"footRoll": -20.0, "bank": 8.0, "toeTap": 12.0}, {"translateY": 0.3, "translateZ": -0.2})
        self._restore(posed)
        self._assert_joints(posed)
        restored = {name: cmds.getAttr(rig_name(FOOT + "." + name)) for name in ("footRoll", "bank", "toeTap")}
        for name, value in (("footRoll", -20.0), ("bank", 8.0), ("toeTap", 12.0)):
            self.assertAlmostEqual(restored[name], value, places=3, msg="%s: restored %s" % (name, restored))
        self.assertAlmostEqual(cmds.getAttr(rig_name(HANDLE + ".translateY")), 0.3, places=3)
        self.assertAlmostEqual(cmds.getAttr(rig_name(HANDLE + ".translateZ")), -0.2, places=3)

    def test_a_tilted_and_scaled_layout_stays_in_the_layout(self):
        # The author's choice (2026-11-25): footRoll and bank are read in the space the Limb's handle hangs in, so a Layout turned off the
        # ground and scaled gives back the foot's own channels, and the handle, which carries the Layout's scale in the scene, puts the
        # ankle and toe back exactly. The Layout has no joints, so the sidecar carries it, as in a real restore. The pre-pass runs after the
        # Layout is solved: in the old order it saw no Layout and put its tilt into footRoll and bank.
        layout = {"rotateX": 15.0, "rotateY": 30.0, "rotateZ": 10.0, "scaleX": 1.2, "scaleY": 1.2, "scaleZ": 1.2}
        self.build_standard("reverse_foot", "reverse_foot", markers="reverse_foot")
        for name, value in layout.items():
            cmds.setAttr(rig_name("layout_00_M_CTRL." + name), value)
        for name, value in (("footRoll", -20.0), ("bank", 8.0), ("toeTap", 12.0)):
            cmds.setAttr(rig_name(FOOT + "." + name), value)
        cmds.setAttr(rig_name(HANDLE + ".translateY"), 0.3)
        posed = jointclips.snapshot()
        self._restore(posed, {"Center": {"layout_0": layout}})
        self._assert_joints(posed)
        for name, value in (("footRoll", -20.0), ("bank", 8.0), ("toeTap", 12.0)):
            self.assertAlmostEqual(cmds.getAttr(rig_name(FOOT + "." + name)), value, places=3, msg=name)
        self.assertAlmostEqual(cmds.getAttr(rig_name(HANDLE + ".translateY")), 0.3, places=3)

    def test_a_sidecars_raise_is_not_counted_again_in_footRoll(self):
        # The author's gauge (2026-11-25): footRoll takes only the pitch the sidecar's channels miss, and the whole pose comes back.
        posed = self._build_and_pose({"footRoll": 25.0, "ballRaise": 10.0})
        self._restore(posed, {"Left": {"foot": {"ballRaise": 10.0}}})
        self._assert_joints(posed)
        self.assertAlmostEqual(cmds.getAttr(rig_name(FOOT + ".footRoll")), 25.0, places=3)
        self.assertAlmostEqual(cmds.getAttr(rig_name(FOOT + ".ballRaise")), 10.0, places=3)

        # Without the sidecar the raise is folded into footRoll (35). That is past the break, so 5 of it turns the toe tip pivot instead
        # of the toe pivot: the ankle still lands exactly (the handle takes the difference), but the toe points a little differently, because
        # these channels can't make a ball raised past the break.
        self._restore(posed)
        hand = next(joint for joint in posed if joint.endswith(HAND))
        self.assertTrue(jointclips.world(hand).isEquivalent(posed[hand], 1e-4), "the ankle lands exactly")
        self.assertAlmostEqual(cmds.getAttr(rig_name(FOOT + ".footRoll")), 35.0, places=3)
        self.assertAlmostEqual(cmds.getAttr(rig_name(FOOT + ".ballRaise")), 0.0, places=3)

    def test_a_match_to_ik_with_a_raised_foot_puts_no_pop_in_the_ankle(self):
        # The open item of design/limb.md section 8: the match places the handle so the foot's motion lands the ankle on the FK one.
        self._build_and_pose({"footRoll": 20.0, "bank": -6.0})
        limb = rig_name(LIMB)
        cmds.setRigIkFkBlend(limb, blend=0)
        cmds.setAttr(rig_name("upperArm_00_L_CTRL.rotateZ"), 15.0)
        before = jointclips.world(rig_name(HAND))
        cmds.setRigIkFkBlend(limb, blend=1)
        after = jointclips.world(rig_name(HAND))
        self.assertTrue(after.isEquivalent(before, 1e-4), "the ankle popped\n  FK %s\n  IK %s" % (list(before), list(after)))
        self.assertAlmostEqual(cmds.getAttr(rig_name(FOOT + ".footRoll")), 20.0, places=4, msg="the match keeps the foot's channels")

    def _assert_toe_holds(self, before, message):
        for joint in (TOE, TOE_TIP, HAND):
            after = jointclips.world(rig_name(joint))
            self.assertTrue(after.isEquivalent(before[joint], 1e-4), "%s %s\n  before %s\n  after  %s" % (joint, message, list(before[joint]), list(after)))

    def _toe_worlds(self):
        return {joint: jointclips.world(rig_name(joint)) for joint in (TOE, TOE_TIP, HAND)}

    def test_a_match_to_fk_puts_the_toe_fk_control_on_the_toe(self):
        # The toe tapped and the foot rolled in IK: the match to FK writes the toe FK control, so the toe and toe tip hold.
        self._build_and_pose({"footRoll": -15.0, "bank": 5.0, "toeTap": 25.0})
        before = self._toe_worlds()
        with CapturedWarnings() as warnings:
            cmds.setRigIkFkBlend(rig_name(LIMB), blend=0)
        self._assert_toe_holds(before, "popped in the match to FK")
        self.assertEqual(warnings.messages, [])
        cmds.undo()
        self.assertAlmostEqual(cmds.getAttr(rig_name(TOE_FK + ".rotateX")), 0.0, places=6, msg="one undo takes the toe FK control back")
        self.assertAlmostEqual(cmds.getAttr(rig_name(TOE_FK + ".rotateY")), 0.0, places=6)
        self.assertAlmostEqual(cmds.getAttr(rig_name(TOE_FK + ".rotateZ")), 0.0, places=6)

    def test_a_match_to_ik_gives_toeTap_the_fk_toes_pitch(self):
        # Matched to FK, the FK toe holds a toeTap of 20. With toeTap zeroed while in FK (it moves nothing there), the match back to IK
        # solves it from the FK toe: 20 again, with the toe where it was.
        self._build_and_pose({"footRoll": 10.0, "toeTap": 20.0})
        limb = rig_name(LIMB)
        cmds.setRigIkFkBlend(limb, blend=0)
        cmds.setAttr(rig_name(FOOT + ".toeTap"), 0.0)
        before = self._toe_worlds()
        with CapturedWarnings() as warnings:
            cmds.setRigIkFkBlend(limb, blend=1)
        self._assert_toe_holds(before, "popped in the match to IK")
        self.assertAlmostEqual(cmds.getAttr(rig_name(FOOT + ".toeTap")), 20.0, places=3)
        self.assertAlmostEqual(cmds.getAttr(rig_name(FOOT + ".footRoll")), 10.0, places=4, msg="the match keeps the other channels")
        self.assertEqual(warnings.messages, [])

    def test_a_match_to_ik_warns_when_the_fk_toe_is_turned_off_its_pitch(self):
        # A toe FK control turned on all three axes has a roll and a swivel that toeTap's pitch can't hold.
        self._build_and_pose({})
        limb = rig_name(LIMB)
        cmds.setRigIkFkBlend(limb, blend=0)
        for axis in "XYZ":
            cmds.setAttr(rig_name(TOE_FK + ".rotate" + axis), 15.0)
        with CapturedWarnings() as warnings:
            cmds.setRigIkFkBlend(limb, blend=1)
        self.assertTrue(warnings.contain("toeTap can't hold"), warnings.messages)


SETTINGS = "armSettings_00_L_CTRL"
TOE_REST = (4.0, 3.3, 1.8)
BONE = 1.9313208   # |toe - hand| at rest: |(0, -0.7, 1.8)|


class ReverseFootToes(RigTestCase):

    def _build(self):
        self.build_standard("reverse_foot", "reverse_foot", markers="reverse_foot")

    def world(self, node):
        return cmds.xform(rig_name(node), query=True, worldSpace=True, translation=True)

    def assertWorld(self, node, position, tolerance=1e-4):
        actual = self.world(node)
        self.assertTrue(all(abs(a - e) <= tolerance for a, e in zip(actual, position)), "%s: expected %s, got %s" % (node, position, actual))

    def test_the_foot_and_toe_fk_sockets_carry_their_controls_worlds(self):
        """`foot` and `toeFk` (outputSocketMatrix elements 8 and 9) are the foot control's and the toe FK control's world matrices, with
        the handle moved (the foot control rides it) and the toe FK control turned."""
        self._build()
        cmds.setAttr(rig_name(HANDLE + ".translateX"), 1)
        cmds.setAttr(rig_name(TOE_FK + ".rotateX"), 20)
        for control, element in ((FOOT, 8), (TOE_FK, 9)):
            socket = cmds.getAttr(rig_name(FOOT_MODULE) + ".outputSocketMatrix[%d]" % element)
            world = cmds.xform(rig_name(control), query=True, worldSpace=True, matrix=True)
            for a, b in zip(socket, world):
                self.assertAlmostEqual(a, b, places=4, msg=control)

    def test_the_plugs_dag_parents_place_the_foot_control_and_the_toe_fk_control(self):
        """The ik plug carries the foot control and the ankle plug the toe FK control, so the template's dagParent places each in the
        outliner: `"ik": "arm_01_L_MOD.ik"` puts the foot control under the IK handle, and `"ankle": "arm_01_L_MOD.end"` puts the toe FK
        control under the FK hand (the end socket carries the FK end control's transform). The DAG is organization only (D1)."""
        self._build()
        for control, parent in ((FOOT, HANDLE), (TOE_FK, "hand_00_L_CTRL")):
            parents = cmds.listRelatives(rig_name(control), parent=True) or []
            self.assertEqual([p.split(":")[-1] for p in parents], [parent], control)

    def test_a_leg_that_cannot_reach_keeps_the_bone_length_and_aims_at_the_pivot(self):
        """The handle goes 10 up (its local X is the world's Y) with the foot at rest: the toe pivot would be at (4, 13.3, 1.8), far beyond the
        arm's reach of 8. The hand stops short, and the toe stays one bone length from it, on the line to where the pivot would be."""
        self._build()
        cmds.setAttr(rig_name(HANDLE + ".translateX"), 10)
        hand = self.world(HAND)
        wanted = (4.0, 13.3, 1.8)
        toward = [w - h for w, h in zip(wanted, hand)]
        length = math.sqrt(sum(v * v for v in toward))
        self.assertGreater(length, 3 * BONE, "the pivot is far from the hand: the limb can't reach")
        self.assertWorld(TOE, [h + v * BONE / length for h, v in zip(hand, toward)])
        toe = self.world(TOE)
        self.assertAlmostEqual(math.sqrt(sum((t - h) ** 2 for t, h in zip(toe, hand))), BONE, places=4)

    def test_the_blend_hides_the_controls_it_makes_useless(self):
        self._build()

        def visible(control):
            # `shown`, not visibility: visibility inherits through the DAG, and a control can be a DAG parent.
            return cmds.getAttr(rig_name(control + ".shown"))

        self.assertEqual((visible(FOOT), visible(TOE_FK)), (True, False))   # IK: the channels drive and the toe FK control hides
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0.5)
        self.assertEqual((visible(FOOT), visible(TOE_FK)), (True, True))
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0)
        self.assertEqual((visible(FOOT), visible(TOE_FK)), (False, True))   # FK: the foot's channels have nothing to move

    def test_the_foot_follows_the_limbs_blend_and_an_animator_cannot_key_it(self):
        self._build()
        module = rig_name(FOOT_MODULE)
        self.assertSource(module + ".ikFkBlend", rig_name(LIMB) + ".ikFkBlend")
        cmds.setAttr(rig_name(SETTINGS + ".ikFkBlend"), 0.25)
        self.assertAlmostEqual(cmds.getAttr(module + ".ikFkBlend"), 0.25)
        self.assertFalse(cmds.getAttr(module + ".ikFkBlend", keyable=True))
        self.assertFalse(cmds.getAttr(module + ".ikFkBlend", channelBox=True))
        self.assertNotIn("ikFkBlend", cmds.listAttr(rig_name(FOOT), keyable=True))
        self.assertNotIn("ikFkBlend", cmds.listAttr(rig_name(TOE_FK), keyable=True))

    def test_the_mirrored_foot_follows_the_other_sides_limb(self):
        self.build_standard("reverse_foot_mirror", "reverse_foot_mirror", markers="reverse_foot_mirror")
        right = rig_name("foot_00_R_MOD")
        left = rig_name(FOOT_MODULE)
        self.assertEqual(cmds.getAttr(right + ".ikFkDriver"), "arm_00_R_MOD")   # the mirror template's limb is arm_00_L_MOD
        self.assertSource(right + ".ikFkBlend", rig_name("arm_00_R_MOD") + ".ikFkBlend")
        self.assertSource(left + ".ikFkBlend", rig_name("arm_00_L_MOD") + ".ikFkBlend")
        cmds.setAttr(rig_name("armSettings_00_R_CTRL.ikFkBlend"), 0.25)
        self.assertAlmostEqual(cmds.getAttr(right + ".ikFkBlend"), 0.25)
        self.assertAlmostEqual(cmds.getAttr(left + ".ikFkBlend"), 1.0, msg="the left foot keeps following the left limb")

    def test_the_toe_fk_control_turns_and_nothing_else(self):
        self._build()
        self.assertEqual(sorted(a for a in cmds.listAttr(rig_name(TOE_FK), keyable=True) if a != "visibility"), ["rotateX", "rotateY", "rotateZ"])
        markup = json.loads(cmds.getAttr(rig_name(TOE_FK + ".controlMarkup")))
        self.assertEqual((markup["side"], markup["usage"]), ("Left", "toeFK"))

    def test_a_numbered_toe_usage_puts_fk_before_the_number(self):
        """A toe joint marked ball_00 (the solomon asset's) gives the toe FK usage ballFK_00 and the control ballFK_00_L_CTRL. Before the fix
        it gave ball_00FK, which is not camelCase, and the build failed validation."""
        joints = self.make_skeleton("reverse_foot")
        index = [i for i, j in enumerate(joints) if j.endswith(TOE)][0]
        joints[index] = ":" + cmds.rename(joints[index], "skel:ball_00_L_JNT")
        cmds.rigJointMarkup(joints[index], edit=True, usage="ball_00")
        template = copy.deepcopy(fixtures.load_template("reverse_foot"))
        template["rigModules"][1]["moduleArgs"]["joints"][1] = ["Left", "ball_00"]
        self.build(template, joints, markers="reverse_foot")
        control = rig_name("ballFK_00_L_CTRL")
        self.assertTrue(cmds.objExists(control))
        markup = json.loads(cmds.getAttr(control + ".controlMarkup"))
        self.assertEqual((markup["side"], markup["usage"]), ("Left", "ballFK_00"))

    def test_the_pivots_and_the_toes_are_sockets_with_their_worlds(self):
        """The pivot sockets follow the chain order (heel, toeTip, bankOut, bankIn, toe). At a roll of 20 (break 30): the heel does not
        turn, so its socket stays at the heel marker (4, 3.2, -0.4). The toe pivot socket turns 20 about X at the toe pivot (4, 3.3, 1.8).
        The toe tip pivot is before the toe in the chain, so nothing turns it: it stays at (4, 3.2, 2.4), not turned. Past the break (roll 50)
        the toe tip turns 20 about its own position, which does not move it. The roles toe and toeTip are named toePivot and toeTipPivot,
        because toe and toeTip are the bind joints' sockets. There is no ball socket (the ball pivot was removed on 2026-11-25)."""
        self._build()
        cmds.setAttr(rig_name(FOOT + ".footRoll"), 20)
        module = rig_name(FOOT_MODULE)
        names = [json.loads(cmds.getAttr("%s.socketNames[%d]" % (module, i)))[0] for i in range(10)]
        self.assertEqual(names, ["ankle", "toe", "toeTip", "heel", "toeTipPivot", "bankOut", "bankIn", "toePivot", "foot", "toeFk"])
        self.assertEqual(cmds.getAttr(module + ".socketNames", size=True), 10)

        def socket(index):
            matrix = cmds.getAttr("%s.outputSocketMatrix[%d]" % (module, index))
            return matrix[12:15], matrix

        def assertAt(index, expected):
            position, _ = socket(index)
            self.assertTrue(all(abs(a - e) < 1e-4 for a, e in zip(position, expected)), "socket %d: expected %s, got %s" % (index, expected, position))

        assertAt(3, (4.0, 3.2, -0.4))
        self.assertAlmostEqual(socket(3)[1][5], 1.0, places=4, msg="the heel socket is not turned")
        assertAt(7, TOE_REST)
        self.assertAlmostEqual(socket(7)[1][6], math.sin(math.radians(20)), places=4, msg="the toe pivot socket is turned 20 about X")
        assertAt(4, (4.0, 3.2, 2.4))
        self.assertAlmostEqual(socket(4)[1][5], 1.0, places=4, msg="the toe tip pivot socket is not turned below the break")
        cmds.setAttr(rig_name(FOOT + ".footRoll"), 50)
        assertAt(4, (4.0, 3.2, 2.4))   # past the break the toe tip turns about itself and stays on the ground
        self.assertAlmostEqual(socket(4)[1][6], math.sin(math.radians(20)), places=4, msg="the toe tip pivot socket is turned 20 past the break")
        cmds.setAttr(rig_name(FOOT + ".footRoll"), 20)
        # The toe and toe tip sockets are the joints' worlds.
        assertAt(1, self.world(TOE))
        assertAt(2, self.world(TOE_TIP))

    def test_the_toe_joints_are_the_foots_and_the_ankle_the_limbs(self):
        self._build()
        for joint in (TOE, TOE_TIP):
            self.assertOpmDrivenBy(rig_name(joint), FOOT_MODULE)
        self.assertOpmDrivenBy(rig_name(HAND), LIMB)

    def test_the_graph_stays_acyclic_with_the_toe_reading_the_end_socket(self):
        """The toe reads the Limb's end socket through the ankle plug, and the ankle socket that feeds the Limb never does: a cold evaluation
        in DG and in parallel mode gives the same toe."""
        self._build()
        for evaluation in ("off", "parallel"):
            cmds.evaluationManager(mode=evaluation)
            cmds.setAttr(rig_name(FOOT + ".footRoll"), 20)
            cmds.dgdirty(allPlugs=True)
            self.assertWorld(HAND, (4.0, 4.573421, 0.347967))   # worked in the toe_tap_25_with_roll_20 pose case
            self.assertWorld(TOE, TOE_REST)
            cmds.setAttr(rig_name(FOOT + ".footRoll"), 0)
