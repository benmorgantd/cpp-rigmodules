# Control library: poses and animation on the rig's own controls

**Status:** Decided (author, 2026-11-12), D42. The ControlAnim key layout and tangent encoding (section 4) are signed off (author, 2026-11-12). The FBX adapters, the bone map, the skeleton export, the ControlAnim file, tangents in the keys payload and `saveRigAnimation` / `loadRigAnimation` are built; the restore from joints with a ControlAnim sidecar is built too; mayadev's library UI and `animproperties` are not. The folder names, the export's control copy, the sampling and FBX as the joint file are settled (sections 10 and 12). Not built apart from the USD removal (section 13, done: tag `usd-joint-io`, suite 626 passed). Sections 2 and 6b hold the data vocabulary and the Unreal pairing. The command names in section 4 are proposals.

D41 built the inverse solve and picked USD as its carrier. It never asked what an animator's pose and animation library should be, so the file roles blurred: `poses/` was going to hold control poses and USD joint files side by side, the sidecar was a "pose file" that was not one, and nothing said how animation reaches an engine. This design starts from the library and the engine, and fits the inverse into it.

## 1. What changed in the premise

D41 assumed control-value libraries are the weak case ("restore 1:1 only on the rig they came from") and joints the strong one. The author's experience says the opposite when the rig is a plugin:

- A rig built from this plugin and a template matches another build of the same template by **control markup** (`side`, `usage`, channel name, D25), and an out-of-date rig has a defined update path (D33). Rigs don't drift apart the way hand-built studio rigs do.
- Control data keeps what joints can't: IK/FK blend, space switches, stretch, Layout controls, and the animator's keys and tangents.
- Joints are right for a smaller set of cases: a different rig or skeleton, mocap, and animation that arrives from an engine or another tool.

So **control data is the primary library**, and joint data (FBX, section 12) is for special remaps, mocap and the engine. The inverse solve stays as built and its role is smaller. D41's USD IO is retired (section 13).

## 2. The data types

Two families, which never share a file or a name. The word that was blurred before is "pose": it means a different thing in each family, so it is never used alone.

**Control data** is what the animator edits. It is bound to a rig's controls by control markup (D25) and holds channel values in UI units.
**Skeleton data** is what the engine and mocap speak. It is bound to a skeleton's joints by joint markup (D15), or by a bone map for a foreign skeleton, and holds joint transforms.

| Family | Name | One frame (a pose) | Many frames (an animation) | File |
|---|---|---|---|---|
| Control | ControlPose / ControlAnim | ControlPose: channel values, no keys, no time. | ControlAnim: sparse keys with tangents per channel (section 4). | `PoseFile` JSON (D34, built) / `<name>.anim.json` (new) |
| Skeleton | JointPose / JointClip | JointPose: every joint's local transform. | JointClip: a JointPose per frame, baked. | FBX (section 12). The JSON JointClip is a transient between Python and the plugin. |
| Skeleton | Skeleton asset | The joints at rest, the hierarchy and the joint markup. No animation. | | `<asset>_skeleton.fbx` and `<asset>_skeleton.bonemap.json` (section 6) |
| Engine | Animation properties | | Loop, root motion, events, curves, additive type. | `<name>.animproperties.json`, mayadev's (section 6) |

Rules that keep the families apart:

- **A ControlPose is not a JointPose.** One is channel values on a rig (an IK/FK blend, a space, a finger curl). The other is joint matrices. Converting between them is the inverse solve (forward is the rig itself), never a file rename.
- **The skeleton asset has no animation, and an animation FBX has no skeleton asset.** The engine imports the skeleton once, and each animation after it binds to that skeleton by bone name. The pairing is in section 6b.
- **A pose is one frame of an animation.** The two control formats stay separate files: a ControlPose has no time, no frame rate and no tangents, and it is applied as values without keys, like `loadRigPose`. Merging them would give a pose file a frame layer it never uses.

Terms to stop using: "pose file" for the sidecar, "sidecar" for control data stored beside a joint file, and "pose" on its own. Say ControlPose or JointPose.

## 3. Folders: one folder, one data type

The asset's file model (D29, `asset-model.md`) gets folders by role. The model is still planned for a mayadev branch, so the names are cheap to change.

