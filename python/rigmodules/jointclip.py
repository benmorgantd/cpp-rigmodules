"""JointClip files: the bridge between joint animation and the plugin's inverse solve (D41).

A JointClip file is JSON: a source skeleton's joints with their markup and rest, and a list of JointPoses, one per
frame, each joint's transform local to its parent (design/inverse-solve.md, sections 6 and 7; the schema is in
cpp/core/JointClipFile.h). poseRigFromJointPose and animateRigFromJointClip read it.

capture_joint_clip writes one from a rig. write_usd and read_usd convert between a JointClip file and a UsdSkel file
(design/inverse-solve.md, section 6.1). The plugin never links USD, so pxr is used here. This module takes paths and
never resolves asset ids, and it imports no Qt or bmaya (D24).
"""

from __future__ import annotations

import json
import math
import os
import re

import maya.api.OpenMaya
import maya.cmds as cmds
import numpy
import pxr.Gf
import pxr.Sdf
import pxr.Usd
import pxr.UsdGeom
import pxr.UsdSkel
import pxr.Vt

import rigmodules._namespace
import rigmodules.query

FORMAT_VERSION = 1

# The rigmodules: attributes on a USD Skeleton prim (inverse-solve.md, section 6.1).
_USD_MARKUP_ATTR = "rigmodules:jointMarkup"
_USD_VERSION_ATTR = "rigmodules:formatVersion"
# A JointClip's translations are in centimeters, Maya's internal unit.
_METERS_PER_UNIT = 0.01
_USD_ROOT = "/Root"
_USD_SKELETON = _USD_ROOT + "/Skeleton"
_USD_ANIMATION = _USD_ROOT + "/Animation"

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


def write_usd(clip_path: str, usd_path: str) -> None:
    """Writes a JointClip file as a UsdSkel file: a SkelRoot with a Skeleton (joints, rest and bind transforms, and
    the markup in rigmodules:jointMarkup) and a SkelAnimation with a time sample per JointPose, on the clip's own
    frame numbers. The format follows the extension (.usd, .usda or .usdc). An existing file is replaced, and a
    failed write leaves it as it was.

    The stage is in centimeters and the scene's up axis, because that is what a JointClip holds. Translations and
    quaternions are single precision and scales are half precision, as UsdSkel stores them, so values come back to
    about 1e-3 for scales and 1e-6 relative for the rest.

    Raises ValueError for a file that isn't a JointClip, an empty clip, or a joint name that isn't a valid USD
    identifier."""
    with open(clip_path, encoding="utf-8") as handle:
        clip = json.load(handle)
    joints = _check_clip(clip, clip_path)
    paths = [joint["path"] for joint in joints]
    parents = _get_path_parent_indices(paths)

    temp_path = _get_temp_usd_path(usd_path)
    try:
        stage = pxr.Usd.Stage.CreateNew(temp_path)
        _write_stage_metadata(stage, clip)
        skel_root = pxr.UsdSkel.Root.Define(stage, _USD_ROOT)
        stage.SetDefaultPrim(skel_root.GetPrim())
        skeleton = _write_skeleton(stage, joints, paths, parents)
        animation = pxr.UsdSkel.Animation.Define(stage, _USD_ANIMATION)
        pxr.UsdSkel.BindingAPI.Apply(skeleton.GetPrim()).CreateAnimationSourceRel().SetTargets([animation.GetPath()])
        _write_animation(animation, clip, paths)
        stage.GetRootLayer().Save()
        del stage
        os.replace(temp_path, usd_path)
    finally:
        if os.path.exists(temp_path):
            os.remove(temp_path)


