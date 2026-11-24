# The pose and animation library

How poses and animation are stored and moved: on the rig's own controls, on a skeleton, and into and out of an engine. The reasons are in [`design/control-library.md`](../design/control-library.md) (D42).

## Two kinds of data

| Kind | Bound to | Holds | Files |
|---|---|---|---|
| **Control data** | A rig's controls, by [control markup](controls.md) | Channel values in UI units | ControlPose (a pose file), ControlAnim |
| **Skeleton data** | A skeleton's joints, by [joint markup](../README.md) or a bone map | Joint transforms | JointClip (JSON, transient), FBX |

Control data is what an animator edits, and it is the primary library: a rig built from a template matches another build of the same template by markup, and keeps what joints can't (IK/FK blend, spaces, stretch, the animator's keys and tangents). Skeleton data is for a different rig or skeleton, mocap and the engine.

A **pose** is one frame and an **animation** is many, in each kind. A ControlPose is channel values with no time and no keys ([`saveRigPose`](commands.md#saverigpose)). A pose that has to be an animation is a ControlAnim over one frame, and a one-pose JointClip written with `write_fbx` is a one-frame animation ([python.md](python.md#fbx-files)).

A TwoBoneIk limb's channels (`ikFkBlend`, `stretch`, `soft`, `pin`, `roll`, and the pole's `space`) are in poses and animation like any other, by control markup and channel name, and `roll` negates when a pose or animation is mirrored. A raw change to `ikFkBlend` or the pole's `space`, whether by `setAttr`, a loaded pose or a key, doesn't match the other side's controls, so it can pop. [`setRigIkFkBlend`](commands.md#setrigikfkblend) is the switch that matches `ikFkBlend`. A pose or animation that holds `ikFkBlend` holds the controls of both sides, so it needs no match when it loads.

## Folders

One folder, one data type. Control data belongs to a rig, because control markup is unique per rig. A clip is skeleton data, so it belongs to a skeleton and every rig on that skeleton shares it. mayadev's asset tools make these folders, at these paths in the asset folder ([`design/asset-model.md`](../design/asset-model.md#file-types-in-projectjson)):

| Folder | Holds |
|---|---|
| `rigs/{rig}/controlposes/` | The rig's approved ControlPoses: `<name>.controlpose.json` |
| `rigs/{rig}/controlanims/` | The rig's approved ControlAnims: `<name>.controlanim.json` |
| `skeletons/{skeleton}/exports/<name>/` | One engine export: `<name>.fbx`, `<name>.animproperties.json`, and `<name>.controlanim.json`, a copy of the ControlAnim it was made from. The skeleton export sits in `exports/` itself |
| `sources/` | The asset's skeleton data brought in for a remap: mocap or an engine export as FBX, with a bone map when the skeleton has no markup |

An animator's saves don't go into the rig's folders. They go to their own library outside the asset, `<projects root>\user\<login>\animlibrary\<asset id>\<rig>\`, and the rig's folders hold the approved set (mayadev's `docs/assets.md`, "Library").

## ControlAnim files

Sparse keys with tangents, on the channels of a rig's controls, written by [`saveRigAnimation`](commands.md#saveriganimation) and read by [`loadRigAnimation`](commands.md#loadriganimation).

```json
{
    "formatVersion": 3,
    "meta": {
        "assetId": "shooter\\main\\characters\\hero",
        "rigName": "body",
        "skeletonHash": "3f2a9c0b7d4e1f68",
        "pluginVersion": "0.1",
        "created": "2026-11-14T10:22:00Z",
        "tags": ["walk"],
        "category": "Locomotion"
    },
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
- `meta` is the [file meta](#file-meta). It is empty (`{}`) when there is none.
- `frameRate` is the scene's frame rate when it was saved. A load into a scene at another rate is refused.
- `startFrame` and `endFrame` are the range saved. Every key lies inside it.
- `controls` is the [keys format](commands.md#keys-format), always written with every channel an object and every key four values. Controls and channels are named by markup (`side`, `usage`) and channel name, never by node name, so an animation survives renames and rebuilds.
- The file holds the keyable, writable channels of a control ([`getRigControlChannels`](commands.md#getrigcontrolchannels)). A channel the rig lacks is skipped with a warning on load, and a control the file lacks keeps its keys.
- Frames are in the scene's UI time unit and values in its UI units, like `cmds.keyframe`.
- Nothing else about a curve is stored: no curve names, no driven keys, no animation on non-control nodes.

## File meta

Every pose file and ControlAnim file has a `meta` object that says where the file came from and how to find it. A library browser searches it, and nothing in it changes what a load does. Every field is optional.

| Field | Type | Filled by |
|---|---|---|
| `assetId` | string | The rig, when the file is saved: the asset the rig was built for |
| `rigName` | string | The rig: its `rigName`, the rig's folder in the asset |
| `skeletonHash` | string | The rig: its `skeletonHash`, the skeleton it was built on (D54) |
| `pluginVersion` | string | The rig: the RigModules version that built it |
| `created` | string | The save: the time in UTC, `2026-11-14T10:22:00Z` |
| `template` | string | The caller: the template the rig was built from, for example `Human` |
| `skeleton` | string | The caller: the name of the skeleton the data goes with, for example `body` |
| `author` | string | The caller |
| `description` | string | The caller |
| `category` | string | The caller: free text, for example `Expressions` |
| `status` | string | The caller: a label on the work, for example `scratch`, `review` or `final` |
| `tags` | list of strings | The caller |
| `rigRegions` | list of strings | The caller: the parts of the rig the file affects, for example `hands` or `upperBody` |

The caller's part goes in through the `meta` flag of [`saveRigPose`](commands.md#saverigpose) and [`saveRigAnimation`](commands.md#saveriganimation), as a JSON object, and is edited later with [`setRigDataFileMeta`](commands.md#setrigdatafilemeta):

```json
{
    "tags": ["hands", "fist"],
    "category": "Expressions",
    "rigRegions": ["hands"],
    "status": "scratch",
    "author": "bmorgan",
    "template": "Human",
    "skeleton": "body",
    "description": "Clenched right hand"
}
```

- RigModules checks the shape of a value (a string, or a list of non-empty strings) and nothing else. The lists of regions and statuses a studio uses belong to the project, not to the file format.
- The fields the rig fills can't be set by a caller. An unknown key is an error.
- [`getRigDataFileInfo`](commands.md#getrigdatafileinfo) reads the meta of many files in one call, with each file's kind, size and how well it fits a rig, and `rigmodules.library.get_data_file_info` wraps it ([python.md](python.md#rigmoduleslibrary)).
- A file saved before the meta existed (formatVersion 1) loads as before and has an empty meta.
- In a formatVersion 2 file, `rigName` was `variant` and `skeleton` was `skeletonId`. It loads with the new names, and has no `skeletonHash`, so it matches no skeleton until it is saved again from a rig (D54).

## Moving data between rigs and the engine

| Use | Path |
|---|---|
| The same rig, or a rig from the same template | `loadRigPose` / `loadRigAnimation` |
| A different rig or skeleton, mocap, an engine export | FBX to a JointClip ([`read_fbx`](python.md#fbx-files)), then [`animateRigFromJointClip`](commands.md#animaterigfromjointclip), with the export's `<name>.controlanim.json` as its `sidecar` for what joints can't give |
| Animation to the engine | [`capture_joint_clip`](python.md#rigmodulesjointclip), then [`write_fbx`](python.md#fbx-files), with a skeleton export from [`write_skeleton_fbx`](python.md#fbx-files) |

The skeleton is the pairing key: an animation FBX goes with the skeleton export whose joint names and hierarchy it matches, and the bone map's `skeletonId` names that skeleton. The animation properties (loop, root motion, events) are engine data kept by mayadev's asset tools, not by RigModules. The Unreal import itself has not been tested ([`design/control-library.md`](../design/control-library.md), sections 6b and 12).
