# Asset model

**Status:** decided 2026-11-08 (D29 in `decisions.md`), amended by D54 (2026-11-16). The scene side is built in RigModules (see "Built in RigModules"). The file model, local versions, registry, publishes, source-control adapter and handlers are built in mayadev (see "Built in mayadev"). Its user guide is mayadev's `docs/assets.md`.

**The file model is the D54 one (built in mayadev 2026-11-16, merged into its `main` as `f81b84b`):** an asset holds skeletons, rigs and looks, replacing D29's variants. See `rigs-and-looks.md` for the reasons and `rigs-and-looks-phase4.md` for the phase 4 design. Where a section below still says "variant", it describes the old model and is kept for its history in "Built in mayadev (2026-11-08)".

An asset is the top-level container for one thing in the game: a character, prop, vehicle, weapon, vfx element or environment piece. It owns its files on disk, its skeletons, rigs and looks, and the namespace it lives under in a Maya scene. This page covers the asset id, the scene structure, the file model, and how tools answer questions about assets without opening Maya, all under Perforce without shared files that everyone has to check out.

## Split between the repos (D24 stands)

| Piece | Home | Why |
|---|---|---|
| Asset id checks, namespace ownership, the asset id, rig name and version stamps on the rig root | RigModules C++ | Nodes, attributes and the build command |
| Query from a scene node to its rig root, asset id, rig name, look and stamps | RigModules `python/` (`rigmodules.query`, D30) | Walks rig nodes |
| Project root, file model, file types, local versions, registry, publishes, source control, the out-of-date check | mayadev | Pipeline layout. No C++ code needs a path from an asset id: every command takes an explicit `file=` |
| Save and load handlers per file type | mayadev, calling RigModules commands where RigModules owns the format (shapes) | Loading belongs to whoever owns the format |

