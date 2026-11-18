# mayadev: who uses RigModules

[mayadev](https://github.com/benmorgantd/mayadev) is the Python pipeline repo that sits on top of RigModules: the asset file model, the menus, the active rig tracker, the save and load tools, skinning and animation tools. Its package is `bmaya`. This page is for someone changing RigModules: which names mayadev calls, how to find the call sites, and what to do when a change touches one.

mayadev's own documentation is in its `docs/` folder. [`docs/rigmodules.md`](https://github.com/benmorgantd/mayadev/blob/main/docs/rigmodules.md) there owns the line-by-line list of every command and `rigmodules` function it calls, and which of its modules calls each. This page is the reverse view and doesn't repeat that list.

The boundary is decided (D24, [`design/mayadev-integration.md`](../design/mayadev-integration.md)): mayadev depends on RigModules and never the reverse, and it reaches a rig only through the `rigmodules` package and the commands, never by walking nodes or parsing names.

## What mayadev uses

| Area | RigModules names | mayadev code that calls them |
|---|---|---|
| Rig build | `createRigFromTemplate` (`template`, `asset`, `namespace`) | `bmaya.maya.assettools.build_rig` |
| Which rig, which asset | `rigmodules.query`: `get_rig_info`, `find_rig_info`, `get_rigs`, `RigInfo` (`rig_root`, `asset_id`, `rig_name`, `skeleton_hash`, `namespace`), `get_rig_controls`, `find_control`, `RIG_CONTROL_TYPE`. `rigmodules.template.get_rig_name` | `activerig`, `assettools`, `posetools`, `assethandlers` |
| Joint markup | `rigJointMarkup` (query and remove) | `assettools.print_joint_markup_interactive` and `remove_joint_markup_interactive` |
| Controls and shapes | `getRigControls`, `getRigMirrorControl`, `saveRigControlShapes`, `loadRigControlShapes`, `mirrorRigControlShapes` | `assettools` (including the Mirror Shapes tools), `posetools` (Select All and Select Mirror Controls), `assethandlers.RigShapesHandler` |
| Spaces | `rigmodules.query.find_control_space` (`ControlSpace`: `constraint`, `module`, `labels`, `current`), `setRigControlSpace` (D52: either kind of space, by control) | `maya.rigpanel.RigPanelSession.refresh_space` and `set_space` |
| IK/FK limbs | `rigmodules.query.find_ik_fk_limb` (`IkFkLimb`: `module`, `controls`, `settings`, `blend`), `setRigIkFkBlend` (by module), `rigmodules.pose.key_controls` for the keyed switch | `maya.rigpanel.RigPanelSession.refresh_limb` and `set_ik_fk` |
| Poses | `zeroRigControls`, `mirrorRigPose`, `flipRigPose`, `saveRigPose`, `loadRigPose`, `rigmodules.pose.key_rig`, `key_modules` | `posetools`, `assethandlers.ControlPoseHandler`, `assettools` |
| Animation | `saveRigAnimation`, `loadRigAnimation` | `assethandlers.ControlAnimHandler` and `AnimClipHandler`, `assettools` |
| Clips | `poseRigFromJointPose`, `animateRigFromJointClip` (`jointPose`, `jointClip`, `sidecar`), `rigmodules.jointclip`: `capture_joint_clip`, `write_fbx`, `read_fbx`, `write_skeleton_fbx`, `check_skeleton_match` | `assethandlers.AnimClipHandler`, `save_skeleton_export`, `assettools` |
| Skin weights | `setRigMeshId`, `saveRigSkinWeights`, `loadRigSkinWeights`, `rigmodules.skinweights`: `get_mesh_id`, `get_meshes`, `get_skinned_meshes`, `read_file_mesh_id`, `read_file_skeleton_hash` | `assethandlers.RigSkinWeightsHandler`, `assettools` |
| Imported model | `rigmodules.displaylayers.add_meshes`, `rigmodules.groups.parent_meshes` | `assettools.build_rig` |
| Asset id | The id rules in `core/AssetId` and the `assetId` on a built rig | `bmaya.core.asset.assetid`, the same rules in Python |

mayadev also reads RigModules' data formats as files, not through a function: it chooses where a shape file, weight file, ControlPose, ControlAnim or FBX clip lives, and passes that path to the command. It never parses them, except that `rigmodules.jointclip` converts FBX.

## Before changing a name mayadev uses

A rename, a removed flag or a changed return shape in anything in the table breaks mayadev, and the break is quiet because a command is called inside a mayadev function and mayadev's Maya tests skip without a plugin build. The menu entries in `toolsets/master_tool_registry.json` name a mayadev function (a module, a function and keyword arguments) and hold no code, so they are not a place to search.

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
| A built rig names its own `assetId`, `rigName` and `skeletonHash`, and a namespace holds one rig | `rigmodules.query.RigInfo`. Every mayadev tool finds an asset folder through it |
| `saveRigPose`, `saveRigAnimation`, `saveRigControlShapes`, `saveRigSkinWeights` and `write_fbx` write exactly the path given, and the folder exists | mayadev makes the folders before it calls, and versions the old file first |
| Commands take forward slashes | mayadev converts Windows paths with `.replace("\\", "/")` |
| `loadRigSkinWeights` matches by the `meshId` in the file | The weight handler loads a folder, skipping files whose mesh isn't in the scene |
| `animateRigFromJointClip(sidecar=)` takes the ControlAnim saved with the clip | An export folder holds `<name>.fbx` beside `<name>.controlanim.json` ([`library.md`](library.md)) |

## Known mismatches

Found reading mayadev's code on 2026-11-14. The code wins, so these are flagged for the author, not changed.

- **Library file names (decided 2026-11-14, D51, built in mayadev 2026-11-14).** [`library.md`](library.md) names ControlAnims `<name>.controlanim.json` and ControlPoses `<name>.controlpose.json`. mayadev's `ControlDataHandler` and `CONTROL_ANIM_SUFFIX` write those names now, list a legacy `<name>.json` and load it, and its Save Pose and Save Animation tools write to the user's own library (`<projects root>\user\<login>\animlibrary\<asset id>\...`), not the asset's folders. This is on mayadev's branch `ui-phase1` until it merges.
- **The reserved `user` folder.** mayadev's `assetid.check` refuses an asset id whose first token is `user` (the top-level folder of the projects root that holds each user's library), and its registry skips that folder. `core/AssetId` here has no such rule, so `createRigFromTemplate(asset="user\\x")` is accepted and mayadev then can't open the asset. Not changed here: it needs a decision on whether the plugin should know a mayadev folder name.
- **Lowercase asset ids and rig names (D54, built here 2026-11-16).** `core/AssetId` rejects an uppercase token, and a template's `rigName` must be lowercase letters, digits and `_` (the default from `rigTemplateName` is snake_case). mayadev's `bmaya.core.asset.assetid` still accepts `[A-Za-z0-9_]` and compares with `casefold`, so New Asset can make `Characters\Hero`, which `createRigFromTemplate` then refuses. New Variant reads `rigName` through `rigmodules.template.get_rig_name`, so a template with `"rigName": "Human"` can still be copied in and then fails to build. Proposed for mayadev: `_TOKEN` becomes `[a-z0-9_]+`, the `casefold` comparisons become exact, and its tests and fixtures use lowercase ids. The author's test asset at `test\character\biped\variants\human\template.json` still has `"rigName": "Human"`, so `tools/perf/perf_scenes.py human` fails until that line is lowercased.
- **Rigs and looks, phase 3 (D54, built here 2026-11-16).** These broke mayadev `main`. Fixed on mayadev branch `rigs-and-looks-phase3` (`10c0b1e`, not merged): `build_rig` builds into `{asset name}_{variant}`, the meta key is `skeleton`, and `RigInfo.rig_name` is read. The call sites were:
  - `RigInfo.variant` is now `rig_name`, and `asset_root` is gone: `bmaya/maya/assettools.py` (lines 126, 157 to 161, 177, 178, 194, 195, 205, 286, 287, 303), `bmaya/maya_ui/rigpanel.py` (151, 158), `tests/asset/test_maya_asset_tools.py` (161).
  - Library `meta`: `skeletonId` is now `skeleton` (the caller's) and the rig fills `skeletonHash`. `variant` is now `rigName`. Pose and ControlAnim files are format version 3 and a version 2 file loads with the new names. Call sites: `assettools.py` (206, which passes `skeletonId`, now refused as an unknown key), `bmaya/core/asset/library.py` (373), `bmaya/maya_ui/librarybrowser.py` (440), and the tests `test_maya_asset_handlers.py` (303), `test_maya_asset_tools.py` (238), `test_maya_library_session.py` (155), `test_asset_library.py` (312).
  - The default namespace is `{asset name}_{rigName}`: `build_rig` without `namespace` now builds `hero` with a `game` template into `hero_game`, not `hero`. `test_maya_asset_tools.py` (161) expects `"hero"`.
  - A namespace holds one rig, so a second variant can no longer be built into the asset's namespace.
- **Rigs and looks, phase 4 (D54, built 2026-11-16 on mayadev branch `rigs-and-looks-phase4`).** mayadev now holds skeletons, rigs and looks. It calls `cmds.saveRigSkeleton` and `cmds.loadRigSkeleton` (`skeleton.json` is the only skeleton format), and `build_rig` takes a rig and a look. Asset ids are lowercase (`assetid`), and a `project.json` of format 2 updates to 3. Call sites: `bmaya/maya/assethandlers.py` (`RigSkeletonHandler`, `ModelHandler`), `assettools.py` (`new_rig`, `new_look`, `save_skeleton`, `load_skeleton`, `build_rig`), `librarybrowser.py` (the library is a rig's).
- **Rigs and looks, phase 5 (D54, built 2026-11-17 on mayadev branch `rigs-and-looks-phase5`).** mayadev calls `cmds.setRigLook` from the new `assettools.apply_look`, which `build_rig` now uses for a look that has a model and weights, and `RigInfo.look` (`LookInfo`) from `save_skin_weights`, `load_skin_weights` and `LibrarySession.worn_look`. `loadRigSkinWeights` now fails on a file whose `skeletonHash` is not the rig's, which reaches mayadev through `RigSkinWeightsHandler.load` and `apply_look`. `RigInfo` gained a field with a default, so the constructor call in `tests/core/test_maya_rigpanel_view.py` still works. Call sites: `bmaya/maya/assettools.py` (`build_rig`, `apply_look`, `save_skin_weights`, `load_skin_weights`), `bmaya/maya/librarybrowser.py` (`looks`, `worn_look`, `apply_look`), `bmaya/maya/assethandlers.py` (`RigSkinWeightsHandler.load`). The library window has no Looks tab yet.
- **Attachments, phase 7 (D55, built here 2026-11-17; the mayadev side is built on mayadev branch `attachments-phase8` (stacked on `rigs-and-looks-phase5`), not yet committed or merged).** `createRigFromTemplate` gained the optional `parentRig` flag, and `getRigInfo` and `rigmodules.query.RigInfo` gained `parentRigRoot` / `parent_rig_root` (a field with a default, so existing constructor calls still work). Nothing mayadev calls changed shape. mayadev now uses them: `rig.json` gets an optional `parent` (no format bump, since the key is optional and additive), `build_rig` builds or finds the parent and passes its root as `parentRig`, and `apply_look` partitions a spanning look's weight files by `skeletonHash`. It reads the hash through the new `rigmodules.skinweights.read_file_skeleton_hash` ([`attachments.md`](attachments.md), [`python.md`](python.md)). Renaming `parentRig`, `parent_rig_root`, `read_file_skeleton_hash` or the weight file's `skeletonHash` key breaks those. Call sites to search before changing the flag or the key: `createRigFromTemplate` in `bmaya/maya/assettools.py` (`build_rig`) and `RigInfo(` in `tests/core/test_maya_rigpanel_view.py`.
- **Rigs and looks, phase 6 (D54, built 2026-11-17 on mayadev branch `rigs-and-looks-phase5`).** No RigModules command changed. mayadev's `assettools.apply_look` now syncs a look that is not on disk through its `SourceControl` adapter before it calls `cmds.setRigLook`, and `LibrarySession.look_entries()` lists looks with a `local` flag. Call sites: `bmaya/core/asset/asset.py` (`all_looks`, `sync_look`), `bmaya/maya/assettools.py` (`list_rig_looks`, `apply_look`), `bmaya/maya/librarybrowser.py` (`look_entries`, `looks`).
- **`mayadev.mod`** is described by [`design/mayadev-integration.md`](../design/mayadev-integration.md) as existing, but is not in the mayadev repo.
- The list of mayadev issues in that document is dated: see its "Status on 2026-11-14".

mayadev's own known problems (a menu entry that imports a module that doesn't exist, Python 2 files, duplicate plug-in folders) are in its `docs/status.md`. They are mayadev's to fix.
