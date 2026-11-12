"""Tests every module must pass. A module's test file supplies data, the mixin supplies the tests.

    class FkChainContract(ModuleContract, RigTestCase):
        TEMPLATE = "fkchain_three"
        SKELETON = "fk_three"
        MODULE_TYPE = "fkChainModule"
        JOINT_OWNERS = {"a_jnt": "fkchain_1", ...}
        MIRROR_TEMPLATE = "mirror_arms"   # a template with this module marked "mirror": true (D28)
        MIRROR_SKELETON = "mirror_arms"   # its skeleton, with joints for both sides

A module that can only be Center sets MIRROR_TEMPLATE = False. Leaving it unset fails the mirror tests.
"""
import json
import os
import re
import tempfile

from maya import cmds
import maya.api.OpenMaya as om

import rigmodules.query
from rigtest import config, jointclips
from rigtest.case import rig_name


def _world_matrix(node):
    return om.MSelectionList().add(node).getDagPath(0).inclusiveMatrix()


OUTPUT_ARRAYS = ("controlOPM", "jointOPM", "outputSocketMatrix")


def _compute_count(module):
    """How many times the module computed since dgtimer was reset, from dgtimer's report."""
    report = os.path.join(tempfile.mkdtemp(), "dgtimer.txt")
    cmds.dgtimer(query=True, outputFile=report)
    with open(report) as handle:
        text = handle.read()
    # The report lists a node in more than one section, some without its count, so take the largest.
    counts = re.findall(r"(\d+)\s+\w+\s+%s\s" % re.escape(module.lstrip(":")), text)
    return max((int(c) for c in counts), default=0)


def assert_one_solve_fills_every_output(testcase, module):
    """After an input changes, pulling every connected output array element computes the module once (D36): the
    same inputs dirty all of them, so one solve writes and cleans them all."""
    outputs = [plug for plug in (cmds.listConnections(module, source=False, destination=True, connections=True, plugs=True) or [])[::2]
               if plug.split(".")[-1].split("[")[0] in OUTPUT_ARRAYS]
    testcase.assertGreater(len(set(p.split("[")[0] for p in outputs)), 1, "%s has too few connected outputs to compare" % module)
    cmds.evaluationManager(mode="off")
    control = (cmds.listConnections(module, source=True, destination=False, type="rigControlNode") or [None])[0]
    testcase.assertIsNotNone(control, "%s has no control to pose" % module)
    cmds.setAttr(control + ".rotateX", 7.0)
    cmds.dgtimer(on=True, reset=True)
    for plug in outputs:
        cmds.getAttr(plug)
    cmds.dgtimer(off=True)
    count = _compute_count(module)
    testcase.assertEqual(count, 1, "%s computed %d times to fill %s, expected 1" % (module, count, outputs))


