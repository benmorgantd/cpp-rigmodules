# Handoff: rigs and looks, phase 4 (the mayadev file model and tools)

Written 2026-11-16, after phase 3. Read `CLAUDE.md` (the "Sibling repo: mayadev" section), then D54 and D29 in `design/decisions.md`, then `design/rigs-and-looks.md`: section 3 is the disk layout, 3.1 the shared skeleton, 3.2 the copied rig, section 10 lists what phase 4 touches in mayadev, and section 11 lists the phases. In mayadev, read `docs/assets.md`, `docs/rigmodules.md` and `tests/README.md`. Delete this file when phase 4 is committed.

## Where things stand

**RigModules** is on branch `rigs-and-looks-phase3`, two commits on top of `main` (`59536b3`, the phase 3 handoff), not pushed and not merged:

| Commit | What |
|---|---|
| `8a7d41c` | Phase 3. A namespace holds one rig, the default namespace is `{asset name}_{rigName}`, and `assetRootModule` is merged into the rig root (id 0x00219 retired). The rig root stamps `skeletonHash`. `getRigInfo` and `RigInfo` changed: `variant` became `rig_name`, `asset_root` is gone and `skeleton_hash` is new. Library `meta` is version 3 (`rigName`, `skeletonHash`, and the caller's `skeleton`). Weight files are version 2 and carry `skeletonHash`, recorded but not checked |
| `dbecc28` | `docs/mayadev.md` notes the mayadev fix |

The build is clean and the suite reports **TOTAL 1034 passed** at `8a7d41c`.

**mayadev** is on branch `rigs-and-looks-phase3`, one commit (`10c0b1e`) on top of `main` (`432cd85`), not pushed and not merged. The commit follows phase 3: `build_rig` builds into `{asset name}_{variant}`, the meta key is `skeleton`, and `RigInfo.rig_name` is read. Results against the phase 3 `.mll`: `tests/asset` 104 + 172, `tests/rigging/rigmodules_menu` 43, `tests/core` 112 + 138, all passing with no skips. `tests/gui` was not run. The author's uncommitted deletion of `tests/basic/startup_test/` is in mayadev's working tree: leave it out of every commit.

Start phase 4 from both phase 3 branches, or merge them first if the author has. Ask which.

**Built and usable now:**
- RigModules: `cmds.saveRigSkeleton(*joints, file=path)` writes `skeleton.json` and returns the hash (`docs/skeletons.md`). The rig root's `skeletonHash` and `rigmodules.query.RigInfo.skeleton_hash` are available. `rigmodules.template.get_rig_name(path)` reads a template's `rigName`.
- mayadev: `bmaya.core.asset` (`project`, `asset`, `assetid`, `registry`, `publishes`, `handlers`, `library`, `versions`), `bmaya.maya.assethandlers`, `bmaya.maya.assettools`, the library browser and the rig panel. The D49 `ui-phase1` branch is merged into mayadev `main`, although RigModules' `CLAUDE.md` still says it isn't. Fix that line in the same change.

## Phase 4 scope (from the plan, with what the code looks like today)

Phase 4 is a **mayadev change**: plain Python in `bmaya.core.asset`, then the Maya tools. The author gave a go-ahead for mayadev edits in phase 3. Ask again for phase 4, because this one redesigns mayadev's own model. Following `CLAUDE.md`, propose the design and get sign-off before writing code.

The phase is large. Consider proposing it as **4a** (`bmaya.core.asset`, plain Python, testable without Maya) and **4b** (handlers, `assettools`, menus, the library browser's paths), with one commit each.

1. **Lowercase ids.** `bmaya/core/asset/assetid.py` has `_TOKEN = [A-Za-z0-9_]+` at line 22, and `key` and `equals` compare with `casefold`. They should become `[a-z0-9_]+` and an exact compare, the same rule as RigModules' `core/AssetId` (D54). The reserved `user` first token stays. Tests and fixtures need lowercase ids.
2. **File types** (`bmaya/core/asset/project.py`, `DEFAULT_PROJECT`, `FORMAT_VERSION = 2`, an update-step list near line 160).
   - Today every per-variant type is `variants/{variant}/...`, and `FileType` knows a single `{variant}` field (`per_variant`, `variant_parent`, `relative_path(name, variant)`).
   - Phase 4 needs three kinds of field, `{skeleton}`, `{rig}` and `{look}`, and the section 3 layout:
     - `skeletons/{skeleton}/`: the `.ma`, `skeleton.json` and `exports/`.
     - `rigs/{rig}/`: `rig.json`, `template.json`, `shapes.json`, `postops.py`, the rig `.ma`, `publish/`, `controlposes/` and `controlanims/`.
     - `looks/{look}/`: `look.json`, `model/`, `textures/`, `shaders/`, `weights/` and `publish/`.
     - `sources/` stays at the asset root.
   - This becomes project format version 3, with an update step.
3. **The asset model** (`bmaya/core/asset/asset.py`).
   - `Asset.variants` (line 79) lists the folders that hold a template. It becomes `skeletons`, `rigs` and `looks`, listed by their `skeleton.json`/`.ma`, `rig.json` and `look.json`.
   - `Asset.skeleton` (line 73) names a whole asset today. It becomes the two-field `{"asset": ..., "name": ...}` from section 3.1, where a plain string means the asset's own skeleton.
   - New small files: `rig.json` names its skeleton, plus its parent rig for an attachment and its source rig for a copy. `look.json` names the skeletons its meshes bind to.
4. **Registry and publishes.**
   - `registry.py` stores `variants` and `latestPublish` per variant (line 125). This becomes a latest publish per rig and per look, and the registry file format bumps with an update step.
   - `publishes.py` keys everything by variant. It becomes per rig and per look, with `publish/` under each.
5. **Tools** (`bmaya/maya/assettools.py`, 707 lines).
   - New Variant becomes New Rig and New Look. New Rig copies a template, its `rigName` must equal the folder (the template handler checks this already, `assethandlers.py` line 93), and it names its skeleton. New Look names its skeletons.
   - `save_skeleton` saves a named skeleton and writes `skeleton.json` through `cmds.saveRigSkeleton`.
   - `save_model` saves per look.
   - `build_rig(asset_id, rig, look="default", namespace=None)` imports the rig's skeleton, builds, then loads the look's model and weights.
   - `save_rig` saves per rig.
   - `_choose_variant` (line 692) becomes choosers for rig and look.
   - The `assethandlers` follow: weights, shapes and model take a rig or look instead of a variant.
6. **Menus** (`toolsets/master_tool_registry.json`, `toolsets/rigmodules_menu.json`). Tool ids are public (D47), and layouts reference them. `rigmodules.asset.new_variant` (registry line 275, menu line 37) becomes `new_rig` and `new_look`. `check_calls` and the menu layout test (tool count 40) need updating.
7. **Library paths.**
   - Control poses and animations move under the rig (section 3), because markup is unique per rig, not per asset.
   - `bmaya/core/asset/library.py` builds the Approved folders from the asset (`FOLDER_TYPE`, line 38) and the user library as `<projects root>\user\<login>\animlibrary\<asset id>\...` (line 12). Both need the rig in the path.
   - The browser's listing follows the active rig, which gives the rig name through `RigInfo.rig_name`.
   - A copied rig's source library (section 3.2) is phase 4 data. Listing it in the browser can wait, but say so.
8. **Skeleton exports.** They move to `skeletons/{skeleton}/exports/`. `save_skeleton_export`, `AnimClipHandler` and `check_clip_skeleton` use the asset-level `exports` type today.
9. **RigModules docs** in the same change, or one following it:
   - Rewrite `design/asset-model.md`, which still describes variants on disk.
   - Amend D29's file model.
   - Update D54's built notes and the phase list.
   - Move `design/control-library.md`'s `exports/` under `skeletons/{skeleton}/`.
   - Update `docs/mayadev.md`.
   - `tools/perf/perf_scenes.py` hard-codes `variants/human/template.json` and `poses/star_pose1.json` (lines 495 and 496).

Not in phase 4: the look node and swap, and the weight hash check (phase 5); source-control listing (phase 6); attachments (phase 7); the asset browser (phase 8).

## Points to settle with the author before designing

- **Existing asset folders on disk.** Should a `variants/` asset be migrated to `rigs/` and `looks/` by a tool or by hand, or not at all? The plan's answer for rigs in scenes was no migration (personal project). The author's test asset, `C:\Users\bmorgan\Documents\Art\Maya\Projects\test\character\biped\`, has `skeleton/biped_skeleton.ma`, `variants/human/` (template, shapes, rig `.ma`, model, weights) and an `anims/test3.usd`. Its template still has `"rigName": "Human"`, which the build refuses. It is the author's data: ask before touching it.
- **The default names.** A one-skeleton asset's skeleton is probably `body`, and its look is `default` (decided). What is a rig called when the template has no `rigName`? The snake_case default from `rigTemplateName` applies, but the folder must equal it.
- **The user library path.** Does `<asset id>\<rig>\controlposes` nest the rig under the asset, or does the rig go into the file name? Nesting matches the asset layout.
- **4a/4b split** and the branch to start from (above).

## Things that cost time in phases 2 and 3

- **Bash heredocs eat backslashes** in this environment (`"tests\\rig"` reached Python as `tests\rig`). Write edit scripts to the scratchpad with the Write tool and run them, or use the Edit tool. Never put a Windows id or path in a heredoc.
- **Line endings are mixed.** Most mayadev `.py` files are CRLF, and some RigModules tests are LF. Open with `newline=""` and write each file's own ending back.
- **mayadev's Maya tests skip silently** without `RIGMODULES_PLUGIN`, so read the skip count. Copy the `.mll` to the scratchpad and set `RIGMODULES_PLUGIN` to the copy, `PYTHONPATH` to RigModules' `python/` folder, and `MAYA_DISABLE_CER=1`. Then run `tests\asset\run.bat`, `tests\rigging\rigmodules_menu\run.bat` and `tests\core\run.bat`.
- **Positional dataclass construction breaks on a field rename:** `test_maya_rigpanel_view.make_rig` built `RigInfo` positionally. Build by keyword.
- **Direct `createRigFromTemplate` calls in mayadev tests** now pass `namespace=` the asset's name. Only the `build_rig` tests check the default.
- **Commit only your own paths in mayadev.** The author has uncommitted work there.

## Done looks like

- The design is signed off, with the points above answered.
- `bmaya.core.asset` holds skeletons, rigs and looks, with lowercase ids, project format 3 and an update step from 2, and the registry and publishes per rig and look. Its plain Python tests (`tests/asset/test_asset_*.py`) are updated and extended, including a version 2 `project.json` updating.
- The tools build a rig with a look, save per rig and per look, and the menus offer New Rig and New Look. The Maya suites pass against the phase 3 `.mll` with no skips. The GUI checks are run, or listed as not run.
- mayadev's docs (`assets.md`, `tools.md`, `rigmodules.md`, `status.md`, `tests/README.md`) and RigModules' docs (the list in item 9) are updated.
- The summary names every RigModules file the change touches (expected: docs and `perf_scenes.py` only) and every author data folder it would affect.
