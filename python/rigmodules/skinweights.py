"""Skin weight helpers for tools that save and load a whole set of meshes.

The RigModules commands do the work: cmds.setRigMeshId, cmds.saveRigSkinWeights and cmds.loadRigSkinWeights.
This module knows the two things a pipeline tool must not hard-code: the meshId attribute, and where a weight
file keeps its meshId. Choosing folders and file names is the tool's job.
"""

from __future__ import annotations

import json

import maya.cmds as cmds

import rigmodules._namespace

MESH_ID_ATTR = "meshId"
MESH_NAME_KEY = "meshName"            # the mesh's name without a namespace when the file was saved
SKELETON_HASH_ATTR = "skeletonHash"   # a version 2 file's rig skeleton hash, "" for the file of an unbound save

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
    pattern = rigmodules._namespace.get_node_pattern(namespace)
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


def get_meshes_by_name(namespace: str) -> dict[str, str]:
    """Name without the namespace -> mesh transform, for every mesh in namespace, marked or not. The fallback match
    for a weight file whose meshId no mesh has: the file's meshName against these keys."""
    return {mesh.split("|")[-1].split(":")[-1]: mesh for mesh in _namespace_meshes(namespace)}


def set_mesh_id(mesh: str, mesh_id: str) -> None:
    """Gives the mesh transform this meshId, adding the attribute when it has none. Undoable. For taking a weight
    file's id when the mesh was matched by name: the next save of the model then carries it."""
    if not cmds.attributeQuery(MESH_ID_ATTR, node=mesh, exists=True):
        cmds.addAttr(mesh, longName=MESH_ID_ATTR, dataType="string")
    cmds.setAttr(f"{mesh}.{MESH_ID_ATTR}", mesh_id, type="string")


def get_skinned_meshes(namespace: str) -> list[str]:
    """The transforms of every mesh in namespace that has a skinCluster in its history, sorted."""
    return [mesh for mesh in _namespace_meshes(namespace) if cmds.ls(cmds.listHistory(mesh) or [], type="skinCluster")]


def read_file_mesh_id(path: str) -> str:
    """The meshId a weight file is for. Raises ValueError when the file has none."""
    mesh_id = _read_header_value(path, MESH_ID_ATTR)
    if not mesh_id:
        raise ValueError(f"{path} is not a weight file: it has no {MESH_ID_ATTR}.")
    return mesh_id


def read_file_mesh_name(path: str) -> str:
    """The name, without a namespace, of the mesh a weight file was saved from. "" when the file has none. Loading
    never matches on it, except the by-name fallback."""
    return _read_header_value(path, MESH_NAME_KEY) or ""


def read_file_skeleton_hash(path: str) -> str:
    """The skeleton hash a weight file was saved from, 16 hex digits. "" for a file with none: a version 1 file
    or one saved off a rig, which loadRigSkinWeights loads onto any skeleton."""
    return _read_header_value(path, SKELETON_HASH_ATTR) or ""


def _read_header_value(path: str, key: str) -> str | None:
    with open(path, encoding="utf-8") as handle:
        for _ in range(_HEADER_LINES):
            line = handle.readline().strip()
            if line.startswith(f'"{key}"'):
                return json.loads("{" + line.rstrip(",") + "}")[key]
    with open(path, encoding="utf-8") as handle:
        return json.load(handle).get(key)