def read_usd(usd_path: str, clip_path: str) -> None:
    """Reads a UsdSkel file and writes the JointClip file clip_path: the file's one Skeleton and the SkelAnimation
    bound to it, with the markup in rigmodules:jointMarkup (a file from another tool has none, so every joint is
    unmarked and a restore matches nothing, with a warning).

    Translations are converted from the stage's meters per unit to centimeters, and the root joints are rotated
    from the stage's up axis to the scene's. The skeleton prim's own transform is ignored. The poses are the integer
    frames from the animation's first sample to its last (interpolated between samples, so a file keyed on fewer
    frames still gives a pose per frame), or a single pose when the animation has no samples. The file's frame rate
    is kept: the plugin's commands refuse a clip that doesn't match the scene's.

    Raises FileNotFoundError, and ValueError for a file that isn't a single skeleton with an animation, a newer
    rigmodules:formatVersion than this module knows, or an animation with no whole frame."""
    if not os.path.isfile(usd_path):
        raise FileNotFoundError(usd_path)
    stage = pxr.Usd.Stage.Open(usd_path)
    if not stage:
        raise ValueError(f"{usd_path} is not a USD file.")

    skeleton_prim = _get_single_skeleton(stage, usd_path)
    skeleton = pxr.UsdSkel.Skeleton(skeleton_prim)
    joints = [str(joint) for joint in skeleton.GetJointsAttr().Get() or []]
    rest_matrices = skeleton.GetRestTransformsAttr().Get()
    if not joints or rest_matrices is None or len(rest_matrices) != len(joints):
        raise ValueError(f"{usd_path}: the Skeleton needs joints and one restTransform per joint.")
    markups = _get_usd_markups(skeleton_prim, len(joints), usd_path)

    source = pxr.UsdSkel.BindingAPI(skeleton_prim).GetInheritedAnimationSource()
    anim_query = pxr.UsdSkel.Cache().GetAnimQuery(source) if source else None
    if not anim_query:
        raise ValueError(f"{usd_path}: the Skeleton has no SkelAnimation bound to it.")
    anim_index = {str(joint): index for index, joint in enumerate(anim_query.GetJointOrder())}

    unit_scale = pxr.UsdGeom.GetStageMetersPerUnit(stage) / _METERS_PER_UNIT
    up_conversion = _get_up_axis_conversion(pxr.UsdGeom.GetStageUpAxis(stage))
    is_root = ["/" not in joint for joint in joints]

    def convert(transform: dict[str, list[float]], root: bool) -> dict[str, list[float]]:
        transform["translate"] = [value * unit_scale for value in transform["translate"]]
        return _apply_conversion(transform, up_conversion) if root and up_conversion else transform

    rests = [convert(_decompose_matrix(matrix), root) for matrix, root in zip(rest_matrices, is_root)]
    frames = _get_frames(anim_query, usd_path)
    previous: list[list[float] | None] = [None] * len(joints)
    poses = []
    for frame in frames:
        translations, rotations, scales = anim_query.ComputeJointLocalTransformComponents(
            pxr.Usd.TimeCode.Default() if frame is None else pxr.Usd.TimeCode(frame))
        pose = []
        for index, joint in enumerate(joints):
            animated = anim_index.get(joint)
            if animated is None:
                transform = {key: list(value) for key, value in rests[index].items()}
            else:
                rotation = rotations[animated]
                transform = convert({
                    "translate": [float(value) for value in translations[animated]],
                    "rotate": [*(float(value) for value in rotation.imaginary), float(rotation.real)],
                    "scale": [float(value) for value in scales[animated]],
                }, is_root[index])
            if previous[index] is not None and _dot_lists(transform["rotate"], previous[index]) < 0.0:
                transform["rotate"] = [-value for value in transform["rotate"]]
            previous[index] = transform["rotate"]
            pose.append(transform)
        poses.append(pose)

    order = sorted(range(len(joints)), key=lambda index: joints[index].count("/"))   # parents first, stable
    clip = {
        "formatVersion": FORMAT_VERSION,
        "frameRate": stage.GetTimeCodesPerSecond(),
        "startFrame": 1 if frames[0] is None else int(frames[0]),
        "joints": [{"path": joints[index], "markup": markups[index], "rest": rests[index]} for index in order],
        "poses": [[pose[index] for index in order] for pose in poses],
    }
    _write_clip(clip, clip_path)


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


def _dot_lists(a: list[float], b: list[float]) -> float:
    return sum(x * y for x, y in zip(a, b))


# ---- USD ----------------------------------------------------------------------------------------

def _check_clip(clip: dict, clip_path: str) -> list[dict]:
    """The clip's joints, after the checks write_usd needs to build a valid stage. The plugin's reader does the full
    validation when a command reads the file."""
    if clip.get("formatVersion") != FORMAT_VERSION:
        raise ValueError(f"{clip_path}: formatVersion {clip.get('formatVersion')!r} is not {FORMAT_VERSION}.")
    joints, poses = clip.get("joints"), clip.get("poses")
    if not joints or not poses:
        raise ValueError(f"{clip_path} has no joints or no poses.")
    if any(len(pose) != len(joints) for pose in poses):
        raise ValueError(f"{clip_path}: every pose needs one transform per joint ({len(joints)}).")
    invalid = sorted({name for joint in joints for name in joint["path"].split("/")
                      if not pxr.Sdf.Path.IsValidIdentifier(name)})
    if invalid:
        raise ValueError("These joint names are not valid USD identifiers: " + ", ".join(invalid))
    return joints


