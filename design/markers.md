# Markers: placed points that follow a joint, kept in their own file

**Status:** Signed off by the author, 2026-11-24 (D73). Phases 0 and 1 built 2026-11-24 (file, hash, five commands, build flag, stamps, `MRK`), except module-side marker resolution, which is not built (see D73). The axis visualizer was built the same day for the editing locators (section 6). Written from the author's answers on the reverse foot (`reverse-foot.md`), with the same day's review folded in: markers are per asset, authoring goes through an Asset Editor tab, and a marker comes with an axis visualizer. The author chose option C: a real marker type, separate from the skeleton file.

## 1. The problem

A module sometimes needs a point that no bind joint marks: the reverse foot's pivots (heel, ball, bank out, bank in, toe tip), Aim's `target` and `up`, and later the engine's lights, VFX emitters and spawn points. Today the only way to place one is a joint marked `export: false, skin: false` (D57 kept that for the foot pivots, D15 and D54 define the flags). That has three costs:

- **A special case.** A joint that is not a joint. Every tool that walks joints has to know to skip it.
- **The wrong hash.** The skeleton hash (D54) counts every marked joint, marker joints included. Moving a heel pivot changes the skeleton hash, so every weight file and look is marked stale although no skin depends on the pivot. (It does change the animation, which is the other half of the problem, section 5.)
- **No engine side.** An `export: false` joint never reaches the engine. A light or an emitter has to, so it is a different thing.

D57 rejected a `markers` array in `skeleton.json` with its own node type. The author reopened it on 2026-11-24 because the foot is a second real case and the engine markers (D46, `module-directions.md` section 4) are a third.

## 2. What a marker is

A marker is a transform that sits at a fixed offset from one joint. It has no skin influence and is not a joint. It is never driven by an animator.

| Field | Meaning |
|---|---|
| `markup` | `[side, usage]`, unique in the file. A separate namespace from joint markup: `["Left", "heel"]` the marker and `["Left", "heel"]` a joint can coexist, and a selector says which it means (section 4) |
| `joint` | The `[side, usage]` of the joint it follows. Required. v1 has no marker parented to a marker |
| `offsetLocal` | The marker's matrix relative to that joint, 16 numbers row by row with translation in the last row, as `restLocal` in `skeleton.json` (`docs/skeletons.md`). Position and orientation both |
| `export` | `true` for a marker the engine needs (a light, an emitter). `false` for a rig-only marker such as a foot pivot |
| `name` | The node name without a namespace, `<camelCaseName>_##_<L|R|M>_MRK` (D58, a new type suffix, section 6) |

The offset is in the joint's own space, so a marker is **1:1 with a joint of a given markup**. Markers belong to an asset, like a skeleton, and are **not designed to be shared between assets** (author, 2026-11-24). Because the offset is joint-local, a file copied to another asset with the same marked joints is a good starting point to adjust, and could be referenced as is, but nothing here builds a sharing mechanism. v1 offsets are absolute lengths and do not scale with a bone, so a copy onto different proportions needs adjusting by hand. A proportional offset is an open question (section 10).

## 3. The file

`markers.json`, beside `skeleton.json` in kind, not inside it:

```json
{
    "formatVersion": 1,
    "markersHash": "9a31c04d7e2b6f55",
    "markers": [
        {"markup": ["Left", "heel"], "joint": ["Left", "ankle"], "export": false, "name": "heel_00_L_MRK", "offsetLocal": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -9.5, -1.2, 1]},
        {"markup": ["Left", "ball"], "joint": ["Left", "ankle"], "export": false, "name": "ball_00_L_MRK", "offsetLocal": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 10, -8.7, 1]}
    ]
}
```

- Sorted by side then usage, so an edit to one marker is a one-line diff, as in `skeleton.json`.
- **The hash** (`markersHash`, 16 hex digits) covers the marker count, then for each marker in file order its side, usage, joint side and usage, `export`, name and the 16 offset values divided by 0.0001 and rounded half away from zero. It reuses the skeleton file's 64-bit FNV-1a code in `core/`, so there is one hash routine and its rounding step is the same. The stored value is not trusted (it is recomputed on load, and a difference is a warning), as for skeletons.
- The skeleton hash does **not** cover markers, so a moved pivot no longer stales a weight file. Section 5 says what it stales instead.

## 4. How a build uses them

