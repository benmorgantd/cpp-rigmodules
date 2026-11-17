# Rigs and looks: the asset holds skeletons, rigs and looks

**Status:** Decided, D54 (author, 2026-11-16). Not built. It replaces the "variant" half of D29. `asset-model.md` keeps describing what is built until phase 4 changes it. The author chose this layout (option A, containment) over separate assets per part (option B, section 9), and answered section 8's questions the same day.

## 1. The problem

Today a **variant** is a rig. The rig root's `rigName` names the variant, and each variant folder holds its own template, shapes, rig file, model, weights and publishes (`variants/{variant}/...`). Every outfit is therefore a separate rig. Thirty outfits means thirty rigs to keep in step, though their controls, skeleton and behaviour are meant to be the same.

What the author meant by a variant is closer to a **look**: the meshes, textures, shaders and skinning worn over a rig. Different outfits on one hero skeleton have different looks and the same rig. Previous studios called this pair "rigs" and "rig renders". A render was a skeleton, model, textures and weights, much like a skeletal mesh FBX sent to the engine, but for the DCC only.

## 2. Terms

| Term | Means |
|---|---|
| **Skeleton** | A set of joints, identified by its hash (section 5). The same skeleton means the same joint markup, the same count and the same rest transforms. |
| **Rig** | One template built on one skeleton: controls, modules, shapes, postops. A skeleton can have many rigs (a game rig and a cinematic copy). Control poses and animation belong to a rig. |
| **Look** | Meshes, textures, shaders and skin weights bound to a skeleton. Any rig of that skeleton can wear any look of it. |
| **Attachment** | A rig with its own skeleton that follows another rig: a face, a cape, a prop. Section 7. |

"Look" replaces "variant" everywhere (author, 2026-11-16). The word changes because the meaning changes. Keeping `variant` with a new meaning would leave every stale call site working while it silently means the wrong thing.

**A different body is a different skeleton.** Different proportions mean different rest transforms, so a different skeleton. It can share control animation only through a retarget (`inverse-solve.md`, the offset seam). A machine-learned retarget for timing and weight is an open direction, not part of this plan.

**Looks stay whole bodies.** A parts-based look (arms, legs, torso, head pieces) was painful in practice: aggregating parts was slow, parts had to be modelled with plug-and-socket edges to fit, and they still intersected. Whole-body designs are also more cohesive. A small change such as a hat, glasses or a backpack is an attachment (a prop rig on a socket), not a look part. A change that deforms with the body (a draped hood) is skinned into the look, which makes it another whole look.

## 3. Layout on disk

```
characters/hero/                          the asset: asset.json
  asset.json
  skeletons/
    body/   skeleton.json, exports/
    face/   skeleton.json, exports/
  rigs/
    body/        rig.json, template.json, shapes.json, postops.py, hero_body_rig.ma, publish/, controlposes/, controlanims/
    body_cine/   the cinematic copy: rig.json (skeleton "body", source rig "body"), its own template
    face/        rig.json (skeleton "face", attaches to rig "body")
  looks/
    default/     look.json, model/hero_default_model.ma, textures/, shaders/, weights/, publish/
    winter/
  sources/
```

- **An asset holds any number of skeletons, rigs and looks** (author, 2026-11-16: multiple rigs per asset is fine, and more than one skeleton per asset, so `hero` holds both body and face). The skeleton does not move under the rig, because a skeleton can have many rigs.
- **`rig.json`** names its skeleton, its parent rig when it is an attachment, and its source rig when it is a copy (section 3.2). It's a new small file because the template can't hold the skeleton: templates are shared between assets (D29).
- **The template's `rigName` stays** and must equal the rig's folder name (author, 2026-11-16), as the template handler checks it against the variant today.
- **`look.json`** names the skeletons its meshes bind to. Each mesh binds to exactly one skeleton, as one skeletal mesh has one Skeleton in Unreal. A look can still carry meshes for several skeletons of the asset (a body mesh on `body`, a head mesh on `face`).
- **`skeleton.json` is the skeleton** (author, 2026-11-16, amending the first plan): its joints and its hash (section 5), and its only stored form. There is no skeleton `.ma`. Saving a skeleton serializes the joints (`saveRigSkeleton`) and loading one creates them from the file (`loadRigSkeleton`). It changes only when someone deliberately saves the skeleton, which D29's source-control rule requires. Rejected: a `.ma` with `skeleton.json` beside it, two formats for one thing that can disagree. What a load can't give back (radius, draw style, labels, groups between joints) is listed in `docs/skeletons.md`.
- **Control poses and animation move under the rig.** They are keyed by control markup, and markup is unique per rig (D25), not per asset. Face poses and body poses become separate libraries, which matches how animators use them.
- **Skeleton exports sit with their skeleton,** `skeletons/{skeleton}/exports/` (author, 2026-11-16): an animation FBX is skeleton data, and it targets one skeleton. `control-library.md` (D42) still shows `exports/` at the asset root and needs updating when this is built. `sources/` stays at the asset root.
- **Publishes are per rig and per look.** A rig publish and a look publish are separate builds with separate ids, and an animation scene imports one of each.
- **The default look is named `default`.**
- **File names keep the role suffix** (D29 amendment, 2026-11-11): `{name}_{rig}_rig.ma`, `{name}_{look}_model.ma`. A skeleton is always `skeleton.json`.
- **A model file is only meshes** (author, 2026-11-16): no joints, no skinCluster, no history. Saving a model strips the skinning and exports the meshes with their history cleared. A look is its model plus its weight files.

