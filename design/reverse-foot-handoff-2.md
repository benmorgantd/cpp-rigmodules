# Handoff 2: ReverseFoot phases 5 and 6, the solomon bugs, the marker axis and the Markers asset row

**Status:** Handoff, written 2026-11-24 at commit `066cdd6` plus one uncommitted edit. It continues `reverse-foot-handoff.md` (read that first for the rules every phase follows: one commit per phase after the build and the full suite, the docs pass in the same commit, hand-worked pose expectations, CRLF through the Edit tool, no `sed -i`, no heredoc over about 7 KB, no python edit scripts, `bench` once in phase 6, mayadev changes proposed and not made). Delete this page and the first one when phase 6 is committed and the items below are done or dropped.

## What is done (branch `reversefoot`, from `fk-hierarchy`)

| Commit | Phase | Suite TOTAL |
|---|---|---|
| `9139339` | 0: `core/MarkerFile`, its hash, the core test harness (`tests/core`, built by `concepts/test_core_cpp`) | passed |
| `47521ae` | 1: the five marker commands, `createRigFromTemplate markers=`, `markersHash` and `markers` on the rig root, `MRK`, the stale-markers warning on pose and animation load | 1621 passed, 15 skipped |
| `3ef8be9` | 2: `core/FootSolve`, 30 hand-worked cases | 1621 passed, 15 skipped |
| `c3eac6b` | 3: `reverseFootModule` stage 1 (MTypeId 0x002194) | 1692 passed, 18 skipped |
| `0698f10` | review fixes on phase 3 | 1693 passed, 18 skipped |
| `066cdd6` | 4: toe and toe tip, the toe FK control, `ikFkDriver`, the blend | 1710 passed, 18 skipped |

The 18 skips are the 15 old ones plus the foot's three inverse tests, which wait for phase 5. Reviewers (convention, compute, docs, api-surface, limb-coupling) ran on phase 3. They did **not** run on `066cdd6` (a fork cannot spawn agents): run all five on `0698f10..HEAD` before or with the phase 5 commit.

## Session 3 (2026-11-24): what changed since this page was written

- The two fixes below are committed with their tests in `d626c26` (suite 1712 passed, 18 skipped), with the noisy "Socket or child plug did not exist for DAG reparent" warning removed.
- The solomon template with the corrections below **builds** (both feet). Probing it showed that the author's `reverseHierarchy` order (`heel, ball, bankIn, bankOut, toeTip, toe`) puts the toe under the ball, so a `footRoll` of 20 (below the break) tips the toe 20 degrees and drops the toe tip 2.9 below the ground. The order `heel, toeTip, bankOut, bankIn, toe, ball` keeps the toe tip within 0.5 of the ground through the roll and past the break. The docs example uses the first order. **Asked the author which they want** (the docs example and the fixture follow the answer).
- The axis marker shape (ask 1) is built: `Axis`, ShapeType 17, with per-point colours. Needs a GUI look.
- Ask 2: the Markers row design was proposed to the author; not signed yet.

## (Done, see above) Two fixes from the author's solomon asset

`cpp/maya/modules/ReverseFootModule.cpp` (built into the Release `.mll`, **not yet tested or in the suite**):

1. **A real bug.** The toe FK control's usage was `<toe usage>FK`, so a toe joint usage of `ball_00` gave `ball_00FK`, which is not camelCase, and the build failed validation. Now FK goes before a trailing `_NN` (`ball_00` gives `ballFK_00`, the control `ballFK_00_L_CTRL`; `toe` still gives `toeFK`). Add a test (a toe usage with a suffix) and a golden or pose fixture that uses one, and document it in `docs/modules.md` and D74.
2. **A clearer error.** `ikFkDriver` is a module name (`leg_00_L_MOD`). The author wrote `leg_00_L_MOD.ik`, so it now says it is a socket id. Add a validation case.

Run the suite on this build and commit it (a small commit before phase 5, or fold it into phase 5 with a note).

## The author's solomon asset: what the exceptions were