The file model has one implementation, in Python, with the layout stored as data in `project.json`. There is no C++ copy to drift out of step. A tool in another language (engine side, C#) reads the same JSON files.

## Asset id

- Backslash-separated (it was `|` until 2026-11-12, D29): `shooter\main\characters\hero`.
- The id is the asset folder's path relative to `RIGGING_PROJECT_DIR`, with `\` between folders. Depth varies, because assets end up everywhere.
- A folder is an asset when it holds an `asset.json`, which keeps variable depth unambiguous. Assets never nest: no asset folder sits inside another.
- Each token is lowercase letters, digits and `_`, and none is empty. Uppercase is rejected, not converted (D54, built in RigModules 2026-11-16), so ids compare exactly and no two ids differ only in case: Windows paths ignore case, and Perforce and engines don't. Until then ids compared case-insensitively and were stored as authored. mayadev's `assetid` has the same rule (`[a-z0-9_]+`, mayadev `866f7ae`).
- The id is relative to the projects root, so it means the same asset in every workspace and every branch.

`RIGGING_PROJECT_DIR` is the projects root. The first levels under it are the game and its project (`<game>\<project>\characters\<name>`). The file model doesn't hard-code those levels: it finds the project from the nearest `project.json` above the asset.

## Scene structure

- **The build takes the asset as a flag:** `cmds.createRigFromTemplate(*joints, template=path, asset="shooter\\main\\characters\\hero")`. Templates don't name an asset, so one template (a shared biped) builds many assets. Each asset's own `template` file type can still hold its own copy.
- **A namespace holds one rig** (D54, built 2026-11-16), and it is flat. Everything goes in it: the rig root and every module, the controls and the joints. A build into a namespace that already holds a rig root fails.
- **The default namespace is `{asset name}_{rigName}`** (`hero_body`), with no special case for an asset with one rig. `namespace` overrides it, for a second instance. Nothing reads the rig from the namespace.
- **The rig root stores the id in `assetId`.** The separate `assetRootModule` node (D29) was merged into it, because with one rig per namespace it was one-to-one with the rig root. Its type id 0x00219 is retired. A scene saved with one is not migrated: rebuild it.
- **The rig name is the rig root's `rigName`,** from the template's `rigName`.
- **A rig is built on one skeleton, and a look names the skeletons it binds to** (`rig.json`, `look.json`). The skeleton can be another asset's, so NPCs and crowds share one. Meshes, weights, textures and shaders are per look.
- **Files are imported into a unique namespace, never referenced.** Animation files keep working as long as they're baked, or as long as RigModules gives the rig an update path. The version stamps are what make that path findable.

### Version stamps

Each rig root records what built it:

| Attribute | Set by | Meaning |
|---|---|---|
| `assetId` | build | The asset id, from the `asset` flag |
| `rigName` | build | The rig, from the template |
| `skeletonHash` | build | The input joints' skeleton hash at build (D54, `rigs-and-looks.md` section 5) |
| `rigVersion` | build | The template's `rigVersion` |
| `pluginVersion` | build | The RigModules plugin version that ran the build |
| `publishVersion` | publish step (mayadev) | The id of the publish the rig came from (`20261108T142012Z_bmorgan`). Empty until published. Nothing sets it yet: the publish step (D70 phase 4) writes `publish.json` and copies of the inputs, not the built rig scene |

The out-of-date check runs in mayadev. For each rig root in a scene, it compares `publishVersion` with the latest publish of that rig in the workspace (`Registry.latest_publish(id, "rig", rig)`), and `pluginVersion` with the loaded plugin. A scene full of imported rigs can then be listed as current, stale (newer publish exists) or built with an older plugin, without opening any other file.

## Source control and branches

**Mainline only (author, 2026-11-08).** Everyone submits to one stream, and builds are labelled changelists on it. Long-lived release streams are out of scope, because one person can't maintain release-stream tooling. Rejected for now: the rules release streams would need, namely integrating frozen publishes into the release stream and refusing to rebuild there.

**The rule: files in the depot change only when someone deliberately changes that asset.** Derived data is computed locally and never submitted. Appended data is a new file, never an edit to an existing one. So two people only ever touch the same file when they work on the same asset.

| Data | Where it lives | Changes when |
|---|---|---|
| `project.json` | Depot, one per project | Someone changes the project's asset types or file layout |
| `asset.json` | Depot, one per asset | Someone changes the asset's type |
| Skeletons, rigs, looks | Depot: a folder each, with its `skeleton.json`, `rig.json` or `look.json` | One is added: new files only |
| Publishes | Depot: a new folder per publish | Never edited: each publish adds a folder |
| Registry | Memory only | Never written |
| Local versions | `%LOCALAPPDATA%\bmaya\versions` | Never submitted |

- **Saves go through a source-control adapter:** `checkout(path)` before an existing file is overwritten, `add(path)` after a new one is written. The default does nothing, so saves work the same without source control. A Perforce adapter (P4Python or the `p4` command line) is set with `sourcecontrol.set_current()` when one is written.
- **JSON is written with sorted keys,** one item per line, so the few shared files diff and merge cleanly.
- **Typemap (Perforce admin setup):** `+l` (exclusive lock) on authored files that can't be merged, including `.ma`. A text merge of a Maya scene is meaningless.
- **Textures** stay game-side where possible. Maya file node paths that need them use `$RIGGING_PROJECT_DIR/...`, never an absolute path.
- **Animation scenes** hold imported rig copies, never references, so they carry everything they need. Exported clips depend on the skeleton and the bake, so a rig change that leaves the skeleton alone invalidates no export.

**Rejected:**
- A shared `registry.json` updated on every asset write, as first built. Under Perforce it means constant exclusive checkouts and a merge conflict on every integrate.
- Latest publishes and variant lists in `asset.json`. Two people publishing different variants of one asset would edit the same file.
- A per-machine registry cache file, as first proposed for this revision. Telling which `asset.json` files changed still needs a walk of the project, so a file would save only the JSON parsing while adding a cache that can go stale. The in-memory scan is simpler. A farm-built snapshot can seed it later if projects get too big to scan.
- Sequential publish numbers, which collide when two people publish at once, and Perforce changelist numbers as publish ids. A pending changelist is renumbered on submit, and personal work has no Perforce.

## Data without opening Maya

### `asset.json`, one per asset folder

```json
{
  "assetType": "Character",
  "formatVersion": 2
}
```

| Key | Meaning |
|---|---|
| `formatVersion` | Version of this file's schema. Version 1 also named a `skeleton`, and is updated by dropping it |
| `assetType` | One of the project's asset types (`project.json`) |

It holds only what a person decides about the asset. The id is not stored, because it is the folder's path, so a copied or moved folder can't carry a stale id. Skeletons, rigs, looks and publishes are found on disk. What a rig is built on and what a look binds to is in each one's own small file, `rig.json` and `look.json`: a template is shared between assets, so it can't hold its skeleton, and one file per rig and look means adding one edits nothing shared.

### `project.json`, one per project folder

```json
{
  "assetTypes": ["Prop", "Character", "Vehicle", "Head", "Weapon", "VFX", "Light", "Environment", "Skeleton", "Other"],
  "fileTypes": { },
  "formatVersion": 1
}
```

The asset types replace the template's old `rigType` and the asset root's old `assetType` enum. They're per project, so a project can add its own. `fileTypes` is below. Any folder can be a project root. An asset belongs to the nearest `project.json` above it.

### Registry, in memory

The registry indexes every asset in the project: its type, its skeletons, each rig (its skeleton, its source, its latest publish) and each look (its skeletons, its latest publish). It answers:

- every asset of a type
- every asset with a rig or look on skeleton X (an asset and a name), which is the set to batch-update when X changes
- the latest publish of a rig or a look, for the out-of-date check

It is built by scanning the workspace and is never written anywhere. `Registry.of(project)` gives one index per project per session, scanned on first use. `refresh()` rescans. The scan stops at each asset folder, so texture and weight folders are never walked, and it skips folders with their own `project.json`. Nesting is checked when an asset is created, because the scan doesn't look inside assets. `errors` lists folders whose path is not a valid id (ids are lowercase only) and unreadable `asset.json` files.

**Skeleton sharing** (D54): a rig or a look names a skeleton by two fields, the asset that holds it and its name, so NPCs and crowds share one skeleton with no copied file. A look-only asset holds `looks/` and nothing else. The skeleton's hash (`skeletonHash`) verifies the sharing and detects copies. Changing a shared skeleton still means batch updating its dependents, and the registry provides the list. The batch job itself is out of scope here.

## File model

Where a file lives is kept apart from how it is saved and loaded.

### File types (in `project.json`)

```json
"fileTypes": {
  "skeleton":     { "path": "skeletons/{skeleton}/skeleton.json" },
  "exports":      { "path": "skeletons/{skeleton}/exports", "folder": true },
  "riginfo":      { "path": "rigs/{rig}/rig.json" },
  "template":     { "path": "rigs/{rig}/template.json" },
  "shapes":       { "path": "rigs/{rig}/shapes.json" },
  "postops":      { "path": "rigs/{rig}/postops.py" },
  "rig":          { "path": "rigs/{rig}/{name}_{rig}_rig.ma" },
  "rigpublish":   { "path": "rigs/{rig}/publish", "folder": true },
  "controlposes": { "path": "rigs/{rig}/controlposes", "folder": true },
  "controlanims": { "path": "rigs/{rig}/controlanims", "folder": true },
  "lookinfo":     { "path": "looks/{look}/look.json" },
  "model":        { "path": "looks/{look}/model/{name}_{look}_model.ma" },
  "textures":     { "path": "looks/{look}/textures", "folder": true },
  "shaders":      { "path": "looks/{look}/shaders", "folder": true },
  "weights":      { "path": "looks/{look}/weights", "folder": true },
  "lookpublish":  { "path": "looks/{look}/publish", "folder": true },
  "sources":      { "path": "sources", "folder": true }
}
```

Paths are relative to the asset folder. The tokens are `{name}` (the id's last token) and at most one owner: `{skeleton}`, `{rig}` or `{look}`. Project format 3 (D54): a format 2 project updates itself, replacing only the default paths it finds. **A skeleton is its `skeleton.json` and nothing else** (author, 2026-11-16): saving serializes joints and loading creates them (`saveRigSkeleton`, `loadRigSkeleton`). **A model file is only meshes**: no joints, no skinCluster, no history, so a look is its model plus its weight files. Scenes keep the role suffix (`_rig`, `_model`). Poses and animations are a rig's, because control markup is unique per rig. Animation clips are skeleton data, so they sit with their skeleton. Sharing across assets goes through the registry's skeleton query.

### Publishes

Each rig and each look has its own `publish` folder, and each publish is a new folder in it, named by its id: the UTC time and the user, `20261108T142012Z_bmorgan`. It holds a `publish.json` (its id, user and whatever the publish step records: plugin version, template `rigVersion`, inputs) and copies of its inputs, and is meant to hold the built rig too (not written yet, see Next). Ids sort in time order, so the latest publish is the last folder. A folder is never edited after it is made. A publish is separate from a local version: it is the build output other files import, and its id is what `publishVersion` stores.

### Local versions

Without Perforce, and between submits with it, a local copy is the only undo for a save. Before a save overwrites a file, the old file is copied to the versions root, mirroring its place under the projects root: `<versions root>\shooter\main\characters\hero\rigs\game\shapes.v007.json`. A folder file type copies the whole folder (`weights.v003\`). The next number is the highest existing number plus one, so a deleted copy's number is never reused.

The versions root is `%LOCALAPPDATA%\bmaya\versions`, private to the machine and never submitted. `BMAYA_VERSIONS_DIR` overrides it, for example to keep the copies on the same synced drive as the files. It replaces mayadev's whole-asset `.Versions/` snapshot, which numbered by counting folder entries.

**Folders are created when something is written, not when a path is read.** `Asset.create` makes the asset folder, and a save makes its file's parent folders. Path getters never touch the disk. mayadev's legacy `root_dir` creates folders whenever it's read, so a typo in an id leaves empty folders behind, and on a synced drive they spread.

## Built in RigModules (2026-11-08)

- `createRigFromTemplate` needs `asset`, checks it with `core/AssetId`, and defaults `namespace` to its last token.
- The build reuses the namespace's asset root or creates one. The read phase fails when the namespace holds more than one asset root, when the asset root's `assetId` differs from `asset` (case is ignored), or when a rig root under it already has the template's `rigName`.
- Rig roots carry `pluginVersion` and `publishVersion` (a string, empty from a build) next to `rigName` and `rigVersion`.
- Removed: the template keys `assetId`, `assetType` and `rigType`, and the matching node attributes and enums.

## Built in mayadev (2026-11-08: `asset-model` merged into main; asset tools on branch `asset-tools`, merged into main by 2026-11-09)

Maya-free, under `bmaya/asset/`, so it runs in plain Python, mayapy and batch jobs. Errors are exceptions.

- `assetid`: parse, check, compare case-insensitively, convert to and from a folder path. Same rules as `core/AssetId` here. Every split and join of an id goes through it.
- `Project`: found from any path by walking up to `project.json`. Holds the asset types and file types. `Project.create` writes the default.
- `Asset`: `info`, `asset_type`, `skeleton`, `variants`, `path(fileType, variant=None)`, `file_exists`, `Asset.create(id, assetType, skeleton=None)`, `Asset.from_path`.
- `Registry`: `Registry.of(project)`, `refresh()`, `ids()`, `get(id)`, `find(asset_type=..., skeleton=...)`, `latest_publish(id, variant)`, `errors`.
- `publishes`: `create(asset, variant, data)`, `latest`, `list_ids`, `info`, `make_id`.
- `versions`: `version(path, root)`, `list_versions`, `versions_root`.
- `sourcecontrol`: the `SourceControl` interface with its do-nothing default, `current()`, `set_current()`.
- `handlers`: `FileHandler` with `save` and `load` stubs, a registry by file type, and `save`/`load` that keep versions, make folders and call source control.

`bmaya/maya/assethandlers.py` registers the Maya scene handler for `skeleton`, `model` and `rig` (load imports into the asset's namespace, merging on a clash) the `shapes` handler, which calls the RigModules shape commands, and the `template` handler, which copies a template into a variant and checks that its `rigName` is the variant. Other file types have no handler.

`bmaya/maya/assettools.py` holds the RigModules menu's asset tools: New Project, New Asset, New Variant, Save Skeleton, Save Model, Build Rig from Asset (skeleton, template, shapes, model, weights), Save Rig, Save and Load Control Shapes, Mark Meshes for Skinning, Save and Load Skin Weights, and Save and Load Pose (a pose is `poses/<name>.json` in the asset, shared by its variants: Save Pose asks for the name and versions an existing file, Load Pose opens its dialog in that folder; both fall back to a plain file dialog before the asset folder exists, and poses have no `FileHandler` yet). They read a rig's asset id and variant through `rigmodules.query` (D30).

Tests: `tests/asset/run.bat` runs 44 plain Python tests, then 3 mayapy tests against the RigModules plugin (`RIGMODULES_PLUGIN`). The legacy `bmaya.maya.asset` package is untouched: it goes with the legacy rig builder. `bmaya.core.log` imports Maya, so the new package doesn't log.

## Next

The proposed extension upstream of the rig (art stages, status, intake, face data, mesh markup) is in `asset-pipeline.md` (D44, D45).

1. The template authoring UI, after a module interface query command.
2. Dropped by D54: a namespace holds one rig, so a second rig on a shared skeleton builds into its own namespace, or follows the first as an attachment (D55).
3. Build Rig from Asset… with a file browser on `asset.json`, loading shapes and weights after the build (`decisions.md`, "Before adding more modules").
4. Finish the publish step. Built (D70 phase 4, mayadev `6ae0473`): `publish_rig` and `publish_look` with their gates, writing `publish.json` and copies of the inputs. Not built: writing the built rig into the folder and setting `publishVersion` on its rig root.
5. Handlers for postops, poses and anims. The weights handler is built (2026-11-09, D35).
6. A Perforce source-control adapter, when a project uses Perforce.

### Potential TODO: asset GUIDs

Not needed now (author, 2026-11-08). Asset ids are relative to the projects root, so they survive branching, and assets are rarely renamed or moved. If renames become common, give each asset a GUID in `asset.json` and use it for references between assets (`skeleton`, and anything that later points at another asset). The registry would map GUIDs to current paths. The path id stays the main id. The cost: an asset folder copied by hand duplicates its GUID, so the registry must flag duplicates, and a "duplicate asset" command must issue a new GUID.
