"""Bone maps: joint markup for a skeleton file that carries none (D42).

A foreign FBX (a mocap take, a vendor skeleton) has no rigMarkup, so a JSON file maps its joints' names to markup
objects, and read_fbx applies them. Our own skeleton export writes one from the rig's markup, with the asset id and
the skeleton id that say which rigs and clips the skeleton belongs to (design/control-library.md, section 12).

{
    "formatVersion": 1,
    "assetId": "proj|character|hero",
    "skeletonId": "proj|skeleton|human",
    "joints": { "LeftArm": { "markupVersion": 1, "side": "Left", "usage": "upperArm", "export": true, "skin": true } }
}

Each entry is the joint's rigMarkup object (cpp/core/JointMarkup.h), keyed by the joint's leaf name without
namespace. The format lives here, not in core/, because only Python reads and writes it: the plugin never sees a bone
map (it sees the markup on the joints). This module takes paths and imports no Qt or bmaya (D24).
"""

from __future__ import annotations

import json
import os

import maya.cmds as cmds

FORMAT_VERSION = 1
_MARKUP_VERSION = 1
_MARKUP_KEYS = {"markupVersion": int, "side": str, "usage": str, "export": bool, "skin": bool}


def write_bone_map(path: str, asset_id: str, skeleton_id: str, markups: dict[str, str]) -> None:
    """Writes a bone map. markups maps a joint's leaf name to its rigMarkup JSON string. Joints with no markup are
    left out of the map. A failed write leaves an existing file as it was."""
    joints = {name: json.loads(markup) for name, markup in sorted(markups.items()) if markup}
    problems = _check({"formatVersion": FORMAT_VERSION, "assetId": asset_id, "skeletonId": skeleton_id, "joints": joints})
    if problems:
        raise ValueError(f"Cannot write a bone map: {'; '.join(problems)}")
    data = {"formatVersion": FORMAT_VERSION, "assetId": asset_id, "skeletonId": skeleton_id, "joints": joints}
    temp = path + ".tmp"
    with open(temp, "w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=4)
    os.replace(temp, path)


def read_bone_map(path: str) -> dict:
    """The bone map in the file at path. Raises FileNotFoundError, and ValueError listing every problem found."""
    with open(path, encoding="utf-8") as handle:
        try:
            data = json.load(handle)
        except json.JSONDecodeError as error:
            raise ValueError(f"{path} is not valid JSON: {error}") from error
    problems = _check(data)
    if problems:
        raise ValueError(f"{path}: {'; '.join(problems)}")
    return data


def apply_bone_map(bone_map: dict, joints: list[str]) -> list[str]:
    """Writes the map's markup on the joints whose leaf name (without namespace) it lists, replacing markup they
    have. Joints the map doesn't list are left as they are. Returns the names the map lists that no joint has."""
    by_leaf = {joint.split("|")[-1].split(":")[-1]: joint for joint in joints}
    missing = []
    for name, markup in bone_map["joints"].items():
        joint = by_leaf.get(name)
        if joint is None:
            missing.append(name)
            continue
        if cmds.rigJointMarkup(joint, query=True)[0]:
            cmds.rigJointMarkup(joint, remove=True)
        cmds.rigJointMarkup(joint, side=markup["side"], usage=markup["usage"], export=markup["export"], skin=markup["skin"])
    return missing


def _check(data: object) -> list[str]:
    if not isinstance(data, dict):
        return ["the top level is not an object"]
    problems = []
    version = data.get("formatVersion")
    if not isinstance(version, int) or isinstance(version, bool):
        problems.append("formatVersion is missing or not an integer")
    elif version != FORMAT_VERSION:
        problems.append(f"formatVersion {version} is not {FORMAT_VERSION}")
    for key in ("assetId", "skeletonId"):
        value = data.get(key)
        if not isinstance(value, str) or not all(value.split("|")):
            problems.append(f"{key} is missing or not a '|' separated id")
    joints = data.get("joints")
    if not isinstance(joints, dict):
        return problems + ["joints is missing or not an object"]
    pairs: dict[tuple[str, str], str] = {}
    for name, markup in joints.items():
        if not isinstance(markup, dict):
            problems.append(f"joints.{name} is not an object")
            continue
        for key, kind in _MARKUP_KEYS.items():
            value = markup.get(key)
            if not isinstance(value, kind) or (kind is int and isinstance(value, bool)):
                problems.append(f"joints.{name}.{key} is missing or not a {kind.__name__}")
        if markup.get("markupVersion") not in (None, _MARKUP_VERSION) and isinstance(markup.get("markupVersion"), int):
            problems.append(f"joints.{name}.markupVersion {markup['markupVersion']} is not {_MARKUP_VERSION}")
        if isinstance(markup.get("usage"), str) and not markup["usage"]:
            problems.append(f"joints.{name}.usage is empty")
        if isinstance(markup.get("side"), str) and isinstance(markup.get("usage"), str):
            pair = (markup["side"], markup["usage"])
            if pair in pairs:
                problems.append(f"joints.{name} and joints.{pairs[pair]} share the markup {list(pair)}")
            pairs[pair] = name
    return problems
