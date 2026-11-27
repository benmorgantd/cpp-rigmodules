"""Pose and ControlAnim library files: what a file holds without loading it onto a rig, and edits to its meta.

The files are written by cmds.saveRigPose and cmds.saveRigAnimation (pass meta=json.dumps(...) for the caller's part of
the meta) and read back by cmds.loadRigPose and cmds.loadRigAnimation. These functions wrap the two commands that work on
a file alone. The meta is a plain dict, keyed as in docs/library.md, so the keys can grow without this module changing.
"""

from __future__ import annotations

import dataclasses
import json

import maya.cmds as cmds


@dataclasses.dataclass(frozen=True)
class DataFileInfo:
    """What one library file holds. Built from getRigDataFileInfo's JSON result. error is set, and the rest has its
    defaults, for a file that could not be read or failed validation."""

    file: str
    error: str = ""
    kind: str = ""                  # "pose" or "animation"
    format_version: int = 0         # as stored in the file, before any update
    meta: dict[str, object] = dataclasses.field(default_factory=dict)
    frame_rate: float = 0.0         # animation only
    start_frame: float = 0.0        # animation only
    end_frame: float = 0.0          # animation only
    control_count: int = 0
    channel_count: int = 0
    unmatched_controls: tuple[tuple[str, str], ...] = ()   # (side, usage) of file controls the rig has no control for


def get_data_file_info(rig_root: str, files: list[str]) -> list[DataFileInfo]:
    """The info of every file, in the order given, in one command call. unmatched_controls says how well each file
    fits rig_root. A file that is missing or invalid gets a DataFileInfo with error set and does not stop the others.
    Raises RuntimeError when rig_root isn't a rig root."""
    if not files:
        return []
    entries = json.loads(cmds.getRigDataFileInfo(rig_root, file=list(files)))
    return [_read_entry(entry) for entry in entries]


def set_data_file_meta(file: str, meta: dict[str, object]) -> None:
    """Edits a file's meta and nothing else: each key replaces that field, and an empty string or list clears one. The
    fields the rig fills (assetId, rigName, skeletonHash, markersHash, pluginVersion, created) can't be set. Raises RuntimeError, leaving the file
    unchanged, for a bad value, an unknown key or a file that fails validation. The caller versions and checks the file
    out first, as for a save."""
    cmds.setRigDataFileMeta(file=file, meta=json.dumps(meta))


def _read_entry(entry: dict) -> DataFileInfo:
    if "error" in entry:
        return DataFileInfo(file=entry["file"], error=entry["error"])
    return DataFileInfo(
        file=entry["file"],
        kind=entry["kind"],
        format_version=entry["formatVersion"],
        meta=entry["meta"],
        frame_rate=entry.get("frameRate", 0.0),
        start_frame=entry.get("startFrame", 0.0),
        end_frame=entry.get("endFrame", 0.0),
        control_count=entry["controlCount"],
        channel_count=entry["channelCount"],
        unmatched_controls=tuple((side, usage) for side, usage in entry["unmatchedControls"]),
    )