### 3.1 Sharing a skeleton between assets

The goal is no copied skeleton files. An asset can name another asset's skeleton (and so wear that rig's looks or use its rigs) by two fields, never a joined string:

```json
{ "skeleton": { "asset": "shooter\\main\\characters\\npc_male", "name": "body" } }
```

A plain `"skeleton": "body"` means the asset's own. This replaces today's `skeleton` key in `asset.json`, which named a whole asset.

Crowds and enemies that share a body are the case this covers. The shared asset holds the skeleton and its rigs. Its looks can be spread across look-only assets (`npc\guards`, `npc\civilians`), each naming the shared skeleton, so no folder has to hold every look. A look-only asset has no `skeletons/` or `rigs/` folder.

### 3.2 A copied rig reads its source's library

A copy made so a shot can change a rig freely (a cinematic body, a shot-specific sword) names its source in `rig.json` (author, 2026-11-16):

```json
{ "skeleton": "body", "source": { "asset": "shooter\\main\\characters\\hero", "rig": "body" } }
```

The library browser lists the source rig's poses and animations next to the copy's own, marked as the source's. Saves always go to the copy's own folders, so the copy never edits its source. Control markup survives a copy, so the source's files load. Where the copy has renamed or removed a control, `getRigDataFileInfo` already reports the controls the rig lacks (D51). One level only: a copy of a copy names the rig it was copied from, and the browser doesn't follow the chain.

### 3.3 Lowercase

Asset id tokens and skeleton, rig and look names are lowercase letters, digits and `_` (author, 2026-11-16). Uppercase is rejected, not converted. This replaces D29's "compare case-insensitively, store as authored", which removes a rule: two ids can no longer differ only in case. The template's `rigName` follows the same rule (`Human` becomes lowercase in `RigTemplates/Human.json`).

## 4. Scene structure

- **One rig per namespace** (author, 2026-11-16). The body is `hero_body:` and the face `hero_face:`. Display layers (D38) and selection sets (D39) then work unchanged, because their names are per namespace, and the "second rig in a namespace" problem in D29, D38 and D39 goes away.
- **The default namespace is always `{asset name}_{rig}`** (`hero_body`, `sword_main`), with no special case for an asset with one rig (author, 2026-11-16). `namespace` still overrides it, for a second instance.
- **The asset root merges into the rig root** (author, 2026-11-16). With one rig per namespace, `assetRootModule` would be one-to-one with the rig root, so the rig root stores `assetId` itself and `assetRootModule` (0x00219) goes. `RigTraversal::getAssetRoot`, the `assetRoot.children → rigRoot.assetRoot` connection and the build's "reuse the namespace's asset root" step go with it.
- **A look is applied into its rig's namespace.** A `lookRoot` node, connected to the rig root, records the look's asset id, look name and `publishVersion`, so tools find it by traversal, never by name.
- **The rig root stamps its skeleton's hash** (`skeletonHash`), computed by the build from its input joints. It joins `assetId`, `rigName`, `rigVersion`, `pluginVersion` and `publishVersion`.

### 4.1 Swapping a look

A look is its model file, which is only meshes, and its weight files (author, 2026-11-16, amending the first plan). Swapping:

