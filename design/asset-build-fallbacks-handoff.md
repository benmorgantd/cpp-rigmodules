# Handoff: asset builds that survive change (D70)

For the next session, which should be local (build and Maya available). Written 2026-11-22 at the end of a cloud design session. The design is `asset-build-fallbacks.md`, the decision is D70 in `decisions/assets.md`. Read those first. This page says where things stand and what to do next.

## State

- **Design only.** No C++, Python or test was changed. Nothing was built or run, so nothing here is verified in Maya.
- **Branch:** `ccr-856bf475-3ezpfd` in cpp-rigmodules, pushed, fast-forwarded to `main` (`b948e4f`) before the design commits. mayadev was not touched (its `main` was `7d0fdc0`).
- **D70 is Leaning.** The author gave directions (below) but has not signed off the whole design.

## What the author decided (2026-11-22)

1. Changed topology gets its weights by **barycentric transfer** (closest point on the old surface, the triangle's three weight rows blended), not nearest vertex.
2. The **"Ignore skeleton changes" checkbox is removed**, so the fallback is always on.
3. A removed joint's weight **walks up the skeleton, parent by parent, to the next skin joint** in the current rig. A gone or non-skin parent is passed through.
4. The weight file stores **the skeleton structure once, under one key** (`skeleton`), not a chain per influence.
5. The Asset Editor is the workflow hub (it will show the per-mesh result).
6. Draft versus checked out: both build the same. Strictness is at the publish gate and, for a checked-out asset, a breaking-change check against the last publish. "Checked out" means a publish exists (computed from files), not a source-control checkout. The author asked the question and did not object to this answer, but it is mine, not confirmed.

## Open questions (the author has not answered)

1. File size of the stored surface (measure on the largest mesh).
2. Is a changed rest a breaking change, or only names, markup and parents?
3. Wrong-asset guard: a warning, or a skeleton asset id in the file?
4. Should `loadRigSkinWeights` keep failing exactly (recommended), or get the same ladder?
5. Legacy files have no surface: a topology change goes rigid until saved once. Acceptable?

## Not read

I read the design docs, D54 and D68, the weight file's header (`SkinWeightFile.h`), `SkeletonFile.h`, and the commit messages of recent work. I did **not** read `LookSwap::prepare`, `SkinWeightLoad::prepare`, `RigLookCmd.cpp` beyond its return line (`setResult(fSwap.lookRootName())`), or mayadev's `apply_look` beyond its docstring and hash check. The work list below is therefore a plan, not a code-level estimate.

## Next steps

1. **Spike (mayapy, an hour).** Confirm the `MPointOnMesh::getBarycentricCoords` convention against `MFnMesh::getPolygonTriangleVertices`: which vertex each coordinate weights, and whether `triangleIndex` indexes the same triangulation. Test: a quad and an n-gon, a point at a known barycentric offset. Also build an `MMeshIntersector` from mesh data with no DAG node. Nothing else in the design depends on this more.
2. **Measure the surface.** Write positions and face data for the largest real mesh as JSON and report the size and the parse time next to the existing weights (D35 measured 0.75 s for 102k vertices).
3. **Phase 1** (needs no publish step and no surface):
   - The hash becomes a status: `SkinWeightLoad::prepare` and `LookSwap::prepare` load whenever topology matches and every influence resolves.
   - The fold (rung 2) over the file's `skeleton` key. This needs weight file version 3 with an update step (D33), so Phase 1 includes the version bump even before the surface exists.
   - The per-mesh report from `setRigLook` (today it returns only the look root name).
   - Remove `ignoreSkeletonHash`.
   - Tests: a `core/` case for the fold with hand-written weights, a Maya-tier case per rung with a hand-built skeleton and mesh, a docs example. The full suite after the build.
4. **Phase 2:** the surface in the file, `SkinWeightTable` blend and prune in `core/`, the Maya lookup, flagged-vertex handling (normal opposed, distance over a fraction of the bounding diagonal, then neighbour average).
5. Phases 3 and 4 (editor status and resave, the publish gate and breaking-change check) wait on the mayadev side, and the gates wait on a publish step that does not exist: `publishes.create` is defined in `bmaya.core.asset` but nothing calls it.

## mayadev call sites to change (proposed, not made)

31 mentions of `ignoreSkeletonHash` / `ignore_skeleton_hash`. `py/scripts/bmaya/maya/assettools.py` has 15 (`build_rig`, `_build_rig`, `apply_look`, the hash check, the interactive build), `assetfiles.py` 3, `maya_ui/asseteditor.py` 1 (the checkbox), tests 4 + 3 + 2 (`tests/asset/test_maya_asset_tools.py`, `test_maya_asset_editor.py`, `tests/core/test_maya_asset_editor_view.py`), and one each in `docs/tools.md`, `docs/rigmodules.md`, `docs/assets.md`. The RigModules and mayadev changes land together, one commit in each repo. mayadev needs the author's go-ahead (D24).

## RigModules docs touched by the code change (when it happens)

`docs/commands.md` (`setRigLook`, `loadRigSkinWeights` rows), `docs/looks.md` (the rigid-fallback paragraphs), `docs/skinning.md` (the skeleton check), `docs/source-map.md` (the `setRigLook` line), `docs/mayadev.md`, and `tests/README.md`. Search for `ignoreSkeletonHash` to find them all.

## Cautions

- The weight file's old versions must still load (D33 update step). Files saved before version 3 have no `skeleton` key, so folding is not available for them: they take the rigid rung.
- Removing a flag is a public name change. No node, attribute, control or socket changes, so no pose, animation or export impact.
- Source files are CRLF in the working tree and LF in the repo (`.gitattributes`). Use the Edit tool.
- Rigid is a quiet failure: a mesh bound to the root exports skinned to the root. The report and the editor status are what make it visible, so do not ship the fallback without the report.
