# Asset art pipeline

**Status:** proposal (D44 and D45, Leaning). Nothing here is built. It extends the asset model (`asset-model.md`, D29) upstream of the rig, to the art that becomes the model: sculpt, lowpoly, UVs, bakes, textures and shaders. It adds C++ only for the skin tools in section 5.1 and, if signed off, mesh markup in section 5.2.

## 1. The problem

Asset art is the least tooled part of the pipeline. It happens before rigging and animation, by people who want to make art, in whatever DCC they like: ZBrush, Blender, 3ds Max, Maya and Substance. Getting the result into Maya cleanly was left to the artist, and it was always a pain.

The author built and sped up a studio tool for this, which was called "the aggregator" because the engine had no way to edit art assets. It held a container of data types (skeleton, mesh, markers, markup properties, skinning) and exported each part separately. Skin weights were engine data (gr2 plus a custom skin format), and meshes were Maya files or gr2. Each item was a series of files that could be checked out on its own. The container was meant to show what the engine saw.

What went wrong with it:

| Aggregator problem | Cause |
|---|---|
| It was separate from vanilla Maya, and work could not be saved without exporting | The editing state lived in the tool, and export was the only way to persist it |
| The container was a poor art-side equivalent of the engine | It mirrored the engine's data types, not the artist's intent |
| Mesh cleanliness was not checked until it caused trouble | No intake step, and no checks tied to a stage |
| An art change broke skinning | Weights were tied to a mesh the artist had just replaced, with no way to carry them across |
| Face blend shape targets were stored as meshes in the rig's Maya file, and edits needed the rig | The Maya file was the source, so a rigger pushed every modeler edit through (section 7) |
| Importing Maya files and loading skin data for many meshes were slow | Too much data written into the files, and one load per mesh |
| Updating a rig cost an animator their keys and layers, so they avoided updates | An update meant reanimating from the skeleton and baking (section 9.1) |
| Nothing said what was out of date | No `publish.json` equivalent. Perforce was the only signal, skeleton versions never moved and meshes had none |
| High-poly source files were out of reach | They lived in a parallel `highpoly` folder that most people did not map in their workspace |

Mesh names mattered little to anyone, so a design that relies on them will fight the studio's habits.

## 2. What already exists

The asset model already has most of the shape:

- An asset is a folder with an id. A file type is one file or one folder, so each can be versioned and checked out on its own (`asset-model.md`, "File model").
- Variants share one skeleton. Model, weights and shaders are per variant.
- Publishes are immutable folders with a `publish.json` that records inputs.
- Local versions protect work between saves. Save is a scene save, never an export.
- Meshes are identified by `meshId`, not by name, and weight files carry a topology hash (D35). That already matches "names do not matter".
- The registry is an in-memory scan and nothing derived is ever submitted (D29).

What is missing: file types for the upstream stages, an intake step into Maya, per-stage checks, a status model, a way to carry skinning across an art change, and an interface that shows it all.

## 3. Principles

1. **Save never needs export.** Exports are derived from scene files and are stamped with their source. Nothing in an artist's loop requires one.
2. **Mirror the art intent, not the engine's types.** The engine's view is the publish manifest (section 9). The artist sees an assembled scene.
3. **Never parse a source format we do not own.** ZBrush, Substance, Blender and Max files are tracked opaquely. Only their exports to a neutral format are read.
4. **Status is computed, never stored.** It comes from stamps inside the files themselves, found by a scan. A shared status file would conflict on every integrate (the D29 reasoning).
5. **A part that cannot be reached is `unavailable`, not `missing` and not an error.** It never blocks a save or a publish (section 8).
6. **Identity is not a name.** Meshes are found by `meshId`, topology hash or closest-point match, never by name. What a mesh is for (face, proxy) is a marked usage, never a name (section 5.2).

## 4. Stages and file types

All of this is data in `project.json`. A file type gains three optional keys: `stage`, `inputs` and `root`.

