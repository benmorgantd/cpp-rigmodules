# Control library: poses and animation on the rig's own controls

**Status:** Decided in direction (author, 2026-11-12), D42. Not built. Needs sign-off on the format and the open questions in section 10 before any code.

D41 built the inverse solve and picked USD as its carrier. It never asked what an animator's pose and animation library should be, so the file roles blurred: `poses/` was going to hold control poses and USD joint files side by side, the sidecar was a "pose file" that was not one, and nothing said how animation reaches an engine. This design starts from the library and the engine, and fits the inverse into it.

## 1. What changed in the premise

D41 assumed control-value libraries are the weak case ("restore 1:1 only on the rig they came from") and joints the strong one. The author's experience says the opposite when the rig is a plugin:

- A rig built from this plugin and a template matches another build of the same template by **control markup** (`side`, `usage`, channel name, D25), and an out-of-date rig has a defined update path (D33). Rigs don't drift apart the way hand-built studio rigs do.
- Control data keeps what joints can't: IK/FK blend, space switches, stretch, Layout controls, and the animator's keys and tangents.
- Joints are right for a smaller set of cases: a different rig or skeleton, mocap, and animation that arrives from an engine or another tool.

So **control data is the primary library**, and joint data (USD) is for special remaps and for the engine. The inverse solve and USD IO stay as built. Their role is smaller.

## 2. The data types

| Name | What it holds | Bound to | File |
|---|---|---|---|
| ControlPose | Control channel values, one frame | A rig's controls, by markup | `PoseFile` (D34), built |
| ControlAnim | Sparse keys with tangents per control channel | A rig's controls, by markup | New, section 4 |
| JointClip / JointPose | Joint transforms per frame | A skeleton, by joint markup | USD (D41), built. The JSON form is a transient. |
| Animation properties | Engine data about a clip: loop, root motion, events, curves, additive type | An engine asset | `<name>.animprops.json`, mayadev's, section 6 |

A pose is still one frame of an animation. The two control formats stay separate files: a ControlPose has no time, no frame rate and no tangents, and it is applied as values without keys, like `loadRigPose`. Merging them would give a pose file a frame layer it never uses.

Terms to stop using: "pose file" for the sidecar, and "sidecar" for control data stored beside a USD. Control data is control data wherever it is stored.

## 3. Folders: one folder, one data type

The asset's file model (D29, `asset-model.md`) gets folders by role. The model is still planned for a mayadev branch, so the names are cheap to change.

| Folder | Holds | Files |
|---|---|---|
| `poses/` | ControlPoses | `<name>.json` (unchanged, so existing poses stay valid) |
| `anims/` | ControlAnims | `<name>.anim.json` |
| `exports/<name>/` | One engine export per folder (section 6) | `<name>.usd`, `<name>.animprops.json`, `<name>.anim.json` |
| `sources/` | Joint data brought in for a remap: mocap, an engine export, another rig's clip | `<name>.usd` |

`poses/` and `anims/` never hold USD. An export folder is a bundle: its files describe one thing and travel together, so the rule is one folder per purpose, with its own folder per entry where an entry has several files.

mayadev's `AnimClipHandler` saves `<name>.usd` into `anims`. It moves to `exports/` or `sources/`, depending on direction.

## 4. ControlAnim

`getRigKeys` and `setRigKeys` carry `[frame, value]` and write global default tangents. A library file needs the tangents, so the keys payload grows and becomes a file with a version.

```json
{
    "formatVersion": 1,
    "frameRate": 30,
    "startFrame": 1,
    "endFrame": 60,
    "controls": {
        "Left": {
            "upperArm": {
                "rotateZ": {
                    "weighted": false,
                    "preInfinity": "constant",
                    "postInfinity": "constant",
                    "keys": [
                        [1, 0.0, "auto", "auto"],
                        [10, 90.0, [-12.5, 1.0], "flat"],
                        [20, 45.0, "step", "step"]
                    ]
                }
            }
        }
    }
}
```

