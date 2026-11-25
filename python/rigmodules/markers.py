"""Marker locators in a scene (D73), for tools that save a whole set of them.

The RigModules commands do the work: cmds.rigMarkerMarkup, cmds.saveRigMarkers, cmds.loadRigMarkers and
cmds.mirrorRigMarkers. This module knows the one thing a pipeline tool must not hard-code: the attribute that
marks a node as a marker. Choosing the file is the tool's job (D24).
"""

from __future__ import annotations

import maya.cmds as cmds

import rigmodules._namespace

MARKUP_ATTR = "rigMarkerMarkup"   # RigAttr::MarkerMarkup

# The editing locator loadRigMarkers makes (MarkerScene::createMarkerNodes): a rigControlNode in the Axis shape, drawn at
# DefaultDisplaySize. add_markers makes the same one, so a new marker looks like a loaded one.
_LOCATOR_TYPE = "rigControlNode"
_LOCATOR_SHAPE = "Axis"
DEFAULT_DISPLAY_SIZE = 2.0


def get_markers(namespace: str) -> list[str]:
    """Every marked node directly in namespace ("" or ":" for the root), as long names, sorted. The editing
    locators loadRigMarkers makes are the usual ones."""
    pattern = rigmodules._namespace.get_node_pattern(namespace)
    nodes = cmds.ls(f"{pattern}.{MARKUP_ATTR}", objectsOnly=True, long=True) or []
    return sorted(set(nodes))


def add_markers(joint: str, usages: list[str], side: str, display_size: float = DEFAULT_DISPLAY_SIZE) -> list[str]:
    """Makes one marker locator per usage at the joint, following it, in the joint's namespace: the locators
    loadRigMarkers makes, named in the convention (heel_00_L_MRK) by cmds.renameRigNodes. Move them into place,
    then save them with cmds.saveRigMarkers. A usage that already has a marker on that side in the namespace is a
    RuntimeError, and nothing is made. Returns the locators. One undo chunk."""
    namespace = cmds.ls(joint, showNamespace=True)[1]
    existing = {(cmds.rigMarkerMarkup(node, query=True, side=True)[0], cmds.rigMarkerMarkup(node, query=True, usage=True)[0]): node
                for node in get_markers(namespace)}
    taken = [existing[(side, usage)] for usage in usages if (side, usage) in existing]
    if taken:
        raise RuntimeError("These markers are already in the scene, so none were added: " + ", ".join(taken))
    matrix = cmds.xform(joint, query=True, worldSpace=True, matrix=True)
    shape_index = cmds.attributeQuery("shapeType", type=_LOCATOR_TYPE, listEnum=True)[0].split(":").index(_LOCATOR_SHAPE)
    scale = [display_size, 0, 0, 0, 0, display_size, 0, 0, 0, 0, display_size, 0, 0, 0, 0, 1]
    created = []
    cmds.undoInfo(openChunk=True, chunkName="addRigMarkers")
    try:
        for usage in usages:
            node = cmds.createNode(_LOCATOR_TYPE, name=f"{namespace.strip(':')}:marker" if namespace.strip(":") else "marker")
            cmds.xform(node, worldSpace=True, matrix=matrix)
            cmds.setAttr(f"{node}.shapeType", shape_index)
            cmds.setAttr(f"{node}.shapeTransform", scale, type="matrix")
            cmds.rigMarkerMarkup(node, usage=usage, side=side, joint=joint, export=False)
            created.append(cmds.renameRigNodes(node, base=usage, side=side)[0])
    finally:
        cmds.undoInfo(closeChunk=True)
    return created
