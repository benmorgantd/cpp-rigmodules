"""ReverseFoot, stage 1 (D74, design/reverse-foot.md): contract tests, pose cases from fixtures/poses/reverse_foot, and what only the
foot does: its channels and their mirror modes, the foot control, the plugs and the ankle socket, the markers it resolves, the loop
check and the graph through the Limb's ikTarget.

The foot is the core test foot (tests/core/FootSolveTests.cpp) at a tenth of its size on the limb_arm hand at (4, 4, 0), so every
expected number is that table's, scaled. The pose cases hold the arithmetic in their descriptions. Nothing here is recorded from the rig.

Stage 2 (the toe and toe tip joints, the toe FK control, the blend) and the inverse are phases 4 and 5. The foot claims the toe and the
toe tip and does not drive them yet, so the contract's joint tests are the ones that don't need the foot to own joints.
"""
import copy
import json
import math
import os

from maya import cmds

import rigmodules.query
from commands.test_two_bone_ik_inverse import CapturedWarnings
from rigtest import config, fixtures
from rigtest.case import RigTestCase, rig_name
from rigtest.cases import add_pose_cases
from rigtest.module_contract import ModuleContract, _compute_count

FOOT = "foot_00_L_CTRL"
FOOT_MODULE = "foot_00_L_MOD"
LIMB = "arm_01_L_MOD"
HAND = "hand_00_L_JNT"
DUMMY = "dummy_00_L_JNT"
HANDLE = "armIK_00_L_CTRL"

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


class ReverseFootContract(ModuleContract, RigTestCase):
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
        """Each output computes for what it reads (D36: no output is solved for another). A channel changes the ankle socket by itself, and
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

    def test_contract_inverse_round_trip(self):
        self.skipTest("the foot's inverse, and the Limb's handle matching a raised foot, are phase 5 (design/reverse-foot.md section 10)")

    def test_contract_inverse_without_a_joint_falls_back_to_the_sidecar(self):
        self.skipTest("the foot's inverse is phase 5 (design/reverse-foot.md section 10)")

    def test_contract_inverse_round_trip_mirrored(self):
        self.skipTest("the foot's inverse, and the Limb's handle matching a raised foot, are phase 5 (design/reverse-foot.md section 10)")


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

    def test_the_foot_control_sits_at_the_ball_marker_and_is_world_aligned(self):
        """The ball marker is 1.6 ahead of and 0.8 below the hand at (4, 4, 0). The control keeps the world's axes."""
        self._build()
        self.assertEqual([round(v, 4) for v in translation(FOOT)], [4.0, 3.2, 1.6])
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
        self.assertAlmostEqual(foot[1], 3.2, places=4)
        self.assertAlmostEqual(foot[2], 1.6, places=4)

    def test_the_hand_follows_the_handle_with_the_foot_at_rest_and_adds_the_roll_on_top(self):
        self._build()
        cmds.setAttr(rig_name(HANDLE + ".translateY"), 1.0)
        self.assertWorldTranslation(rig_name(HAND), [3.0, 4.0, 0.0], 1e-4)
        # A roll of 20 turns the foot first and the handle carries the result: 0.1 * (4.989863, 3.701079) more, from the hand-worked table.
        cmds.setAttr(rig_name(FOOT + ".footRoll"), 20)
        self.assertWorldTranslation(rig_name(HAND), [3.0, 4.4989863, 0.3701079], 1e-4)

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
            ("Left", "heel"): (4, 3.2, -0.4), ("Left", "ball"): (4, 3.2, 1.6), ("Left", "bankOut"): (4.4, 3.2, 1.6),
            ("Left", "bankIn"): (3.6, 3.2, 1.6), ("Left", "toeTip"): (4, 3.2, 2.4), ("Left", "toe"): (4, 3.3, 1.8),
        }
        for markup, position in expected.items():
            for got, want in zip(listed[markup], position):
                self.assertAlmostEqual(got, want, places=5, msg=str(markup))

    def test_rotated_pivot_markers_are_a_warning_because_only_their_positions_are_read(self):
        """The fixture's markers have an identity offset on the hand, which is turned Rz 90, so none is world aligned. The foot reads
        positions only (its axes come from heel to toe tip and world up, D74), so the build warns once, naming every rotated marker."""
        with CapturedWarnings() as warnings:
            self._build()
        found = [m for m in warnings.messages if "not world aligned" in m]
        self.assertEqual(len(found), 1, warnings.messages)
        for role in ("heel", "ball", "bankOut", "bankIn", "toeTip", "toe"):
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
        self.assertAlmostEqual(translation(FOOT)[2], 1.6, places=5)   # the foot control still sits at the ball marker

    def test_the_mirrored_right_foots_markers_resolve_through_the_turned_right_hand(self):
        """The right hand is Rx(180) Rz(90): local = (wy, wx, -wz). The right foot's pivots are the left's mirrored across X = 0."""
        self.build_standard("reverse_foot_mirror", "reverse_foot_mirror", markers="reverse_foot_mirror")
        listed = {tuple(m["markup"]): m["world"][12:15] for m in json.loads(cmds.getRigMarkers(rig_name("layout_00_M_MOD")))}
        expected = {
            ("Right", "heel"): (-4, 3.2, -0.4), ("Right", "ball"): (-4, 3.2, 1.6), ("Right", "bankOut"): (-4.4, 3.2, 1.6),
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
        its ankle plug (the end socket, which follows the target). Maya reports a real cycle as a warning when it evaluates (D62 phase 4)."""
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

    def test_a_foot_whose_ik_plug_takes_the_end_socket_is_still_a_loop(self):
        data = copy.deepcopy(fixtures.load_template("reverse_foot"))
        data["rigModules"][1]["parentStructure"]["ik"] = LIMB + ".end"
        self.assertMayaError(lambda: self.build(data, self.make_skeleton("reverse_foot"), markers="reverse_foot", label="reverse_foot_loop"), "cycle")
        self.assertNothingBuilt()

    def test_a_constraint_on_the_foots_plugs_is_an_error(self):
        for plug in ("ik", "ankle"):
            with self.subTest(plug=plug):
                self.setUp()
                data = copy.deepcopy(fixtures.load_template("reverse_foot"))
                data["constraints"] = [{"name": "footSpace_00_L_CON", "target": "%s.%s" % (FOOT_MODULE, plug),
                                        "drivers": [{"socket": "layout_00_M_MOD.end"}]}]
                self.assertMayaError(lambda: self.build(data, self.make_skeleton("reverse_foot"), markers="reverse_foot", label="reverse_foot_constraint"), "constraint")
                self.assertNothingBuilt()

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
        self.assertWorldTranslation(rig_name(HAND), [4.0, 4.4989863, 0.3701079], 1e-4)


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
