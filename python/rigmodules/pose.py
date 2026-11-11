"""Keying a rig's controls (D34). The pose commands (zeroRigControls, mirrorRigPose, flipRigPose, saveRigPose,
loadRigPose) set values and key nothing, so keying is a separate step."""

from __future__ import annotations

import maya.cmds as cmds

import rigmodules.query


def key_controls(controls: list[str], time: float | None = None) -> int:
    """Keys every channel of each control at the current time, or at time. One setKeyframe call for every plug,
    because that is far faster than one call per channel. Returns the number of channels keyed."""
    plugs = [
        f"{control}.{channel.name}"
        for control, channels in rigmodules.query.get_channels(controls).items()
        for channel in channels
    ]
    if not plugs:
        return 0
    kwargs = {} if time is None else {"time": time}
    cmds.setKeyframe(plugs, **kwargs)
    return len(plugs)


def key_rig(rig_root: str, time: float | None = None) -> int:
    """Keys every channel of every control in the rig. Returns the number of channels keyed."""
    controls = [control.node for control in rigmodules.query.get_rig_controls(rig_root)]
    return key_controls(controls, time)


def key_modules(rig_root: str, modules: list[str], time: float | None = None) -> int:
    """Keys every channel of every control of the given modules (module nodes of the rig). Returns the number of
    channels keyed. Raises RuntimeError for a module the rig has no controls for, so a typo isn't a silent no-op."""
    wanted = {_long_name(module) for module in modules}
    controls = [
        control.node
        for control in rigmodules.query.get_rig_controls(rig_root)
        if _long_name(control.module) in wanted
    ]
    found = {_long_name(control.module) for control in rigmodules.query.get_rig_controls(rig_root)}
    missing = sorted(wanted - found)
    if missing:
        raise RuntimeError(f"These are not modules with controls in {rig_root}: {', '.join(missing)}")
    return key_controls(controls, time)


def _long_name(node: str) -> str:
    found = cmds.ls(node, long=True)
    return found[0] if found else node
