"""A rig's display layers (D38). The build makes the joint, mesh and control layers, and the model is imported
after it, so a pipeline tool puts the imported meshes in the mesh layer with add_meshes."""

from __future__ import annotations

import maya.cmds as cmds

LAYER_ATTRS = {"joint": "jointDisplayLayer", "mesh": "meshDisplayLayer", "control": "controlDisplayLayer"}


def get_layer(rig_root: str, kind: str) -> str:
    """The rig's "joint", "mesh" or "control" display layer. Raises RuntimeError for a rig built without it."""
    attr = LAYER_ATTRS[kind]
    layers = []
    if cmds.attributeQuery(attr, node=rig_root, exists=True):
        layers = cmds.listConnections(f"{rig_root}.{attr}", source=True, destination=False) or []
    if not layers:
        raise RuntimeError(f"{rig_root} has no {kind} display layer.")
    return layers[0]


def add_meshes(rig_root: str, nodes: list[str]) -> list[str]:
    """Puts the mesh objects among nodes (the new nodes of an import) in the rig's mesh layer, moving them out of
    any other layer. Each node counts as itself and everything under it, so a mesh shape, a mesh transform or an
    imported group all work. Intermediate shapes (a deformer's input) are skipped. Returns the mesh transforms
    added, which may be none."""
    layer = get_layer(rig_root, "mesh")
    shapes = cmds.ls(nodes, dag=True, type="mesh", noIntermediate=True, long=True) or []
    transforms = list(dict.fromkeys(cmds.listRelatives(shapes, parent=True, fullPath=True) or []))
    if transforms:
        cmds.editDisplayLayerMembers(layer, transforms, noRecurse=True)
    return transforms