def _get_path_parent_indices(paths: list[str]) -> list[int | None]:
    """For each joint path, the index of the path above it, or None when the list has no such joint."""
    index_by_path = {path: index for index, path in enumerate(paths)}
    return [index_by_path.get(path.rpartition("/")[0]) for path in paths]


def _get_temp_usd_path(usd_path: str) -> str:
    """A sibling path with the same extension, so USD picks the same format and os.replace is atomic."""
    stem, extension = os.path.splitext(usd_path)
    return f"{stem}.writing{extension}"


def _write_stage_metadata(stage: pxr.Usd.Stage, clip: dict) -> None:
    start = clip["startFrame"]
    stage.SetTimeCodesPerSecond(clip["frameRate"])
    stage.SetFramesPerSecond(clip["frameRate"])
    stage.SetStartTimeCode(start)
    stage.SetEndTimeCode(start + len(clip["poses"]) - 1)
    pxr.UsdGeom.SetStageMetersPerUnit(stage, _METERS_PER_UNIT)
    pxr.UsdGeom.SetStageUpAxis(stage, pxr.UsdGeom.Tokens.z if _is_scene_z_up() else pxr.UsdGeom.Tokens.y)


def _is_scene_z_up() -> bool:
    return cmds.upAxis(query=True, axis=True).lower() == "z"


def _write_skeleton(
    stage: pxr.Usd.Stage,
    joints: list[dict],
    paths: list[str],
    parents: list[int | None],
) -> pxr.UsdSkel.Skeleton:
    skeleton = pxr.UsdSkel.Skeleton.Define(stage, _USD_SKELETON)
    rests = [_matrix_from_transform(joint["rest"]) for joint in joints]
    binds: list[pxr.Gf.Matrix4d] = []
    for rest, parent in zip(rests, parents):
        binds.append(rest if parent is None else rest * binds[parent])   # parents come first in a JointClip
    skeleton.CreateJointsAttr(pxr.Vt.TokenArray(paths))
    skeleton.CreateRestTransformsAttr(pxr.Vt.Matrix4dArray(rests))
    skeleton.CreateBindTransformsAttr(pxr.Vt.Matrix4dArray(binds))
    prim = skeleton.GetPrim()
    prim.CreateAttribute(_USD_MARKUP_ATTR, pxr.Sdf.ValueTypeNames.StringArray, custom=True).Set(
        pxr.Vt.StringArray([joint["markup"] for joint in joints]))
    prim.CreateAttribute(_USD_VERSION_ATTR, pxr.Sdf.ValueTypeNames.Int, custom=True).Set(FORMAT_VERSION)
    return skeleton


def _write_animation(animation: pxr.UsdSkel.Animation, clip: dict, paths: list[str]) -> None:
    poses = clip["poses"]
    translations = numpy.array([[joint["translate"] for joint in pose] for pose in poses], dtype=numpy.float32)
    scales = numpy.array([[joint["scale"] for joint in pose] for pose in poses], dtype=numpy.float16)
    animation.CreateJointsAttr(pxr.Vt.TokenArray(paths))
    translations_attr = animation.CreateTranslationsAttr()
    rotations_attr = animation.CreateRotationsAttr()
    scales_attr = animation.CreateScalesAttr()
    for index, pose in enumerate(poses):
        time = pxr.Usd.TimeCode(clip["startFrame"] + index)
        translations_attr.Set(pxr.Vt.Vec3fArray.FromNumpy(translations[index]), time)
        rotations_attr.Set(pxr.Vt.QuatfArray(
            [pxr.Gf.Quatf(*(float(value) for value in (joint["rotate"][3], *joint["rotate"][:3]))) for joint in pose]), time)
        scales_attr.Set(pxr.Vt.Vec3hArray.FromNumpy(scales[index]), time)


