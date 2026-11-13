"""A rig's groups (D43). The build makes rig_all, the parent of the rig's topmost joints and controls, and mesh_grp
under it. The model is imported after the build, so a pipeline tool parents the imported transforms to mesh_grp
with parent_meshes."""

from __future__ import annotations

import maya.api.OpenMaya as om
import maya.cmds as cmds

GROUP_ATTRS = {"all": "allGroup", "mesh": "meshGroup"}


def get_group(rig_root: str, kind: str) -> str:
    """The rig's "all" (rig_all) or "mesh" (mesh_grp) group, as a full path: a model's own group can share its short
    name. Raises RuntimeError for a rig built without it."""
    attr = GROUP_ATTRS[kind]
    if cmds.attributeQuery(attr, node=rig_root, exists=True):
        selection = om.MSelectionList()
        selection.add(f"{rig_root}.{attr}")
        source = selection.getPlug(0).source()
        if not source.isNull:
            return om.MDagPath.getAPathTo(source.node()).fullPathName()
    raise RuntimeError(f"{rig_root} has no {kind} group.")


def parent_meshes(rig_root: str, nodes: list[str]) -> list[str]:
    """Parents the transforms among nodes (the new nodes of an import) to the rig's mesh group. Only the topmost
    ones move, since the rest follow their parents: a mesh shape counts as its transform, joints are skipped, and a
    transform already under the mesh group stays. World transforms are kept. Returns the topmost transforms moved
    (long names), which may be none. Undoable."""
    group = get_group(rig_root, "mesh")
    # Long names throughout: the model's own group may be called mesh_grp too, so a short name could mean either.
    candidates = [n for n in cmds.ls(nodes, long=True) or [] if cmds.nodeType(n) == "transform"]
    shapes = cmds.ls(nodes, type="mesh", noIntermediate=True, long=True) or []
    candidates += cmds.listRelatives(shapes, parent=True, fullPath=True) or []
    candidates = list(dict.fromkeys(candidates))

    members = set(candidates)
    topmost = [n for n in candidates if n != group and n.rpartition("|")[0] not in members and not n.startswith(group + "|")]
    if not topmost:
        return []
    cmds.parent(topmost, group)
    return [f"{group}|{n.rpartition('|')[2]}" for n in topmost]
