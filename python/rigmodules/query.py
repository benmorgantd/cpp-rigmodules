"""Queries on a built rig. Each one calls a RigModules command, which walks the rig by its connections."""

from __future__ import annotations

import dataclasses
import json

import maya.cmds as cmds

import rigmodules._namespace


@dataclasses.dataclass(frozen=True)
class LookInfo:
    """The look a rig wears (D54), from its lookRoot node. Built from the "look" object of getRigInfo's JSON result."""

    asset_id: str          # the look's own asset, which may differ from the rig's
    name: str              # the look's folder name in its asset
    publish_version: str   # the publish id the look came from, "" for an unpublished one


@dataclasses.dataclass(frozen=True)
class RigInfo:
    """What a rig node belongs to (D29). Built from getRigInfo's JSON result."""

    rig_root: str          # the rigRootModule node
    asset_id: str          # "shooter\\main\\characters\\hero"
    rig_name: str          # the rig root's rigName: the rig's folder name in the asset (D54)
    skeleton_hash: str     # the rig root's skeletonHash: its input joints' hash at build, 16 hex digits (D54)
    namespace: str         # the namespace the rig was built into, without a leading ":". It holds only this rig
    rig_version: int       # the template's rigVersion
    plugin_version: str    # the RigModules version that built the rig
    publish_version: str   # the publish id the rig came from, "" until published
    look: LookInfo | None = None   # the look the rig wears, None until setRigLook has applied one (D54)
    parent_rig_root: str | None = None   # the rig root an attachment follows, None for a rig that is not attached (D55)


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


@dataclasses.dataclass(frozen=True)
class RigControl:
    """What a rig control is (D25, D34). Built from getRigControlInfo's JSON result."""

    node: str              # the control's name
    module: str            # the module node that owns it
    module_type: str       # the template moduleType: "Layout", "FkChain", "Aim"
    side: str              # "Center", "Left" or "Right"
    usage: str             # the control markup usage. (side, usage) is unique in a rig.
    mirror: str | None     # the control on the other side. A Center control is its own mirror. None when it has none.


@dataclasses.dataclass(frozen=True)
class ControlSpace:
    """A control's space switch (D19, D50, D52). Built from getRigControlSpace's JSON result. A constraint space has a
    `constraint` and an empty `module`. A module-owned space (the TwoBoneIk pole's) has a `module` and an empty
    `constraint`. Switch either kind with setRigControlSpace on the control."""

    constraint: str        # the rigConstraint node of a constraint space, "" for a module-owned one
    labels: tuple[str, ...]  # the space labels, in order. The `space` enum lists the same names
    current: int           # the index in labels that the control is in now
    module: str = ""       # the module node of a module-owned space, "" for a constraint space


@dataclasses.dataclass(frozen=True)
class IkFkLimb:
    """A TwoBoneIk or Limb limb (D52, D62): the module, its controls, the settings control that holds `ikFkBlend`, and the
    blend now (0 is FK, 1 is IK, between is a blend). Pass `module` to `cmds.setRigIkFkBlend`."""

    module: str                  # the twoBoneIkModule or limbModule node
    controls: tuple[str, ...]    # the module's controls: a TwoBoneIk's six, a Limb's six and its ribbon's ik controls and tweakers
    settings: str                # the settings control: its channels are ikFkBlend, stretch and soft
    blend: float                 # the settings control's ikFkBlend


RIG_ROOT_TYPE = "rigRootModule"
RIG_CONTROL_TYPE = "rigControlNode"
TWO_BONE_IK_TYPE = "TwoBoneIk"
LIMB_TYPE = "Limb"
IK_FK_BLEND_CHANNEL = "ikFkBlend"


def get_rigs(namespace: str | None = None) -> list[RigInfo]:
    """Every rig in the scene, or only those built into namespace, sorted by rig root name."""
    pattern = rigmodules._namespace.get_node_pattern(namespace) if namespace else "*"
    rig_roots = cmds.ls(pattern, type=RIG_ROOT_TYPE, recursive=not namespace) or []
    return [get_rig_info(rig_root) for rig_root in sorted(rig_roots)]