def _matrix_from_transform(transform: dict[str, list[float]]) -> pxr.Gf.Matrix4d:
    """The matrix of a JointClip transform, in USD's row-vector order: scale, then rotation, then translation."""
    x, y, z, w = transform["rotate"]
    scale = pxr.Gf.Matrix4d(1.0).SetScale(pxr.Gf.Vec3d(*transform["scale"]))
    rotation = pxr.Gf.Matrix4d(1.0).SetRotate(pxr.Gf.Quatd(w, pxr.Gf.Vec3d(x, y, z)))
    translation = pxr.Gf.Matrix4d(1.0).SetTranslate(pxr.Gf.Vec3d(*transform["translate"]))
    return scale * rotation * translation


def _decompose_matrix(matrix: pxr.Gf.Matrix4d) -> dict[str, list[float]]:
    """A JointClip transform from a matrix, at double precision (UsdSkel.DecomposeTransform returns half scale).
    Shear is dropped. The quaternion has a non-negative w, so the same rotation always gives the same numbers."""
    rotation = matrix.RemoveScaleShear().ExtractRotationQuat().GetNormalized()
    sign = -1.0 if rotation.GetReal() < 0.0 else 1.0
    imaginary = rotation.GetImaginary()
    return {
        "translate": list(matrix.ExtractTranslation()),
        "rotate": [sign * imaginary[0], sign * imaginary[1], sign * imaginary[2], sign * rotation.GetReal()],
        "scale": [pxr.Gf.Vec3d(*matrix.GetRow3(row)).GetLength() for row in range(3)],
    }


def _get_single_skeleton(stage: pxr.Usd.Stage, usd_path: str) -> pxr.Usd.Prim:
    skeletons = [prim for prim in stage.Traverse() if prim.IsA(pxr.UsdSkel.Skeleton)]
    if len(skeletons) != 1:
        raise ValueError(f"{usd_path} must have exactly one Skeleton, and has {len(skeletons)}.")
    return skeletons[0]


def _get_usd_markups(skeleton: pxr.Usd.Prim, joint_count: int, usd_path: str) -> list[str]:
    """The skeleton's rigmodules:jointMarkup, or "" for every joint when the file has none."""
    version = skeleton.GetAttribute(_USD_VERSION_ATTR)
    if version and version.HasValue() and version.Get() > FORMAT_VERSION:
        raise ValueError(f"{usd_path}: {_USD_VERSION_ATTR} {version.Get()} is newer than this module's {FORMAT_VERSION}.")
    markups = skeleton.GetAttribute(_USD_MARKUP_ATTR)
    if not markups or not markups.HasValue():
        cmds.warning(f"{usd_path} has no {_USD_MARKUP_ATTR}: its joints are all unmarked and will match no rig joint.")
        return [""] * joint_count
    values = [str(value) for value in markups.Get()]
    if len(values) != joint_count:
        raise ValueError(f"{usd_path}: {_USD_MARKUP_ATTR} has {len(values)} entries for {joint_count} joints.")
    return values


def _get_up_axis_conversion(stage_up_axis: str) -> pxr.Gf.Matrix4d | None:
    """The rotation that takes the stage's up axis to the scene's, or None when they match. USD rotates +Y to +Z by 90
    degrees about X."""
    scene_up_axis = "Z" if _is_scene_z_up() else "Y"
    if stage_up_axis == scene_up_axis:
        return None
    if stage_up_axis == "Y":
        angle = 90.0
    elif stage_up_axis == "Z":
        angle = -90.0
    else:
        raise ValueError(f"A stage with upAxis {stage_up_axis} can't be converted to the scene's {scene_up_axis}.")
    return pxr.Gf.Matrix4d(1.0).SetRotate(pxr.Gf.Rotation(pxr.Gf.Vec3d(1.0, 0.0, 0.0), angle))


def _apply_conversion(transform: dict[str, list[float]], conversion: pxr.Gf.Matrix4d) -> dict[str, list[float]]:
    return _decompose_matrix(_matrix_from_transform(transform) * conversion)


def _get_frames(anim_query: pxr.UsdSkel.AnimQuery, usd_path: str) -> list[int | None]:
    """The frames to sample: every whole frame from the first time sample to the last, or [None] (the default value)
    for an animation with no samples."""
    samples = anim_query.GetJointTransformTimeSamples()
    if not samples:
        return [None]
    first, last = math.ceil(min(samples)), math.floor(max(samples))
    if first > last:
        raise ValueError(f"{usd_path}: the animation has no whole frame between its samples {min(samples)} and {max(samples)}.")
    return list(range(first, last + 1))