| Stage | Example file types | Who | Parsed by us? |
|---|---|---|---|
| `source` | `sculpt` (ZBrush), `substance` (project files), `bakes` | Modelers, texture artists | No. Tracked as opaque files or folders |
| `intake` | `modelsource` (the lowpoly in a neutral interchange format) | Modelers | Read on import into Maya |
| `scene` | `model`, `skeleton`, `rig` (exist today) | Modelers, riggers | Yes, by Maya |
| `export` | `publish` (exists, to be built) | The publish step | Written by us |

`inputs` names the file types a type derives from (`model` from `modelsource`, `weights` from `model` and `skeleton`). The status model (section 6) needs nothing else.

Nothing changes for an existing project: a type with no `stage` is a `scene` type.

## 5. Intake and mesh checks

Getting art into Maya cleanly is the first pain, so it becomes a step with a defined result rather than the artist's problem.

**Intake.** A modeler delivers the lowpoly in a neutral interchange format (FBX, OBJ or USD), or a Maya scene if they work in Maya. An intake tool imports it, applies the checks, assigns `meshId`s (D35) and writes the `model` scene file. The modeler never needs to know about namespaces or the rig.

**Checks.** They run in plain mayapy, so they work in a batch job and need no GUI. Each has a severity: `error` blocks publishing, `warning` does not, and some have an automatic fix.

| Check | Fix | Notes |
|---|---|---|
| Units and up axis | Convert | The usual source of a mesh arriving at the wrong scale |
| Transform history and non-frozen transforms | Delete history, freeze | Must not run on a skinned mesh |
| Normals, n-gons, non-manifold geometry, lamina faces, zero-area faces | Report | Fixing silently changes topology, and that breaks skinning (section 5.1) |
| UV sets (missing, overlapping, outside 0-1), texel density | Report | Thresholds come from `project.json` |
| Material and shader assignment | Report | Count against a budget |
| Vertex and triangle budget | Report | Budget per asset type |

Mesh **names are not checked.** If the engine ever needs names, the publish step derives them from `meshId` and markup.

**Where the code lives.** The checks are pipeline logic, so they live in mayadev (D24, D30), in Python. If a check proves too slow on a large mesh, move that one to `core/` or the plugin and measure first. A check that fixes topology is never automatic.

### 5.1 Skinning that survives an art change

The author's most common pain was an art change breaking skinning. There are three cases:

| What changed | What happens today | What it should do |
|---|---|---|
| Vertex positions only (a sculpt tweak) | The topology hash matches, so the weights load as they are (D35) | Nothing new |
| Topology changed | The hash does not match, and the load fails | **Transfer** the old weights to the new mesh by closest point (or by UV, which the author used successfully for heads), then show the result as unreviewed |
| The mesh was renamed or replaced wholesale | Names do not matter, so a new `meshId` makes it look like a new mesh | The intake step offers to carry the old `meshId` across, by closest-point match to the previous version of the mesh |

The transfer is the "closest-point transfer for changed meshes" that D35 lists as not built. It belongs in `core/SkinWeightTable` (a table in, a table out, no Maya), with the closest-point and UV lookups in the Maya layer. The previous weights come from the last publish or the local versions (`asset-model.md`), so nothing new is stored.

**Load speed.** D35 already measured this: a 102,082-vertex mesh with 60 influences loads in place in 0.75 s (JSON parse 0.25 s, `setWeights` 0.44 s) and onto a new skinCluster in 1.0 s, so Maya's own calls are most of it. The author's pain was the previous studio's gr2 and custom format, not this one. The author reports loads were still slow for many meshes, so what is not known is the total for a whole asset, with every mesh loaded at build, and how much of it is the Maya file import rather than the weights. Measure both before changing anything. Options, cheapest first:

1. Skip the load when the mesh already holds the file's weights (a content hash, stored in the file and checked first).
2. Load only the meshes that changed.
3. D35's open question: a binary format or a faster text parser. Binary gives up the clean diffs the author asked for.

### 5.2 Mesh markup

Two separate things, as at the previous studio: tools identify meshes by a **usage**, and the engine reads **properties** from the parts of a mesh.

**Usage.**

