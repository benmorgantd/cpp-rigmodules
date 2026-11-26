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
- The solomon template with the corrections below **builds** (both feet). Probing it showed that the author's `reverseHierarchy` order (`heel, ball, bankIn, bankOut, toeTip, toe`) puts the toe under the ball, so a `footRoll` of 20 (below the break) tips the toe 20 degrees and drops the toe tip 2.9 below the ground. The order `heel, toeTip, bankOut, bankIn, toe, ball` keeps the toe tip within 0.5 of the ground through the roll and past the break. **Resolved:** the author dropped `reverseHierarchy` altogether. The foot finds its six markers by role on its side and uses the fixed order `heel, toeTip, bankOut, bankIn, toe, ball` (D74 bullet of 2026-11-24). Solomon builds with the key deleted.
- Also this session: the marker-alignment warning (derived frame kept, author), and mayadev `build_rig` passing `markers=` (uncommitted in mayadev at the time of writing).
- **For phase 5:** in the fixed order the ball turns the ankle before the bank, so a ball roll with a bank does not decompose exactly into `footRoll` and `bank` in the roll-first order (a heel roll with a bank does). Choose the inverse's gauge with that in mind (D74).
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
- It has extra markers the foot does not use (`ankle`): harmless. (Superseded in session 3: `reverseHierarchy` is gone and all six pivot markers are required.)

## Asks from the author (new work, not in the original handoff)

1. **A real axis marker shape** (done in session 3: the `Axis` shape, `e44e2cf`). The editing locators used the `Pivot` shape (three double-sided arrows). The author wants a proper axis: three axes like a regular pivot, **red, green and blue for X, Y, Z**. This is the "axis visualizer" in `markers.md` section 6, which phase 1 left out. Work: a new `ShapeType` (add to the end: `AxisTriad` = 17, `RigControlNodeAttr::ShapeTypes::All`, the `static_assert`), generated by `tools/generate_control_shapes.py` (a thin cylinder and a cone per axis) into `core/ControlShapeLibrary.cpp`, and **per-part colour** in the draw override: `ShapeMesh` gets per-vertex colours (or per-part ranges), `RigControlDrawOverride::prepareForDraw` fills an `MColorArray`, `addUIDrawables` passes it to `MUIDrawManager::mesh(...)` (it takes a per-vertex colour array) when the shape has colours and the control is not selected (selected controls keep the selection colour). Then `MarkerScene::createMarkerNodes` uses it with `displaySize`. Update `docs/controls.md`, `docs/markers.md` (the "not built yet" paragraph), `design/markers.md` section 6, test_control_shapes, and the viewport gallery shapes. It needs a GUI look: say so.
2. **A Markers row in the mayadev Asset Editor** (done in session 3, mayadev `75260ac`, one file per asset with no `rig.json` key; the row's right-click menu has Add Foot Markers, Mirror Selected Markers and Select Markers), like skeleton and model, because markers are another asset component to save, load and so on (the author prefers a row to a tab). This is **mayadev** (`C:\Users\bmorgan\Documents\dev\maya\mayadev`, package `bmaya`): per the rules the plan is proposed and the author's go-ahead is needed before editing mayadev. Read `design/mayadev-integration.md`, `docs/mayadev.md`, mayadev's `docs/` and the asset model (`bmaya.core.asset`: skeletons, rigs, looks, `rig.json`'s `skeleton` key; the file kinds and registry: `design/asset-model.md`) with the `mayadev-explorer` agent, then write a design: a `markers` kind in the asset (`markers/{name}/markers.json`), a `"markers": {...}` key in `rig.json`, `build_rig` passing `markers=`, the Asset Editor row (save from the scene's marker locators with `saveRigMarkers`, load with `loadRigMarkers`, a mirror button with `mirrorRigMarkers`, an Add marker for the selected joints with `rigMarkerMarkup`, the markers status like the skeleton's), and the library browser's "markers differ" status. Ask the author to sign the design, then build it (mayadev tests too). The solomon asset already has a `markers\markers.json` with no `rig.json` key, so migration is "none, the key is optional".

## Phase 5 (the inverse and the match)

Per `reverse-foot-handoff.md` phase 5 and `reverse-foot.md` section 10, `inverse-solve.md` sections 4, 5 and 8:

- The foot's `inverse` (today a stub returning the identity) solves `footRoll` (the ankle's total pitch), `bank` (minus its roll) and `toeTap` (the toe's pitch relative to the ankle) from the joints with `FootSolve::decomposeRotation`; the toe FK control comes from the joints exactly; the other channels are read as scalars from the sidecar or the rig (`inverseChannels`, D52). The handle takes yaw and position. Note (D74): with a roll between two pitches (bank between ball and toe tip, past the break) the decomposed pitch and roll are not the forward `footRoll` and `bank`; the handle absorbs the rest, so the ankle and toe still come back exactly.
- `RigInverse` pre-pass over modules that feed an `ikTarget`; `ModuleInverseContext::ikTargetOffset`; the Limb's inverse uses `handle = end * offset^-1`; `setRigIkFkBlend` runs the same inverse so a match from FK has no pop with a raised foot. Rigs with no foot must keep their order and results. Closes the open item in `limb.md` section 8 (say so in the commit and update the page, including the stale lines in 8.1 and 14).
- Done when: the round trip is 1e-4 on the ankle and toe, the FK to IK match with a raised foot has no pop, the pre-pass order is tested, the missing-joint and sidecar-precedence cases pass, `test_limb_matches_parts` gets the foot-shaped case (Limb alone, identity foot, foot at rest give the same joints), and the three skipped inverse tests run.

### Phase 5 status (session 3, 2026-11-25): written, not committed, one failure left

Uncommitted in the worktree `.claude/worktrees/reversefoot-review` (branch `reversefoot-review`, on `172683d`). It builds.

- **Signed off by the author:** the gauge is "subtract the sidecar". The API additions are `ModuleInverseContext::moduleRest`, `ModuleInverseInput::ikTargetJointWorld` and `ikTargetMotion`, `ModuleInverseResult::channels` and `ikTargetMotion`, `ModuleDescriptor::readInverseRest`, and a pre-pass in `RigInverse::solveFrame`. The pre-pass runs the foot's own inverse once, before the sweep, for `F`. A Limb's `ikTarget` source is found from the `ikTargetMatrix` connection.
- **Written:**
  - `FootSolve::solveAnkleChannels`: `footRoll` and `bank` take the pitch and roll the given channels miss, added to the given values, so a sidecar that already makes the turn comes back unchanged.
  - `FootSolve::solveToeTap`.
  - `decomposeRotation` divides out a uniform scale.
  - `ReverseFootModuleNode::inverse`, `inverseChannels` and `readInverseRest`.
  - The two-bone inverse places the handle at `ikRest * F^-1 * ikRest^-1 * reach`, and its forward check reaches the target.
  - The Limb forwards `ikTargetMotion`, and its match passes the target's motion to `TwoBoneIkPart::computeMatch`, with the match outputs affected by the ikTarget inputs.
  - Core tests in `FootSolveTests.cpp` pass: the gauge with and without a sidecar, given channels kept, uniform scale, and the toe tap under the carrying pivots.
  - `ReverseFootInverse` in `test_reverse_foot.py`:
    - the no-sidecar restore and the match with a raised foot pass;
    - the sidecar gauge test was rewritten and not rerun. Its first restore failed before the rewrite: the toe tip joint came back about 0.003 off. That needs a look, because a sidecar restore of that pose should be exact.
- **The one open failure: the contract round trip.** The three contract inverse tests are unskipped, and they fail.
  - With every control posed (`_pose_every_transform`), the Limb's two-bone half comes back wrong: the IK handle, the upper arm and the hand. It does so even with the foot's channels at rest.
  - A bisection probe posed one group of controls at a time. Every group restores exactly on its own: the handle (translate and rotate), soft, stretch, the pole, the FK controls, the ribbon, the tweakers, the settings, the toe FK, and one Layout control's scale or rotation with the handle.
  - Every pose **minus the three Layout controls** restores exactly too, apart from bind joints 2 to 8. **The Layout controls posed together with the rest** break it.
  - The plugin from `172683d` (no foot inverse) restores the two-bone half exactly in the same full pose, so phase 5 causes it. Suspects: the pre-pass input built with identity parent deltas (`makeInput(..., false)`), or the hand joint read for `F` while the Layout carries a rotation and a scale that the foot's rest pivots don't share.
- **Already broken before phase 5:** in this rig's full-pose round trip, ribbon bind joints 2 to 8 come back off even with the old plugin. That's a Limb or ribbon problem in the `reverse_foot` template (it has a `dummy` joint and toes under the hand). The contract test fails on it once the two-bone half is fixed.
- **Not done:**
  - the IK to FK match of the toe FK control: `setRigIkFkBlend` writes only the Limb, so switching to FK pops the toe when `toeTap` or the roll moved it;
  - `test_limb_matches_parts`'s foot-shaped case;
  - the docs: D74's gauge and API entry, `reverse-foot.md` section 10 (the toe is exact only when the channels can make the pose: a raise folded into `footRoll` past the break turns the toe tip pivot instead), `inverse-solve.md`, `limb.md` section 8 (close the match item), `source-map.md` (`ModuleInverse`, `RigInverse`, `FootSolve`), and `docs/commands.md` on `poseRigFromJointPose` and `setRigIkFkBlend` with a foot.

### Phase 5 status (session 4, 2026-11-25): done

**Outcome (end of session 4):** phase 5 is built and committed on `reversefoot-review`; see the commit and D74's phase 5 entry. The three things the notes below were chasing:

- **Ribbon bind joints 2 to 8 were a test rule, not a rig bug.** `LimbContract` does not pose the ribbon's ik controls (no joint gives them back, and a posed one's scale with `volumePreserve` shears the bind joints beyond what a tweaker holds: the restore warns "the source joint has shear"). `ReverseFootContract` posed them. The rule is now `LimbRoundTripRules` in `test_limb.py`, shared by both contracts. The inverse's anchors were checked against the source's limb joints and were exact.
- **The toe in the full pose:** the Layout's share of the gauge leak is fixed by reading the pitch and roll in the handle's space (the author chose it, 2026-11-25; `ModuleInverseInput::ikHandleParentDelta`, the pre-pass moved into the sweep just before each Limb). The handle's own turn still goes into `footRoll` and `bank` by design, so the contract moves the foot's handle without turning it.
- **Not built, proposed to the author:** matching the toe FK control on IK to FK in `setRigIkFkBlend`.

