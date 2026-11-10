"""Skin weight helpers for tools that save and load a whole set of meshes (D35).

The RigModules commands do the work: cmds.setRigMeshId, cmds.saveRigSkinWeights and cmds.loadRigSkinWeights.
This module knows the two things a pipeline tool must not hard-code: the meshId attribute, and where a weight
file keeps its meshId. Choosing folders and file names is the tool's job (D24).
"""

from __future__ import annotations

import json

import maya.cmds as cmds

MESH_ID_ATTR = "meshId"

# saveRigSkinWeights writes "meshId" near the top of the file, so its value can be read without parsing the
# whole weights list. A hand-edited file that moved it still reads, through the full parse.
_HEADER_LINES = 10


def get_mesh_id(mesh: str) -> str | None:
    """The mesh transform's meshId, or None when it has none."""
    if not cmds.attributeQuery(MESH_ID_ATTR, node=mesh, exists=True):
        return None
    return cmds.getAttr(f"{mesh}.{MESH_ID_ATTR}") or None


def _namespace_meshes(namespace: str) -> list[str]:
    """The transforms of every visible mesh shape directly in namespace ("" or ":" for the root), sorted."""
    pattern = f":{namespace.strip(':')}:*" if namespace.strip(":") else ":*"
    shapes = cmds.ls(pattern, type="mesh", noIntermediate=True, long=True) or []
    return sorted({cmds.listRelatives(shape, parent=True, fullPath=True)[0] for shape in shapes})


def get_meshes(namespace: str) -> dict[str, str]:
    """meshId -> mesh transform for every mesh in namespace that has a meshId. Raises RuntimeError when two share
    one: their weights would load onto each other. Give the copy a new id with setRigMeshId(newId=True)."""
    meshes: dict[str, str] = {}
    for mesh in _namespace_meshes(namespace):
        mesh_id = get_mesh_id(mesh)
        if not mesh_id:
            continue
        if mesh_id in meshes:
            raise RuntimeError(f"{meshes[mesh_id]} and {mesh} share meshId {mesh_id}. "
                               "Give the copy a new one with setRigMeshId(newId=True).")
        meshes[mesh_id] = mesh
    return meshes


def get_skinned_meshes(namespace: str) -> list[str]:
    """The transforms of every mesh in namespace that has a skinCluster in its history, sorted."""
    return [mesh for mesh in _namespace_meshes(namespace) if cmds.ls(cmds.listHistory(mesh) or [], type="skinCluster")]


def read_file_mesh_id(path: str) -> str:
    """The meshId a weight file is for. Raises ValueError when the file has none."""
    with open(path, encoding="utf-8") as handle:
        for _ in range(_HEADER_LINES):
            line = handle.readline().strip()
            if line.startswith(f'"{MESH_ID_ATTR}"'):
                return json.loads("{" + line.rstrip(",") + "}")[MESH_ID_ATTR]
    with open(path, encoding="utf-8") as handle:
        mesh_id = json.load(handle).get(MESH_ID_ATTR)
    if not mesh_id:
        raise ValueError(f"{path} is not a weight file: it has no {MESH_ID_ATTR}.")
    return mesh_id