- An optional string on a mesh: what the mesh is for. Its presence is how a specific tool recognises a mesh. There are no defaults, and no usage is normal, because markup is generally not enforced.
- Examples: `face_proxy` and `helmet`, used to toggle between a face view and a helmeted view (a Python tool shows one usage and hides another).
- **Gear parts are a legacy need, supported but not assumed.** For the player and some characters the previous studio separated meshes by usage (arms, legs, torso, helmet, cape) for a plug and socket gear system. That system dated from about 2013, limited what designs could do, cost design continuity, and paid off mainly for microtransactions. Usage supports it where an asset needs it, as the opt-in set below, and nothing in the asset model assumes characters are built from swappable parts. Rig variants (D29) and looks (properties, below) are the other route. Whether a replacement is wanted is open.
- Not unique, so a query returns a list.
- **Required usages are opt-in.** `project.json` can define a usage set (`"usageSets": { "gear": ["arms", "legs", "torso", "helmet", "cape"] }`), and an asset's `asset.json` names the set it follows. Publish validation then requires every usage in the set, and only for that asset. Which level opts in, per asset or per asset type, is open.
- **Gear pieces and the skeleton.** Swappable pieces share the character's skeleton through the registry's skeleton query (D29). Weights are per mesh, keyed by joint markup (D35), so a piece's weights should load onto any rig on that skeleton. That is design reasoning, not tested.

**Properties.**

- Engine-side data: shaders, looks and the like. They are **per face group**, because a mesh can have several materials. They are not usage.
- **A face group is a Maya shading group,** the face assignment Maya and the engine already agree on. Storing it that way means we keep no per-face data of our own. The markup goes on the shading group, not the mesh.
- A **dye system** applied materials to regions generically, from painted maps. Those regions are texture-space, not geometry, so the C++ stores none of them. A face group's properties can only reference the map and say which channel is which dye region, and the painted map is a file under the `textures` type. Whether that fits ordinary properties is open below.

**The attribute.** One string, `meshMarkup` (D16), read and written with `JsonUtils`, on a mesh transform or on a shading group:

```json
{ "markupVersion": 1, "usage": "helmet" }
{ "markupVersion": 1, "properties": { "shader": "armor_metal", "look": "damaged", "dyeMap": "helmet_dye.tga" } }
```

A mesh carries `usage`. A shading group carries `properties`. The C++ checks only the shape when it writes.

**The schema is the project's, and C++ checks against it.**

- `project.json` lists the properties that exist for each target: a type, optional allowed values, a default and whether it is required.
  ```json
  "faceGroupProperties": {
    "shader": { "type": "string", "required": true },
    "look":   { "type": "enum", "values": ["clean", "damaged"], "default": "clean" }
  }
  ```
- A Maya-free `core/MeshPropertySchema` validates a properties object against it: unknown names, wrong types, values outside an enum and missing required ones. The C++ never reads a project path (D24), so the schema arrives as a file. Engine-specific names live in the project's file, never in C++.
- The word is `look`, not `variant`. A variant in D29 is a rig variant.

**Not part of any key.** Weight files stay keyed by `meshId` and topology hash (D35), so changing markup never invalidates weights. The markup is one small string attribute, nothing like the mesh data section the previous studio paid for.

**What tools do with it.**

- View toggles by usage.
- Publish writes each mesh's usage and each face group's properties into `publish.json` (section 9), so the engine reads them without Maya.
- Checks and budgets can apply per usage.
- Face tools find the face mesh of any variant, whatever it is called.
- Intake sets them from the neutral file (an object property or a naming prefix the project chooses).

