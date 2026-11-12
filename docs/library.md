# The pose and animation library

How poses and animation are stored and moved: on the rig's own controls, on a skeleton, and into and out of an engine. The reasons are in [`design/control-library.md`](../design/control-library.md) (D42).

## Two kinds of data

| Kind | Bound to | Holds | Files |
|---|---|---|---|
| **Control data** | A rig's controls, by [control markup](controls.md) | Channel values in UI units | ControlPose (a pose file), ControlAnim |
| **Skeleton data** | A skeleton's joints, by [joint markup](../README.md) or a bone map | Joint transforms | JointClip (JSON, transient), FBX |

Control data is what an animator edits, and it is the primary library: a rig built from a template matches another build of the same template by markup, and keeps what joints can't (IK/FK blend, spaces, stretch, the animator's keys and tangents). Skeleton data is for a different rig or skeleton, mocap and the engine.

A **pose** is one frame and an **animation** is many, in each kind. A ControlPose is channel values with no time and no keys ([`saveRigPose`](commands.md#saverigpose)). A pose that has to be an animation is a ControlAnim over one frame, and a one-pose JointClip written with `write_fbx` is a one-frame animation ([python.md](python.md#fbx-files)).

## Folders

One folder, one data type. An asset gets these (mayadev's asset tools make them):

| Folder | Holds |
|---|---|
| `controlposes/` | ControlPoses: `<name>.json` |
| `controlanims/` | ControlAnims: `<name>.anim.json` |
| `exports/<name>/` | One engine export: `<name>.fbx`, `<name>.animproperties.json`, a copy of the ControlAnim it was made from |
| `sources/` | Skeleton data brought in for a remap: mocap or an engine export as FBX, with a bone map when the skeleton has no markup |

## ControlAnim files

Sparse keys with tangents, on the channels of a rig's controls, written by [`saveRigAnimation`](commands.md#saveriganimation) and read by [`loadRigAnimation`](commands.md#loadriganimation).

```json
{
    "formatVersion": 1,
    "frameRate": 24.0,
    "startFrame": 1,
    "endFrame": 20,
    "controls": {
        "Left": {
            "upperArm": {
                "rotateZ": {
                    "weighted": false,
                    "preInfinity": "constant",
                    "postInfinity": "constant",
                    "keys": [[1, 0.0, "auto", "auto"], [10, 90.0, [-12.5, 1.0], "flat"], [20, 45.0, "auto", "step"]]
                }
            }
        }
    }
}
```

- `formatVersion` goes up only when saved data changes shape or meaning, and an older file is brought forward when it loads. A newer one is refused.
- `frameRate` is the scene's frame rate when it was saved. A load into a scene at another rate is refused.
- `startFrame` and `endFrame` are the range saved. Every key lies inside it.
- `controls` is the [keys format](commands.md#keys-format), always written with every channel an object and every key four values. Controls and channels are named by markup (`side`, `usage`) and channel name, never by node name, so an animation survives renames and rebuilds.
- The file holds the keyable, writable channels of a control ([`getRigControlChannels`](commands.md#getrigcontrolchannels)). A channel the rig lacks is skipped with a warning on load, and a control the file lacks keeps its keys.
- Frames are in the scene's UI time unit and values in its UI units, like `cmds.keyframe`.
- Nothing else about a curve is stored: no curve names, no driven keys, no animation on non-control nodes.

## Moving data between rigs and the engine

| Use | Path |
|---|---|
| The same rig, or a rig from the same template | `loadRigPose` / `loadRigAnimation` |
| A different rig or skeleton, mocap, an engine export | FBX to a JointClip ([`read_fbx`](python.md#fbx-files)), then [`animateRigFromJointClip`](commands.md#animaterigfromjointclip), with the export's `<name>.anim.json` as its `sidecar` for what joints can't give |
| Animation to the engine | [`capture_joint_clip`](python.md#rigmodulesjointclip), then [`write_fbx`](python.md#fbx-files), with a skeleton export from [`write_skeleton_fbx`](python.md#fbx-files) |

The skeleton is the pairing key: an animation FBX goes with the skeleton export whose joint names and hierarchy it matches, and the bone map's `skeletonId` names that skeleton. The animation properties (loop, root motion, events) are engine data kept by mayadev's asset tools, not by RigModules. The Unreal import itself has not been tested ([`design/control-library.md`](../design/control-library.md), sections 6b and 12).
