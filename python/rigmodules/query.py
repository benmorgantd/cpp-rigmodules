"""Queries on a built rig. Each one calls a RigModules command, which walks the rig by its connections."""

from __future__ import annotations

import dataclasses
import json

import maya.cmds as cmds

import rigmodules._namespace


@dataclasses.dataclass(frozen=True)
class RigInfo:
    """What a rig node belongs to (D29). Built from getRigInfo's JSON result."""

    rig_root: str          # the rigRootModule node
    asset_root: str        # the assetRootModule node that owns the rig's namespace
    asset_id: str          # "shooter|main|characters|hero"
    variant: str           # the rig root's rigName
    namespace: str         # the namespace the rig was built into, without a leading ":"
    rig_version: int       # the template's rigVersion
    plugin_version: str    # the RigModules version that built the rig
    publish_version: str   # the publish id the rig came from, "" until published


@dataclasses.dataclass(frozen=True)
class ControlChannel:
    """One channel of a control's animation interface (D34). Built from getRigControlChannels' JSON result.
    Values are in the scene's UI units, like cmds.getAttr and cmds.setAttr."""

    name: str              # the attribute's long name
    type: str              # Maya's attribute type: "double", "doubleAngle", "doubleLinear", "enum", "bool", ...
    rest: float            # the attribute default, which is the channel's value in the build pose
    min: float | None      # None when the attribute has no minimum
    max: float | None      # None when the attribute has no maximum
    mirror: str            # "copy" or "negate": how the value goes to the other side


RIG_ROOT_TYPE = "rigRootModule"


def get_rigs(namespace: str | None = None) -> list[RigInfo]:
    """Every rig in the scene, or only those built into namespace, sorted by rig root name."""
    pattern = rigmodules._namespace.get_node_pattern(namespace) if namespace else "*"
    rig_roots = cmds.ls(pattern, type=RIG_ROOT_TYPE, recursive=not namespace) or []
    return [get_rig_info(rig_root) for rig_root in sorted(rig_roots)]


def find_rig_info(node: str) -> RigInfo | None:
    """The rig that a rig root, rig module, rig control or rig-driven joint belongs to, or None for any other
    node. Reports nothing for those, so it suits a selection callback. Raises RuntimeError for a missing node
    or a broken rig, such as a rig root with no asset root."""
    text = cmds.getRigInfo(node)
    if not text:
        return None
    data = json.loads(text)
    return RigInfo(
        rig_root=data["rigRoot"],
        asset_root=data["assetRoot"],
        asset_id=data["assetId"],
        variant=data["rigName"],
        namespace=data["namespace"],
        rig_version=data["rigVersion"],
        plugin_version=data["pluginVersion"],
        publish_version=data["publishVersion"],
    )


def get_rig_info(node: str) -> RigInfo:
    """Like find_rig_info, but raises RuntimeError when the node isn't part of a rig."""
    info = find_rig_info(node)
    if info is None:
        raise RuntimeError(f"{node} is not a rig root, rig module, rig control or joint driven by a rig.")
    return info


def get_control_channels(control: str) -> list[ControlChannel]:
    """A rig control's channels in channel box order: its keyable attributes that a tool may write. Raises
    RuntimeError for a node that isn't a rig control, or a custom channel that declares no mirror mode."""
    (entries,) = json.loads(cmds.getRigControlChannels(control)).values()
    return [ControlChannel(**entry) for entry in entries]