class ModuleContract:
    TEMPLATE = None       # fixture template that builds the module
    SKELETON = None       # fixture skeleton the template needs
    MODULE_TYPE = None    # Maya node type of the module
    JOINT_OWNERS = {}     # joint short name -> module name that must drive its offsetParentMatrix
    MIRROR_TEMPLATE = None  # fixture template with a "mirror": true entry of this module, or False for a Center-only module
    MIRROR_SKELETON = None  # fixture skeleton for MIRROR_TEMPLATE, with joints for both sides

    def _build(self):
        return self.build_standard(self.TEMPLATE, self.SKELETON)

    def test_contract_builds_module(self):
        self._build()
        modules = sorted(set(self.JOINT_OWNERS.values()))
        built = sorted(n.split(":")[-1] for n in cmds.ls(rig_name("*"), type=self.MODULE_TYPE) or [])
        self.assertEqual(built, modules, "%s nodes built: expected %s, got %s" % (self.MODULE_TYPE, modules, built))

    def test_contract_joints_keep_rest_pose(self):
        joints = self.make_skeleton(self.SKELETON)
        before = {j.split(":")[-1]: _world_matrix(j) for j in joints}
        self.build(self.TEMPLATE, joints)
        for short, matrix in before.items():
            after = _world_matrix(rig_name(short))
            if not after.isEquivalent(matrix, 1e-4):
                self.fail("%s moved when the rig was built\n  before %s\n  after  %s" % (short, list(matrix), list(after)))

    def test_contract_module_drives_its_joints(self):
        self._build()
        for joint, module in self.JOINT_OWNERS.items():
            self.assertOpmDrivenBy(rig_name(joint), rig_name(module))

    def test_contract_nothing_inherits_transform(self):
        self._build()
        nodes = (cmds.ls(rig_name("*"), type="rigControlNode") or []) + (cmds.ls(rig_name("*"), type="joint") or [])
        inheriting = [n for n in nodes if cmds.getAttr(n + ".inheritsTransform")]
        self.assertEqual(inheriting, [], "inheritsTransform must be off on every control and joint: %s" % inheriting)

    def test_contract_zero_returns_to_the_build_pose(self):
        """The animation interface (D34): every control's channels can be queried, which fails for a custom
        channel with no mirror mode, and their rests together are the pose the rig was built in."""
        self._build()
        joints = cmds.ls(rig_name("*"), type="joint")
        rest = {j: _world_matrix(j) for j in joints}
        for control in cmds.ls(rig_name("*"), type="rigControlNode"):
            for channel in rigmodules.query.get_control_channels(control):
                if channel.type in ("enum", "bool"):
                    continue
                value = channel.rest + 0.25
                if channel.max is not None:
                    value = min(value, channel.max)
                cmds.setAttr("%s.%s" % (control, channel.name), value)
        self.assertTrue(any(not _world_matrix(j).isEquivalent(rest[j], 1e-4) for j in joints),
                        "posing every channel moved no joint, so the test proves nothing")

        cmds.zeroRigControls(cmds.ls(rig_name("*"), type="rigRootModule")[0])
        for joint in joints:
            after = _world_matrix(joint)
            if not after.isEquivalent(rest[joint], 1e-4):
                self.fail("%s is off its build pose after zeroRigControls\n  built %s\n  after %s"
                          % (joint, list(rest[joint]), list(after)))

    def test_contract_rotate_order_is_locked(self):
        """A module owns its controls' rotate orders (D40): set at creation, locked after the build, and
        still locked after undo and redo."""
        self._build()
        controls = cmds.ls(rig_name("*"), type="rigControlNode")
        self.assertTrue(controls)
        for phase in ("built", "redone"):
            for control in controls:
                self.assertTrue(cmds.getAttr(control + ".rotateOrder", lock=True), "%s rotateOrder is not locked (%s)" % (control, phase))
            if phase == "built":
                cmds.undo()
                cmds.redo()
                controls = cmds.ls(rig_name("*"), type="rigControlNode")

    def test_contract_undo_redo_round_trip(self):
        self._build()
        built = sorted(cmds.ls(rig_name("*"), long=True))
        positions = {j: cmds.xform(j, q=True, ws=True, t=True) for j in cmds.ls(rig_name("*"), type="joint")}
        cmds.undo()
        self.assertEqual(cmds.ls(type=config.RIG_NODE_TYPES), [], "undo left rig nodes behind")
        cmds.redo()
        self.assertEqual(sorted(cmds.ls(rig_name("*"), long=True)), built, "redo did not rebuild the same nodes")
        for joint, position in positions.items():
            self.assertWorldTranslation(joint, position)

    def test_contract_one_solve_fills_every_output(self):
        self._build()
        for module in cmds.ls(rig_name("*"), type=self.MODULE_TYPE):
            assert_one_solve_fills_every_output(self, module)

    # ---- the inverse solve (D41) -----------------------------------------------------------------

    def _pose_every_transform(self):
        """Moves every plain channel of every control off rest, and returns {control: transform values}."""
        for control in cmds.ls(rig_name("*"), type="rigControlNode"):
            self._pose_channels(control)
        return {c: jointclips.values_of(c) for c in cmds.ls(rig_name("*"), type="rigControlNode")}

    def _sidecar_for_what_joints_cannot_give(self, root, folder):
        """A pose file holding the posed rig's Layout controls (no joints can give them) and its non-transform channels."""
        path = os.path.join(folder, "full.json")
        cmds.saveRigPose(root, file=path.replace("\\", "/"))
        with open(path, encoding="utf-8") as handle:
            controls = json.load(handle)["controls"]
        transforms = jointclips.CHANNELS
        kept = {}
        for side, usages in controls.items():
            for usage, channels in usages.items():
                values = channels if usage.startswith("layout_") else {k: v for k, v in channels.items() if k not in transforms}
                if values:
                    kept.setdefault(side, {})[usage] = values
        return jointclips.write_json(os.path.join(folder, "sidecar.json"), {"formatVersion": 1, "controls": kept})

    def _check_inverse_round_trip(self):
        """Forward and inverse agree (D41): pose every control, write the joints as a JointClip, zero the controls and
        restore from the clip. The joints come back where they were and so does every control's transform. The Layout
        has no joints, so its values come from the sidecar, as in a real restore."""
        root = jointclips.rig_root()
        folder = tempfile.mkdtemp()
        rest = jointclips.snapshot()
        posed_values = self._pose_every_transform()
        posed_joints = jointclips.snapshot()
        sidecar = self._sidecar_for_what_joints_cannot_give(root, folder)
        clip = jointclips.write_json(os.path.join(folder, "clip.json"), jointclips.clip_data(rest, [posed_joints]))

        cmds.zeroRigControls(root)
        cmds.poseRigFromJointPose(root, jointPose=clip.replace("\\", "/"), sidecar=sidecar.replace("\\", "/"))

        for joint, matrix in posed_joints.items():
            after = jointclips.world(joint)
            if not after.isEquivalent(matrix, 1e-4):
                self.fail("%s is off after restoring from joints\n  posed %s\n  after %s" % (joint, list(matrix), list(after)))
        for control, values in posed_values.items():
            for name, value in values.items():
                actual = cmds.getAttr("%s.%s" % (control, name))
                self.assertAlmostEqual(actual, value, places=3, msg="%s.%s was posed to %s, and restored as %s" % (control, name, value, actual))

    def test_contract_inverse_round_trip(self):
        """Every module has an inverse that agrees with its forward solve, in the standard build."""
        self._build()
        self._check_inverse_round_trip()

    def test_contract_inverse_round_trip_mirrored(self):
        """The same for a mirrored build: a mirrored module is an ordinary module (D28)."""
        self._build_mirrored()
        self._check_inverse_round_trip()

    def test_contract_inverse_without_a_joint_falls_back_to_the_sidecar(self):
        """A source that lacks a joint is normal (D41): the module's control for it comes from the sidecar, and
        everything else still matches."""
        if not self.JOINT_OWNERS:
            self.skipTest("%s has no joints" % self.MODULE_TYPE)
        self._build()
        root = jointclips.rig_root()
        folder = tempfile.mkdtemp()
        rest = jointclips.snapshot()
        posed_values = self._pose_every_transform()
        posed_joints = jointclips.snapshot()

        # Leave out the last joint the module drives. The control that shares its markup comes from the sidecar.
        last = rig_name(list(self.JOINT_OWNERS)[-1])
        markup = json.loads(jointclips.markup_of(last))
        controls = [c for c in rigmodules.query.get_rig_controls(root) if (c.side, c.usage) == (markup["side"], markup["usage"])]
        short = last.split(":")[-1]
        clip = jointclips.write_json(os.path.join(folder, "clip.json"), jointclips.clip_data(rest, [posed_joints], skip=(short,)))
        # The Layout has no joints, so it always comes from the sidecar, and the control for the missing joint with it.
        sidecar_controls = {}
        layout = [c for c in rigmodules.query.get_rig_controls(root) if c.usage.startswith("layout_")]
        for control in controls + layout:
            sidecar_controls.setdefault(control.side, {})[control.usage] = dict(posed_values[control.node])
        sidecar = jointclips.write_json(os.path.join(folder, "sidecar.json"), {"formatVersion": 1, "controls": sidecar_controls})

        cmds.zeroRigControls(root)
        cmds.poseRigFromJointPose(root, jointPose=clip.replace("\\", "/"), sidecar=sidecar.replace("\\", "/"))

        for control in controls:
            for name, value in posed_values[control.node].items():
                actual = cmds.getAttr("%s.%s" % (control.node, name))
                self.assertAlmostEqual(actual, value, places=3, msg="%s.%s should come from the sidecar" % (control.node, name))
        for joint, matrix in posed_joints.items():
            if joint.split(":")[-1] != short and not jointclips.world(joint).isEquivalent(matrix, 1e-4):
                self.fail("%s is off when the source lacks %s" % (joint, short))

    # ---- mirrored build (D28, D34) ---------------------------------------------------------------

    def _build_mirrored(self):
        """Builds MIRROR_TEMPLATE and returns the rig root and this module type's controls (RigControl)."""
        if self.MIRROR_TEMPLATE is False:
            self.skipTest("%s has no Left or Right side, so it can't be mirrored" % self.MODULE_TYPE)
        self.assertIsNotNone(self.MIRROR_TEMPLATE, "set MIRROR_TEMPLATE and MIRROR_SKELETON, or MIRROR_TEMPLATE = False if %s can't mirror" % self.MODULE_TYPE)
        joints = self.make_skeleton(self.MIRROR_SKELETON)
        self._mirror_joint_rest = {j.split(":")[-1]: _world_matrix(j) for j in joints}
        self.build(self.MIRROR_TEMPLATE, joints)
        root = cmds.ls(rig_name("*"), type="rigRootModule")[0]
        controls = [c for c in rigmodules.query.get_rig_controls(root) if cmds.nodeType(c.module) == self.MODULE_TYPE]
        self.assertTrue(controls, "%s built no %s controls" % (self.MIRROR_TEMPLATE, self.MODULE_TYPE))
        return root, controls

    def _pose_channels(self, control):
        """Moves every plain channel of a control off its rest, within its range, and returns {name: value}."""
        posed = {}
        for channel in rigmodules.query.get_control_channels(control):
            if channel.type in ("enum", "bool"):
                continue
            value = channel.rest + 0.25
            if channel.max is not None:
                value = min(value, channel.max)
            cmds.setAttr("%s.%s" % (control, channel.name), value)
            posed[channel.name] = cmds.getAttr("%s.%s" % (control, channel.name))
        return posed

    def test_contract_mirrored_build_pairs_every_control(self):
        """A mirrored module gets a Left and a Right copy, and every Left or Right control pairs with its mirror:
        the other side, the same usage, a different module, and the same channels with the same rests and mirror
        modes. A Center control is its own mirror."""
        root, controls = self._build_mirrored()
        by_node = {c.node: c for c in controls}
        sides = set(c.side for c in controls)
        self.assertTrue({"Left", "Right"} <= sides, "%s built these sides: %s" % (self.MIRROR_TEMPLATE, sorted(sides)))
        for control in controls:
            self.assertIsNotNone(control.mirror, "%s has no mirror control" % control.node)
            if control.side == "Center":
                self.assertEqual(control.mirror, control.node, "a Center control is its own mirror")
                continue
            mirror = by_node.get(control.mirror)
            self.assertIsNotNone(mirror, "%s's mirror %s is not a %s control" % (control.node, control.mirror, self.MODULE_TYPE))
            self.assertEqual(mirror.mirror, control.node, "%s and %s are not each other's mirror" % (control.node, mirror.node))
            self.assertEqual(mirror.side, {"Left": "Right", "Right": "Left"}[control.side], control.node)
            self.assertEqual(mirror.usage, control.usage, control.node)
            self.assertNotEqual(mirror.module, control.module, "%s and its mirror share a module" % control.node)
            self.assertEqual(rigmodules.query.get_control_channels(mirror.node),
                             rigmodules.query.get_control_channels(control.node),
                             "%s and %s have different channels" % (control.node, mirror.node))

    def test_contract_mirrored_joints_keep_rest_pose(self):
        self._build_mirrored()
        for short, matrix in self._mirror_joint_rest.items():
            after = _world_matrix(rig_name(short))
            if not after.isEquivalent(matrix, 1e-4):
                self.fail("%s moved when the mirrored rig was built\n  before %s\n  after  %s" % (short, list(matrix), list(after)))

    def test_contract_mirror_pose_follows_each_channels_declared_mode(self):
        """mirrorRigPose copies a channel's value or negates it, as the channel's mirrorCopy or mirrorNegate
        says, and flipRigPose does the same for a pair. Zero then brings the mirrored rig back to its build pose."""
        root, controls = self._build_mirrored()
        left = [c for c in controls if c.side == "Left"]
        joints = cmds.ls(rig_name("*"), type="joint")
        rest = {j: _world_matrix(j) for j in joints}
        channels = {c.node: rigmodules.query.get_control_channels(c.node) for c in left}
        modes = {node: {ch.name: ch.mirror for ch in found} for node, found in channels.items()}

        def assert_mirrored(label, source_values):
            for control in left:
                for name, value in source_values[control.node].items():
                    expected = -value if modes[control.node][name] == "negate" else value
                    actual = cmds.getAttr("%s.%s" % (control.mirror, name))
                    self.assertAlmostEqual(
                        actual, expected, places=3,
                        msg="%s: %s.%s expected %s from %s (mirror%s), got %s"
                            % (label, control.mirror, name, expected, control.node, modes[control.node][name].title(), actual))

        posed = {c.node: self._pose_channels(c.node) for c in left}
        self.assertTrue(any(posed.values()), "no Left channel could be posed, so the test proves nothing")
        cmds.mirrorRigPose(root)
        assert_mirrored("mirrorRigPose", posed)

        # Only the Left side is posed, so the Right side takes its mirror and the Left side takes the mirror of
        # the Right side's rest, which is the Left side's own rest.
        cmds.zeroRigControls(root)
        posed = {c.node: self._pose_channels(c.node) for c in left}
        cmds.flipRigPose(root)
        assert_mirrored("flipRigPose", posed)
        for control in left:
            for channel in channels[control.node]:
                if channel.type not in ("enum", "bool"):
                    self.assertAlmostEqual(
                        cmds.getAttr("%s.%s" % (control.node, channel.name)), channel.rest, places=3,
                        msg="flipRigPose: %s.%s should be at its rest, since its mirror was" % (control.node, channel.name))

        cmds.zeroRigControls(root)
        for joint in joints:
            after = _world_matrix(joint)
            if not after.isEquivalent(rest[joint], 1e-4):
                self.fail("%s is off its build pose after zeroRigControls on the mirrored rig\n  built %s\n  after %s"
                          % (joint, list(rest[joint]), list(after)))