Asset: `C:\Users\bmorgan\Documents\Art\Maya\Projects\test\character\solomon` (`rigs\default\template.json`, `markers\markers.json`, `skeletons\default\skeleton.json`). A mayapy reproduction is in the first session's scratchpad (`repro_solomon.py`: loads the skeleton into `skel`, builds with `markers=`; copy the pattern, it needs a fresh `.mll` copy).

- The three validation errors were the toe FK name above (**our bug**), `ikFkDriver` as a socket id (**their template**), and the mirror copy failing for the same reason.
- **Their template is also wrong in two other ways, and the build fails again after those fixes:** the foot's `parentStructure` is `{"start": "leg_00_L_MOD.end"}` but the foot's plugs are `ik` and `ankle`, so it needs `{"ik": "leg_00_L_MOD.ik", "ankle": "leg_00_L_MOD.end"}`, and the leg needs `"ikTarget": "foot_00_L_MOD.ankle"` in its `parentStructure`. Rerun `repro_solomon.py fixed` (it applies exactly these edits) and see what is still wrong; I had not seen that output yet. Tell the author the corrected module JSON.
- **The markers file is accepted but warns:** its stored `markersHash` (`418d84e1...`) does not match the recomputed one (`a6832b78...`), and the markers are not in `(side, usage)` order (ankle, ball, toe, bankIn, bankOut, heel, toeTip). So something other than `saveRigMarkers` wrote it (hand edit, a script, or an earlier version). It is a warning only and the markers are used. Ask the author how they made it. Check the marker offsets make sense (they carry a rotation: the marker frames copy the joint's), and that a `bankIn`/`bankOut` pair sits on opposite lateral sides of the foot (the chosen bank pivot follows lateral position; for a left foot a positive bank must use the pivot on the character's +X side).
- It has extra markers the foot does not use (`ankle`): harmless. The foot needs `heel`, `ball`, `toeTip` and `toe` at least (design: `reverseHierarchy`).

## Asks from the author (new work, not in the original handoff)

