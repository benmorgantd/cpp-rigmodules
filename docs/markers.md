# Markers

A marker is a placed point that follows one joint: a foot's heel and ball pivots, and later a light, an emitter or a gun's grip. It sits at a fixed offset from the joint, has no skin influence, is not a joint and is never driven by an animator. Markers live in a file of their own, beside the skeleton file and not inside it (D73, `design/markers.md`).

A module that needs a point no bind joint marks selects a marker with a `[side, usage]` pair, as it selects a joint. The [ReverseFoot](modules.md#reversefoot) is the first user.

## The file

`markers.json`, one marker per line, sorted by side then usage:

```json
{
    "formatVersion": 1,
    "markersHash": "61a75a97adb11637",
    "markers": [
        {"markup": ["Left", "ball"], "joint": ["Left", "ankle"], "export": false, "name": "ball_00_L_MRK", "offsetLocal": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 10, -8.7, 1]},
        {"markup": ["Left", "heel"], "joint": ["Left", "ankle"], "export": false, "name": "heel_00_L_MRK", "offsetLocal": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -9.5, -1.2, 1]}
    ]
}
```

| Key | Meaning |
|---|---|
| `markup` | `[side, usage]`, unique in the file. A separate namespace from joint markup: the marker `["Left", "heel"]` and a joint `["Left", "heel"]` can both exist, and the key a module reads says which it means |
| `joint` | The `[side, usage]` of the joint the marker follows. Required. A marker cannot follow another marker |
| `export` | `true` for a marker the engine needs. `false` for a rig-only marker such as a foot pivot. A file may carry `export: true` markers, which are read, hashed and stamped. A build makes no node for them yet |
| `name` | The node name without a namespace, `<camelCaseName>_##_<L|R|M>_MRK` ([naming.md](naming.md)) |
| `offsetLocal` | The marker's matrix relative to the joint, 16 numbers row by row with the translation in the last row, as `restLocal` in the [skeleton file](skeletons.md) |

An offset is in the joint's own space, in absolute lengths. Markers belong to an asset, and a file copied to another asset with the same marked joints is a starting point to adjust by hand, not a shared set.

## The markers hash

`markersHash` (16 hex digits) covers the marker count, then for each marker in sorted order its side, usage, joint side and usage, `export`, name and the 16 offset values rounded to steps of 0.0001. It uses the same 64-bit hash as the skeleton hash. The stored value is never trusted: loading recomputes it, and a difference is a warning.

The skeleton hash does not cover markers. Moving a heel pivot does not mark a weight file or a look stale, because no skin depends on it. It does change how a foot rolls, so the **rig root** stamps `markersHash` at build (empty when the build was given no file) and the placed markers as a `markers` string, [`getRigInfo`](commands.md#getriginfo) returns the hash as `markersHash`, and the `meta` of poses and animations records it. `loadRigPose` and `loadRigAnimation` warn when a file's hash and the rig's are both known and differ (D70: a status, never an error), and the file still loads. A file saved before markers existed has no value and reads as unknown, never as a difference.

## Authoring

A built rig has no node for a rig-only marker, so offsets are authored in an editing scene, the way joints are placed before `saveRigSkeleton`:

```python
cmds.loadRigSkeleton(file="C:/assets/hero/skeletons/body/skeleton.json", namespace="skel")
locator = cmds.spaceLocator()[0]
cmds.xform(locator, worldSpace=True, translation=(10, 0, 8.7))
cmds.rigMarkerMarkup(locator, usage="ball", side="Left", joint="skel:ankle_00_L_JNT", export=False)
cmds.saveRigMarkers(locator, file="C:/assets/hero/markers/body/markers.json")
# Returns the markers hash, "9a31c04d7e2b6f55"
```

`loadRigMarkers` puts a marked locator at each marker of an existing file, sized by its `displaySize`. `mirrorRigMarkers` writes the other side's marker. All five commands are in [commands.md](commands.md#rigmarkermarkup).

**The locators** are `rigControlNode`s in the `Pivot` shape, outside every rig. They show where a marker is but not which way it faces. The planned axis visualizer (a thin cylinder and a cone for each of X, Y and Z, coloured red, green and blue as Maya's manipulator is) is not built yet.

**Mirroring is an authoring step, not a build step.** A marker's offset is in its joint's frame, and a mirrored limb's joint frames are turned, so negating one axis of an offset depends on the chain's convention. `mirrorRigMarkers` reflects the world position across X = 0 and writes the other side's marker as an offset in that side's joint frame. The file holds both sides, and the template's `"mirror": true` only swaps the selector's side.

## Using markers in a build

```python
cmds.createRigFromTemplate("skel:root_00_M_JNT", template="C:/rigs/hero.json", asset="characters\\hero",
                           namespace="hero", markers="C:/assets/hero/markers/body/markers.json")
```

`markers` is optional. The build reads and validates the file, finds each marker's `joint` among the joints it was given, takes that joint's rest world matrix and multiplies it by `offsetLocal`. [`getRigMarkers`](commands.md#getrigmarkers) returns the result. A marker whose joint is not among the build's joints is left out with a warning, because the file may belong to a larger skeleton.

A module that selects a marker resolves it the same way and fails the build when the marker or its joint is missing, or no file was passed, naming the marker and the module, with every error listed at once. No module selects markers yet (the ReverseFoot is the first, [design/reverse-foot.md](../design/reverse-foot.md)).

A rig-only marker has no node: the module that selected it stores its rest matrix. A module that needs the point to move with a part of the rig takes that motion from its own child plug, never from a marker.