The notes from the session before the outcome follow, for the record.

- **Fixed, the two-bone half:** the handle has no scale channel, so in the scene it carries its space's (the Layout's) scale. The inverse wrote an orthonormal handle, and with a foot `F` turns about pivots away from the handle, so the scale the scene put back moved the reach. `TwoBoneIkNode::inverse` now gives the reach the scale of `eval.mIkDelta` (row 0 length, a uniform scale), so the handle's local is rigid and the scene's handle is the one placed. Without a foot only the reach's position is read, so nothing changes there. Built, not yet suite-run.
- **After the fix**, the suite with filter `reverse_foot`: 93 passed, 3 failed. The three failures are the contract inverse tests, now on ribbon bind joints 2 to 8 (the problem that predates phase 5), plus the toe and toe tip about 0.0016 off. `ReverseFootInverse` passes all three, including the sidecar gauge test (so task 2's 0.003 toe tip is gone after the rewrite; not yet explained why it was off before).
- **Probe:** a copy of `probe_full.py` is in the session notes folder (`side-session-notes/local_f3f79a64-.../probe_full.py`). It builds the contract rig, poses controls by mode (`all`, `only:<prefixes>`, `skip:<prefixes>`, `set:ctrl.attr=v,...`, plus env `EXTRA="ctrl.attr=v,..."` applied after posing, `limb` for the `limb_arm` contract), restores with `poseRigFromJointPose` and lists the joints off by more than 1e-4. Run it with mayapy and the copied .mll as its first argument.
- **Bisection of bind 2 to 8** (Layout, foot, toe FK and dummy left out, still fails; the `limb_arm` contract restores all 12 joints, so the ikTarget is what differs):
  - posing only `armPole`, `armSettings` and `armRibbonIK` together fails; any two of them restore;
  - from that set, resetting the pole's `pin` to 0 or `volumePreserve` to 0 fixes it; resetting stretch, soft, the sine or the twists does not; `bendSoftness` 0 leaves 6 off;
  - a hand-made pose (pole translate plus pin, ribbon IK translate, rotate and scale 1.25, stretch, soft, bendSoftness, volumePreserve) did **not** reproduce it, so the trigger also needs the contract's exact values (+0.25 on every channel, the ribbon IK controls' rotations of 0.25 degrees about all three axes);
  - next step: from the `only:` failing set, reset channels one at a time to find the minimal set, then compare the Limb inverse's anchors (`ik.socketWorld`, from `TwoBoneIkNode::inverse`'s `fillSockets`) with the scene's limb joints when pin and volumePreserve are on and the target is connected. Hypothesis: with pin (the pole's pin moves the mid) and the ikTarget, the inverse's internal forward evaluation differs from `LimbNode::compute`'s in something the ribbon's volume preservation reads (the stretched lengths), though the three limb joints themselves come back exactly.
- **Toe and toe tip 0.0016 off in the full pose:** most likely the gauge. The ankle motion read for footRoll and bank includes the handle's and the Layout's rotation, so their pitch and roll leak into footRoll and bank, and toeTap (a pitch only) can't then make the toe's leftover yaw and roll. Worth putting to the author: decompose the ankle's motion in the handle's parent space (the Limb's ik control space), so a rotated Layout or handle doesn't change the restored footRoll. That needs the pre-pass to run after the Limb's parents (just before the Limb in the sweep) instead of before the sweep.
- **Still to do:** tasks 3 to 5 of the brief (the foot-shaped `test_limb_matches_parts` case, the toe FK match proposal, the docs pass, the build, the full suite and the commit).

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
