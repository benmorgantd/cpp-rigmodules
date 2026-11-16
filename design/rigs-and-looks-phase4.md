# Rigs and looks, phase 4: the mayadev file model and tools

**Status:** Signed off by the author, 2026-11-16, with section 10.3's four points and the model-file rule in section 10. Being built. Phase 4 of D54 (`rigs-and-looks.md`, section 11). When it is built, this folds into `asset-model.md` and `rigs-and-looks.md`, and this file and `rigs-and-looks-phase4-handoff.md` are deleted.

Most of it is mayadev code. RigModules gains the skeleton file's reader and `loadRigSkeleton` (section 10), because `skeleton.json` becomes the only skeleton format, plus docs and `tools/perf/perf_scenes.py`.

## 1. Settled with the author (2026-11-16)

| Point | Answer |
|---|---|
| Branches | `rigs-and-looks-phase4` off `rigs-and-looks-phase3`, in both repos. Nothing merged or pushed. |
| mayadev edits | Go-ahead given. Two commits, each passing every suite: (1) lowercase asset ids, (2) the model and the tools together. |
| The author's asset folders | The author moves them by hand. The change touches nothing under `Projects\`. Section 9 lists what the new layout expects. No migration tool. |
| Where a skeleton reference lives | `rig.json` and `look.json` only. `asset.json` drops `skeleton`. |
| New Rig's name | New Rig asks for the name (offering the template's `rigName`) and writes it as `rigName` in the copied template. |
| Default names | The first skeleton is offered as `default`, like the default look. |
| The `skeleton` text in library meta and the bone map's `skeletonId` | The skeleton's name (`default`). Identity is the `skeletonHash` the rig stamps. |
| The library's Shared source | The source rig's library (`rig.json`'s `source`, plan section 3.2). |
| The skeleton's format | **`skeleton.json` only, no `.ma`** (author, 2026-11-16, after the first draft: two formats for a skeleton is a no go). Saving a skeleton serializes it and loading deserializes it. Section 10. |
| The user library path | Nested: `user\<login>\animlibrary\<asset id>\<rig>\controlposes`. (The handoff's suggestion, taken as the default.) |

**Still to confirm at sign-off:**
- The four points in section 10.3, which follow from `skeleton.json` being the only format: unmarked joints are refused, what a load can't give back, rotation loads into `jointOrient`, and a stale stored hash is a warning.
- The points marked **Confirm** below.

## 2. Discrepancies with the handoff

- **The registry has no file.** The handoff says the registry file format bumps with an update step. `registry.py` is an in-memory index, never written, so there is nothing to version. Only its entry shape changes.
- **`asset.json` does get a version step** (1 to 2), which the handoff doesn't list: the `skeleton` key goes.
- **The 4a/4b split by layer doesn't give two passing commits.** The project format bump moves every per-variant path at once, so the Maya layer breaks with it. Hence the split in section 1.
- **The author's `project.json` has two file types the defaults never had,** `anims` and `poses`. The update step leaves unknown types alone.

## 3. `bmaya.core.asset`

### 3.1 `assetid.py` (commit 1)

- `_TOKEN` becomes `[a-z0-9_]+`. Uppercase is rejected, not converted.
- `key()` goes and `equals()` becomes `==` at its call sites (both existed only for the case-insensitive compare). `Asset.__eq__`/`__hash__` and the registry use the id itself. The registry's "differs only in case" error goes: it can't happen.
- `is_valid_token` stays the one name rule, used for skeleton, rig and look names too. `RESERVED_ROOT` stays.
- Tests and fixtures move to lowercase ids. Asset types (`"Character"`) are not ids and keep their case.

### 3.2 `project.py`: format version 3

A file type's path has at most one **owner field**: `{skeleton}`, `{rig}` or `{look}`. `{name}` is still the asset's name.

```
skeleton       skeletons/{skeleton}/skeleton.json
exports        skeletons/{skeleton}/exports            (folder)
riginfo        rigs/{rig}/rig.json
template       rigs/{rig}/template.json
shapes         rigs/{rig}/shapes.json
postops        rigs/{rig}/postops.py
rig            rigs/{rig}/{name}_{rig}_rig.ma
rigpublish     rigs/{rig}/publish                      (folder)
controlposes   rigs/{rig}/controlposes                 (folder)
controlanims   rigs/{rig}/controlanims                 (folder)
lookinfo       looks/{look}/look.json
model          looks/{look}/model/{name}_{look}_model.ma
textures       looks/{look}/textures                   (folder)
shaders        looks/{look}/shaders                    (folder)
weights        looks/{look}/weights                    (folder)
lookpublish    looks/{look}/publish                    (folder)
sources        sources                                 (folder)
```

- **`FileType`:** `per_variant` and `variant_parent()` become `owner` (`"skeleton"`, `"rig"`, `"look"` or `None`, read from the pattern) and `owner_parent()`. `relative_path(name, owner=None)` keeps today's checks: an owned type needs a valid owner name, an asset-level type refuses one. Two owner fields in one path, or `{variant}`, is a `ProjectError`.
- **`publish` splits into `rigpublish` and `lookpublish`,** because a file type has one path and publishes are per rig and per look (plan section 3).
- **`riginfo` and `lookinfo` are file types,** not constants like `asset.json`, because where they sit depends on the `rigs/{rig}` patterns, which a project can edit.
- **Update step 2 to 3** (frozen, spelled out, like step 1):
  - For each version 2 default type, if the path still equals the version 2 default, replace it with the version 3 path. `publish` is removed and the two new publish types added. `riginfo` and `lookinfo` are added. `skeleton` goes from the `.ma` path to `skeletons/{skeleton}/skeleton.json`.
  - A path the studio edited that still uses `{variant}` is left as it is, and loading fails with a `ProjectError` that names the type and the path. A guess at where an edited path should go would be wrong silently.
  - Types the defaults never had are untouched.
- The word "variant" leaves the module.

### 3.3 `asset.py`

`asset.json` becomes format version 2: `{"formatVersion": 2, "assetType": ...}`. An update step drops `skeleton`. `Asset.skeleton` and `Asset.create(skeleton=)` go.

```python
asset.skeletons            # names: folders under skeletons/ that hold a skeleton.json
asset.rigs                 # names: folders under rigs/ that hold a rig.json
asset.looks                # names: folders under looks/ that hold a look.json
asset.path(file_type, owner=None)          # owner: the skeleton, rig or look name the type needs
asset.file_exists(file_type, owner=None)
asset.rig(name)    -> RigRecord            # parsed rig.json
asset.look(name)   -> LookRecord           # parsed look.json
asset.add_rig(name, skeleton, source=None) -> RigRecord     # writes rig.json, source control add
asset.add_look(name, skeletons)            -> LookRecord    # writes look.json
asset.resolve_skeleton(reference) -> tuple[Asset, str]      # the owning asset and the skeleton's name
```

- The parameter is `owner`, not `name`, because the clip and pose handlers already take `name=` for the clip's name.
- **New module `records.py`** (plain dataclasses and their JSON):
  - `SkeletonRef(asset: str | None, name: str)`. JSON is a plain string for the asset's own skeleton, or `{"asset": ..., "name": ...}`. Never a joined string.
  - `RigRef(asset: str | None, rig: str)`, the same shape with the key `rig`.
  - `RigRecord(skeleton: SkeletonRef, source: RigRef | None)`. `rig.json`: `{"formatVersion": 1, "skeleton": "default"}`, plus `"source": {"asset": ..., "rig": ...}` on a copy.
  - `LookRecord(skeletons: list[SkeletonRef])`. `look.json`: `{"formatVersion": 1, "skeletons": ["default"]}`.
- **`rig.json` has no attachment parent key yet.** The handoff lists it, but nothing reads it until phase 7, and its shape depends on the attach node's design. **Confirm.**
- `add_rig` and `add_look` check the name rule and that the folder has no record yet. They don't check that the skeleton exists: a rig can be set up before its skeleton is saved, and Build Rig reports a missing one.
- A look-only asset has no `skeletons/` or `rigs/`: its lists are empty and its `look.json` names another asset's skeleton.

### 3.4 `registry.py`

Entry shape:

```python
{
    "assetType": "Character",
    "skeletons": ["default"],
    "rigs":  {"body": {"skeleton": SkeletonRef, "source": RigRef | None, "latestPublish": id | None}},
    "looks": {"default": {"skeletons": [SkeletonRef], "latestPublish": id | None}},
}
```

- `find(asset_type=None, skeleton=None)`: `skeleton` is `(asset_id, name)`, and matches the asset that owns it and every asset with a rig or look on it. This is the "which assets share this skeleton" question, which `asset.json`'s key answered before.
- `latest_publish(asset_id, kind, name)`, `kind` being `"rig"` or `"look"`.

### 3.5 `publishes.py`

Every function takes `(asset, kind, name)` in place of `(asset, variant)`, with `kind` choosing `rigpublish` or `lookpublish`. `create` checks the name against `asset.rigs` or `asset.looks`. Ids and `publish.json` are unchanged. No tool publishes yet, so only the registry and tests call it.

### 3.6 `handlers.py`, `versions.py`

`handlers.save(asset, file_type, owner=None, **context)` and `load` the same. The handler receives `owner=` in its context in place of `variant=`. `versions.py` is path-based and changes only its docstring.

### 3.7 `library.py`

- `sources(asset, rig)` and `user_source(asset, rig)` take the rig's name.
  - **Approved:** the asset's `rigs/{rig}/controlposes` and `controlanims`.
  - **Shared:** the same folders of the rig named by `rig.json`'s `source`, when the rig has one and it is on disk. One level, as the plan says. Read only.
  - **User:** `user\<login>\animlibrary\<asset id>\<rig>\controlposes` and `controlanims`.
- `masks.json` keeps whatever location it has today unless it sits under the per-asset user folder, in which case it follows the rig. I'll check when building and say which.

## 4. The Maya layer

### 4.1 `assethandlers.py`

- **New `RigSkeletonHandler`** for the `skeleton` type: `save(joints=)` calls `cmds.saveRigSkeleton`, `load(namespace=)` calls `cmds.loadRigSkeleton` and returns the joints. `skeleton` leaves `MAYA_FILE_TYPES`: a skeleton is never a Maya scene.
- `RigTemplateHandler.save(owner=, source=)` writes the template with `rigName` set to `owner`: read the source JSON, set the key, write it with the source's indent. Key order is kept. **The file is re-serialized,** so hand formatting in the source (a one-line list) is not preserved.
- `RigSkinWeightsHandler` is per look in its paths and unchanged in behaviour (meshes by `meshId` in a namespace).
- `save_skeleton_export`, `get_skeleton_export_path` and `save_anim_clip` take the skeleton's owning asset and name. The export is `skeletons/{skeleton}/exports/{name}_{skeleton}_skeleton.fbx`, clips are `exports/<clip>/<clip>.fbx` beside it.
- `write_skeleton_fbx` gets the skeleton's name as `skeleton_id`.

### 4.2 `assettools.py`

| Function | Behaviour |
|---|---|
| `new_rig(asset_id, template_path, rig=None, skeleton="default", source=None)` | `rig` defaults to the template's `rigName`. Fails when the rig exists. Copies the template (with `rigName` set) and writes `rig.json`. |
| `new_look(asset_id, look="default", skeletons=("default",))` | Writes `look.json`. Fails when the look exists. |
| `save_skeleton(asset_id, skeleton="default", joints=None)` | Serializes `joints` (every joint in the scene when left out) to the skeleton's `skeleton.json` through the handler, with a local version and source control. Fails, writing nothing, on an unmarked joint, bad markup or a joint a rig drives (it isn't at rest). The scene is not saved or renamed: the scene is a workspace, not the skeleton. Returns the path. |
| `load_skeleton(asset_id, skeleton="default", namespace=None)` | New. Deserializes the skeleton into the scene (the current namespace unless given), for editing. Returns the joints. |
| `save_model(asset_id, look, meshes=None)` | The look must exist. Exports `meshes` (every visible mesh in the scene when left out) as meshes only: each is copied from its undeformed shape, so a posed rig doesn't matter, with no history, no skinCluster and no joints, keeping its name, `meshId`, parent groups and shading. The scene is not saved, renamed or changed. The `meshId` warning stays. |
| `build_rig(asset_id, rig, look=None, namespace=None)` | Reads `rig.json`, resolves the skeleton (possibly in another asset), loads its `skeleton.json` into `{asset name}_{rig}`, builds, loads the rig's shapes. Then the look: `look=None` means `default` when the asset has it, else no look. A look that is named and missing, or whose `look.json` doesn't list the rig's skeleton, is an error raised before anything is imported. Imports the look's model, adds it to the layers and groups, loads its weights. |
| `save_rig`, `save_control_shapes`, `load_control_shapes` | Per rig, from the rig root's `rig_name`. Unchanged apart from the wording. |
| `save_skin_weights(node, look=None, prompter=None)`, `load_skin_weights` | **A built rig doesn't record its look until phase 5's `lookRoot`.** So these take the look: the asset's only look when there is one, else the caller says. |
| `save_skeleton_export`, `check_clip_skeleton`, `save_joint_clip`, the clip dialogs | The exports folder comes from the rig: `rig_name` to `rig.json` to the skeleton. |
| `library_meta` | `"template"` stays the rig's name, `"skeleton"` is the skeleton's name. |
| `save_pose`, `save_animation` | Take the rig name from the scope's rig, for the user library path. |

- `build_rig` looks for the look in the rig's own asset only. Wearing a look from a look-only asset needs a look reference as two fields, which belongs with the look swap. **Confirm:** phase 5.
- A look with meshes for several skeletons (body and face) imports whole into the one rig's namespace. Splitting it per rig needs the look node and attachments (phases 5 and 7).
- Build Rig doesn't compare the rig root's `skeletonHash` with `skeleton.json`. Nothing in `rigmodules` reads `skeleton.json` yet, and mayadev parsing it would break D24. The check comes with the look swap.

**Interactive entry points:**
- `new_variant_interactive` becomes `new_rig_interactive` (asset id, template file, rig name, skeleton: the asset's only one, a choice, or typed with `default` offered when it has none) and `new_look_interactive` (asset id, look name offering `default`, skeleton).
- `save_skeleton_interactive` asks for the skeleton name, offering the asset's only skeleton or `default`. New `load_skeleton_interactive` asks for the asset id and chooses the skeleton.
- `build_rig_interactive` chooses the rig, then the look only when the asset has more than one.
- `_choose_variant` becomes one `_choose_owner(prompter, title, asset_id, kind)` for rigs, looks and skeletons.

### 4.3 Library browser and rig panel

`LibrarySession` passes `info.rig_name` to `library.sources` and `user_source`. The browser's label shows the asset id and the rig. The rig panel already shows `rig_name`. No new widgets.

### 4.4 Menus

- `rigmodules.asset.new_variant` is removed. `rigmodules.asset.new_rig` and `rigmodules.asset.new_look` are added, in the same place in `rigmodules_menu.json`. Tool ids are public (D47): the only layout that names the old id is that menu.
- `rigmodules.asset.load_skeleton` is added, beside Save Skeleton.
- The menu layout test's tool count goes from 40 to 42.

## 5. Tests (mayadev)

- **Commit 1:** `test_asset_model.py` id cases (uppercase rejected, exact compare), fixtures lowercased across `tests/asset`, `tests/core` and `tests/rigging/rigmodules_menu`.
- **Commit 2, plain Python:**
  - A version 2 `project.json` updates to 3: default paths replaced, an edited `{variant}` path fails by name, unknown types kept, `publish` split.
  - A version 1 `asset.json` with `skeleton` updates to 2.
  - `FileType.owner`, `relative_path` and its errors, for each owner kind.
  - `skeletons`, `rigs`, `looks` listing; `add_rig`, `add_look`; record round trips, both reference forms; `resolve_skeleton` to another asset; a look-only asset.
  - Registry entries, `find(skeleton=)` across assets, `latest_publish` per rig and per look.
  - Library sources per rig, the Shared source from `source`, the user path.
- **Commit 2, Maya:** New Rig (name written into the template, existing rig refused), New Look, Save Skeleton (writes `skeleton.json`, an unmarked joint or bad markup writes nothing, the scene's name is unchanged), Load Skeleton, a save, load and save again giving the same file, Build Rig with the default look, with a named look, with no look, with a look on the wrong skeleton, on another asset's skeleton; Save Rig, shapes, weights per look; skeleton export and clip paths; the menu `check_calls` and layout tests.
- Suites run against the phase 3 `.mll` with the skip count read. `tests/gui` is run, or listed as not run.

## 6. Docs

**mayadev:** `docs/assets.md`, `tools.md`, `rigmodules.md`, `status.md`, `tests/README.md`, and `py/scripts/docs/guides/Asset Layout.md`.

**RigModules** (section 10's commit carries its own docs; the rest is one commit after mayadev's):
- `design/asset-model.md` rewritten for the new layout. D29's file model amended. D54's built notes and the phase list in `rigs-and-looks.md` updated. This file folded in and deleted, with the handoff.
- `design/control-library.md`: `exports/` under `skeletons/{skeleton}/`, the library folders under `rigs/{rig}/`.
- `docs/mayadev.md`, and `CLAUDE.md` (the `ui-phase1` line the handoff flags, the mayadev summary's "variants").
- `tools/perf/perf_scenes.py`: its two hard-coded paths. They point into the author's test asset, so they follow section 9's layout.

## 7. Complexity added

- Two new small JSON files (`rig.json`, `look.json`) and a `records.py`.
- Two file types for them, and `publish` becoming two types.
- RigModules: one command (`loadRigSkeleton`), the skeleton reader and its update table (section 10). It removes a format: the skeleton `.ma`.
- One more thing a caller of the weight tools must say (the look), until phase 5.
- Removed: `Asset.skeleton`, the case-insensitive id compare, the `variant` field.

## 8. Not in phase 4

The look node, the swap and the weight hash check (5). Looks from another asset in Build Rig (5). The browser's Looks kind (5). Source-control listing (6). The attachment parent in `rig.json`, the attach node (7). The asset browser (8). A hash in the bone map (a follow-up to propose: two assets' `default` skeletons give the same `skeletonId` text).

## 9. What the new layout expects of the author's folders

Nothing here is touched by the change. For `Projects\test\character\biped`:

| Today | New |
|---|---|
| `test\project.json` (version 2) | Updates itself to 3 on first load. `anims` and `poses` stay. |
| `asset.json` | Updates itself to 2 on first load. |
| `skeleton\biped_skeleton.ma` | Open it, mark any unmarked joints, and run Save Skeleton as `default`: it writes `skeletons\default\skeleton.json`. The `.ma` is then outside the layout. |
| `variants\human\template.json` (`"rigName": "Human"`) | `rigs\human\template.json` with `"rigName": "human"`, plus `rigs\human\rig.json`: `{"formatVersion": 1, "skeleton": "default"}` |
| `variants\human\shapes.json`, `biped_human_rig.ma` | `rigs\human\` |
| `variants\human\model\biped_Human_model.ma` | `looks\default\model\biped_default_model.ma`, plus `looks\default\look.json`: `{"formatVersion": 1, "skeletons": ["default"]}` |
| `variants\human\weights\` | `looks\default\weights\` |
| `user\bmorgan\animlibrary\test\character\biped\controlposes\` | `...\biped\human\controlposes\` |
| `anims\test3.usd` | No home in the layout (USD IO was removed). Left alone. |

## 10. RigModules: `skeleton.json` is the only skeleton format

Decided by the author (2026-11-16): a skeleton is never stored as a `.ma` beside its description. The file `saveRigSkeleton` writes is the skeleton, and loading one deserializes it. Rejected: the `.ma` plus `skeleton.json` pair of the plan (section 3) and of this doc's first draft, which stores one thing twice and lets the two disagree.

This changes the plan: `rigs-and-looks.md` section 3 (the `.ma` in `skeletons/{skeleton}/`, the `_skeleton.ma` file name suffix) and D54 get amended in the same change. D54's "no reader yet" ends here, not in phase 5.

### 10.1 What is built

- **`core/SkeletonFile`:** `deserializeSkeleton(text, outSkeleton, report)` and `skeletonUpdateTable` (D33; empty, the format stays version 1). The reader checks the keys, the rotate order names, unique markup, that every `parent` names a joint in the file, and that parents make no cycle.
- **`maya/shared/SkeletonScene::createSkeleton`:** creates the joints parents first through the caller's modifier, in the given namespace: name, parent, rotate order, the local rest, and `rigMarkup` through `JointMarkupScene`. It is the inverse of `readSkeleton`.
- **`cmds.loadRigSkeleton(file=path, namespace=ns)`** in `commands/SkeletonCmds`, in the D14 shape, undoable. Returns the joints it created. `namespace` is optional (the current namespace) and created when missing. It fails when a joint name it would create already exists in that namespace, since Maya would rename it and a renamed joint is another skeleton.
- No Python wrapper: mayadev calls the command, as it calls `saveRigSkeleton`.

### 10.2 What a load guarantees

Loading a file and saving the loaded joints gives the same joints, the same `restLocal` values to well inside the 1e-4 grid, and the same hash. The rig root's `skeletonHash` of a rig built on the loaded joints equals the file's. Both are suite tests.

### 10.3 What follows from one format (**Confirm**)

1. **Every joint must be marked.** The file holds marked joints only, so an unmarked joint has nowhere to go. mayadev's Save Skeleton passes every joint in the scene, so an unmarked helper joint stops the save and is named. Rejected: dropping unmarked joints silently.
2. **A load gives back only what the file holds:** markup, name, parent, rotate order, rest. Lost against a `.ma`: joint radius, draw style, colour, labels, preferred angle, limits, custom attributes, and any group between two joints (it is already folded into the child's rest). If radius matters at an asset's scale, it can be added later as a key outside the hash, with a version step. My recommendation is to leave it out now.
3. **Rotation loads into `jointOrient`, with `rotate` at zero,** translation and scale into their channels, and shear into `shear`. A skeleton saved with values on `rotate` comes back with them in `jointOrient`: the same rest and the same hash. `segmentScaleCompensate` is off on loaded joints, so the local matrix is exactly `restLocal`.
4. **The stored `skeletonHash` is not trusted on load.** The reader recomputes it. A file whose stored hash is stale (a hand edit, a merge) loads with a warning naming both, and the next save writes the right one. Rejected: failing the load, which would block the fix.

**A model file is only meshes** (author, 2026-11-16): no joints, no skinCluster, no history. Save Model strips the skinning and exports the meshes with their history cleared, and the scene it was saved from keeps its skinning. A look is therefore its model (meshes) plus its weight files, and a look is put on a rig by importing the meshes and loading the weights onto the rig's joints, which is what Build Rig already does. This replaces plan section 4.1's "a look file holds its meshes skinned to a copy of the skeleton": phase 5's swap becomes import, check the weight files' `skeletonHash`, load weights, with no skinCluster inputs to re-point. `rigs-and-looks.md` section 4.1 and D54 are amended in the same change. Rejected: a model `.ma` carrying bound joints, a second place a skeleton would be stored.

The skeleton export FBX is an export for the engine (D42) and stays.

### 10.4 Tests and docs (RigModules, its own commit, before mayadev's)

- **Suite:** reader cases in the data tier (bad keys, unknown parent, cycle, duplicate markup, stale hash); save, load and save again gives identical text; `rotate` folds into `jointOrient`; a group between joints flattens; the namespace flag; a name clash fails and creates nothing; undo and redo; a rig built on loaded joints stamps the file's hash.
- **Docs:** `docs/skeletons.md` (loading, what a load gives back), `docs/commands.md`, `docs/architecture.md`, `docs/mayadev.md`, `CLAUDE.md` (the core and maya tables, the command list), D54 and `rigs-and-looks.md`.
- Build, then the full suite, reported with its TOTAL line.

### 10.5 Order of work

1. RigModules: the reader and `loadRigSkeleton`, with tests and docs. Build and suite.
2. mayadev commit 1: lowercase ids.
3. mayadev commit 2: the model and tools, against the new `.mll`.
4. RigModules: the remaining docs and `perf_scenes.py`.
