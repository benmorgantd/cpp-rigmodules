# Asset model

**Status:** decided 2026-11-08 (D29 in `decisions.md`). The scene side is built in RigModules (see "Built in RigModules"). The file model, local versions, registry, publishes, source-control adapter and handlers are built on mayadev branch `asset-model` (see "Built in mayadev"), not yet merged. Its user guide is mayadev's `py/scripts/docs/guides/Asset Layout.md`.

An asset is the top-level container for one thing in the game: a character, prop, vehicle, weapon, vfx element or environment piece. It owns its files on disk, its rig variants, and the namespace it lives under in a Maya scene. This page covers the asset id, the scene structure, the file model, and how tools answer questions about assets without opening Maya, all under Perforce without shared files that everyone has to check out.

## Split between the repos (D24 stands)

| Piece | Home | Why |
|---|---|---|
| Asset id checks, asset root node, namespace ownership, rig variant and version stamps on the rig root | RigModules C++ | Nodes, attributes and the build command |
| Query from a scene node to its asset root, id, variant and stamps | RigModules `python/` (not started) | Walks rig nodes |
| Project root, file model, file types, local versions, registry, publishes, source control, the out-of-date check | mayadev | Pipeline layout. No C++ code needs a path from an asset id: every command takes an explicit `file=` |
| Save and load handlers per file type | mayadev, calling RigModules commands where RigModules owns the format (shapes) | Loading belongs to whoever owns the format |