1. **A real axis marker shape.** The editing locators use the `Pivot` shape (three double-sided arrows). The author wants a proper axis: three axes like a regular pivot, **red, green and blue for X, Y, Z**. This is the "axis visualizer" in `markers.md` section 6, which phase 1 left out. Work: a new `ShapeType` (add to the end: `AxisTriad` = 17, `RigControlNodeAttr::ShapeTypes::All`, the `static_assert`), generated by `tools/generate_control_shapes.py` (a thin cylinder and a cone per axis) into `core/ControlShapeLibrary.cpp`, and **per-part colour** in the draw override: `ShapeMesh` gets per-vertex colours (or per-part ranges), `RigControlDrawOverride::prepareForDraw` fills an `MColorArray`, `addUIDrawables` passes it to `MUIDrawManager::mesh(...)` (it takes a per-vertex colour array) when the shape has colours and the control is not selected (selected controls keep the selection colour). Then `MarkerScene::createMarkerNodes` uses it with `displaySize`. Update `docs/controls.md`, `docs/markers.md` (the "not built yet" paragraph), `design/markers.md` section 6, test_control_shapes, and the viewport gallery shapes. It needs a GUI look: say so.
2. **A Markers row in the mayadev Asset Editor**, like skeleton and model, because markers are another asset component to save, load and so on (the author prefers a row to a tab). This is **mayadev** (`C:\Users\bmorgan\Documents\dev\maya\mayadev`, package `bmaya`): per the rules the plan is proposed and the author's go-ahead is needed before editing mayadev. Read `design/mayadev-integration.md`, `docs/mayadev.md`, mayadev's `docs/` and the asset model (`bmaya.core.asset`: skeletons, rigs, looks, `rig.json`'s `skeleton` key; the file kinds and registry: `design/asset-model.md`) with the `mayadev-explorer` agent, then write a design: a `markers` kind in the asset (`markers/{name}/markers.json`), a `"markers": {...}` key in `rig.json`, `build_rig` passing `markers=`, the Asset Editor row (save from the scene's marker locators with `saveRigMarkers`, load with `loadRigMarkers`, a mirror button with `mirrorRigMarkers`, an Add marker for the selected joints with `rigMarkerMarkup`, the markers status like the skeleton's), and the library browser's "markers differ" status. Ask the author to sign the design, then build it (mayadev tests too). The solomon asset already has a `markers\markers.json` with no `rig.json` key, so migration is "none, the key is optional".

## Phase 5 (the inverse and the match)

Per `reverse-foot-handoff.md` phase 5 and `reverse-foot.md` section 10, `inverse-solve.md` sections 4, 5 and 8:

- The foot's `inverse` (today a stub returning the identity) solves `footRoll` (the ankle's total pitch), `bank` (minus its roll) and `toeTap` (the toe's pitch relative to the ankle) from the joints with `FootSolve::decomposeRotation`; the toe FK control comes from the joints exactly; the other channels are read as scalars from the sidecar or the rig (`inverseChannels`, D52). The handle takes yaw and position. Note (D74): with a roll between two pitches (bank between ball and toe tip, past the break) the decomposed pitch and roll are not the forward `footRoll` and `bank`; the handle absorbs the rest, so the ankle and toe still come back exactly.
- `RigInverse` pre-pass over modules that feed an `ikTarget`; `ModuleInverseContext::ikTargetOffset`; the Limb's inverse uses `handle = end * offset^-1`; `setRigIkFkBlend` runs the same inverse so a match from FK has no pop with a raised foot. Rigs with no foot must keep their order and results. Closes the open item in `limb.md` section 8 (say so in the commit and update the page, including the stale lines in 8.1 and 14).
- Done when: the round trip is 1e-4 on the ankle and toe, the FK to IK match with a raised foot has no pop, the pre-pass order is tested, the missing-joint and sidecar-precedence cases pass, `test_limb_matches_parts` gets the foot-shaped case (Limb alone, identity foot, foot at rest give the same joints), and the three skipped inverse tests run.

## Phase 6 (perf rig and one `bench`)

`foot_specs` in `tools/perf/perf_scenes.py` on the existing `limb_module_specs` limbs (markers are needed: a markers file for the perf rig), then `bench` **once** (D36). Report the fps and the ratio to the floor in the commit message and say so when below target (30 fps for 20 rigs, 1.5 times the floor on 50). Run `compute-reviewer` on the node. The compute review of phase 3 asked for the two outputs to be computed separately and for the pivot decode to be cached; the first was done in `0698f10`, the second was skipped (parallel evaluation does not call `setDependentsDirty`): measure first.

## Facts a new session needs

- Decisions made with the author this session: the pivot chain is the **literal hierarchy with the heel as root** (each rotation about a REST pivot and rest axis, the toe tip moving the ankle first; a foot past the break carries the toe tip below the ground; D74 has the worked numbers and the alternative); the foot's `ik` and `ankle` plugs have **no controls and take no constraint**; `controlName` is the **whole usage** (default `foot`, so the control is `foot_00_L_CTRL`); the pivot sockets for the `toe` and `toeTip` roles are named `toePivot` and `toeTipPivot` (the `toe` and `toeTip` sockets are the bind joints), recorded in D74, and the author has not yet confirmed those names.
- The channel names are public API: `footRoll`, `breakAngle` (rest 30), `heelRaise`, `heelSwivel`, `ballRaise`, `ballSwivel`, `ballRoll`, `bank`, `toeRaise`, `toeSwivel`, `toeTap`.
- Not verified anywhere: GUI Maya, the viewport gallery, whether the world-aligned foot control is easy to select next to the foot (ask the author to look), the colour axis (not built).
- mayadev proposals, none made: the Markers asset row (above), `build_rig` passing `markers=`, the library browser's "markers differ" status, the active rig panel showing the foot's channels.
- Environment: mayapy is `C:\Program Files\Autodesk\Maya2026\bin\mayapy.exe`; set `MAYA_DISABLE_CER=1`; copy the `.mll` before loading; the build is the MSBuild command in `CLAUDE.md`; the suite is `cmd /c tests\RigTests.cmd` (filters allowed, the full suite before a commit).

## When you report

Per phase: the commit, the TOTAL line, tiers that failed, the docs touched, the mayadev proposals, what was not verified, the complexity added. Stop and ask on any ambiguity about rig behaviour.