- **Key:** `[frame, value, in, out]`. A tangent is a type name (`auto`, `clamped`, `linear`, `flat`, `step`, `spline`) or `[angleDegrees, weight]` for a fixed tangent. The default key is two `"auto"`. Frames are in the scene's UI time unit and values in UI units, like `cmds.keyframe` and the pose file.
- **Curve:** `weighted`, `preInfinity`, `postInfinity`. Nothing else about a curve is stored (no curve names, no driven keys, no non-control animation).
- **Which channels:** the keyable, writable channels of D34, by markup and channel name, exactly as a pose. A channel the rig lacks warns and is skipped. A control the file lacks keeps its keys.
- **Frame rate:** an error when it differs from the scene's, as in D41. A resample is not built.
- **Versioning:** its own `formatVersion` and update table (D33).
- **Payload:** `getRigKeys` and `setRigKeys` take and return the same shape. A two-number key (`[frame, value]`) stays valid and means two `auto` tangents, so existing callers and tests keep working. The `setRigKeys` default tangent changes from the scene's global default to `auto` only for keys that name a tangent.
- **Commands (names proposed):** `saveRigAnimation(rigRoot | *controls, file=path, startFrame=n, endFrame=n)` and `loadRigAnimation(rigRoot | *controls, file=path, offset=n, mirror=False)`. They follow D14. `loadRigAnimation` goes through `RigKeyScene::setRigKeys`, so it is one undo step.
- **Mirror on load** uses the pose tools' per-channel transform (D34, D28): copy or negate by channel, the Center axis flip. For keys a negate also negates tangent angles.

## 5. Which path restores what

| Use case | Path | Joints used? |
|---|---|---|
| Reuse a pose or animation on the same rig, or a rig from the same template | ControlPose / ControlAnim load | No |
| An older rig of the same asset | The same, after the D33 update | No |
| A different rig, a different skeleton, mocap, a game export | USD to JointClip, then the inverse solve | Yes |
| An engine export reopened with full fidelity | Its ControlAnim | No |
| An export reopened on a rig that can't use the control data | The inverse solve from its USD, with its ControlAnim supplying what joints can't | Yes |

Markup is the only mapping. The "if rig controls can map" case is the markup match, and it already tolerates differences: an unmatched control or channel is skipped with a warning (D34, D25). A real remap table (usage `forearm` to `lowerArm`) is a later feature. It is a small file keyed by markup and would sit beside the control data, but nothing needs it yet.

## 6. Engine export

An export of one animation is a folder, and the animation is described twice, for two readers:

```
exports/walk/
    walk.usd              joints: the export skeleton, baked, for the engine (D41 capture and write_usd)
    walk.animprops.json   engine properties, read by the engine importer
    walk.anim.json        the ControlAnim the export was made from
```

- **The USD is the engine's file.** The engine imports it against its skeleton asset. Bones bind by name, and the engine's skeleton asset comes from the skeleton export (below). Nothing in the engine path reads `walk.anim.json`.
- **The ControlAnim is a copy as of export time.** It makes the export reproducible and reopenable on the rig with keys and tangents, and it is the supplement for a restore through the joints. A copy, not a reference to `anims/`, so a later edit of the library entry can't change a past export.
- **`animprops` is not control data.** It is engine data about the clip: loop, root motion, events and notifies, curves, sync markers, additive type, compression. It never mentions RigModules names, so by the placement rule it belongs to mayadev's asset tools. RigModules only keeps from blocking it.
- **The skeleton export** is the same as a clip of one pose at rest: the export joints, their rest and markup, written through `write_usd`. It carries `rigmodules:assetId` and `rigmodules:skeletonId`, which were left out of D41 for lack of a reader. The engine now reads them, so they are written. The markup in the skeleton gives the engine's bone name to markup map, the bone id sidecar of the author's previous pipeline.
- **How Unreal imports it** (USD Importer or Interchange, or a script that builds `AnimSequence`s from the bone tracks and applies `animprops`) is an engine-side choice that this design doesn't fix. It was not tested.