The file model has one implementation, in Python, with the layout stored as data in `project.json`. There is no C++ copy to drift out of step. A tool in another language (engine side, C#) reads the same JSON files.

## Asset id

- Pipe-separated, kept for existing tools: `shooter|main|characters|hero`.
- The id is the asset folder's path relative to `RIGGING_PROJECT_DIR`, with `\` replaced by `|`. Depth varies, because assets end up everywhere.
- A folder is an asset when it holds an `asset.json`, which keeps variable depth unambiguous. Assets never nest: no asset folder sits inside another.
- Each token is letters, digits and `_`, and none is empty.
- Ids compare case-insensitively and are stored as authored. Two assets whose ids differ only in case are an error, because Windows paths ignore case and Perforce and engines don't.
- The id is relative to the projects root, so it means the same asset in every workspace and every branch.

`RIGGING_PROJECT_DIR` is the projects root. The first levels under it are the game and its project (`<game>\<project>\characters\<name>`). The file model doesn't hard-code those levels: it finds the project from the nearest `project.json` above the asset.

## Scene structure

- **The build takes the asset as a flag:** `cmds.createRigFromTemplate(*joints, template=path, asset="shooter|main|characters|hero")`. Templates don't name an asset, so one template (a shared biped) builds many assets. Each asset's own `template` file type can still hold its own copy.
- **The asset owns the namespace,** and it is flat: one namespace per asset instance in a scene. Everything goes in it: the asset root, every rig root and module, the controls and the joints.
- **The default namespace is the id's last token** (`hero`). `namespace` overrides it, for example a second instance, or `hero_damaged` when the rigger wants the namespace to hint at the variant. Nothing reads the variant from the namespace.
- **One `assetRootModule` per namespace.** It stores the id in `assetId`. Rig roots connect to it through `assetRoot.children → rigRoot.assetRoot`.
- **Variants share the namespace.** The variant name is the rig root's `rigName`, from the template's `rigName`.
- **Variants share one skeleton.** A variant may add joints, never remove them (a cinematic rig adding face or cloth joints). Meshes, weights, textures and shaders are per variant.
- **Files are imported into a unique namespace, never referenced.** Animation files keep working as long as they're baked, or as long as RigModules gives the rig an update path. The version stamps are what make that path findable.

### Version stamps

Each rig root records what built it:

| Attribute | Set by | Meaning |
|---|---|---|
| `rigName` | build | The variant |
| `rigVersion` | build | The template's `rigVersion` |
| `pluginVersion` | build | The RigModules plugin version that ran the build |
| `publishVersion` | publish step (mayadev) | The id of the publish the rig came from (`20261108T142012Z_bmorgan`). Empty until published |

The out-of-date check runs in mayadev. For each rig root in a scene, it compares `publishVersion` with the latest publish of that asset and variant in the workspace, and `pluginVersion` with the loaded plugin. A scene full of imported rigs can then be listed as current, stale (newer publish exists) or built with an older plugin, without opening any other file.

## Source control and branches

**Mainline only (author, 2026-11-08).** Everyone submits to one stream, and builds are labelled changelists on it. Long-lived release streams are out of scope, because one person can't maintain release-stream tooling. Rejected for now: the rules release streams would need, namely integrating frozen publishes into the release stream and refusing to rebuild there.

**The rule: files in the depot change only when someone deliberately changes that asset.** Derived data is computed locally and never submitted. Appended data is a new file, never an edit to an existing one. So two people only ever touch the same file when they work on the same asset.

| Data | Where it lives | Changes when |
|---|---|---|
| `project.json` | Depot, one per project | Someone changes the project's asset types or file layout |
| `asset.json` | Depot, one per asset | Someone changes the asset's type or skeleton |
| Variants | Depot: a folder per variant with its `template.json` | A variant is added: new files only |
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
  "formatVersion": 1,
  "skeleton": "shooter|main|skeletons|biped"
}
```

| Key | Meaning |
|---|---|
| `formatVersion` | Version of this file's schema |
| `assetType` | One of the project's asset types (`project.json`) |
| `skeleton` | Optional. The id of the asset whose skeleton this asset uses. Left out, the asset owns its skeleton |

It holds only what a person decides about the asset. The id is not stored, because it is the folder's path, so a copied or moved folder can't carry a stale id. Variants and publishes are found on disk.

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

The registry indexes every asset in the project: its type, its skeleton (its own id unless `asset.json` names another), its variants and each variant's latest publish. It answers:

- every asset of a type
- every asset that uses skeleton X, which is the set whose poses and animations can be shared, and the set to batch-update when X changes
- the latest publish of an asset's variant, for the out-of-date check

It is built by scanning the workspace and is never written anywhere. `Registry.of(project)` gives one index per project per session, scanned on first use. `refresh()` rescans. The scan stops at each asset folder, so texture and weight folders are never walked, and it skips folders with their own `project.json`. Nesting is checked when an asset is created, because the scan doesn't look inside assets. `errors` lists ids that differ only in case and unreadable `asset.json` files.

**Skeleton sharing** replaces the old studio practice of matching assets by skeleton file path. A skeleton is its own asset (`assetType: "Skeleton"`), and characters name it. Assets with the same resolved skeleton id can share poses and animations. A variant's added joints don't break sharing, because they only add. Changing a shared skeleton still means batch updating its dependents (load the new skeleton, reanimate the rig, delete the old skeleton, re-export). The registry provides the list. The batch job itself is out of scope here.

## File model

Where a file lives is kept apart from how it is saved and loaded.

### File types (in `project.json`)

```json
"fileTypes": {
  "skeleton": { "path": "skeleton/{name}_skeleton.ma" },
  "model":    { "path": "variants/{variant}/model/{name}_{variant}_model.ma" },
  "textures": { "path": "variants/{variant}/textures", "folder": true },
  "shaders":  { "path": "variants/{variant}/shaders", "folder": true },
  "template": { "path": "variants/{variant}/template.json" },
  "shapes":   { "path": "variants/{variant}/shapes.json" },
  "weights":  { "path": "variants/{variant}/weights", "folder": true },
  "postops":  { "path": "variants/{variant}/postops.py" },
  "rig":      { "path": "variants/{variant}/{name}_{variant}.ma" },
  "publish":  { "path": "variants/{variant}/publish", "folder": true },
  "poses":    { "path": "poses", "folder": true },
  "anims":    { "path": "anims", "folder": true }
}
```

Paths are relative to the asset folder. The tokens are `{name}` (the id's last token) and `{variant}`. The skeleton, poses and animations belong to the asset, because variants share the skeleton. Everything that can differ between variants lives under `variants/{variant}/`. A variant is a folder there that holds the `template` file. Sharing poses and animations across assets goes through the registry's skeleton query.

### Publishes

Each publish is a new folder under the variant's `publish` folder, named by its id: the UTC time and the user, `20261108T142012Z_bmorgan`. It holds a `publish.json` (its id, user and whatever the publish step records: plugin version, template `rigVersion`, inputs), the built rig and copies of its inputs. Ids sort in time order, so the latest publish is the last folder. A folder is never edited after it is made. A publish is separate from a local version: it is the build output other files import, and its id is what `publishVersion` stores.

### Local versions

Without Perforce, and between submits with it, a local copy is the only undo for a save. Before a save overwrites a file, the old file is copied to the versions root, mirroring its place under the projects root: `<versions root>\shooter\main\characters\hero\variants\game\shapes.v007.json`. A folder file type copies the whole folder (`weights.v003\`). The next number is the highest existing number plus one, so a deleted copy's number is never reused.

The versions root is `%LOCALAPPDATA%\bmaya\versions`, private to the machine and never submitted. `BMAYA_VERSIONS_DIR` overrides it, for example to keep the copies on the same synced drive as the files. It replaces mayadev's whole-asset `.Versions/` snapshot, which numbered by counting folder entries.

**Folders are created when something is written, not when a path is read.** `Asset.create` makes the asset folder, and a save makes its file's parent folders. Path getters never touch the disk. mayadev's legacy `root_dir` creates folders whenever it's read, so a typo in an id leaves empty folders behind, and on a synced drive they spread.

## Built in RigModules (2026-11-08)

- `createRigFromTemplate` needs `asset`, checks it with `core/AssetId`, and defaults `namespace` to its last token.
- The build reuses the namespace's asset root or creates one. The read phase fails when the namespace holds more than one asset root, when the asset root's `assetId` differs from `asset` (case is ignored), or when a rig root under it already has the template's `rigName`.
- Rig roots carry `pluginVersion` and `publishVersion` (a string, empty from a build) next to `rigName` and `rigVersion`.
- Removed: the template keys `assetId`, `assetType` and `rigType`, and the matching node attributes and enums.

## Built in mayadev (branch `asset-model`, 2026-11-08)

Maya-free, under `bmaya/asset/`, so it runs in plain Python, mayapy and batch jobs. Errors are exceptions.

- `assetid`: parse, check, compare case-insensitively, convert to and from a folder path. Same rules as `core/AssetId` here. Every split and join of an id goes through it.
- `Project`: found from any path by walking up to `project.json`. Holds the asset types and file types. `Project.create` writes the default.
- `Asset`: `info`, `asset_type`, `skeleton`, `variants`, `path(fileType, variant=None)`, `file_exists`, `Asset.create(id, assetType, skeleton=None)`, `Asset.from_path`.
- `Registry`: `Registry.of(project)`, `refresh()`, `ids()`, `get(id)`, `find(asset_type=..., skeleton=...)`, `latest_publish(id, variant)`, `errors`.
- `publishes`: `create(asset, variant, data)`, `latest`, `list_ids`, `info`, `make_id`.
- `versions`: `version(path, root)`, `list_versions`, `versions_root`.
- `sourcecontrol`: the `SourceControl` interface with its do-nothing default, `current()`, `set_current()`.
- `handlers`: `FileHandler` with `save` and `load` stubs, a registry by file type, and `save`/`load` that keep versions, make folders and call source control.

`bmaya/maya/assethandlers.py` registers the Maya scene handler for `skeleton`, `model` and `rig` (load imports into the asset's namespace, merging on a clash) and the `shapes` handler, which calls the RigModules shape commands. Other file types have no handler.

Tests: `tests/asset/run.bat` runs 44 plain Python tests, then 3 mayapy tests against the RigModules plugin (`RIGMODULES_PLUGIN`). The legacy `bmaya.maya.asset` package is untouched: it goes with the legacy rig builder. `bmaya.core.log` imports Maya, so the new package doesn't log.

## Next

1. Merge the mayadev branch once the author has reviewed it.
2. The RigModules `python/` query from a node to its asset root, id, variant and stamps, which the out-of-date check needs.
3. A publish step: build the rig, write it and its input copies into `publishes.create`'s folder, and set `publishVersion`.
4. Handlers for weights (after weightio's API 1 calls are fixed), postops, poses and anims.
5. A Perforce source-control adapter, when a project uses Perforce.

### Potential TODO: asset GUIDs

Not needed now (author, 2026-11-08). Asset ids are relative to the projects root, so they survive branching, and assets are rarely renamed or moved. If renames become common, give each asset a GUID in `asset.json` and use it for references between assets (`skeleton`, and anything that later points at another asset). The registry would map GUIDs to current paths. The path id stays the main id. The cost: an asset folder copied by hand duplicates its GUID, so the registry must flag duplicates, and a "duplicate asset" command must issue a new GUID.
