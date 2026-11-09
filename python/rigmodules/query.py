"""Queries on a built rig. Each one calls a RigModules command, which walks the rig by its connections."""

from __future__ import annotations

import dataclasses
import json

import maya.cmds as cmds


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


RIG_ROOT_TYPE = "rigRootModule"


def get_rigs(namespace: str | None = None) -> list[RigInfo]:
    """Every rig in the scene, or only those built into namespace, sorted by rig root name."""
    pattern = f":{namespace.strip(':')}:*" if namespace else "*"
    rig_roots = cmds.ls(pattern, type=RIG_ROOT_TYPE, recursive=not namespace) or []
    return [get_rig_info(rig_root) for rig_root in sorted(rig_roots)]


def get_rig_info(node: str) -> RigInfo:
    """The rig that a rig root, rig module or rig control belongs to. Raises RuntimeError for any other node,
    or for a rig root with no asset root."""
    data = json.loads(cmds.getRigInfo(node))
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