## 7. What changes in D41

- **The animation sidecar stops being a stored file.** It was the per-frame, baked control supplement. With ControlAnims, a restore from joints gets custom channels (`space`, `ikFkBlend`) and non-derivable transforms (Layout) from the ControlAnim. To keep the C++ solve unchanged, Python loads the ControlAnim onto the rig, samples the non-derived channels per frame, and hands the solve the existing in-memory sidecar. So `AnimationSidecar` becomes a transient like the JointClip file, never saved in an asset. Doing it in Python avoids a curve evaluator in `core/` (open question 3).
- **The "derived channels" problem goes away.** The sidecar writer is no longer needed, and so neither is the list it would have consulted. Transform channels of a joint-driven control come from joints, and everything else from the ControlAnim.
- **The Layout keying wart goes away** if the solve stops writing a control it could not solve (D41 currently writes rest on every frame). A control with no joints and no supplement keeps its own keys.
- **`poses/` in `asset-model.md` and `mayadev-integration.md`** now holds only ControlPoses, and the sentence about USD pairing by basename in the same folder is replaced by section 3.
- **Not changed:** the inverse, `poseRigFromJointPose`, `animateRigFromJointClip` (their `sidecar` argument stays, fed by Python), the JointClip file, USD IO and the keyframe command.

## 8. Names and compatibility

No existing control, channel, socket or child plug changes. `poses/<name>.json` files keep working. `getRigKeys` output gains tangent fields only when the keys have non-default tangents, and `setRigKeys` input stays valid. The only renamed idea is the sidecar, and no sidecar file was ever written by a tool (the writer was never built), so nothing stored needs migration.

## 9. Build order

1. **Design sign-off** (this file, D42).
2. **ControlAnim in `core/`**: the types, serialize and deserialize, the update table, validation. Tests by hand-written files, including every error path.
3. **Tangents in `RigKeyScene`**: read and write, curve flags, stepped enum channels. Round trip test on a rig: key with mixed tangents, save, zero, load, compare keys and curve shape.
4. **`saveRigAnimation` and `loadRigAnimation`**, with offset and mirror, docs and tests.
5. **Skeleton export** and `assetId` and `skeletonId` in the USD.
6. **The restore from joints with a ControlAnim supplement** in Python (section 7).
7. **mayadev:** the folders, handlers and library UI, and `animprops`.
8. IK, then retargeting, as in D41.

## 10. Open questions (author)

1. **Folder names.** `poses/` and `anims/` stay as control-only and USD moves to `exports/` and `sources/`? Or rename the control folders (`controlposes/`, `controlanims/`) so an old `anims/` full of USD files can't be mistaken for them?
2. **Is the export's control copy required or optional?** I assumed an export always has one, because it is the export's source.
3. **Sampling for the restore.** Python samples the loaded keys through Maya (no new C++, a scene round trip), or a curve evaluator goes in `core/` (exact, in memory, but Maya's weighted tangents are real code to write and test)? I recommend Python.
4. **Tangent data on a mirror.** Negating a channel flips tangent angles. Whether to rewrite tangents on a mirror or bake and re-key is an implementation detail to decide when building step 4.

## 11. Maintenance surface and cost

- **New permanent surface:** `core/ControlAnimFile`, the extended keys payload, `saveRigAnimation` and `loadRigAnimation`, the folder rules, and `animprops` in mayadev. A new `formatVersion` to maintain.
- **Complexity removed:** the stored sidecar format and its writer, the derived channels question, and the mixed-type `poses/` folder.
- **Evaluation cost:** none. No node or `compute()` change, so D36 isn't affected and no `bench` run is needed.
- **Tests:** ControlAnim file tests (core tier), key round trips with tangents (commands tier), the docs example test for each worked example, and a restore-with-supplement test (Python tier).
- **Docs:** `commands.md` for the two commands and the changed keys payload, a new `docs/library.md` for the folders, formats and paths (owned page, row in `docs/README.md`), and `docs/python.md` for any new Python function.