| Folder | Holds | Files |
|---|---|---|
| `controlposes/` | ControlPoses | `<name>.json`, the `PoseFile` format unchanged |
| `controlanims/` | ControlAnims | `<name>.anim.json` |
| `exports/<name>/` | One engine export per folder (section 6) | `<name>.fbx`, `<name>.animproperties.json`, `<name>.anim.json` |
| `sources/` | Skeleton data brought in for a remap: mocap, an engine export, another rig's clip | `<name>.fbx`, and `<name>.bonemap.json` when the skeleton has no markup (section 12) |

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
                        [20, 45.0, "auto", "step"]
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
- **Payload:** `getRigKeys` and `setRigKeys` take and return the same shape. A two-number key (`[frame, value]`) stays valid and means two `auto` tangents, so existing callers and tests keep working. As built, `setRigKeys` now keys a two-number key with `auto` tangents where it used the scene's global default, and a curve's `weighted` flag and infinities are set from the data (a curve the data gives no flags goes back to unweighted and constant). Maya has no step in-tangent, so `"step"` as an in tangent reads back `auto`; the examples write `["auto", "step"]`.
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
    walk.animproperties.json   engine properties, read by the engine importer
    walk.anim.json        the ControlAnim the export was made from
```

- **The FBX is the engine's file.** The engine imports it against its skeleton asset. Bones bind by name, and the engine's skeleton asset comes from the skeleton export (below). Nothing in the engine path reads `walk.anim.json`.
- **The ControlAnim is a copy as of export time, and every export has one.** It makes the export reproducible and reopenable on the rig with keys and tangents, and it is the supplement for a restore through the joints. A copy, not a reference to `controlanims/`, so a later edit of the library entry can't change a past export.
- **`animproperties` is not control data.** Built 2026-11-12 in mayadev (`bmaya.asset.animproperties`, version 1): `loop`, `rootMotion` (a flag, not a joint), `additive` (`none`, `local`, `mesh`), `events` (name and frame) and float `curves` (name and keys of frame and value). The engine-side importer that reads it is not built, and Unreal was never run against it. It is engine data about the clip: loop, root motion, events and notifies, curves, sync markers, additive type, compression. It never mentions RigModules names, so by the placement rule it belongs to mayadev's asset tools. RigModules only keeps from blocking it.
- **The skeleton export** is the engine's skeleton asset source: an FBX of the export joints at rest, plus `<asset>_skeleton.bonemap.json`, the bone map (section 12) with the asset's `assetId` and `skeletonId`. This is the author's previous pipeline's asset skeleton export and bone id sidecar. A clip's export folder doesn't repeat the bone map: the clip is made on the asset's skeleton, so its joint names are the skeleton's.
- **How Unreal imports it** is an engine-side choice that this design doesn't fix: the FBX importer against the skeleton asset, or a script that builds `AnimSequence`s from the bone tracks and applies `animproperties`. It was not tested.

## 6b. The Unreal paths: skeleton, animation and control data pairing

Three things travel to the engine, and each has one owner and one place in the asset's folders.

| Item | Made from | Files | Engine-side |
|---|---|---|---|
| Skeleton asset | The rig's export joints at rest | `<asset>_skeleton.fbx` + `<asset>_skeleton.bonemap.json` (`assetId`, `skeletonId`) | The Skeleton (and mesh) asset, imported once |
| Animation | A ControlAnim, baked by `capture_joint_clip` | `exports/<name>/<name>.fbx` | An AnimSequence on that Skeleton, bound by bone name |
| Properties | Authored in mayadev | `exports/<name>/<name>.animproperties.json` | Loop, root motion, notifies, curves |

The ControlAnim copy (`<name>.anim.json`) sits in the export folder but isn't engine data: it makes the export reopenable on the rig.

**The pairing key is the skeleton.** An animation FBX is valid for a skeleton asset when its joint names and hierarchy match the skeleton's. The bone map's `skeletonId` is the stable name for that skeleton, and an export folder carries no copy of it: the clip is made on the asset's skeleton, so its names are the skeleton's. A check that compares an animation FBX's joints to the skeleton export's, and refuses a mismatch with the names that differ, is `rigmodules.jointclip.check_skeleton_match` (built 2026-11-12, in the adapter, not the engine). It compares joint names and parents and ignores rests, so an animation made on other proportions passes. A caller runs it before it hands an animation FBX on; `write_fbx` itself still checks the clip alone.

**Out of Maya (export), per animation:**
1. Pick a ControlAnim from `controlanims/` (or the animator's keys on the scene).
2. `capture_joint_clip` bakes the export joints to a JointClip.
3. `write_fbx` writes the animation FBX, with the skeleton's names.
4. mayadev writes `animproperties.json` and copies the ControlAnim beside them. The folder is `exports/<name>/`.
5. The skeleton export is made once per skeleton and again when the skeleton changes. A skeleton change is a versioned event: the bone map's `skeletonId` and `assetId` say which clips it can break.

**Into Maya (import), per source:**
1. A mocap take, an engine export or another rig's clip lands in `sources/` as an FBX, with a bone map when the skeleton has no markup.
2. `read_fbx` imports it, applies the bone map and captures a JointClip.
3. The inverse solve (`animateRigFromJointClip`) keys the rig's controls from the JointClip.
4. The animator cleans the keys on the rig and saves a ControlAnim to `controlanims/`. From then on the animation is control data, and the source FBX is kept only as a record.

**An export reopened** skips steps 1 to 3 of the import: the ControlAnim copy loads on the rig directly (`loadRigAnimation`), with the FBX as the fallback for a rig that can't use it (section 5).

**Not fixed here:** whether Unreal imports the FBX through its importer or a script that builds AnimSequences from bone tracks (section 6), and the import options. The spike decides both. Until it runs, this section is the intended flow, not a verified one.

## 7. What changes in D41

- **The animation sidecar is a ControlAnim, and the old sidecar format is gone** (built 2026-11-12, author: the flag keeps the name `sidecar`). With ControlAnims, a restore from joints gets custom channels (`space`, `ikFkBlend`) and non-derivable transforms (Layout) from the ControlAnim given as `sidecar`. `animateRigFromJointClip` samples its curves on the clip's frames in C++, on scratch animCurves that are deleted afterwards (`RigKeyScene::sampleRigKeys`), and hands the solve the per-frame values. The scene, the animator's keys and the undo queue are untouched until the one keying step. This replaces the plan to do it in Python (decision 3): a curve evaluator in `core/` was never needed, because Maya evaluates the curves. `core/AnimationSidecar` and its format are deleted, and nothing stored changes, because no sidecar file was ever written. `poseRigFromJointPose` keeps a ControlPose as its `sidecar`.
- **A channel only the sidecar gave keeps the sidecar's own keys** (author, 2026-11-12): the original exported keys, tangents and curve flags inside the restore range, not a key on every frame, because they are not joint data. A channel the joints gave is still baked on every frame, and the joints win when both have a channel.
- **The "derived channels" problem goes away.** The sidecar writer is no longer needed, and so neither is the list it would have consulted. Transform channels of a joint-driven control come from joints, and everything else from the ControlAnim.
- **The Layout keying wart is gone** (built 2026-11-12): the solve reports where each channel came from (`RigInverse::ChannelOrigins`), and `animateRigFromJointClip` keys only channels the joints or the sidecar gave. A control with no joints and no sidecar entry keeps its own keys. A clip that matches no joint and has no sidecar keys nothing.
- **`poses/` in `asset-model.md` and `mayadev-integration.md`** becomes `controlposes/` and holds only ControlPoses, and the sentence about USD pairing by basename in the same folder is replaced by section 3.
- **Not changed:** the inverse, `poseRigFromJointPose`, `animateRigFromJointClip` (their `sidecar` argument stays, now a ControlAnim for the animation command and a ControlPose for the pose command), the JointClip file and the keyframe command. USD IO is removed (section 13).

## 8. Names and compatibility

No existing control, channel, socket or child plug changes. `poses/<name>.json` files keep their format and move to `controlposes/`. `getRigKeys` output gains tangent fields only when the keys have non-default tangents, and `setRigKeys` input stays valid. The only renamed idea is the sidecar, and no sidecar file was ever written by a tool (the writer was never built), so nothing stored needs migration.

## 9. Build order

1. **Design sign-off** (this file, D42).
1b. **Spike** (section 12, Maya half done 2026-11-12, the Unreal half open): Maya's FBX export and import in mayapy, and one baked animation FBX into the target Unreal version. It decides the FBX options and whether the adapters work headless.
2. **ControlAnim in `core/`** (built 2026-11-12): the types, serialize and deserialize, the update table, validation. Tests by hand-written files, including every error path.
3. **Tangents in `RigKeyScene`** (built 2026-11-12): read and write, curve flags, stepped enum channels. Round trip test on a rig: key with mixed tangents, save, zero, load, compare keys and curve shape.
4. **`saveRigAnimation` and `loadRigAnimation`** (built 2026-11-12), with offset and mirror, docs and tests.
5. **FBX adapters and the skeleton export** (section 12). Built 2026-11-12: `rigmodules.jointclip.write_fbx`, `read_fbx` and `write_skeleton_fbx`, and `rigmodules.bonemap`.
6. **The restore from joints with a ControlAnim sidecar** (section 7). Built 2026-11-12, in C++ instead of Python.
7. **mayadev:** the folders, handlers and library UI, and `animproperties`.
8. IK, then retargeting, as in D41.

## 10. Decisions (author, 2026-11-12)

1. **Folder names:** `controlposes/` and `controlanims/`, so an old `anims/` full of USD can't be mistaken for them. `exports/` and `sources/` stay. An existing `poses/` folder, if any asset has one, moves to `controlposes/` (a mayadev change, no format change).
2. **Every export carries its ControlAnim copy.** It is the export's source data, so it is not optional.
3. **The restore samples through Maya.** Python loads the ControlAnim onto the rig and reads the non-derived channels per frame, so `core/` needs no curve evaluator.
4. **Tangents mirror** by negating fixed angles on a negated channel (section 4).
5. **The joint file is FBX** for mocap and Unreal (section 12).
6. **USD IO is retired** to drop the `pxr` dependency (section 13).

## 11. Maintenance surface and cost

- **New permanent surface:** `core/ControlAnimFile`, the extended keys payload, `saveRigAnimation` and `loadRigAnimation`, the folder rules, and `animproperties` in mayadev. A new `formatVersion` to maintain.
- **Complexity removed:** the stored sidecar format and its writer, the derived channels question, and the mixed-type `poses/` folder.
- **Evaluation cost:** none. No node or `compute()` change, so D36 isn't affected and no `bench` run is needed.
- **Tests:** ControlAnim file tests (core tier), key round trips with tangents (commands tier), the docs example test for each worked example, and a restore-with-supplement test (Python tier).
- **Docs:** `commands.md` for the two commands and the changed keys payload, a new `docs/library.md` for the folders, formats and paths (owned page, row in `docs/README.md`), and `docs/python.md` for any new Python function.

## 12. FBX is the joint file (author, 2026-11-12)

The author switched from USD to FBX because FBX is the mocap standard (Vicon, OptiTrack, Rokoko, Xsens and others hand over FBX) and it is the long-standing, mature path into Unreal. USD's advantages were mostly on RigModules' side: `pxr` runs without Maya, `.usda` diffs as text, and its schema is defined. FBX gives those up for the exchange format everyone else already speaks.

**The core doesn't change.** It depends on the JointClip, not on a file format (inverse-solve.md, section 7), so FBX is an adapter at the edge, written in Python in `rigmodules.jointclip` beside `write_usd` and `read_usd`. Both go through a Maya scene:

- **`write_fbx(clip_path, fbx_path)`**: builds a scratch skeleton of plain joints from the JointClip (real parenting, each joint's local translate, rotate and scale keyed on its frame), exports it with Maya's FBX exporter, and deletes it. A scratch skeleton, not the rig's joints, because the rig drives its joints through `offsetParentMatrix` with `inheritsTransform` off. The spike showed the exporter ignores `offsetParentMatrix`. Keying euler rotation needs the rule D41 already uses: the euler nearest the previous frame, so no curve flips.
- **`read_fbx(fbx_path, clip_path, bone_map=None)`**: imports the FBX into a scratch namespace, applies the bone map's markup to the imported joints with `rigJointMarkup`, runs `capture_joint_clip` over the file's frame range, and deletes the scratch nodes. Capture reads Maya's own space, so the unit and up-axis code written for USD isn't needed. The FBX import and export options (up axis, scale, frame rate) are still needed, and the engine's convention decides them (the spike).

**The bone map** is the bone id markup sidecar of the author's previous pipeline. A foreign FBX (mocap) has no `rigMarkup`, so a JSON file maps joint names to markup:

```json
{
    "formatVersion": 1,
    "assetId": "proj\\character\\hero",
    "skeletonId": "proj\\skeleton\\human",
    "joints": {
        "LeftArm": { "markupVersion": 1, "side": "Left", "usage": "upperArm", "export": true, "skin": true },
        "Hips": { "markupVersion": 1, "side": "Center", "usage": "pelvis", "export": true, "skin": true }
    }
}
```

- Each entry is the joint's `rigMarkup` object (D15, D16), keyed by the joint's leaf name, so one markup schema serves the scene, the clip and the map.
- Our own skeleton export writes one from the skeleton scene's markup, with `assetId` and `skeletonId` (D29). A mocap vendor's skeleton gets one authored once, kept in `sources/` and reused for every take.
- **The bone map is Python (`rigmodules.bonemap`), not `core/BoneMapFile` as first planned.** Only Python reads and writes it: the plugin sees the markup on the joints, never the map, so a core reader would have no caller. It has a `formatVersion` and refuses any other until a version 2 needs an update table (D33). If a C++ command ever reads a bone map, the format moves to `core/` then.
- A joint the map leaves out stays unmarked. It is kept for composing world matrices and never mapped to a rig (inverse-solve.md, section 6.1). The rig's `rigMarkup` rides in FBX user attributes only if the exporter keeps them, so the bone map is the dependable carrier.

**What it costs against USD:**

- **Needs a Maya session**, with the FBX plugin loaded. Whether it loads and runs under mayapy is part of the spike. A farm without Maya can't read a clip.
- **Scene round trip per file**, slower than reading USD with `pxr`.
- **A foreign skeleton's rest** is its pose when imported (its first frame), because FBX has no separate rest. That pose is only used for the source check and a later retarget. If mocap needs another rest frame, the bone map can name one. Not designed.
- **Tests:** the bone map and JointClip logic stay testable without FBX. The FBX round trip is a Maya test (write, read, compare every joint's matrices to the JointClip, tolerance 1e-4), and depends on the plugin loading in the suite.
- **Precision is better**: FBX stores doubles, where USD's scale was half precision.

**USD IO is retired** (author, 2026-11-12), see section 13.

**Spike first (build order 1b):** write one baked animation FBX from a scratch skeleton, check that Maya's exporter and importer run in mayapy, and import the file into the target Unreal version against a skeleton asset. Record the options that worked. Unreal's FBX importer is the standard path, but nothing here has been run.

### Spike results (2026-11-12, Maya 2026 mayapy, `tools/fbx_spike.py`)

Run: a keyed three-joint scratch skeleton (translate, rotate through 90 degrees and past, scale), exported baked, imported into a new scene, worldMatrix compared on 10 frames.

- **The FBX plugin loads and runs in mayapy.** `cmds.loadPlugin("fbxmaya")`, then the `FBXExport` and `FBXImport` MEL commands (called with `mel.eval`). The adapters can run headless.
- **The round trip is exact:** worst matrix error 4.5e-8 over 3 joints and 10 frames, including scale. Time unit (ntsc), frame range (1 to 10) and up axis (y) came back unchanged.
- **Options that worked:** `FBXResetExport`, `FBXExportBakeComplexAnimation -v true`, `FBXExportBakeComplexStart/End/Step`, `FBXExportUpAxis y`, binary, `FBXExport -f path -s` on the selected joint hierarchy. Import: `FBXResetImport`, `FBXImportMode -v add`. Hierarchy names came back as written.
- **A custom string attribute on a joint survived** (`rigMarkup`), but only one case was tried, on an unanimated joint. The bone map stays the dependable carrier.
- **The exporter does not honour `offsetParentMatrix`:** a joint at world (5, 7, 9) through its OPM came back at the origin. This confirms the scratch skeleton of plain joints (section 12). The rig's own joints can't be exported directly.
- **Not tested:** Unreal. The Unreal import (importer settings, bone binding by name, frame rate, up axis and scale against the skeleton asset) needs a manual run by the author, with the baked `walk.fbx` the script writes. Until then the Unreal paths in section 6b stay unverified. Also untried: a euler-flip case from a JointClip, and the FBX unit setting (centimeters).

## 13. Retiring USD (author, 2026-11-12). Done 2026-11-12

**Why.** The plugin never linked USD (D41), but the Python package did: `rigmodules/jointclip.py` imports `pxr` and `numpy` at module level, so even `capture_joint_clip`, which uses neither, fails to import without Maya's bundled USD build. That ties the package to the OpenUSD version Maya ships (0.24.11 in Maya 2026), and USD is no longer the export path. Removing it takes the dependency out of the package and a test file out of the suite.

**Remove, don't deprecate.** A deprecated adapter is dead code that still needs `pxr` to import. Git keeps it: tag the commit just before the removal as `usd-joint-io`, the way `legacy-rig-builder` keeps the old builder.

**What goes** (the removal is one commit):

| Where | Remove | Keep |
|---|---|---|
| `python/rigmodules/jointclip.py` | `write_usd`, `read_usd`, the `_USD_*` constants and the helpers only they use: `_get_path_parent_indices`, `_get_temp_usd_path`, `_write_stage_metadata`, `_is_scene_z_up`, `_write_skeleton`, `_write_animation`, `_matrix_from_transform`, `_decompose_matrix`, `_get_single_skeleton`, `_get_usd_markups`, `_get_up_axis_conversion`, `_apply_conversion`, `_get_frames`, `_dot_lists`, `_check_clip`, and the `pxr` and `numpy` imports. The module docstring loses its USD lines. | `capture_joint_clip`, `_write_clip`, `_get_frame_rate`, `_dot`, and the rest of capture's helpers |
| `tests/` | `commands/test_joint_clip_usd.py` and its `TIERS` line in `run_tests.py`. `UsdDocExample` in `concepts/test_docs_examples.py`. The "needs neither pxr nor" comments in `rigtest/jointclips.py` and `test_inverse_solve.py`. | `test_joint_clip_capture`, every solve and contract test |
| `docs/python.md` | The `write_usd` and `read_usd` rows and the "USD files" section, and the `pxr` note. | The capture row and the JointClip file format section |
| `cpp/core` | Comments only: "USD" and "UsdSkel's convention" in `JointClipFile.h`, `JointClipFile.cpp` and `Constants.h`. | The format: parent-local transforms, quaternion rotations, centimeters. No version bump, so FBX-fed clips use the same file. |
| `design/` and `CLAUDE.md` | The repo map's `jointclip.py` row loses `write_usd` and `read_usd`. `inverse-solve.md` section 6.1 (the USD layout) gets a withdrawn banner, as the rest of that file got its amendment. D41's USD IO bullet gets a pointer to D42. | The history |
| mayadev | `AnimClipHandler` saves and loads `.usd`. It loses that until `write_fbx` and `read_fbx` exist. Needs the author's go-ahead and a commit in that repo. | |

No C++ source, `.vcxproj`, `.mod` file or plugin build changes. The `.mll` is the same.

**What the removal leaves.** Capture, the JointClip JSON file and both restore commands, so a rig's joints can still be sampled and restored through JSON. There is no joint file format until the FBX adapters exist (build order 5). Nothing else in this repo reads USD, and the clip menu and the engine export are not built, so the gap costs no animator-facing feature. Doing it first is simpler than keeping `pxr` until the FBX path is done.

**What to carry into the FBX adapters** (lessons, not code kept as dead code): a quaternion keeps `w >= 0` and the sign nearest the previous frame; joints come parents before children; joints the bone map leaves out stay in the file; a write goes to a temporary file and `os.replace`, so a failed write leaves the old file; a name the format can't hold is refused before anything is written. `write_fbx` needs a clip check and a Maya-side matrix helper, and writes them fresh against `maya.api.OpenMaya` rather than the `pxr.Gf` versions.

**Tests.** The suite shrinks by the USD file's tests (the TOTAL moves, and `CLAUDE.md`'s 662 goes stale, so update it from the run). Proposed new test, in the concepts tier: no module under `python/rigmodules` imports `pxr` or `numpy`, checked statically like `test_source_layout.py`, so the dependency can't come back unnoticed. Needs a local build and suite run: the removal touches Python and tests that run in Maya.

**Risks.** Anyone who saved `.usd` clips loses the reader, and the tag is their way back. mayadev's handler breaks until FBX lands, so decide whether to remove its USD paths now or leave them failing with a clear message.
