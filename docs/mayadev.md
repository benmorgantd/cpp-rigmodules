# mayadev: who uses RigModules

[mayadev](https://github.com/benmorgantd/mayadev) is the Python pipeline repo that sits on top of RigModules: the asset file model, the menus, the active rig tracker, the save and load tools, skinning and animation tools. Its package is `bmaya`. This page is for someone changing RigModules: which names mayadev calls, how to find the call sites, and what to do when a change touches one.

mayadev's own documentation is in its `docs/` folder. [`docs/rigmodules.md`](https://github.com/benmorgantd/mayadev/blob/main/docs/rigmodules.md) there owns the line-by-line list of every command and `rigmodules` function it calls, and which of its modules calls each. This page is the reverse view and doesn't repeat that list.

The boundary is decided (D24, [`design/mayadev-integration.md`](../design/mayadev-integration.md)): mayadev depends on RigModules and never the reverse, and it reaches a rig only through the `rigmodules` package and the commands, never by walking nodes or parsing names.

## What mayadev uses

| Area | RigModules names | mayadev code that calls them |
|---|---|---|
| Rig build | `createRigFromTemplate` (`template`, `asset`, `namespace`) | `bmaya.maya.assettools.build_rig` |
| Which rig, which asset | `rigmodules.query`: `get_rig_info`, `find_rig_info`, `get_rigs`, `RigInfo` (`rig_root`, `asset_id`, `variant`, `namespace`), `get_rig_controls`, `find_control`, `RIG_CONTROL_TYPE`. `rigmodules.template.get_rig_name` | `activerig`, `assettools`, `posetools`, `assethandlers` |
| Joint markup | `rigJointMarkup` (query and remove) | Two registry scripts in `toolsets/master_tool_registry.json` |
| Controls and shapes | `getRigControls`, `getRigMirrorControl`, `saveRigControlShapes`, `loadRigControlShapes`, `mirrorRigControlShapes` | Registry scripts, `assethandlers.RigShapesHandler`, `assettools` |
| Poses | `zeroRigControls`, `mirrorRigPose`, `flipRigPose`, `saveRigPose`, `loadRigPose`, `rigmodules.pose.key_rig`, `key_modules` | `posetools`, `assethandlers.ControlPoseHandler`, `assettools` |
| Animation | `saveRigAnimation`, `loadRigAnimation` | `assethandlers.ControlAnimHandler` and `AnimClipHandler`, `assettools` |
| Clips | `poseRigFromJointPose`, `animateRigFromJointClip` (`jointPose`, `jointClip`, `sidecar`), `rigmodules.jointclip`: `capture_joint_clip`, `write_fbx`, `read_fbx`, `write_skeleton_fbx`, `check_skeleton_match` | `assethandlers.AnimClipHandler`, `save_skeleton_export`, `assettools` |
| Skin weights | `setRigMeshId`, `saveRigSkinWeights`, `loadRigSkinWeights`, `rigmodules.skinweights`: `get_mesh_id`, `get_meshes`, `get_skinned_meshes`, `read_file_mesh_id` | `assethandlers.RigSkinWeightsHandler`, `assettools` |
| Imported model | `rigmodules.displaylayers.add_meshes`, `rigmodules.groups.parent_meshes` | `assettools.build_rig` |
| Asset id | The id rules in `core/AssetId` and the `assetId` on a built rig | `bmaya.asset.assetid`, the same rules in Python |

mayadev also reads RigModules' data formats as files, not through a function: it chooses where a shape file, weight file, ControlPose, ControlAnim or FBX clip lives, and passes that path to the command. It never parses them, except that `rigmodules.jointclip` converts FBX.

## Before changing a name mayadev uses

A rename, a removed flag or a changed return shape in anything in the table breaks mayadev, and the break is quiet in two places: the menu scripts in `toolsets/master_tool_registry.json` are Python strings in JSON, and mayadev's Maya tests skip without a plugin build.

1. **Find the call sites.** In a local session, search the mayadev checkout (`C:\Users\bmorgan\Documents\dev\maya\mayadev`). In a cloud session, `benmorgantd/mayadev` is in the session's repository scope and is cloned beside this repo (attach it with `add_repo` when it isn't). Search `py/scripts`, `toolsets` and `tests`:

   ```
   rg "saveRigAnimation|rigmodules\.jointclip" py/scripts toolsets tests
   ```

2. **Say it in the change summary.** List each mayadev file that calls the name, whether the change breaks it, and what the fix is. mayadev changes need the author's go-ahead and a commit in that repo ([`CLAUDE.md`](../CLAUDE.md)), so propose them, don't make them.
3. **Names are public API** ([`CLAUDE.md`](../CLAUDE.md)). A change that alters a name mayadev stores (a control `side` or `usage`, a channel name, a file type key) also breaks files already saved in mayadev's asset folders, not just its code.
4. **Update this page** when a new command or `rigmodules` function is meant for mayadev, and tell the author to add it to mayadev's `docs/rigmodules.md`.

## What mayadev expects of the files RigModules writes

| Expectation | Where it matters |
|---|---|
| A built rig names its own `assetId` and variant (`rigName`) | `rigmodules.query.RigInfo`. Every mayadev tool finds an asset folder through it |
| `saveRigPose`, `saveRigAnimation`, `saveRigControlShapes`, `saveRigSkinWeights` and `write_fbx` write exactly the path given, and the folder exists | mayadev makes the folders before it calls, and versions the old file first |
| Commands take forward slashes | mayadev converts Windows paths with `.replace("\\", "/")` |
| `loadRigSkinWeights` matches by the `meshId` in the file | The weight handler loads a folder, skipping files whose mesh isn't in the scene |
| `animateRigFromJointClip(sidecar=)` takes the ControlAnim saved with the clip | An export folder holds `<name>.fbx` beside `<name>.anim.json` ([`library.md`](library.md)) |

## Known mismatches

Found reading mayadev's code on 2026-11-14. The code wins, so these are flagged for the author, not changed.

- **ControlAnim file name.** [`library.md`](library.md) and [`design/control-library.md`](../design/control-library.md) put ControlAnims in `controlanims/` as `<name>.anim.json`. mayadev's `ControlAnimHandler` writes `controlanims/<name>.json`. It uses `.anim.json` only for the ControlAnim beside an exported clip. Undecided which side moves.
- **`mayadev.mod`** is described by [`design/mayadev-integration.md`](../design/mayadev-integration.md) as existing, but is not in the mayadev repo.
- The list of mayadev issues in that document is dated: see its "Status on 2026-11-14".

mayadev's own known problems (a menu entry that imports a module that doesn't exist, Python 2 files, duplicate plug-in folders) are in its `docs/status.md`. They are mayadev's to fix.
