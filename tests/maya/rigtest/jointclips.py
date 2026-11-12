"""Helpers that write a rig's joints as a JointClip file (D41), for tests of the inverse solve.

A JointClip here is flat: every joint is a root, and its local transform is its world transform, which the format allows.
That keeps the helper joints a rig has (an aim's target and up are not exported) in the file, and needs neither pxr nor
rigmodules.jointclip.capture_joint_clip, so a test of the solve doesn't depend on the capture.
"""
import json

from maya import cmds
import maya.api.OpenMaya as om

from rigtest.case import rig_name

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
