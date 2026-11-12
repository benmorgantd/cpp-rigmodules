"""JointClip files: the bridge between USD joint files and the plugin's inverse solve (D41).

Skeleton: every function raises NotImplementedError. The plugin never links USD, so this module reads and writes it
with pxr (mayapy: cmds.loadPlugin("mayaUsdPlugin") first), and hands the plugin a JointClip file, a JSON file with the
joints, their markup and rest, and a list of JointPoses (design/inverse-solve.md, sections 6 and 7). It takes paths and
never resolves asset ids, and it imports no Qt or bmaya (D24).
"""

from __future__ import annotations


def read_usd(usd_path: str, clip_path: str) -> None:
    """Reads a UsdSkel file and writes the JointClip file clip_path for poseRigFromJointPose or
    animateRigFromJointClip. Converts meters per unit, the up axis and the frame rate to the scene's, and raises
    ValueError for a file that lacks the rigmodules: attributes or whose frame rate differs from the scene's."""
    raise NotImplementedError("rigmodules.jointclip.read_usd is not built yet (D41 skeleton)")


def write_usd(clip_path: str, usd_path: str) -> None:
    """Writes a JointClip file as a UsdSkel file."""
    raise NotImplementedError("rigmodules.jointclip.write_usd is not built yet (D41 skeleton)")


def capture(rig_root: str, start_frame: int, end_frame: int, usd_path: str, sidecar_path: str) -> None:
    """Samples the rig's export joints over a frame range into a USD file, and writes the sidecar beside it with the
    control channels the joints can't derive."""
    raise NotImplementedError("rigmodules.jointclip.capture is not built yet (D41 skeleton)")
