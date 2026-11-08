# Asset model

**Status:** decided 2026-11-08 (D29 in `decisions.md`). The scene side is built (see "Built in RigModules"). The file model, local versions and registry are designed here and not built yet. They go on a new mayadev branch.

An asset is the top-level container for one thing in the game: a character, prop, vehicle, weapon, vfx element or environment piece. It owns its files on disk, its rig variants, and the namespace it lives under in a Maya scene. This page covers the asset id, the scene structure, the file model, and the registry that answers questions about assets without opening Maya.

## Split between the repos (D24 stands)

| Piece | Home | Why |
|---|---|---|
| Asset id checks, asset root node, namespace ownership, rig variant and version stamps on the rig root | RigModules C++ | Nodes, attributes and the build command |
| Query from a scene node to its asset root, id, variant and stamps | RigModules `python/` (not started) | Walks rig nodes |
| Project root, file model, file types, local versions, registry, the out-of-date check | mayadev, on a new branch | Pipeline layout. No C++ code needs a path from an asset id: every command takes an explicit `file=` |
| Save and load handlers per file type | mayadev, calling RigModules commands where RigModules owns the format (shapes) | Loading belongs to whoever owns the format |

The file model has one implementation, in Python, with the layout stored as data in `project.json`. There is no C++ copy to drift out of step. A tool in another language (engine side, C#) reads the same JSON files.

## Asset id

- Pipe-separated, kept for existing tools: `shooter|main|characters|hero`.
- The id is the asset folder's path relative to `RIGGING_PROJECT_DIR`, with `\` replaced by `|`. Depth varies, because assets end up everywhere.
- A folder is an asset when it holds an `asset.json`, which keeps variable depth unambiguous. Assets never nest: no asset folder sits inside another.
- Each token is letters, digits and `_`, and none is empty.
- Ids compare case-insensitively and are stored as authored. Two assets whose ids differ only in case are an error, because Windows paths ignore case and Perforce and engines don't.

`RIGGING_PROJECT_DIR` is the one environment variable. The first levels under it are the game and its project (`<game>\<project>\characters\<name>`). The file model doesn't hard-code those levels: it finds the project from the nearest `project.json` above the asset.

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
| `publishVersion` | publish step (mayadev) | The asset publish the rig came from. 0 until published |

The out-of-date check runs in mayadev. For each rig root in a scene, it compares `publishVersion` with the registry's latest publish of that asset and variant, and `pluginVersion` with the loaded plugin. A scene full of imported rigs can then be listed as current, stale (newer publish exists) or built with an older plugin, without opening any other file.

## Data without opening Maya

Three JSON files, all readable from plain Python or any other language.

### `asset.json`, one per asset folder (the source of truth)

```json
{
  "formatVersion": 1,
  "assetType": "Character",
  "skeleton": "shooter|main|skeletons|biped",
  "rigs": ["game", "cine"]
}
```

| Key | Meaning |
|---|---|
| `formatVersion` | Version of this file's schema |
| `assetType` | One of the project's asset types (`project.json`) |
| `skeleton` | Optional. The id of the asset whose skeleton this asset uses. Left out, the asset owns its skeleton |
| `rigs` | Rig variant names. Each matches a template's `rigName` |

The id is not stored. It is the folder's path, so a copied or moved folder can't carry a stale id.

### `project.json`, one per project folder

```json
{
  "formatVersion": 1,
  "assetTypes": ["Prop", "Character", "Vehicle", "Head", "Weapon", "VFX", "Light", "Environment", "Skeleton", "Other"],
  "fileTypes": { }
}
```

The asset types replace the template's old `rigType` and the asset root's old `assetType` enum. They're per project, so a project can add its own. `fileTypes` is below. Any folder can be a project root. An asset belongs to the nearest `project.json` above it.

### `registry.json`, next to `project.json` (a cache, rebuilt from the `asset.json` files)

For every asset id in the project, the registry holds its type, skeleton, variants, the files that exist, and the latest publish per variant. It answers these without walking the disk each time:

- every asset of a type
- every asset that uses skeleton X, which is the set whose poses and animations can be shared
- every asset to batch-update when skeleton X changes
- the latest publish of an asset's variant, for the out-of-date check

The registry is derived data. `rebuild()` walks the project for `asset.json` files, and creating, editing or publishing an asset updates its own entry. **Rejected:** the registry as the only source of truth. Every artist would edit one file (merge conflicts under Perforce), and one bad write would break every asset.

**Skeleton sharing** replaces the old studio practice of matching assets by skeleton file path. A skeleton is its own asset (`assetType: "Skeleton"`), and characters name it. Assets with the same resolved skeleton id can share poses and animations. A variant's added joints don't break sharing, because they only add.

Changing a shared skeleton still means batch updating its dependents (load the new skeleton, reanimate the rig, delete the old skeleton, re-export). The registry provides the list. The batch job itself is out of scope here.

## File model

Where a file lives is kept apart from how it is saved and loaded.

### File types (in `project.json`)

```json
"fileTypes": {
  "skeleton": { "path": "skeleton/{name}_skeleton.ma" },
  "model":    { "path": "variants/{variant}/model/{name}_{variant}_model.ma" },
  "textures": { "path": "variants/{variant}/textures/", "folder": true },
  "shaders":  { "path": "variants/{variant}/shaders/", "folder": true },
  "template": { "path": "variants/{variant}/template.json" },
  "shapes":   { "path": "variants/{variant}/shapes.json" },
  "weights":  { "path": "variants/{variant}/weights/", "folder": true },
  "postops":  { "path": "variants/{variant}/postops.py" },
  "rig":      { "path": "variants/{variant}/{name}_{variant}.ma" },
  "poses":    { "path": "poses/", "folder": true },
  "anims":    { "path": "anims/", "folder": true }
}
```

Paths are relative to the asset folder. The tokens are `{name}` (the id's last token) and `{variant}`. The skeleton, poses and animations belong to the asset, because variants share the skeleton. Everything that can differ between variants lives under `variants/{variant}/`. Sharing poses and animations across assets goes through the registry's skeleton query.

### Python (mayadev branch)

Maya-free, under `bmaya/asset/`, so it runs in plain Python, mayapy and batch jobs:

- `assetid`: parse, check, compare case-insensitively, convert to and from a folder path. Same rules as `core/AssetId` here.
- `Project`: found from any path by walking up to `project.json`. Holds the asset types and file types.
- `Asset`: id, folder, `info` (the parsed `asset.json`), `path(fileType, variant=None)`, `exists(fileType, variant=None)`. `Asset.create(id, assetType)` makes the folder and `asset.json` and updates the registry.
- `Registry`: `rebuild()`, `get(id)`, `find(assetType=..., skeleton=...)`, `latestPublish(id, variant)`.
- `versions`: local version copies (below).

The Maya side registers a `FileHandler` per file type, with `save(asset, path, **context)` and `load(asset, path, **context)`. Both are stubs that raise `NotImplementedError` until a handler is written. For example, the `shapes` handler calls `cmds.saveRigControlShapes(rigRoot, file=path)`, Maya files import into the instance namespace, and `weights` calls weightio. The core never imports a handler.

**Folders are created when something is written, not when a path is read.** `Asset.create` makes the asset folder, and a save makes its file's parent folders. Path getters never touch the disk. mayadev's `root_dir` creates folders whenever it's read, so a typo in an id or a registry query leaves empty folders behind, and on a synced drive they spread.

### Local versions

Without Perforce, and between submits with it, a local copy is the only undo for a save. Before a save overwrites a file, the old file is copied to `versions/` beside it: `variants/game/versions/shapes.v007.json`. A folder file type copies the whole folder (`weights.v003/`). The next number is the highest existing number plus one. (mayadev counts folder entries instead, which reuses a number after a deletion.)

This replaces mayadev's whole-asset `.Versions/` snapshot. Per-file copies are what artists make by hand today. A whole-asset snapshot is still possible later, as one call that versions every file type.

A publish is separate from a local version. It is the numbered build output that other files import, and it is what `publishVersion` and the registry's latest publish count.

## Built in RigModules (2026-11-08)

- `createRigFromTemplate` needs `asset`, checks it with `core/AssetId`, and defaults `namespace` to its last token.
- The build reuses the namespace's asset root or creates one. The read phase fails when the namespace holds more than one asset root, when the asset root's `assetId` differs from `asset` (case is ignored), or when a rig root under it already has the template's `rigName`.
- Rig roots carry `pluginVersion` and `publishVersion` next to `rigName` and `rigVersion`.
- Removed: the template keys `assetId`, `assetType` and `rigType`, and the matching node attributes and enums.

## Next

1. The mayadev branch: `bmaya/asset/` as above, its tests, and handlers for the file types that already have a format (shapes, Maya files, weights).
2. The RigModules `python/` query from a node to its asset root, id, variant and stamps, which the out-of-date check needs.
3. A publish step that writes the `rig` file type, sets `publishVersion` and updates the registry.
