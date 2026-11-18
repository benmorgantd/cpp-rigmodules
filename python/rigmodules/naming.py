"""The naming convention (D58): <camelCaseName>_##_<L|R|M>_<JNT|CTRL|MOD|CON>, such as upperArm_00_L_JNT.

Both functions call renameRigNodes, so the rules live in the plugin's NamingConvention class and nowhere else.
"""

from __future__ import annotations

import dataclasses
import json

import maya.cmds as cmds


@dataclasses.dataclass(frozen=True)
class RigName:
    """The parts of a name in the convention."""

    base: str    # camelCase, "upperArm"
    index: int   # 0 for "_00_"
    side: str    # "Left", "Right" or "Center"
    kind: str    # "Joint", "Control", "Module" or "Constraint"


def parse(name: str) -> RigName | None:
    """The parts of a name (a namespace is ignored), or None when it is not in the convention."""
    result = cmds.renameRigNodes(name, query=True)[0]
    if not result:
        return None
    return RigName(**json.loads(result))


def rename_nodes(nodes: list[str] | None = None, base: str | None = None, side: str | None = None, start_index: int = 0) -> list[str]:
    """Renames joints, rig controls, rig modules and rig constraints into the convention and returns the new names, in order.

    The nodes are the ones passed, else the selection, and the index counts up in that order from start_index: ``base="finger"``
    on three nodes gives finger_00_..., finger_01_..., finger_02_.... Leave base out to keep each node's own base and renumber
    only. side ("Left", "Right" or "Center") defaults to each node's current side, else "Center". The kind comes from the node's
    type. Fails without changing anything when a name is taken or repeated. One undo step.

    A selection of more than one node needs Maya to track selection order (cmds.selectPref(trackSelectionOrder=True)).
    """
    kwargs: dict = {"startIndex": start_index}
    if base is not None:
        kwargs["base"] = base
    if side is not None:
        kwargs["side"] = side
    return cmds.renameRigNodes(*(nodes or []), **kwargs)
