"""Helpers that write a rig's joints as a JointClip file, for tests of the inverse solve.

A JointClip here is flat: every joint is a root, and its local transform is its world transform, which the format allows.
That keeps the helper joints a rig has (an aim's target and up are not exported) in the file, and needs no
rigmodules.jointclip.capture_joint_clip, so a test of the solve doesn't depend on the capture.
"""
import json
import os
import shutil
import tempfile

from maya import cmds
import maya.api.OpenMaya as om

from rigtest.case import RigTestCase, rig_name

CHANNELS = ("translateX", "translateY", "translateZ", "rotateX", "rotateY", "rotateZ", "scaleX", "scaleY", "scaleZ")


def rig_root():
    return cmds.ls(rig_name("*"), type="rigRootModule")[0]


def markup_of(joint):
    """The joint's rigMarkup JSON, or "" for a joint with none."""
    return (cmds.rigJointMarkup(joint, query=True) or [""])[0]


def rig_joints():
    """Every marked joint of the rig, as long names."""
    return [j for j in cmds.ls(rig_name("*"), type="joint", long=True) if markup_of(j)]


def world(node):
    return om.MMatrix(cmds.xform(node, query=True, worldSpace=True, matrix=True))


def transform_of(matrix):
    """A JointClip transform (translate, rotate as a quaternion x y z w, scale) from a matrix."""
    transformation = om.MTransformationMatrix(matrix)
    rotation = transformation.rotation(asQuaternion=True)
    translation = transformation.translation(om.MSpace.kTransform)
    return {
        "translate": [translation.x, translation.y, translation.z],
        "rotate": [rotation.x, rotation.y, rotation.z, rotation.w],
        "scale": list(transformation.scale(om.MSpace.kTransform)),
    }


def values_of(control):
    """The nine transform channel values of a control."""
    return {name: cmds.getAttr("%s.%s" % (control, name)) for name in CHANNELS}


def snapshot():
    """{joint: world matrix} for every marked joint of the rig."""
    return {joint: world(joint) for joint in rig_joints()}


def clip_data(rest, frames, frame_rate=24.0, start_frame=1, skip=()):
    """A flat JointClip as a dict. rest and each of frames are snapshots. skip lists joints (short names) to leave out."""
    joints = [j for j in rest if j.split(":")[-1] not in skip]
    return {
        "formatVersion": 1,
        "frameRate": frame_rate,
        "startFrame": start_frame,
        "joints": [{"path": j.split(":")[-1], "markup": markup_of(j), "rest": transform_of(rest[j])} for j in joints],
        "poses": [[transform_of(frame[j]) for j in joints] for frame in frames],
    }


def write_json(path, data):
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(data if isinstance(data, str) else json.dumps(data))
    return path


class SolveTestCase(RigTestCase):
    """A rig, and the helpers that write its joints as a JointClip."""

    def setUp(self):
        super().setUp()
        self.folder = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.folder, True)
        self.counter = 0

    def write(self, name, data):
        path = os.path.join(self.folder, name)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(data if isinstance(data, str) else json.dumps(data))
        return path

    def snapshot(self):
        return snapshot()

    def clip_data(self, rest, frames, **kwargs):
        return clip_data(rest, frames, **kwargs)

    def write_clip(self, rest, frames, **kwargs):
        self.counter += 1
        return self.write("clip%d.json" % self.counter, self.clip_data(rest, frames, **kwargs))

    def write_sidecar(self, controls):
        """A pose file: {side: {usage: {channel: value}}}."""
        self.counter += 1
        return self.write("sidecar%d.json" % self.counter, {"formatVersion": 1, "controls": controls})

    def assertJointsAt(self, expected, tolerance=1e-4):
        for joint, matrix in expected.items():
            if not world(joint).isEquivalent(matrix, tolerance):
                self.fail("%s is off\n  wanted %s\n  got    %s" % (joint, list(matrix), list(world(joint))))

    def assertValues(self, control, expected, places=3):
        actual = values_of(control)
        for name, value in expected.items():
            self.assertAlmostEqual(actual[name], value, places=places, msg="%s.%s: wanted %s, got %s" % (control, name, value, actual[name]))

    def round_trip(self, posed, sidecar=None):
        """posed: {plug: value} set on the rig. Writes the posed joints, zeroes the controls and restores from the
        clip. Returns (rest snapshot, posed snapshot, posed control values by control)."""
        rest = self.snapshot()
        for plug, value in posed.items():
            cmds.setAttr(rig_name(plug), value)
        posed_joints = self.snapshot()
        controls = {c: values_of(c) for c in cmds.ls(rig_name("*"), type="rigControlNode")}
        clip = self.write_clip(rest, [posed_joints])
        cmds.zeroRigControls(rig_root())
        kwargs = {"sidecar": sidecar} if sidecar else {}
        cmds.poseRigFromJointPose(rig_root(), jointPose=clip, **kwargs)
        return rest, posed_joints, controls
