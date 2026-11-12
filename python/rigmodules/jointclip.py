"""JointClip files: the bridge between joint animation and the plugin's inverse solve (D41).

A JointClip file is JSON: a source skeleton's joints with their markup and rest, and a list of JointPoses, one per
frame, each joint's transform local to its parent (design/inverse-solve.md, sections 6 and 7; the schema is in
cpp/core/JointClipFile.h). poseRigFromJointPose and animateRigFromJointClip read it.

capture_joint_clip writes one from a rig. A joint file for mocap and the engine will be FBX, an adapter over this
file (design/control-library.md, section 12, D42). This module takes paths and never resolves asset ids, and it
imports no Qt, bmaya, pxr or numpy (D24).
"""

from __future__ import annotations

import json
import re

import maya.api.OpenMaya
import maya.cmds as cmds

import rigmodules._namespace
import rigmodules.query

FORMAT_VERSION = 1

# What a module writes for the joints it drives (cpp/core/Constants.h, RigAttr). A joint's offsetParentMatrix is
# connected to its module's jointOPM[i], and the build pose of that joint is stored at jointRestWorld[i].
_JOINT_OPM_ATTR = "jointOPM"
_JOINT_REST_ATTR = "jointRestWorld"
_ELEMENT_INDEX = re.compile(r"\[(\d+)\]$")


def capture_joint_clip(rig_root: str, start_frame: int, end_frame: int, clip_path: str) -> list[str]:
    """Samples the rig's export joints (joints in the rig's namespace whose markup has export set) on every frame from
    start_frame to end_frame, and writes them as a JointClip file. The same frame twice is a JointPose. Reads the
    scene at each frame without moving the time slider. Returns the captured joints, parents first.

    Each joint's transform is its world matrix relative to its nearest captured ancestor's, so it doesn't depend on
    the rig's joints inheriting transforms. Translations are in Maya's internal units (centimeters). A joint's rest
    is its build pose, stored on the module that drives it (its current pose for a joint no module drives).

    Raises ValueError when start_frame is after end_frame, and RuntimeError when the rig has no export joints or two
    of them would share a path."""
    if start_frame > end_frame:
        raise ValueError(f"start_frame {start_frame} is after end_frame {end_frame}.")

    joints = _get_export_joints(rig_root)
    parents = _get_parent_indices(joints)
    paths = _get_paths(joints, parents)
    markups = cmds.rigJointMarkup(*joints, query=True)

    rest_worlds = [_get_rest_world(joint) for joint in joints]
    previous_rotations: list[maya.api.OpenMaya.MQuaternion | None] = [None] * len(joints)
    poses = []
    for frame in range(start_frame, end_frame + 1):
        worlds = [_get_matrix(joint + ".worldMatrix[0]", frame) for joint in joints]
        pose = []
        for index, parent in enumerate(parents):
            transform = _get_local_transform(worlds[index], worlds[parent] if parent is not None else None, previous_rotations[index])
            previous_rotations[index] = maya.api.OpenMaya.MQuaternion(*transform["rotate"])
            pose.append(transform)
        poses.append(pose)

    clip = {
        "formatVersion": FORMAT_VERSION,
        "frameRate": _get_frame_rate(),
        "startFrame": start_frame,
        "joints": [
            {
                "path": paths[index],
                "markup": markups[index],
                "rest": _get_local_transform(
                    rest_worlds[index], rest_worlds[parents[index]] if parents[index] is not None else None, None),
            }
            for index in range(len(joints))
        ],
        "poses": poses,
    }
    _write_clip(clip, clip_path)
    return joints


def _get_export_joints(rig_root: str) -> list[str]:
    """The rig's export joints as long names, parents before children (shallower paths first, then by name)."""
    info = rigmodules.query.get_rig_info(rig_root)
    joints = cmds.ls(rigmodules._namespace.get_node_pattern(info.namespace), type="joint", long=True) or []
    marked = cmds.rigJointMarkup(*joints, query=True) if joints else []
    export_joints = [joint for joint, markup in zip(joints, marked) if markup and json.loads(markup)["export"]]
    if not export_joints:
        raise RuntimeError(f"{rig_root} has no joints marked for export in its namespace.")
    return sorted(export_joints, key=lambda joint: (joint.count("|"), joint))


