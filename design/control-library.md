# Control library: poses and animation on the rig's own controls

**Status:** Decided (author, 2026-11-12), D42. The folder names, the export's control copy, the sampling and FBX as the joint file are settled (sections 10 and 12). Not built. The ControlAnim format in section 4 still needs sign-off before code.

D41 built the inverse solve and picked USD as its carrier. It never asked what an animator's pose and animation library should be, so the file roles blurred: `poses/` was going to hold control poses and USD joint files side by side, the sidecar was a "pose file" that was not one, and nothing said how animation reaches an engine. This design starts from the library and the engine, and fits the inverse into it.

## 1. What changed in the premise

D41 assumed control-value libraries are the weak case ("restore 1:1 only on the rig they came from") and joints the strong one. The author's experience says the opposite when the rig is a plugin:

- A rig built from this plugin and a template matches another build of the same template by **control markup** (`side`, `usage`, channel name, D25), and an out-of-date rig has a defined update path (D33). Rigs don't drift apart the way hand-built studio rigs do.
- Control data keeps what joints can't: IK/FK blend, space switches, stretch, Layout controls, and the animator's keys and tangents.
- Joints are right for a smaller set of cases: a different rig or skeleton, mocap, and animation that arrives from an engine or another tool.

So **control data is the primary library**, and joint data (FBX, section 12) is for special remaps, mocap and the engine. The inverse solve stays as built and its role is smaller. D41's USD IO stays built and is no longer the export path.

## 2. The data types

| Name | What it holds | Bound to | File |
|---|---|---|---|
| ControlPose | Control channel values, one frame | A rig's controls, by markup | `PoseFile` (D34), built |
| ControlAnim | Sparse keys with tangents per control channel | A rig's controls, by markup | New, section 4 |
| JointClip / JointPose | Joint transforms per frame | A skeleton, by joint markup | FBX through Maya (section 12). The JSON form is a transient. USD IO (D41) is built and secondary. |
| Animation properties | Engine data about a clip: loop, root motion, events, curves, additive type | An engine asset | `<name>.animprops.json`, mayadev's, section 6 |

A pose is still one frame of an animation. The two control formats stay separate files: a ControlPose has no time, no frame rate and no tangents, and it is applied as values without keys, like `loadRigPose`. Merging them would give a pose file a frame layer it never uses.

Terms to stop using: "pose file" for the sidecar, and "sidecar" for control data stored beside a joint file. Control data is control data wherever it is stored.

## 3. Folders: one folder, one data type

The asset's file model (D29, `asset-model.md`) gets folders by role. The model is still planned for a mayadev branch, so the names are cheap to change.

| Folder | Holds | Files |
|---|---|---|
| `controlposes/` | ControlPoses | `<name>.json`, the `PoseFile` format unchanged |
| `controlanims/` | ControlAnims | `<name>.anim.json` |
| `exports/<name>/` | One engine export per folder (section 6) | `<name>.fbx`, `<name>.animprops.json`, `<name>.anim.json` |
| `sources/` | Joint data brought in for a remap: mocap, an engine export, another rig's clip | `<name>.fbx`, and `<name>.bonemap.json` when the skeleton has no markup (section 12) |

`controlposes/` and `controlanims/` never hold joint files. An export folder is a bundle: its files describe one thing and travel together, so the rule is one folder per purpose, with its own folder per entry where an entry has several files.

mayadev's pose handler writes `poses/<name>.json` today, so it and any existing files move to `controlposes/`. Its `AnimClipHandler` saves `<name>.usd` into `anims`. It moves to `exports/` or `sources/`, depending on direction, and writes FBX.

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
- **Mirror on load** uses the pose tools' per-channel transform (D34, D28): copy or negate by channel, and the Center axis flip. It is a flip (the sides swap). A negated channel negates its values and its fixed tangent angles, and weights, tangent types and frames never change. `auto`, `clamped`, `spline`, `linear`, `flat` and `step` tangents are recomputed from the negated keys and come out as the mirror image, so only an `[angle, weight]` tangent needs the sign change. Baking and re-keying is rejected: it throws away the sparse keys.

## 5. Which path restores what

| Use case | Path | Joints used? |
|---|---|---|
| Reuse a pose or animation on the same rig, or a rig from the same template | ControlPose / ControlAnim load | No |
| An older rig of the same asset | The same, after the D33 update | No |
| A different rig, a different skeleton, mocap, a game export | FBX to JointClip, then the inverse solve | Yes |
| An engine export reopened with full fidelity | Its ControlAnim | No |
| An export reopened on a rig that can't use the control data | The inverse solve from its FBX, with its ControlAnim supplying what joints can't | Yes |

Markup is the only mapping. The "if rig controls can map" case is the markup match, and it already tolerates differences: an unmatched control or channel is skipped with a warning (D34, D25). A real remap table (usage `forearm` to `lowerArm`) is a later feature. It is a small file keyed by markup and would sit beside the control data, but nothing needs it yet.

## 6. Engine export

An export of one animation is a folder, and the animation is described twice, for two readers:

```
exports/walk/
    walk.fbx              joints: the export skeleton, baked, for the engine (capture_joint_clip, then write_fbx)
    walk.animprops.json   engine properties, read by the engine importer
    walk.anim.json        the ControlAnim the export was made from
```

- **The FBX is the engine's file.** The engine imports it against its skeleton asset. Bones bind by name, and the engine's skeleton asset comes from the skeleton export (below). Nothing in the engine path reads `walk.anim.json`.
- **The ControlAnim is a copy as of export time, and every export has one.** It makes the export reproducible and reopenable on the rig with keys and tangents, and it is the supplement for a restore through the joints. A copy, not a reference to `controlanims/`, so a later edit of the library entry can't change a past export.
- **`animprops` is not control data.** It is engine data about the clip: loop, root motion, events and notifies, curves, sync markers, additive type, compression. It never mentions RigModules names, so by the placement rule it belongs to mayadev's asset tools. RigModules only keeps from blocking it.
- **The skeleton export** is the engine's skeleton asset source: an FBX of the export joints at rest, plus `<asset>_skeleton.bonemap.json`, the bone map (section 12) with the asset's `assetId` and `skeletonId`. This is the author's previous pipeline's asset skeleton export and bone id sidecar. A clip's export folder doesn't repeat the bone map: the clip is made on the asset's skeleton, so its joint names are the skeleton's.
- **How Unreal imports it** is an engine-side choice that this design doesn't fix: the FBX importer against the skeleton asset, or a script that builds `AnimSequence`s from the bone tracks and applies `animprops`. It was not tested.

## 7. What changes in D41

- **The animation sidecar stops being a stored file.** It was the per-frame, baked control supplement. With ControlAnims, a restore from joints gets custom channels (`space`, `ikFkBlend`) and non-derivable transforms (Layout) from the ControlAnim. To keep the C++ solve unchanged, Python loads the ControlAnim onto the rig, samples the non-derived channels per frame, and hands the solve the existing in-memory sidecar. So `AnimationSidecar` becomes a transient like the JointClip file, never saved in an asset. Doing it in Python avoids a curve evaluator in `core/` (decision 3).
- **The "derived channels" problem goes away.** The sidecar writer is no longer needed, and so neither is the list it would have consulted. Transform channels of a joint-driven control come from joints, and everything else from the ControlAnim.
- **The Layout keying wart goes away** if the solve stops writing a control it could not solve (D41 currently writes rest on every frame). A control with no joints and no supplement keeps its own keys.
- **`poses/` in `asset-model.md` and `mayadev-integration.md`** becomes `controlposes/` and holds only ControlPoses, and the sentence about USD pairing by basename in the same folder is replaced by section 3.
- **Not changed:** the inverse, `poseRigFromJointPose`, `animateRigFromJointClip` (their `sidecar` argument stays, fed by Python), the JointClip file, USD IO and the keyframe command.

## 8. Names and compatibility

No existing control, channel, socket or child plug changes. `poses/<name>.json` files keep their format and move to `controlposes/`. `getRigKeys` output gains tangent fields only when the keys have non-default tangents, and `setRigKeys` input stays valid. The only renamed idea is the sidecar, and no sidecar file was ever written by a tool (the writer was never built), so nothing stored needs migration.

## 9. Build order

1. **Design sign-off** (this file, D42).
1b. **Spike** (section 12): Maya's FBX export and import in mayapy, and one baked animation FBX into the target Unreal version. It decides the FBX options and whether the adapters work headless.
2. **ControlAnim in `core/`**: the types, serialize and deserialize, the update table, validation. Tests by hand-written files, including every error path.
3. **Tangents in `RigKeyScene`**: read and write, curve flags, stepped enum channels. Round trip test on a rig: key with mixed tangents, save, zero, load, compare keys and curve shape.
4. **`saveRigAnimation` and `loadRigAnimation`**, with offset and mirror, docs and tests.
5. **FBX adapters and the skeleton export** (section 12): `core/BoneMapFile`, `rigmodules.jointclip.write_fbx` and `read_fbx`, and the skeleton export with its bone map.
6. **The restore from joints with a ControlAnim supplement** in Python (section 7).
7. **mayadev:** the folders, handlers and library UI, and `animprops`.
8. IK, then retargeting, as in D41.

## 10. Decisions (author, 2026-11-12)

1. **Folder names:** `controlposes/` and `controlanims/`, so an old `anims/` full of USD can't be mistaken for them. `exports/` and `sources/` stay. An existing `poses/` folder, if any asset has one, moves to `controlposes/` (a mayadev change, no format change).
2. **Every export carries its ControlAnim copy.** It is the export's source data, so it is not optional.
3. **The restore samples through Maya.** Python loads the ControlAnim onto the rig and reads the non-derived channels per frame, so `core/` needs no curve evaluator.
4. **Tangents mirror** by negating fixed angles on a negated channel (section 4).
5. **The joint file is FBX** for mocap and Unreal (section 12). The open question is whether USD IO is kept or retired once the FBX path round-trips.