1. Check the new look's weight files' `skeletonHash` against the rig root's. A mismatch is an error naming the joints that differ (from the two `skeleton.json` files).
2. Import the new look's meshes into the rig's namespace.
3. Load its weights onto the rig's joints. `loadRigSkinWeights` matches influences by joint markup, never by name, and binds at the joints' rest, so a posed rig doesn't matter.
4. Delete the previous look's nodes, then update the `lookRoot`.

There is no bake, and the controls are never touched, so the animation in the scene survives a swap.

Rejected: a look file whose meshes are skinned to a copy of the skeleton, with the swap moving each `skinCluster.matrix[i]` input to the rig's joint (the first plan). It saved a bind, but a model file with joints in it is a second place a skeleton is stored.

The swap also reconnects anything else the rig drives on the look. For the face this is the parameters node's outputs into the look's blend shapes (section 7.3).

Rejected: keeping the look's joints and rewiring the rig onto them. The modules own their joint wiring (`wireJoint`, `jointRestWorld`), so that swap would reach into every module and would put the export skeleton in question.

## 5. The skeleton hash

The author prefers an automatic hash to a user-updated version number. A manual version never got updated and became difficult to track.

**Contents:** for each joint, sorted by markup: its markup, its name, its parent's markup, its local rest matrix and its rotate order. Names are included (author, 2026-11-16) because the engine matches bones by name, so a rename makes a different skeleton. Matrix values are rounded to a 1e-4 grid before hashing (0.0001 cm in translation, and the same step in the rotation matrix elements). The hash is FNV-1a, as `topologyHash` already is (D35).

**It is deterministic.** The same skeleton always gives the same hash, so an accidental edit that is reverted (by undo, or a source-control revert) gives back the original hash. Two hash changes happen only in the record of saves, never in the identity: anything stamped with the old hash matches again. The one weak spot is a hand-made revert, such as moving a joint back by eye. It lands near the old value, but not exactly on it. Quantization absorbs that unless the value sits right on a grid boundary, which is rare, and then the mismatch message names the joint.

**It changes only for what matters.** Edits outside the contents above (display settings, colour, a label) leave it alone. That avoids the old problem where almost every edit counted as a new version.

**Where it is stamped:** the rig root (`skeletonHash`, at build), `skeleton.json`, skin weight files (D35, next to `topologyHash`), rig and look publishes, and the `meta` of pose and animation files (D51, filled by the rig, replacing the caller-passed `skeletonId`).

**What it gives:**
- A check at load: a look, weights, a pose or an animation made against another skeleton is reported before it deforms or poses wrongly.
- Finding duplicates: two assets whose copied skeletons have equal hashes are the same skeleton, and the registry can group them.
- Catching drift: an edit to one copy changes its hash, so a silent split becomes visible.

It doesn't stop files being copied. It detects copies. The shared reference (section 3.1) is what prevents duplicates, and the hash verifies it.

**Home:** the description format and the hash are Maya-free, so they go in `core/` (a `SkeletonFile`, like `SkinWeightFile`). Reading the joints is a Maya-side helper next to `JointMarkupScene`.

## 6. Listing looks that aren't synced

A directory scan sees only what is synced. A crowd asset with hundreds of looks may be mapped sparsely, and the library should still list every look. The options:

| Option | Verdict |
|---|---|
| A master list per asset ("these are this asset's looks") | Rejected. Two people adding looks to one asset edit the same file and conflict, which is the shared-file edit D29 rejected. |
| Scan the local disk | The fallback without source control. Lists only synced looks. |
| Ask source control for the folders | Chosen. Perforce lists depot folders without syncing them. The source-control adapter (D29) gets a "list folders" call, and the do-nothing default falls back to the local scan. |
| A farm-built index | Later, if the depot query gets slow. D29 already leaves room for it. |

Each look keeps its own `look.json`, so adding a look is still only new files. In the library, a look that isn't synced shows as not local, and choosing it syncs it.

## 7. Attachments

An attachment is a rig with its own skeleton that follows another rig, such as a face, cape or prop. Older engines couldn't do this, so studios used one master skeleton with many unused joints, or made a whole new asset to avoid spending animation time on unskinned joints. In Unreal the author would use attachment rigs for faces, capes and props.

**Its template lives in its rig folder,** `rigs/{rig}/template.json`, like every rig. An attachment is a rig like any other on disk. `rig.json` says what it attaches to.

**Its own asset or part of the asset:** a face or cape that belongs to one character is a rig in that character's asset. A prop used by many characters (a sword) is its own asset. A cinematic copy of a shared prop, made so a shot can change it freely, is a new rig in the prop asset (a skeleton can have many rigs). If the copy needs more joints, those come from an attachment skeleton, never by extending a skeleton.

