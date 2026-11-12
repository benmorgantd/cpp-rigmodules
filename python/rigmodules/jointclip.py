"""JointClip files: the bridge between joint animation and the plugin's inverse solve (D41).

A JointClip file is JSON: a source skeleton's joints with their markup and rest, and a list of JointPoses, one per
frame, each joint's transform local to its parent (design/inverse-solve.md, sections 6 and 7; the schema is in
cpp/core/JointClipFile.h). poseRigFromJointPose and animateRigFromJointClip read it.

capture_joint_clip writes one from a rig. FBX is the joint file for mocap and the engine (D42): write_fbx and read_fbx
are adapters over the JointClip file, through a scratch skeleton in Maya, and write_skeleton_fbx writes the skeleton
asset source (design/control-library.md, section 12). This module takes paths and never resolves asset ids, and it
imports no Qt, bmaya, pxr or numpy (D24).
"""

from __future__ import annotations

import contextlib
import json
import math
import os
import re
import tempfile

import maya.api.OpenMaya
import maya.api.OpenMayaAnim
import maya.cmds as cmds
import maya.mel as mel

import rigmodules._namespace
import rigmodules.bonemap
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
    _write_clip(_capture_clip(joints, start_frame, end_frame), clip_path)
    return joints


def _capture_clip(joints: list[str], start_frame: int, end_frame: int) -> dict:
    """The JointClip for joints (long names, parents before children) over start_frame to end_frame. A joint with
    no markup gets "", stays in the clip, and composes the world matrices of the joints below it."""
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

    return {
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


# ---- FBX ----------------------------------------------------------------------------------------

_NODE_NAME = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
_FRAME_RATE_TOLERANCE = 1e-3
_FBX_PLUGIN = "fbxmaya"


def write_fbx(clip_path: str, fbx_path: str, hold_frames: int = 1) -> None:
    """Writes a JointClip file as an FBX file: a scratch skeleton of plain joints (real parenting, each joint's local
    translate, rotate and scale keyed on the clip's frames), exported baked and deleted. A clip with one pose writes
    a static skeleton with no animation, unless hold_frames is above 1: an engine that needs a pose to be an
    animation gets the pose held on that many frames, the same values on each. A joint's markup rides along as its rigMarkup attribute, so read_fbx gives
    it back without a bone map. An existing file is replaced, and a failed write leaves it as it was.

    The scratch joints are not the rig's joints: the rig drives them through offsetParentMatrix, which the exporter
    ignores. Raises ValueError for a file that isn't a JointClip, a clip with no poses, a joint name that isn't a
    valid Maya node name, two joints with the same name, a frame rate that differs from the scene's, or a
    hold_frames that is below 1 or is above 1 for a clip with more than one pose, and RuntimeError when the scene
    already has a node named like one of the joints (FBX bones bind by name)."""
    with open(clip_path, encoding="utf-8") as handle:
        clip = json.load(handle)
    if hold_frames < 1 or (hold_frames > 1 and len(clip.get("poses") or []) > 1):
        raise ValueError(f"hold_frames is {hold_frames}: it must be 1 or more, and above 1 only for a clip with one pose.")
    if hold_frames > 1:
        clip["poses"] = clip["poses"] * hold_frames
    _write_fbx(clip, clip_path, fbx_path)


def write_skeleton_fbx(rig_root: str, skeleton_id: str, fbx_path: str, bone_map_path: str) -> None:
    """The skeleton export, the engine's skeleton asset source (D42): the rig's export joints at rest as a static FBX,
    and their bone map (rigmodules.bonemap) with the rig's asset id and skeleton_id, the id of the shared skeleton
    asset (D29, which this package can't know). Raises what write_fbx raises, and RuntimeError when the rig has no
    export joints."""
    joints = _get_export_joints(rig_root)
    frame = int(cmds.currentTime(query=True))
    clip = _capture_clip(joints, frame, frame)
    clip["poses"] = [[joint["rest"] for joint in clip["joints"]]]
    info = rigmodules.query.get_rig_info(rig_root)
    _write_fbx(clip, rig_root, fbx_path)
    rigmodules.bonemap.write_bone_map(
        bone_map_path, info.asset_id, skeleton_id, {joint["path"].split("/")[-1]: joint["markup"] for joint in clip["joints"]})


def read_fbx(fbx_path: str, clip_path: str, bone_map: str | None = None) -> None:
    """Reads an FBX file into a JointClip file: imports it into the scene, writes the bone map's markup on its
    joints (bone_map is a bone map file, or None), samples every joint on the file's whole frames and deletes
    what it imported. Joints the map doesn't list keep the markup the file carries, or none. A file with no
    animation gives one pose, at frame 1. The clip's frame rate is the file's, and its rest is the file's first
    frame. The scene's time unit, playback range, time and selection are restored.

    Needs a Maya session with the FBX plugin, which it loads. Raises FileNotFoundError, and ValueError for a bad
    bone map or a file with no joints."""
    if not os.path.isfile(fbx_path):
        raise FileNotFoundError(fbx_path)
    map_data = rigmodules.bonemap.read_bone_map(bone_map) if bone_map else None
    _load_fbx_plugin()
    namespace = _get_free_namespace("rmfbx")
    unit = cmds.currentUnit(query=True, time=True)
    time = cmds.currentTime(query=True)
    playback = (cmds.playbackOptions(query=True, minTime=True), cmds.playbackOptions(query=True, maxTime=True))
    clip = None
    try:
        with _scratch_scene() as before:
            cmds.namespace(add=namespace)
            cmds.namespace(set=namespace)
            try:
                mel.eval("FBXResetImport")
                mel.eval("FBXImportMode -v add")
                mel.eval('FBXImport -f "%s"' % fbx_path.replace("\\", "/"))
            finally:
                cmds.namespace(set=":")
            joints = [joint for joint in cmds.ls(type="joint", long=True) or [] if joint not in before]
            if not joints:
                raise ValueError(f"{fbx_path} has no joints.")
            joints.sort(key=lambda joint: (joint.count("|"), joint))
            if map_data:
                missing = rigmodules.bonemap.apply_bone_map(map_data, joints)
                if missing:
                    cmds.warning(f"{bone_map} lists joints {fbx_path} doesn't have: {', '.join(missing)}")
            times = cmds.keyframe(joints, query=True, timeChange=True) or [1]
            start, end = int(round(min(times))), int(round(max(times)))
            cmds.currentTime(start)
            try:
                clip = _capture_clip(joints, start, end)
            finally:
                # The import set the scene's unit to the file's. Frame numbers stay as they are.
                cmds.currentUnit(time=unit, updateAnimation=False)
                cmds.currentTime(time)
                cmds.playbackOptions(minTime=playback[0], maxTime=playback[1])
    finally:
        if cmds.namespace(exists=namespace):
            cmds.namespace(removeNamespace=namespace)
    _write_clip(clip, clip_path)


def check_skeleton_match(animation_fbx: str, skeleton_fbx: str) -> None:
    """Checks that an animation FBX is valid for a skeleton export (design/control-library.md, section 6b): the same
    joint names, each under the same parent. Rests and bone lengths aren't compared, so an animation made on a
    skeleton of other proportions still matches. Reads both files with read_fbx, so it has the same needs. Raises
    FileNotFoundError, and ValueError naming every joint that differs."""
    with tempfile.TemporaryDirectory() as folder:
        clips = []
        for index, fbx_path in enumerate((skeleton_fbx, animation_fbx)):
            clip_path = os.path.join(folder, f"{index}.json")
            read_fbx(fbx_path, clip_path)
            with open(clip_path, encoding="utf-8") as handle:
                clips.append(json.load(handle))
    differences = _get_skeleton_differences(*clips)
    if differences:
        raise ValueError(f"{animation_fbx} doesn't match the skeleton {skeleton_fbx}: {'; '.join(differences)}")


def _get_skeleton_differences(skeleton: dict, animation: dict) -> list[str]:
    """What differs between two clips' joints, by name and parent name. Empty when they match."""
    def parents(clip: dict) -> dict[str, str]:
        result = {}
        for joint in clip["joints"]:
            *above, leaf = joint["path"].split("/")
            result[leaf] = above[-1] if above else ""
        return result

    expected, actual = parents(skeleton), parents(animation)
    differences = []
    missing = [name for name in expected if name not in actual]
    extra = [name for name in actual if name not in expected]
    if missing:
        differences.append("the animation lacks " + ", ".join(missing))
    if extra:
        differences.append("the skeleton lacks " + ", ".join(extra))
    for name in expected:
        if name in actual and expected[name] != actual[name]:
            differences.append(f"{name} is under {actual[name] or 'nothing'} but the skeleton has it under {expected[name] or 'nothing'}")
    return differences


def _write_fbx(clip: dict, source: str, fbx_path: str) -> None:
    joints = _check_clip(clip, source)
    leaves = [joint["path"].split("/")[-1] for joint in joints]
    taken = [leaf for leaf in leaves if cmds.objExists(leaf)]
    if taken:
        raise RuntimeError("The scene already has nodes named like these joints, and FBX bones bind by name: " + ", ".join(taken))
    _load_fbx_plugin()
    animated = len(clip["poses"]) > 1
    start = int(clip["startFrame"])
    end = start + len(clip["poses"]) - 1
    temp = os.path.splitext(fbx_path)[0] + ".tmp.fbx"
    try:
        with _scratch_scene():
            nodes = _build_scratch_skeleton(clip, joints, leaves)
            cmds.select([node for node, joint in zip(nodes, joints) if "/" not in joint["path"]], replace=True)
            cmds.select(hierarchy=True)
            mel.eval("FBXResetExport")
            mel.eval("FBXExportBakeComplexAnimation -v %s" % ("true" if animated else "false"))
            if animated:
                mel.eval("FBXExportBakeComplexStart -v %d" % start)
                mel.eval("FBXExportBakeComplexEnd -v %d" % end)
                mel.eval("FBXExportBakeComplexStep -v 1")
            mel.eval("FBXExportUpAxis %s" % cmds.upAxis(query=True, axis=True))
            mel.eval("FBXExportInAscii -v false")
            mel.eval('FBXExport -f "%s" -s' % temp.replace("\\", "/"))
        os.replace(temp, fbx_path)
    finally:
        if os.path.exists(temp):
            os.remove(temp)


def _check_clip(clip: dict, source: str) -> list[dict]:
    """The clip's joints, after the checks write_fbx needs to build a scratch skeleton. The plugin's reader does the
    full validation when a command reads the file."""
    if clip.get("formatVersion") != FORMAT_VERSION:
        raise ValueError(f"{source}: formatVersion {clip.get('formatVersion')!r} is not {FORMAT_VERSION}.")
    joints, poses = clip.get("joints"), clip.get("poses")
    if not joints or not poses:
        raise ValueError(f"{source} has no joints or no poses.")
    if any(len(pose) != len(joints) for pose in poses):
        raise ValueError(f"{source}: every pose needs one transform per joint ({len(joints)}).")
    leaves = [joint["path"].split("/")[-1] for joint in joints]
    invalid = sorted({leaf for leaf in leaves if not _NODE_NAME.match(leaf)})
    if invalid:
        raise ValueError("These joint names are not valid Maya node names: " + ", ".join(invalid))
    repeated = sorted({leaf for leaf in leaves if leaves.count(leaf) > 1})
    if repeated:
        raise ValueError("FBX bones bind by name, so these joint names must be unique: " + ", ".join(repeated))
    if abs(clip["frameRate"] - _get_frame_rate()) > _FRAME_RATE_TOLERANCE:
        raise ValueError(f"{source}: the clip's frame rate {clip['frameRate']} is not the scene's {_get_frame_rate()}.")
    return joints


def _build_scratch_skeleton(clip: dict, joints: list[dict], leaves: list[str]) -> list[str]:
    """Plain joints named like the clip's, parented by path, with the clip's transforms: values for one pose, anim
    curves with a key per frame for more. Returns the joints in clip order."""
    nodes: list[str] = []
    node_by_path: dict[str, str] = {}
    for joint, leaf in zip(joints, leaves):
        parent = node_by_path.get(joint["path"].rpartition("/")[0])
        arguments = {"name": leaf, "skipSelect": True}
        if parent:
            arguments["parent"] = parent
        node = cmds.createNode("joint", **arguments)
        if node != leaf:
            raise RuntimeError(f"Could not name a scratch joint {leaf!r}: Maya called it {node!r}.")
        # The clip's transforms are local to the parent with no compensation for the parent's scale.
        cmds.setAttr(node + ".segmentScaleCompensate", 0)
        if joint["markup"]:
            markup = json.loads(joint["markup"])
            cmds.rigJointMarkup(
                node, side=markup["side"], usage=markup["usage"], export=markup["export"], skin=markup["skin"])
        nodes.append(node)
        node_by_path[joint["path"]] = node

    eulers = _get_euler_tracks(clip["poses"])
    times = maya.api.OpenMaya.MTimeArray()
    for index in range(len(clip["poses"])):
        times.append(maya.api.OpenMaya.MTime(int(clip["startFrame"]) + index, maya.api.OpenMaya.MTime.uiUnit()))
    for joint_index, node in enumerate(nodes):
        channels = {
            "translate": [[pose[joint_index]["translate"][axis] for pose in clip["poses"]] for axis in range(3)],
            "rotate": [[euler[axis] for euler in eulers[joint_index]] for axis in range(3)],
            "scale": [[pose[joint_index]["scale"][axis] for pose in clip["poses"]] for axis in range(3)],
        }
        for attribute, tracks in channels.items():
            for axis, values in zip("XYZ", tracks):
                plug = f"{node}.{attribute}{axis}"
                if len(values) == 1:
                    # setAttr takes degrees for a rotation, the curves below take radians.
                    cmds.setAttr(plug, math.degrees(values[0]) if attribute == "rotate" else values[0])
                else:
                    curve = maya.api.OpenMayaAnim.MFnAnimCurve()
                    curve.create(_get_plug(plug))
                    curve.addKeys(times, maya.api.OpenMaya.MDoubleArray(values))
    return nodes


def _get_euler_tracks(poses: list[list[dict]]) -> list[list[tuple[float, float, float]]]:
    """Per joint, its rotation on every frame as xyz euler radians, each the solution nearest the frame before, so
    a curve through the frames doesn't flip."""
    tracks: list[list[tuple[float, float, float]]] = []
    for joint_index in range(len(poses[0])):
        track = []
        previous = None
        for pose in poses:
            euler = maya.api.OpenMaya.MQuaternion(*pose[joint_index]["rotate"]).asEulerRotation()
            if previous is not None:
                euler = euler.closestSolution(previous)
            previous = euler
            track.append((euler.x, euler.y, euler.z))
        tracks.append(track)
    return tracks


def _get_plug(plug: str) -> maya.api.OpenMaya.MPlug:
    selection = maya.api.OpenMaya.MSelectionList()
    selection.add(plug)
    return selection.getPlug(0)


def _load_fbx_plugin() -> None:
    if not cmds.pluginInfo(_FBX_PLUGIN, query=True, loaded=True):
        cmds.loadPlugin(_FBX_PLUGIN, quiet=True)


def _get_free_namespace(prefix: str) -> str:
    index = 0
    while cmds.namespace(exists=f"{prefix}{index}"):
        index += 1
    return f"{prefix}{index}"


@contextlib.contextmanager
def _scratch_scene():
    """Runs a block that creates nodes, and afterwards deletes every node it made, restores the selection and leaves
    nothing on the undo queue. Yields the set of the scene's nodes (long names) from before."""
    before = set(cmds.ls(long=True))
    selection = cmds.ls(selection=True, long=True)
    undo = cmds.undoInfo(query=True, stateWithoutFlush=True)
    cmds.undoInfo(stateWithoutFlush=False)
    try:
        yield before
    finally:
        for node in [node for node in cmds.ls(long=True) if node not in before]:
            if cmds.objExists(node):
                cmds.delete(node)
        if selection:
            cmds.select(selection, replace=True)
        else:
            cmds.select(clear=True)
        cmds.undoInfo(stateWithoutFlush=undo)


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
