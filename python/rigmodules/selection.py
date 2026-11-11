"""A rig's selection sets and the skinning selections built on them (D39).

The build makes one hierarchy of sets for the controls and one for the skin joints: a top set that holds a Left,
Right and Center set and a set that holds one set per module. Each set says what it is in its setMarkup
attribute, so tools find sets and joints through data, never names (D24).
"""

from __future__ import annotations

import json

import maya.cmds as cmds

SELECTION_SETS_ATTR = "selectionSets"
SET_MARKUP_ATTR = "setMarkup"
JOINT_MARKUP_ATTR = "rigMarkup"

CONTROLS = "controls"
JOINTS = "joints"   # the skin joints


def _get_set_markup(set_node: str) -> dict:
    return json.loads(cmds.getAttr(f"{set_node}.{SET_MARKUP_ATTR}"))


def find_set(rig_root: str, kind: str, side: str | None = None, module: str | None = None) -> str | None:
    """The rig's set of this kind ("controls" or "joints", the skin joints) for a side ("Left", "Right" or "Center")
    or a module (its node name without the namespace), or the top set with neither. None when there is no such
    set: a module with no skin joints has no joints set. The set that holds the module sets is not returned. Raises RuntimeError for a node that is not a rig root or has no sets."""
    if not cmds.attributeQuery(SELECTION_SETS_ATTR, node=rig_root, exists=True):
        raise RuntimeError(f"{rig_root} is not a rig root.")
    sets = cmds.listConnections(f"{rig_root}.{SELECTION_SETS_ATTR}", source=True, destination=False) or []
    if not sets:
        raise RuntimeError(f"{rig_root} has no selection sets.")
    for set_node in sets:
        markup = _get_set_markup(set_node)
        if markup["role"] != "modules" and markup["kind"] == kind and markup["side"] == side and markup["module"] == module:
            return set_node
    return None


def get_set(rig_root: str, kind: str, side: str | None = None, module: str | None = None) -> str:
    """find_set, raising RuntimeError instead of returning None."""
    set_node = find_set(rig_root, kind, side, module)
    if set_node is None:
        raise RuntimeError(f"{rig_root} has no {kind} set for side={side}, module={module}.")
    return set_node


def get_members(set_node: str) -> list[str]:
    """Every node in a set, nested sets included, as unique long names in set order."""
    members: dict[str, None] = {}
    for member in cmds.sets(set_node, query=True) or []:
        if cmds.nodeType(member) == "objectSet":
            members.update(dict.fromkeys(get_members(member)))
        else:
            members[cmds.ls(member, long=True)[0]] = None
    return list(members)


def _get_nodes(rig_root: str, kind: str, side: str | None, module: str | None) -> list[str]:
    """The nodes in the sets for a side and a module, the nodes in both when given both."""
    if side is None and module is None:
        return get_members(get_set(rig_root, kind))
    nodes: list[str] | None = None
    if side is not None:
        nodes = get_members(get_set(rig_root, kind, side=side))
    if module is not None:
        module_set = find_set(rig_root, kind, module=module)
        in_module = get_members(module_set) if module_set else []
        nodes = in_module if nodes is None else [node for node in nodes if node in in_module]
    return nodes


def get_skin_joints(rig_root: str, side: str | None = None, module: str | None = None, usage: str | None = None) -> list[str]:
    """The rig's skin joints, as long names, narrowed by side, module and usage. Narrowing by a module with no
    skin joints returns an empty list."""
    joints = _get_nodes(rig_root, JOINTS, side, module)
    if usage is not None:
        joints = [joint for joint in joints if json.loads(cmds.getAttr(f"{joint}.{JOINT_MARKUP_ATTR}"))["usage"] == usage]
    return joints


def select_skin_joints(rig_root: str, side: str | None = None, module: str | None = None, usage: str | None = None,
                       add: bool = False) -> list[str]:
    """Selects get_skin_joints(...), replacing the selection or, with add, extending it. Returns the joints."""
    joints = get_skin_joints(rig_root, side, module, usage)
    cmds.select(joints, add=add, replace=not add)
    return joints


def _get_influences(mesh: str) -> list[str]:
    """The long names of the influences of the mesh's skinCluster, none when it has no skinCluster."""
    clusters = cmds.ls(cmds.listHistory(mesh) or [], type="skinCluster")
    if not clusters:
        return []
    return cmds.ls(cmds.skinCluster(clusters[0], query=True, influence=True) or [], long=True)


def get_missing_influences(rig_root: str, mesh: str) -> list[str]:
    """The rig's skin joints that are not influences of the mesh's skinCluster. All of them when the mesh has
    none, so it is the joint list for a first bind."""
    bound = set(_get_influences(mesh))
    return [joint for joint in get_skin_joints(rig_root) if joint not in bound]


def get_extra_influences(rig_root: str, mesh: str) -> list[str]:
    """The influences of the mesh's skinCluster that are not skin joints of the rig: a joint marked not to
    skin, or one from outside the rig."""
    skin_joints = set(get_skin_joints(rig_root))
    return [joint for joint in _get_influences(mesh) if joint not in skin_joints]
