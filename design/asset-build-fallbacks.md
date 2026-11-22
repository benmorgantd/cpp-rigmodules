# Asset builds that survive change (D70)

**Status:** proposal (D70, Leaning). Nothing here is built. It amends D54 (the skeleton hash gate) and D68 (rigid fallback), and fills in the "transfer" row of `asset-pipeline.md` section 5.1. The author's answers of 2026-11-22 are folded in. Open questions are in section 8.

## 1. The problem

While an asset is in progress, the skeleton, the meshes and the weights all change, and they change independently. Today a build fails when they disagree, and the only way out is a flag (`ignoreSkeletonHash`, the Asset Editor's "Ignore skeleton changes") followed by saving everything again by hand. A failed build leaves nothing in the scene to fix, so the failure blocks the repair.

Three causes, from reading D54, D68 and `apply_look`:

| Change | Today |
|---|---|
| A joint is added or a rest moves | The skeleton hash differs, so the whole look fails unless the flag is set. The weights themselves would load fine. |
| A joint is removed or its markup changes | The file names an influence that no longer resolves. With the flag the mesh goes rigid and its painting is lost. |
| A mesh's topology changes | The topology hash differs and the load fails. With the flag the mesh goes rigid. |

## 2. Principles

1. **A build never fails over source data.** The build is how the rigger repairs the data, so it must always produce a scene. This is D68, extended from "no file" to every mismatch.
2. **A hash decides status, never success.** A file made for another skeleton or mesh version is *stale*, which is shown and cleared by saving. It is not an error. Only an influence that cannot be resolved, or a topology that differs, needs a fallback.
3. **Each mesh degrades one step at a time, and says which step.** No silent loss. The build returns a per-mesh report, and the editor shows it.
4. **Mechanism in C++, policy in mayadev.** RigModules always applies the best fallback and reports. mayadev decides what the report means for saving and publishing. This is why the checkbox and the C++ flag can both go.
5. **Status is computed, never stored** (D44, principle 4). Nothing records "this mesh was transferred". The file on disk is unchanged by a build, so its stamps already disagree with the scene until the rigger saves.

## 3. Draft, checked out and published

The question was whether strictness should follow "unpublished" or "checked out". Neither changes how a *build* behaves. What differs is what a *save* and a *publish* are allowed to do, and that depends on whether a published baseline exists.

| State | How it is computed | What it means |
|---|---|---|
| **Draft** | The rig or look has no publish folder (`registry.latest_publish` is empty) | No one depends on it. Any change is allowed. |
| **Checked out** | A publish exists, and the working files are newer or different | Animation, other rigs and exports may depend on the baseline. |
| **Published** | A publish folder | Immutable (D29). Imported by animators, never rebuilt from source, so no fallback ever runs on it. |

"Checked out" here is not the source-control checkout. Personal work has no Perforce (D29), and the state must be computable from files alone, so it is *a baseline exists*. A Perforce checkout adds nothing to it.

**Builds are the same in all three.** The difference is two gates, both in mayadev:

1. **Publish gate (draft and checked out).** A publish is refused while any mesh in the report is not `loaded` (section 4), or any weight file is stale against the scene. This is where "strict" lives. It replaces the build-time error.
2. **Breaking-change check (checked out only).** Compare the working skeleton with the baseline's. Added joints are fine (D29: a variant may add, never remove). A removed joint, a changed markup identity or a changed parent is *breaking*, because animation, other variants and exports key on it. The editor shows the list when the skeleton is saved, and the publish needs it acknowledged. In a draft there is no baseline, so there is nothing to compare.

Rejected: a stricter *build* for checked-out assets. It would bring back the problem in section 1 at the moment a rigger most needs a scene, and a stricter build catches nothing the publish gate does not.

## 4. The fallback ladder

Per mesh, in order, in `setRigLook`. Each rung records its result.

| Rung | Condition | Result | Reported as |
|---|---|---|---|
| 0 | The mesh has a `meshId` and no file | Rigid to the topmost skin joint (D68, built) | `rigid` |
| 1 | Topology hash matches and every influence resolves | Load as saved. A different `skeletonHash` is not a failure. | `loaded` (and `staleSkeleton` when the hash differs) |
| 2 | Topology matches, some influences no longer resolve | Fold each missing influence's weight into its nearest resolvable skin ancestor, merge, renormalize | `folded` |
| 3 | Topology differs, the file holds its source surface | Barycentric transfer from the file's surface (section 5), after rung 2 on the table's columns | `transferred` |
| 4 | Topology differs, and the file has no surface (a file saved before this change) | Rigid to the topmost skin joint, warning "save once to enable transfer" | `rigid` |
| 5 | No influence in the file resolves, or a vertex cannot be transferred | That mesh, or that vertex, goes to the topmost skin joint | `rigid` |

Rung 1 is the big change. Adding a joint or moving a rest no longer touches the weights, so most skeleton edits stop being a problem. The hash stays in the file and in the report as a staleness stamp. The one thing it guarded, loading weights from another skeleton entirely, is handled by the influences failing to resolve. The residual risk is a wrong file whose selectors all resolve. The look folder belongs to one asset, so I judge it small. It is open question 3.

The report is one entry per mesh: `meshId`, `meshName`, result, reasons, `flaggedVertices`, `staleSkeleton`, `staleTopology`. `setRigLook` returns it as JSON next to the look root name. Today it returns only the look root name.

## 5. Barycentric transfer

For each vertex of the new mesh, find the closest point on the old surface, take the old triangle's three vertices and the point's barycentric coordinates `(u, v, w)`, and blend their weight rows: `row_new = u·row_a + v·row_b + w·row_c`. Then renormalize and prune to the project's max influences. This replaces nearest-vertex, which snaps to the old vertex pattern and shows it as a faceted weight map wherever the new topology is denser or differently flowed.

**Where the old surface comes from.** The weight file today holds weights and a topology hash, not positions. Options:

| Option | For | Against |
|---|---|---|
| **A. The weight file stores its bind-pose surface** (positions as floats, face counts and connects) | Self-contained: works on any machine, in a batch job, with no stamp lookups. Needs no baseline, so a draft works. | File size grows. Needs a file version step (D33). |
| B. Find the old model by stamp in local versions or a publish | No new data | Local versions are per machine (D29). A publish exists only after publishing. A draft on a teammate's machine has neither. Fails when the transfer is most needed. |

Recommendation: A. The weights file gains `surface` (version 3). A 100k vertex mesh adds roughly 2 to 3 MB of JSON next to weights that D35 measured at 0.75 s to load. Measure before settling (open question 1). It also gives rung 2 what it needs: the file stores each influence's nearest skin ancestor (`parents`), so a missing joint can fold without the old skeleton file.

**Split of the work** (the `core/` rule):

- **Maya layer** (`maya/shared/`): build an `MMeshIntersector` over the file's surface (mesh data object, no DAG node), query the closest point for each new vertex, and read back the triangle's vertex ids and barycentric coordinates. Output: per new vertex, three source ids and three weights.
- **`core/SkinWeightTable`**: given the old table and those correspondences, blend, renormalize and prune. No Maya, so a test needs no scene (D20 has no GoogleTest harness yet, so the tier is open).

**Known weak spots of closest-point, and the handling:**

- Thin or layered geometry (eyelids, lips, fingers, cloth over a body) picks the wrong layer. A vertex is *flagged* when the closest point's normal opposes the new vertex's normal, or when its distance exceeds a fraction of the mesh's bounding diagonal. A flagged vertex takes the average of its unflagged neighbours' rows, and a vertex with none goes to the topmost skin joint. The count is in the report.
- The author used UV-based transfer successfully for heads (`asset-pipeline.md` 5.1). It is better when the UV layout survived the change. Not in the first build. A later `uv` mode would choose itself when both surfaces carry the same UV set.

**Unverifiable from here:** the order and convention `MPointOnMesh::getBarycentricCoords` uses relative to `getPolygonTriangleVertices`. That needs a local spike in mayapy before the design is relied on, with a test that moves a vertex by a known barycentric offset.

## 6. What goes away, what is added

**Removed** (each is a name change, so each is flagged): `ignoreSkeletonHash` on `setRigLook`, `loadRigSkinWeights` (decision needed, section 8, question 4), `apply_look` and `build_rig`, and the Asset Editor's "Ignore skeleton changes" checkbox. mayadev and RigModules change together, in one commit each. **mayadev call sites of `ignoreSkeletonHash` / `ignore_skeleton_hash`** (31 mentions, found 2026-11-22; mayadev edits are proposed, not made):

| File | Mentions |
|---|---|
| `py/scripts/bmaya/maya/assettools.py` (`build_rig`, `_build_rig`, `apply_look`, the hash check, the interactive build) | 15 |
| `py/scripts/bmaya/maya/assetfiles.py` | 3 |
| `py/scripts/bmaya/maya_ui/asseteditor.py` (the checkbox) | 1 |
| `tests/asset/test_maya_asset_tools.py`, `test_maya_asset_editor.py`, `tests/core/test_maya_asset_editor_view.py` | 4, 3, 2 |
| `docs/tools.md`, `docs/rigmodules.md`, `docs/assets.md` | 1 each |

No saved asset file changes shape except the weight file's new version (a version step reads old files, D33). No impact on poses, animation or export.

**Added:**

- RigModules: `SkinWeights.surface` and `parents` (file version 3, with an update step), the transfer in `SkinWeightTable`, the fold, the per-mesh report, the changes in `LookSwap::prepare`.
- mayadev: the report in the Asset Editor (one row per mesh: result, reasons, a stale marker), a "Save weights as they are now" action that writes new files with the current hash and surface and so clears every stale mark, the publish gate, and the breaking-change check.

**Complexity added:** one file version, two arrays in the weight file, one `core/` function, one Maya-layer lookup and one report shape. The ladder's rungs are data in one table, not branches spread over the build.

## 7. Phases

1. **Build only, no publish step needed.** Rung 1 (hash is a status), rung 2 (fold), and the report. Remove the flag. This fixes most of the author's pain alone.
2. **Transfer.** The surface in the weight file, the version step, the Maya lookup, `SkinWeightTable`. Spike the barycentric convention first.
3. **Editor.** The per-mesh status, the resave action.
4. **Gates.** The publish gate and the breaking-change check. They need the publish step, which is not built: `publishes.create` exists in `bmaya.core.asset`, but nothing calls it yet.

Tests: a `core/` case for the blend and prune with hand-written weights (never recorded from the rig); a Maya-tier case per ladder rung with a hand-built mesh and skeleton; a docs example for the report. A change to the complex perf rig is not needed, since no module or constraint is added (D36 does not apply: build time only, but the transfer cost on 100k vertices should be measured once).

## 8. Open questions

1. **File size** of the stored surface. Measure on the largest mesh before committing to option A. A cheaper form (float32 as a base64 string) breaks the "JSON, not delimited formats" rule, so it would need your sign-off.
2. **Breaking-change rules.** Is a changed rest a breaking change, or only names, markup and parents? Animation keys on controls, not joints, but an engine export keys on joints.
3. **Wrong-asset guard.** Is a warning (the hash differs and the mesh also changed) enough, or should a weight file also carry the skeleton asset id?
4. **`loadRigSkinWeights` on its own.** It is an explicit single-mesh call. Keep it exact and failing (my recommendation), or give it the same ladder?
5. **Legacy files.** Files saved before version 3 have no surface, so a topology change can only go rigid until they are saved once. Acceptable, or should a one-time migration read the model from local versions?