- **Input.** `createRigFromTemplate(..., markers="path/markers.json")`, one optional flag, like `template`. A template that selects a marker with no file passed is a read-phase error naming the marker.
- **Selectors.** A module argument names a marker with a `[side, usage]` pair, exactly like a joint selector, in a key whose meaning is "markers" (the reverse foot's `reverseHierarchy`). The module's `planFromJson` asks the context for a marker through `ctx` the way it asks for a joint. A mirrored module swaps the side (D28).
- **Resolution (read phase).** For each selected marker the context finds its `joint` among the joints given to the build, takes that joint's **rest world** and multiplies by `offsetLocal`. The result is a rest world matrix, with the marker's markup and `export` flag, handed to the plan. A joint that is not in the build, or a marker the file does not hold, is an error that names the marker and the module. Every error is reported at once (D17).
- **In the scene.** A rig-only marker has **no node**: the module that selected it stores its rest matrix, as Limb stores its rests. Fewer nodes is the rig's cost lever (D36, D52). Tools that need to see the placed points ask `getRigMarkers` (section 6). Only `export: true` markers get a scene node (phase 5, `MRK` transforms under the rig's joints, driven by the joint's world times the offset, `inheritsTransform` off), because the export has to find them.
- **Not parented.** A marker is placed from its joint's *rest*. A module that needs the point to move with a part of the rig gets that motion from its own child plug (the foot's `ik` and `end` plugs), never from a marker node. This keeps the DAG-parenting rule (`inheritsTransform` is off, nothing drives through DAG).

## 5. Stamps: markers change animation, not only skinning

The author's point: a pivot moved by 2 cm changes how a foot rolls, so an animation made against the old pivots does not play back the same on the new rig. A skin weight file does not care. So:

- The rig root gets a `markersHash` attribute, stamped by the build from the file it was given (empty when none was passed). `getRigInfo` returns it next to `skeletonHash`.
- The `meta` of poses and ControlAnim files (`LibraryMeta`, D51) gains a `markersHash` field. It is additive: an older file has no value and reads as "unknown", never as a mismatch.
- **A different hash is a status and never an error** (D70). The library browser shows "markers differ" on an animation or pose, and `loadRigPose` and `loadRigAnimation` report it. They still load: the control values are valid, the result may look different.
- Weight files and looks do not read `markersHash`. D54's skeleton hash rule is unchanged for the joints it covers.

## 6. Where it lives and the commands

**Files.** A new asset file kind next to skeletons (`asset-model.md` section on the file model): `markers/{markers}/markers.json` in the asset's folder, named from `rig.json` by the same two fields as a skeleton (`"markers": {"asset": "...", "name": "body"}`, optional). The two fields mean a rig could name another asset's set, as it can a skeleton, but the design does not rely on it (section 2). The asset side is mayadev's (`bmaya.core.asset`, the Asset Editor); RigModules defines the file and reads the path it is given (D24).

**Authoring: where the offset comes from.** A built rig has no node for a rig-only marker, so the offset is authored in an **editing scene**, the way joints are placed before `saveRigSkeleton`:

1. The skeleton is loaded (`loadRigSkeleton`) and `loadRigMarkers` puts a marked locator at each existing marker. For a new file there are none.
2. The rigger adds a marker for a joint: a new locator, marked with `rigMarkerMarkup(node, usage=, side=, joint=)`, and moves it to where the point belongs. Its offset is whatever its world is relative to the joint.
3. `saveRigMarkers` writes the file, and the locators are throwaway: they are never part of a built rig.

The tool for this is an **Asset Editor tab** (mayadev, proposed, like the Markup tab): a Markers asset type with **Add marker** for the selected joint or joints (a locator at each joint, marked with a usage and side the rigger picks, `joint` set to the selected joint) and **Mirror markers** (`mirrorRigMarkers` on the selected ones), then Save. It needs no command beyond the five below. The same `getRigMarkers` read-back shows a built rig's resolved points for checking.

**The visualizer (author, 2026-11-24).** A marker with a node comes with an **axis visualizer** that looks like Maya's own manipulator axes: a thin cylinder and a cone for each of X, Y and Z, coloured red, green and blue. It is a mesh in the generated control shape library (D26, `tools/` shape generator), made of three coloured parts, and it has a display size so a small point on a gun is not lost in a big scene. It is shown on:

**Built 2026-11-24 for the editing locators:** the `Axis` control shape (ShapeType 17), an arrow (a square shaft and an 8-sided cone) down each of +X, +Y and +Z from the origin, generated with per-point colours (`ShapeMesh::pointColors`, the first shape with any). The draw override passes them per vertex while the control is dormant and uses the selection colour when it is selected, like every shape; `wireframeAlpha` still fades it. A square shaft rather than a cylinder keeps the wire to four lines per axis. The rig's export-marker nodes do not exist yet, so the second bullet waits for them.

- the editing locators that `loadRigMarkers` and the Asset Editor's Add marker make, so the rigger sees the orientation as well as the place;
- every `export: true` marker's node in a built rig. Markers are also used as attachment points (a gun's grip) and are children of an *animated* joint, so the rigger and the animator both need to see them move with the hand. This is why the node exists for export markers and why the visualizer is not authoring-only.