### 7.1 The attach node (needs its own design through the gate)

Requirements captured so far (author, 2026-11-16):

- An attachment follows a hierarchy, not one socket. A head attaches from the chest up: its skeleton follows the body's chest, clavicle, neck and head controls.
- If the attached rig has its own controls for those regions (so it can be animated on its own), they are hidden and driven by the body while attached. They should also be locked and zeroed. Keys left on a hidden control would surprise an animator later.
- It reuses the child plug and socket mechanism. A custom node probably holds the mapping (child plug to socket, several pairs), with the child rig declaring what it needs and the node checking that the parent rig has those sockets.
- Rigs are in different namespaces, so every connection crosses namespaces.
- Building the body and face together and attaching them is mayadev Python tooling. The attach command itself belongs in RigModules, because it is about rig nodes.

It is a new node type, so it goes through the gate: design, a `ModuleContract`-style test, many instances in the perf rig, and `bench`.

### 7.2 `rigConstraint` across rigs

Props move between hand, hip and world, which is a space switch across two rigs, and that will be very common. A constraint driver today names `module.socket` inside one rig. The change is a driver that names a socket on another rig. Designed with section 7.1.

### 7.3 The face's parameters node (recorded here, designed later)

The face's output to blend shapes should be a node, not blend shape weights: control channels in, named 0 to 1 parameters out. The engine reads the parameters, not the deformer's values, to drive its own face. The node is where in-betweens and correctives are wired. On a look swap it is the one node that reconnects to the new look's blend shapes. It is a new module and goes through the gate. It belongs with the face module in `module-directions.md` (D46).

## 8. Answered questions (author, 2026-11-16)

| Question | Answer | Rejected |
|---|---|---|
| Joint names in the hash | Included: the engine matches bones by name | Markup and rest only |
| Rounding grid for rest matrices | 1e-4 | 1e-3, which misses small edits |
| Default namespace | Always `{asset name}_{rig}` | The asset name alone for a one-rig asset: a special case, and the namespace would change when a second rig is added |
| The asset root node | Merged into the rig root | Keeping a node that is one-to-one with the rig root |
| Skeletons per asset | Many: `hero` holds body and face | One per asset, which makes every face its own asset |
| The template's `rigName` | Kept, and must equal the rig folder name | Dropping it for a build flag |
| Skeleton exports | `skeletons/{skeleton}/exports/` | The asset root |
| A copied rig's library | `rig.json` names its `source`, and the browser lists the source's files (section 3.2) | An empty library until a copy needs one |

## 9. Option B, rejected: one asset per part

Each skeleton, rig and look would be its own small asset that names the others by asset id, with a `kind` key in `asset.json`.

| | A. Containment (this plan) | B. One asset per part |
|---|---|---|
| Assets per hero (body and face) | 1 | about 8 |
| Naming a rig or look | asset id plus a name, two fields | its own asset id |
| Kind of a thing | its folder (`rigs/`, `looks/`) | `kind` in `asset.json` |
| Moving a rig out to share it | its name becomes a new asset id, so references are updated | already an asset |
| Large look sets | `looks/` folders, spread over look-only assets | look assets anywhere |
| Change from today | moderate: `variants/` splits into `rigs/` and `looks/`, skeletons gain a folder | large: a new asset per part, new tools |
| Reading the disk | one folder shows the whole character | a character spreads over sibling folders |

**Why rejected (author, 2026-11-16):** too many asset folders per character, and it still ends in large look folders. B makes a rare case (moving a rig out of its asset) free, and costs folders and concepts on every character every day.

