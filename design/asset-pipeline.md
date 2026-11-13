# Asset art pipeline

**Status:** proposal (D44, Leaning). Nothing here is built. It extends the asset model (`asset-model.md`, D29) upstream of the rig, to the art that becomes the model: sculpt, lowpoly, UVs, bakes, textures and shaders. It adds no C++ except possibly the skin tools named in section 5.

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
| Face data was hard to get in | No path for it that matched the rest of the pipeline (see section 7, still open) |
| Loading skin data was slow | The author's pain with the load length |
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
6. **Identity is not a name.** Meshes are found by `meshId`, topology hash or closest-point match, never by name.

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

**Load speed.** The weight file is JSON. Before changing the format, measure where the time goes: parsing, building the `MDoubleArray`s, or the bind and `setWeights` calls. Likely options, in order of cost:

1. Skip the load when the mesh already has the weights in the file (a content hash, stored with the file and checked first).
2. Load only the meshes that changed.
3. A compact binary encoding of the table.

Do not pick one until the author has numbers for a real asset.

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

## 7. Face data (open)

Getting face data in was hard, and the design does not yet say what "face data" means here. The likely pieces are blendshape targets, face skinning, and the topology a head must share so that data can move between heads. Questions for the author:

- Which of those did the aggregator handle badly?
- Do heads share a topology family, so that data moves between them without transfer?

If they do, the registry could answer "every asset on this topology", the way it answers "every asset on this skeleton" for poses. That would be a **topology id** on the asset, in `asset.json`. Do not add it until the answer is known.

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
| Weight transfer on a table, content hash on weights | RigModules `core/` (`SkinWeightTable`, `SkinWeightFile`) |
| Closest-point and UV lookups, applying the transfer to a skinCluster | RigModules `maya/shared/SkinWeightScene` |
| Rig version stamps, `getRigInfo` | RigModules, unchanged |

The boundary is D24 and D30 unchanged: no C++ resolves a path from an asset id.

## 12. Order

1. A read-only status query in mayadev, over the file types that exist today. No UI, and `stage` and `inputs` not yet needed.
2. `stage`, `inputs` and `root` in `project.json`, with the handlers writing input stamps.
3. A measured profile of skin weight load time on a real asset (section 5.1). The answer picks the fix.
4. The intake tool and the mesh checks.
5. The weight transfer for a changed topology.
6. The panel.
7. The publish manifest, then the Perforce adapter, which makes per-part checkout real.

## 13. Maintenance surface

- Three optional keys on file types in `project.json` (`stage`, `inputs`, `root`), and one new status vocabulary.
- One new environment variable for the source root.
- A weight-table transfer in `core/` and a content hash in the weight file (a `formatVersion` step, D33).
- A set of mesh checks, with thresholds in `project.json`.
- Tests: plain Python for status and file-type rules, mayapy for the checks and the transfer (pose-style cases written by hand, never recorded), and a round trip for the weights.

## 14. Rejected

- **A second container that mirrors the engine's data types.** It repeats the aggregator's mistake.
- **A stored, shared status or registry file.** It conflicts on every integrate (D29).
- **Parsing ZBrush, Substance, Blender or Max files.** They are not our formats.
- **Fixing topology silently during intake.** It breaks skinning.
- **Checking mesh names.** The studio never cared, and identity is by `meshId`.
- **Making the source root a requirement.** Most people do not map it.

## 15. Open questions

- Which face data was hardest to get in, and do heads share a topology family (section 7)?
- How long does a skin load take on a real asset, and where does the time go (section 5.1)?
- Which neutral format do the Blender and Max artists deliver, FBX or USD?
- Which mesh checks hurt most when missing, and what are the budgets per asset type?
- Does the engine side read `publish.json`, or does it need its own format?