def _get_parent_indices(joints: list[str]) -> list[int | None]:
    """For each joint, the index of its nearest ancestor in the list, or None for a root."""
    index_by_name = {joint: index for index, joint in enumerate(joints)}
    parents = []
    for joint in joints:
        names = joint.split("|")
        parent = None
        for length in range(len(names) - 1, 1, -1):
            parent = index_by_name.get("|".join(names[:length]))
            if parent is not None:
                break
        parents.append(parent)
    return parents


def _get_paths(joints: list[str], parents: list[int | None]) -> list[str]:
    """Each joint's path in the file: its parent's path plus its own name without namespace."""
    paths: list[str] = []
    for joint, parent in zip(joints, parents):
        name = joint.split("|")[-1].split(":")[-1]
        paths.append(name if parent is None else f"{paths[parent]}/{name}")
    if len(set(paths)) != len(paths):
        raise RuntimeError("Two export joints would have the same path in the JointClip file: " + ", ".join(sorted(
            path for path in set(paths) if paths.count(path) > 1)))
    return paths


def _get_rest_world(joint: str) -> maya.api.OpenMaya.MMatrix:
    """The joint's build pose, from the module that drives it. A joint no module drives is at rest where it is."""
    sources = cmds.listConnections(joint + ".offsetParentMatrix", source=True, destination=False, plugs=True) or []
    if sources:
        module, attribute = sources[0].split(".", 1)
        match = _ELEMENT_INDEX.search(attribute)
        if attribute.startswith(_JOINT_OPM_ATTR) and match:
            return _get_matrix(f"{module}.{_JOINT_REST_ATTR}[{match.group(1)}]")
    return _get_matrix(joint + ".worldMatrix[0]")


def _get_matrix(plug: str, frame: int | None = None) -> maya.api.OpenMaya.MMatrix:
    values = cmds.getAttr(plug) if frame is None else cmds.getAttr(plug, time=frame)
    return maya.api.OpenMaya.MMatrix(values)


def _get_local_transform(
    world: maya.api.OpenMaya.MMatrix,
    parent_world: maya.api.OpenMaya.MMatrix | None,
    previous_rotation: maya.api.OpenMaya.MQuaternion | None,
) -> dict[str, list[float]]:
    """world relative to parent_world as translate, rotate (a quaternion x, y, z, w) and scale. The quaternion takes
    the sign nearest previous_rotation, so a curve through the frames doesn't flip. Shear is dropped."""
    local = world if parent_world is None else world * parent_world.inverse()
    transformation = maya.api.OpenMaya.MTransformationMatrix(local)
    rotation = transformation.rotation(asQuaternion=True)
    if previous_rotation is not None and _dot(rotation, previous_rotation) < 0.0:
        rotation = maya.api.OpenMaya.MQuaternion(-rotation.x, -rotation.y, -rotation.z, -rotation.w)
    translation = transformation.translation(maya.api.OpenMaya.MSpace.kTransform)
    return {
        "translate": [translation.x, translation.y, translation.z],
        "rotate": [rotation.x, rotation.y, rotation.z, rotation.w],
        "scale": list(transformation.scale(maya.api.OpenMaya.MSpace.kTransform)),
    }


def _dot(a: maya.api.OpenMaya.MQuaternion, b: maya.api.OpenMaya.MQuaternion) -> float:
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w


def _get_frame_rate() -> float:
    """The scene's frames per second."""
    one_frame = maya.api.OpenMaya.MTime(1, maya.api.OpenMaya.MTime.uiUnit())
    return 1.0 / one_frame.asUnits(maya.api.OpenMaya.MTime.kSeconds)


def _write_clip(clip: dict, clip_path: str) -> None:
    with open(clip_path, "w", encoding="utf-8") as handle:
        json.dump(clip, handle, separators=(",", ":"))