**Also rejected:**
- **Variants as rigs** (today's model): every outfit is a rig.
- **A user-updated skeleton version:** it never got updated, and almost every edit became a new version.
- **A master list of an asset's looks:** see section 6.
- **Parts-based looks:** see section 2.

## 10. What it touches

### RigModules

- `core/AssetId`: lowercase only. Template validation: lowercase `rigName`. `RigTemplates/Human.json` renamed.
- New `core/SkeletonFile` (description and hash), a Maya helper that reads joints into it, and `saveRigSkeleton` to write `skeleton.json`. Built 2026-11-16 (phase 2). Its reader, `SkeletonScene::createSkeleton` and `loadRigSkeleton` were built 2026-11-16 with phase 4, when `skeleton.json` became the only skeleton format.
- Rig root: `assetId` and `skeletonHash` attributes (in `RigAttr`), set by the build. The read phase rejects a namespace that already holds a rig, replacing the `rigName`-per-namespace check.
- `assetRootModule` removed (`AssetRoot.h/.cpp`, its `MTypeId`, `RigTraversal::getAssetRoot`, the `assetRoot` connection). `getRigInfo`'s asset root key goes. Its id 0x00219 is not reused.
- `getRigInfo`: the `variant` key becomes `rig`, adding `skeletonHash` (`RigAttr::Variant` goes). `rigmodules.query.RigInfo.variant` becomes `rig`. This is public API for mayadev.
- Pose and ControlAnim `meta` (D51): `variant` becomes `rig`, and `skeletonId` becomes `skeleton` plus `skeletonHash`, filled by the rig. Format version 3 for both, with an update step (D33).
- Weight files (D35): a `skeletonHash`, format version bump with an update step.
- New: the `lookRoot` node and the swap command. The attach node and cross-rig constraint drivers come later, through the gate.
- Docs: `asset-model.md` rewritten, D29 amended, `commands.md`, `python.md`, `library.md`, `skinning.md`, `mayadev.md`.

### mayadev (proposed edits, the author's go-ahead needed)

- `bmaya.core.asset`: `project.py` file types (`skeletons/`, `rigs/`, `looks/`), `Asset.variants` replaced by `skeletons`, `rigs` and `looks`, `registry` entries and `latest_publish` per rig and per look, `publishes` per rig and per look, `assetid` lowercase.
- `assettools`: New Variant becomes New Rig and New Look. `save_model` per look, `build_rig` builds a rig into `{asset}_{rig}` and applies its default look, and `save_rig` per rig. `assethandlers` follow.
- `maya_ui/rigpanel.py` shows the rig name in place of `variant`.
- The library browser (D51) gets a Looks kind: the looks whose skeleton hash matches the active rig's, with tags and search, and not-local looks from the source-control listing. It also lists a copied rig's source library (section 3.2).
- `control-library.md` (D42): exports move under `skeletons/{skeleton}/`.
- A new asset browser for riggers and modelers (assets, skeletons, rigs, looks, publishes, D44 status). It is separate from the library browser, which is for animators acting on the active rig.
- `asset-pipeline.md` (D44): per-variant status becomes per look and per rig.

## 11. Phases

Each phase is one change with its tests and docs.

1. **Decide.** Done 2026-11-16: section 8 answered, D54 Decided. `asset-model.md` is rewritten in phase 4, when the layout is built, because it describes what is built.
2. **Core.** Lowercase ids and names, `SkeletonFile` and its hash, and their tests. Done 2026-11-16, with `saveRigSkeleton` (moved here from phase 3, so the hash could be tested in the suite) and `docs/skeletons.md`. Every marked joint counts, marker joints included.
3. **RigModules scene.** One rig per namespace, the asset root merged into the rig root, `skeletonHash` on the rig root, `getRigInfo` and `RigInfo` renames, and the `meta` and weight file version steps. Done 2026-11-16. As built: the `getRigInfo` key stays `rigName` (it was never `variant`), and Python's `RigInfo.variant` became `rig_name`, matching the key, with `skeleton_hash` added. The library `meta` key is `rigName` too, not `rig`, for the same reason. A weight save takes `skeletonHash` from the rig root that drives the influences, saves `""` with a warning when they belong to no rig, and fails when they belong to two. A weight load reads the hash and doesn't check it: the check comes with the look swap in phase 5. Old scenes with an `assetRootModule` are not migrated (author, 2026-11-16).
4. **mayadev file model and tools.** The layout, `Asset`, the registry and publishes, then New Rig, New Look, Build Rig and Save Rig.
5. **Looks in the scene.** The `lookRoot` node, the swap command, and the library browser's Looks kind. Done 2026-11-17, as D54's built note describes.
6. **Source-control listing** of unsynced looks. Done 2026-11-17 in mayadev: `SourceControl.list_folders` and `sync`, `Asset.all_looks()` and `sync_look`, `assettools.list_rig_looks`, and `apply_look` syncing first. No RigModules change. The Perforce adapter (when a project uses Perforce) and the library window's Looks tab are not built.
7. **Attachments.** The attach node and cross-rig constraint drivers, designed through the gate, then the mayadev build-and-attach tool.
8. **Asset browser** (mayadev design first).
9. **The parameters node,** with the face module (D46).