A rig-only marker still has no node. Whether a build can show them anyway, for checking a foot's pivots, is an open question (section 10): the likely answer is a display command that builds temporary visualizer nodes from `getRigMarkers` and deletes them again.

**Commands** (each follows the D14 shape, `JointMarkupCmd` the reference; all examples are `maya.cmds`):

| Command | Does |
|---|---|
| `rigMarkerMarkup(node, usage=, side=, joint=, export=)` | Marks any transform as a marker: `usage`, `side`, the followed joint (default: the nearest marked ancestor joint) and `export`. Query form returns it, as `rigJointMarkup` does |
| `saveRigMarkers(*nodes, file=)` | Writes the file: each node's offset is its world times the followed joint's world inverse. Returns the hash. The joints should be at rest |
| `loadRigMarkers(file=, namespace=)` | Creates a locator for each marker at its place on the joints in the namespace, marked, for editing. Returns the nodes |
| `mirrorRigMarkers(*nodes)` | The authoring mirror: reflects the world position across X = 0 and writes the other side's marker, expressed in the other side's joint frame. Used while placing, never by the build |
| `getRigMarkers(rigRoot)` | Returns every marker the rig was built with as JSON text: markup, joint, `export`, and the resolved world at rest |

**Why an authoring mirror and not template mirroring (D28).** A marker's offset is in its joint's local frame, and a mirrored limb's joint frames are turned (a chain with -X down it, `RibbonMath::chainSign`). Negating one axis of an offset depends on that convention. Reflecting the world position and re-expressing it in the other joint's frame does not. So the file holds both sides, written by `mirrorRigMarkers`, and the template's `"mirror": true` only swaps the selector's side.

**Naming.** `MRK` joins `JNT | CTRL | MOD | CON` in `core/NamingConvention`, in `docs/naming.md` and in `cmds.renameRigNodes`. A `GEO` suffix was added the same way (D69).

## 7. Aim, and the marker joints left in a skeleton

Aim's `target` and `up` are the same thing (a placed point, no skin). Moving them to markers removes the last special case, and it needs a `moduleVersion` step on Aim (a template key changes from a joint selector to a marker selector) and a skeleton re-save for the author's assets (the skeleton hash changes once). **Not part of this change.** It is the follow-up the markers make possible, with its own entry. Until it lands, Aim keeps marker joints and D54's "marker joints count in the hash" still applies to them.

## 8. What it adds to the maintenance surface

A file format and its version table (D33), a hash, five commands, a build flag, two stamps (`markersHash` on the rig root and in `meta`), a `MRK` suffix, a resolution step in the read phase, an asset file kind on the mayadev side and its registry query. That is real, and it is the price of removing the joint special case and of having engine markers at all.

## 9. Export and engine markers

The file carries `export` from the first version so the format does not change when the engine side is built. What the engine round trip looks like (an extra section in the export skeleton, a second file, a node in the FBX) is **not decided here** and is phase 5. v1 builds rig-only markers; an `export: true` marker in a file is read, hashed and stamped, and builds nothing until phase 5. This answers D46's open question (is a marker an export extension or a module): neither, a file kind.

## 10. Open questions

1. **Proportional offsets.** Should an offset optionally scale with a joint's length so a copied file fits different proportions better? v1 says no, and sharing is not a goal (section 2).
2. **Where the engine reads markers from.** The marker file, or a section the export writes. Decided with phase 5.
3. **A marker under a marker.** v1 forbids it. The foot does not need it.
4. **Showing rig-only markers in a built rig.** A display command with temporary visualizer nodes, or none. Decided when the visualizer is built (phase 1 for the editing locators, phase 5 for export markers).

## 11. Tests and docs

- `core/`: the file read and write, every parse error, the hash (rounding, order independence, a one-line change changes it), a version step. A standalone test module in the layout tier like the skeleton file's.
- Maya: the commands (markup, save, load, mirror, get), resolution with a missing joint and a missing marker, the build with and without the flag, the stamp on the rig root and in a pose's `meta`, the stale status on load, `getRigInfo`.
- Docs: `docs/markers.md` (new, the user page), `docs/commands.md`, `docs/skeletons.md` (what the hash does not cover), `docs/naming.md` (`MRK`), `docs/templates.md` (marker selectors), `docs/source-map.md`, `docs/README.md`, `README.md`'s list, a worked example with a test in `test_docs_examples.py`.

## 12. mayadev

Additive: a new optional flag, a new optional `rig.json` key, a new meta field. **No existing call site breaks.** Proposed for the author, not made: `bmaya.core.asset` learns the `markers` kind, `build_rig` passes the flag, the Asset Editor gets a Markers tab beside Markup (add marker for the selected joints, mirror markers, save: section 6), and the library browser shows the "markers differ" status.