**Commands** (names need the author's call): `cmds.setRigMeshMarkup(*nodes, usage=..., properties=json)`, `cmds.getRigMeshMarkup(node)` and `cmds.validateRigMeshMarkup(*nodes, schema=path)`, each taking meshes or shading groups. `validate` is not one of the verb pairs in CLAUDE.md, but the template checks already use that word (D17). Setting and getting check only the structure, and `validate` checks the project schema, so a tool can set markup without the project file at hand.

**Where it lives.**

| Piece | Home |
|---|---|
| `RigAttr::MeshMarkup`, `core/MeshMarkup` (schema, version, serialize) and `core/MeshPropertySchema` (parse the schema, validate properties) | RigModules `core/` |
| The three commands | RigModules `maya/commands/` (D14 shape) |
| A query by usage in a namespace (`get_meshes(namespace, usage=...)`) | RigModules `python/rigmodules/skinweights.py` |
| `meshProperties`, `faceGroupProperties` and `usageSets`, which file holds the schema, the view toggle tool | mayadev (`project.json`) |

**Complexity added:** one attribute, two `core/` files, three commands and three `project.json` keys. The author chose C++ for anything with a schema (2026-11-13). It still needs sign-off through the gate (D12) before implementation.

## 6. Status model

For each part of each variant, the tool computes one status:

| Status | Meaning |
|---|---|
| `missing` | The file is not there, and nothing has been made yet |
| `current` | It exists, and the stamp of each input matches the input's current stamp |
| `stale` | An input has changed since this file was made |
| `unvalidated` | It is current, but its stage's checks have not run on this version |
| `unavailable` | Its location is not reachable on this machine (section 8) |

Each derived file stores the id or hash of each input when it was made, as `publish.json` already does for publishes. Staleness is then a comparison, so nothing is written anywhere central. A scan reads the stamps, the same way the registry reads `asset.json`.

## 7. Face data

### 7.1 What the previous studio did

- **Targets lived in the Maya file.** Blend shape targets were stored in a "mesh data section" of the rig's Maya file, as non-export meshes found by markup. They held the delta between a target and the base. To keep the file light, their normals were stripped, they had one standard surface and no UVs.
- **It was slow and out of step.** Importing Maya files was among the slowest operations, because of how much data they carry, and loading skinning for many meshes was slow too. The targets in the file were the source, but editing them needed the rig, so a rigger almost always had to push a modeler's target edits through to the rig.
- **A shape editing tool helped, but on the rig.** The author wrapped Maya's native shape editor into a tool for the face rig. It worked well and had useful tools, but it ran on a rig, not in an aggregated data editing context.
- **The face rig was both things at once.** Blend shapes drove joints that rode a master base mesh on UV pins, and the engine result was skinning, solved with linear skinning decomposition. The author's verdict: the worst of both worlds.

### 7.2 Direction (D45)

Face expressions are driven **almost entirely by blend shapes**. A rig may layer a joint rig under them where it is needed, such as the skinning of the head and neck, but expressions do not come from joints or from a skinning solve such as linear skinning decomposition. A strong, flexible module-based joint rig is the alternative for a face that cannot use blend shapes. Which one an asset uses is a template choice. The animation interface is the same either way: channel-only controls (D34), so poses, animation and mocap data (D42) do not care which one is underneath. The face modules themselves go through the gate (D12) separately. This page covers only their data.

### 7.3 Blend shape targets as asset data (proposed)

Targets are data, not meshes in the production scene.

- **A `blendshapes` file type per variant** (a folder, one file per base mesh). The name keeps it apart from the control `shapes` file. Each file holds its targets by name, as **sparse deltas** (vertex index and offset), keyed by the base mesh's `meshId` and topology hash like weight files (D35), with a `formatVersion` (D33). Most targets move one region, so the files are small and a diff shows which target changed.
- **The rig loads them at build,** like weights, and builds the deformer from them. No target meshes sit in the rig scene, and the `model` file stays lean, so the import cost the previous studio paid goes away.
- **Targets are edited in context, on a rig built from the template, with the targets as data.** A modeler judges a target on a lightweight build (head and neck, from a template, with the deltas loaded), together with a simple pose library of face poses (ControlPoses, D34) and optionally a test clip (a ControlAnim, D42), so a target is seen in combination and in motion, not on a bare mesh. Nobody hand-edits the rig for this. The working target meshes are generated from the deltas when the scene opens and written back as deltas on save, so what the rig reads is what the modeler saved, and a rigger does not push anything. The working meshes carry the usage `target` (section 5.2), so tools find them and the publish step leaves them out.
- **A topology change** transfers deltas the way it transfers weights (section 5.1), with the same closest-point or UV lookup.
- **The engine.** A blend-shape-first face needs the engine to take deltas (morph targets), or a bake to joints it can use. That is a project decision, and it is open below.
- **Not decided:** the file encoding (text for diffs or binary for size; D35 chose text and left binary open), the target naming vocabulary (FACS ids or free names), and whether a corrective's combination rule belongs in this file or in the face module.

### 7.4 Face animation and mocap

Getting a face into a scene with its keys lined up to an editor's mocap takes was slow. The pieces already exist or are planned: a ControlAnim (D42) holds keys by control markup and channel, and `cmds.loadRigAnimation` takes an `offset`. What is missing is a tool in mayadev that places a take onto a shot's frame range, and the channel map from a source's channel names to ours. Both go with the face module design.

### 7.5 Shared topology (open)

If heads share a topology family, data moves between them without a transfer, and the registry could answer "every asset on this topology", like "every asset on this skeleton" for poses. That would be a **topology id** in `asset.json`. A mesh with the usage `face` (section 5.2) already lets tools find the face mesh of any variant. Do not add the topology id until the author says heads share one.

## 8. Source files outside the asset folder

Large source files (ZBrush, Substance) lived in a parallel `highpoly` folder that most people never mapped. The tool should expect this, not fight it.

A file type may name a `root`: a second root that holds files for the same asset id.

```json
"sculpt": { "stage": "source", "root": "highpoly", "path": "{name}/{name}_sculpt.zpr" }
```

- The root's location comes from the environment, next to `RIGGING_PROJECT_DIR`, with a fallback to a mapped path in `project.json`.
- A file under a root that is not mapped on this machine is `unavailable`, never `missing`.
- A derived file records its source's stamp when it was made. When the source is reachable the stamp is compared. When it is not, the derived file is shown as current and flagged unchecked.
- Publishing never needs an unavailable source. This is why principle 5 exists.
- The registry scan never walks a source root.

## 9. Publish manifest, the engine equivalent

The publish step (item 4 in the asset model's "Next") writes one folder per publish with a `publish.json`. This is the container the aggregator was trying to be. It lists every part with its file, its id or hash, its input stamps and its validation result, in JSON that an engine or C# tool can read without Maya.

Two rules keep the parts from drifting apart:

1. **A publish holds every part, with shared stamps.** A new skeleton with old skinning cannot be published, because the weights' recorded inputs would not match the skeleton in the same publish.
2. **Per-part export is allowed, and stamped.** A rigger can export only the skin weights for an engine test, and the result carries the same stamps, so it is comparable to a full publish.

### 9.1 Updates must be cheap

At the previous studio nothing like `publish.json` existed. Perforce was the only way to tell something was out of date. Skeleton versions never got updated, meshes had no version, and a rig had one, but an out-of-date rig meant reanimating from the skeleton onto the new rig and baking, which lost keyframes and animation layers. Animators avoided updating rigs, and faces had the same problem. A version number that nobody dares act on is worth nothing, so the cost of an update is a requirement.

- **Every part has a version** (its publish id, `publish.json`), and its status is computed (section 6), so skeletons and meshes can be stale in view, not only rigs.
- **An update never bakes** (D33). Animation lives only on control channels, control and channel names are public API, and a format change comes with an update step, so keys survive.
- **Layers are an open gap.** The animation file format (D42) stores keys per control and channel. Nothing in the code or docs mentions animation layers, so a rebuild that saves and loads a ControlAnim would drop layer membership. The referenced-rig path (D32, a newer publish replaces the old rig and the curves follow by channel name) may keep them. Find out which cases keep layers before promising that updates are safe.
- **Show what an update changes** before it runs: the stamps, the update steps it will apply and whether any step touches a control or channel name.
- **Proposed test** (for when this is built; not written): build a rig at one version, key several channels, apply an update to the next version, and check that every key and tangent is unchanged. A second case adds an animation layer and checks that its membership survives. Tier: the mayapy concepts tier, with the expectation written by hand.

## 10. Interface

One panel in mayadev, thin over the registry and the handlers:

- An asset browser from the registry (type, skeleton, variants, latest publish).
- An asset page with one row per part and variant, showing its status, with open, save, validate, check out and publish buttons.
- A modeler-facing mode that shows only the stages they own: drop a file, run intake, see the checks. No rig vocabulary.

All of the logic stays in Python modules under `bmaya/asset`, and the panel only calls them. That keeps the checks and status runnable in batch.

## 11. Split between the repos

| Piece | Home |
|---|---|
| File types, stages, status, intake tool, mesh checks, publish manifest, panel | mayadev |
| Mesh markup and its property schema (`meshMarkup`, `core/MeshMarkup`, `core/MeshPropertySchema`, three commands), if signed off | RigModules (`core/`, `maya/commands/`) |
| Weight transfer on a table, content hash on weights | RigModules `core/` (`SkinWeightTable`, `SkinWeightFile`) |
| Closest-point and UV lookups, applying the transfer to a skinCluster | RigModules `maya/shared/SkinWeightScene` |
| Rig version stamps, `getRigInfo` | RigModules, unchanged |

The boundary is D24 and D30 unchanged: no C++ resolves a path from an asset id.

## 12. Order

1. A read-only status query in mayadev, over the file types that exist today. No UI, and `stage` and `inputs` not yet needed.
2. `stage`, `inputs` and `root` in `project.json`, with the handlers writing input stamps.
3. A measured build of a whole asset's weights (section 5.1). The answer picks the fix.
4. Mesh markup (section 5.2), so intake, checks and the skin and publish tools can filter by usage.
5. The intake tool and the mesh checks.
6. The weight transfer for a changed topology.
7. The blend shape delta file and the target editing scene, designed with the face module (section 7).
8. The panel.
9. The publish manifest, with the update preview (section 9.1), then the Perforce adapter, which makes per-part checkout real.

## 13. Maintenance surface

- Three optional keys on file types in `project.json` (`stage`, `inputs`, `root`), the `meshProperties`, `faceGroupProperties` and `usageSets` keys, and one new status vocabulary.
- If signed off: one string attribute (`meshMarkup`), its markup and property schema code in `core/`, and three commands.
- One new environment variable for the source root.
- A weight-table transfer in `core/` and a content hash in the weight file (a `formatVersion` step, D33).
- If adopted: a blend shape delta file format in `core/` (versioned, D33), a load command, and the target editing scene.
- A set of mesh checks, with thresholds in `project.json`.
- Tests: plain Python for status and file-type rules, mayapy for the checks and the transfer (pose-style cases written by hand, never recorded), and a round trip for the weights.

## 14. Rejected

- **A second container that mirrors the engine's data types.** It repeats the aggregator's mistake.
- **A stored, shared status or registry file.** It conflicts on every integrate (D29).
- **Parsing ZBrush, Substance, Blender or Max files.** They are not our formats.
- **Fixing topology silently during intake.** It breaks skinning.
- **Checking mesh names.** The studio never cared, and identity is by `meshId`.
- **Making the source root a requirement.** Most people do not map it.
- **Putting usage or properties in the weight file key.** It would invalidate weights whenever someone relabelled a mesh.
- **A property bag with no schema in C++.** It would let a typo ship to the engine unnoticed.
- **Blend shape targets as meshes in the production scene.** They bloat the Maya file and make the rig the only place to edit them (section 7).
- **A face whose expressions come from a mix of blend shapes, joints and a skinning solve** (D45). Layering a joint rig for the head and neck is fine.

## 15. Open questions

- Do heads share a topology family (section 7.5)?
- Does the engine take blend shape deltas, or does a blend-shape-first face need a bake to joints (section 7.3)?
- Which updates keep animation layers, and which drop them (section 9.1)?
- Delta file: text or binary, and FACS ids or free target names?
- How long does a whole asset take at build, split into Maya file import and weight load, given D35's per-mesh numbers (section 5.1)?
- Are a mesh's face groups its Maya shading groups, or separate face sets (section 5.2)?
- Do dye regions fit as ordinary properties (a map name and a channel to dye mapping), or do they need their own data (section 5.2)?
- Do required usages (the gear set) opt in per asset or per asset type, and is a replacement for the plug and socket gear model wanted at all (section 5.2)?
- The names of the three mesh markup commands (section 5.2).
- Which neutral format do the Blender and Max artists deliver, FBX or USD?
- Which mesh checks hurt most when missing, and what are the budgets per asset type?
- Does the engine side read `publish.json`, or does it need its own format?