## 11. Maintenance surface and cost

- **New permanent surface:** `core/ControlAnimFile`, the extended keys payload, `saveRigAnimation` and `loadRigAnimation`, the folder rules, and `animprops` in mayadev. A new `formatVersion` to maintain.
- **Complexity removed:** the stored sidecar format and its writer, the derived channels question, and the mixed-type `poses/` folder.
- **Evaluation cost:** none. No node or `compute()` change, so D36 isn't affected and no `bench` run is needed.
- **Tests:** ControlAnim file tests (core tier), key round trips with tangents (commands tier), the docs example test for each worked example, and a restore-with-supplement test (Python tier).
- **Docs:** `commands.md` for the two commands and the changed keys payload, a new `docs/library.md` for the folders, formats and paths (owned page, row in `docs/README.md`), and `docs/python.md` for any new Python function.

## 12. FBX is the joint file (author, 2026-11-12)

The author switched from USD to FBX because FBX is the mocap standard (Vicon, OptiTrack, Rokoko, Xsens and others hand over FBX) and it is the long-standing, mature path into Unreal. USD's advantages were mostly on RigModules' side: `pxr` runs without Maya, `.usda` diffs as text, and its schema is defined. FBX gives those up for the exchange format everyone else already speaks.

**The core doesn't change.** It depends on the JointClip, not on a file format (inverse-solve.md, section 7), so FBX is an adapter at the edge, written in Python in `rigmodules.jointclip` beside `write_usd` and `read_usd`. Both go through a Maya scene:

- **`write_fbx(clip_path, fbx_path)`**: builds a scratch skeleton of plain joints from the JointClip (real parenting, each joint's local translate, rotate and scale keyed on its frame), exports it with Maya's FBX exporter, and deletes it. A scratch skeleton, not the rig's joints, because the rig drives its joints through `offsetParentMatrix` with `inheritsTransform` off. The exporter reads transform attributes and the DAG parent, and whether it honours `offsetParentMatrix` was not checked. This plan doesn't depend on it. Keying euler rotation needs the rule D41 already uses: the euler nearest the previous frame, so no curve flips.
- **`read_fbx(fbx_path, clip_path, bone_map=None)`**: imports the FBX into a scratch namespace, applies the bone map's markup to the imported joints with `rigJointMarkup`, runs `capture_joint_clip` over the file's frame range, and deletes the scratch nodes. Capture reads Maya's own space, so the unit and up-axis code written for USD isn't needed. The FBX import and export options (up axis, scale, frame rate) are still needed, and the engine's convention decides them (the spike).

**The bone map** is the bone id markup sidecar of the author's previous pipeline. A foreign FBX (mocap) has no `rigMarkup`, so a JSON file maps joint names to markup:

```json
{
    "formatVersion": 1,
    "assetId": "proj|character|hero",
    "skeletonId": "proj|skeleton|human",
    "joints": {
        "LeftArm": { "markupVersion": 1, "side": "Left", "usage": "upperArm", "export": true, "skin": true },
        "Hips": { "markupVersion": 1, "side": "Center", "usage": "pelvis", "export": true, "skin": true }
    }
}
```

- Each entry is the joint's `rigMarkup` object (D15, D16), keyed by the joint's leaf name, so one markup schema serves the scene, the clip and the map.
- Our own skeleton export writes one from the skeleton scene's markup, with `assetId` and `skeletonId` (D29). A mocap vendor's skeleton gets one authored once, kept in `sources/` and reused for every take.
- `core/BoneMapFile` holds the format and its update table (D33), with the reader, validation and tests. Python writes the file.
- A joint the map leaves out stays unmarked. It is kept for composing world matrices and never mapped to a rig (inverse-solve.md, section 6.1). The rig's `rigMarkup` rides in FBX user attributes only if the exporter keeps them, so the bone map is the dependable carrier.

**What it costs against USD:**

- **Needs a Maya session**, with the FBX plugin loaded. Whether it loads and runs under mayapy is part of the spike. A farm without Maya can't read a clip.
- **Scene round trip per file**, slower than reading USD with `pxr`.
- **A foreign skeleton's rest** is its pose when imported (its first frame), because FBX has no separate rest. That pose is only used for the source check and a later retarget. If mocap needs another rest frame, the bone map can name one. Not designed.
- **Tests:** the bone map and JointClip logic stay testable without FBX. The FBX round trip is a Maya test (write, read, compare every joint's matrices to the JointClip, tolerance 1e-4), and depends on the plugin loading in the suite.
- **Precision is better**: FBX stores doubles, where USD's scale was half precision.

**USD IO stays built, tested and not extended.** It remains a working second adapter, but it adds the `pxr` dependency and a test tier. Whether to keep it or retire it is an open question (author). My suggestion is to keep it until the FBX path round-trips, then decide.

**Spike first (build order 1b):** write one baked animation FBX from a scratch skeleton, check that Maya's exporter and importer run in mayapy, and import the file into the target Unreal version against a skeleton asset. Record the options that worked. Unreal's FBX importer is the standard path, but nothing here has been run.