def find_rig_info(node: str) -> RigInfo | None:
    """The rig that a rig root, rig module, rig control or rig-driven joint belongs to, or None for any other
    node. Reports nothing for those, so it suits a selection callback. Raises RuntimeError for a missing node
    or a broken rig, such as a rig module with no rig root."""
    text = cmds.getRigInfo(node)
    if not text:
        return None
    data = json.loads(text)
    look = data.get("look")
    return RigInfo(
        rig_root=data["rigRoot"],
        asset_id=data["assetId"],
        rig_name=data["rigName"],
        skeleton_hash=data["skeletonHash"],
        namespace=data["namespace"],
        rig_version=data["rigVersion"],
        plugin_version=data["pluginVersion"],
        publish_version=data["publishVersion"],
        look=LookInfo(asset_id=look["assetId"], name=look["name"], publish_version=look["publishVersion"]) if look else None,
        parent_rig_root=data.get("parentRigRoot"),
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


def find_control_space(control: str) -> ControlSpace | None:
    """The space switch of a rig control, or None when it has none. Raises RuntimeError for a node that isn't a rig
    control."""
    text = cmds.getRigControlSpace(control)
    if not text:
        return None
    data = json.loads(text)
    return ControlSpace(constraint=data["constraint"], labels=tuple(data["labels"]), current=data["current"], module=data["module"])


def get_rig_controls(
    rig_root: str,
    module_type: str | None = None,
    side: str | None = None,
    usage: str | None = None,
) -> list[RigControl]:
    """The rig's controls in module order, then each module's control order. Each filter is optional and every
    one given must match. A filter takes one value or a list of alternatives."""
    filters = {"moduleType": module_type, "side": side, "usage": usage}
    controls = cmds.getRigControls(rig_root, **{key: value for key, value in filters.items() if value is not None})
    return list(_get_controls_info(controls).values())


def find_control(node: str) -> RigControl | None:
    """The RigControl for a rig control, or None for any other node. Raises RuntimeError for a missing node or a
    broken control."""
    if cmds.nodeType(node) != RIG_CONTROL_TYPE:
        return None
    return get_control(node)


def get_control(node: str) -> RigControl:
    """The RigControl for a rig control. Raises RuntimeError when the node isn't one."""
    (control,) = _get_controls_info([node]).values()
    return control


def find_ik_fk_limb(control: str) -> IkFkLimb | None:
    """The TwoBoneIk or Limb limb a rig control belongs to, or None for a control of any other module. Raises
    RuntimeError for a node that isn't a rig control. The settings control is the limb control that has an
    ikFkBlend channel, so no control name or usage is parsed."""
    found = get_control(control)
    if found.module_type not in (TWO_BONE_IK_TYPE, LIMB_TYPE):
        return None
    rig_root = get_rig_info(control).rig_root
    controls = [other.node for other in get_rig_controls(rig_root, module_type=found.module_type) if other.module == found.module]
    channels = get_channels(controls)
    (settings,) = (name for name in controls if any(channel.name == IK_FK_BLEND_CHANNEL for channel in channels[name]))
    return IkFkLimb(found.module, tuple(controls), settings, cmds.getAttr(f"{settings}.{IK_FK_BLEND_CHANNEL}"))


def get_channels(controls: list[str]) -> dict[str, list[ControlChannel]]:
    """Each control's channels, keyed by the control name as the command returns it. One command call for all of
    them, so use it instead of get_control_channels in a loop. Raises like get_control_channels."""
    if not controls:
        return {}
    data = json.loads(cmds.getRigControlChannels(*controls))
    return {name: [ControlChannel(**entry) for entry in entries] for name, entries in data.items()}


def _get_controls_info(controls: list[str]) -> dict[str, RigControl]:
    if not controls:
        return {}
    data = json.loads(cmds.getRigControlInfo(*controls))
    return {
        name: RigControl(
            node=name,
            module=entry["module"],
            module_type=entry["moduleType"],
            side=entry["side"],
            usage=entry["usage"],
            mirror=entry["mirror"],
        )
        for name, entry in data.items()
    }
