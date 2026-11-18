# Derived controls, phase 2 (the Ribbon): handoff

**Status:** written 2026-11-18, after commit `6b4d421` (D57 phases 0 and 1). Nothing of phase 2 is built. Delete this page when phase 2 is committed.

## 1. Where things stand

- **Design:** `derived-controls.md` (signed off 2026-11-17) and D57. Read its sections 2, 4, 8, 9 and 10 first. Then `ribbon.md` (section 14, "As built") and `ControlPlacement.h`.
- **Done and committed (`6b4d421`):**
  - Phase 0: `RibbonMath::Setup::chainSign`, stored on the node as `chainSign`. A chain whose joints' X points back up it (a mirrored limb) now solves. The `negx_*` pose cases, `RibbonNegXContract` and the mirror-symmetry test cover it.
  - Phase 1: `core/VectorMath.h` and `core/ControlPlacement` (`checkChain`, `describeChainProblem`, `placeAlongChain`, `placeLimb`). **No module calls them yet.** The suite can't reach `core/` on its own (D20), so a throwaway probe was the only check: 24 hand-worked cases, all passed. Phase 2 turns the Ribbon-related ones into suite tests (section 5).
- **Suite on that commit:** TOTAL 1207 passed, 8 skipped.
- **`bench` on that commit** (the existing scenes, 2026-11-18): `complex_x20` 21.8 fps (45.9 ms), `complex_x50` 144.8 ms against `floor_x50` 84.0 ms (1.72 times). That is the standing miss D56 recorded (20 to 21 fps, 1.6 to 1.7 times). A phase 2 change can only be judged against these.
- **Not mine, don't commit:** `tests/README.md` and `tests/run_tests.py` carry the author's uncommitted `--durations` change.

## 2. What phase 2 builds

The Ribbon takes its ik controls from the bind joints, not from control joints.

1. **moduleArgs.** Remove `controls`. Add `controlCount` (integer, default 3, 2 to `RibbonMath::MaxControls`, which stays 8) and `controlName` (optional string). `joints` is unchanged.
2. **`moduleVersion` 2.** `ModuleRegistry.cpp` passes `{}` for the Ribbon's `argUpdates` (the fifth field of its descriptor). Replace it with an `UpdateTable` with one step (`FormatUpdate.h`: `steps[0]` takes version 1 to 2). The step turns a `controls` array into `controlCount` (its length) and erases `controls`. It is tolerant: data it doesn't recognise is left for validation. Hand-check that a version 1 template still builds (a test, section 5).
3. **`planFromJson`** (`RibbonModule.cpp`, `RibbonModuleNode::planFromJson`):
   - Claim only the bind joints (`claimJoint`).
   - Run `ControlPlacement::checkChain` on the bind joints' rest matrices. Turn a problem into a read-phase error with `describeChainProblem`, passing the joints' selectors as names (`formatJointSelector` from `core/JointMarkup.h`). The error path is `moduleArgs.joints`.
   - `ControlPlacement::placeAlongChain(bindRests, controlCount)` gives the ik controls' rest matrices. They take the place of the claimed control joints' `mRestWorld`, so `RibbonMath::buildSetup` is called as it is today. It recomputes `chainSign` from them, which agrees with `checkChain`'s sign because the frames keep the joints' axes.
   - A `controlCount` outside 2 to the cap is an error naming `moduleArgs.controlCount`. Keep the existing `joints` count check.
   - `buildSetup`'s failure reason currently reports on `.controls`. It has no control joints to blame now, so report on `.joints`.
4. **Control usages.** The base is `controlName` when given, else the first bind joint's usage with a trailing `_NN` removed (the whole usage when it has none).
   - ik control `j`: `<base>IK_<nn>` (two digits, from 00).
   - Tweaker `i`: `<base>Tweak_<nn>`, `i` the bind joint's index.
   - These go in `ModuleInterface::controls` (ik controls first, then tweakers, as now) and are passed to `createRigControl` as `makeControlMarkup(side, usage)`. The control usages must stay unique per rig (D25). Decide, and test, what a collision between two modules' bases does.
5. **`createModule`.** This is the largest change, and the part to read twice.
   - `RibbonSlotInput` holds `oJoint`, `mRestWorld` and `controlMarkup`. An ik slot has no joint now: its `oJoint` is null and its `mRestWorld` is the derived frame.
   - Today every slot calls `wireJoint(oModule, joint, slot, rest)`, which hands the joint to the module, stores its rest in `jointRestWorld[slot]` and connects `jointOPM[slot]`. **Skip it for the ik slots.** Check that `compute`, `jointOPM`, `jointRestWorld` and `RigInverse` tolerate holes in those arrays, since the ik slots are 0 to j-1 and the bind slots follow. The inverse's `hasJoint(slot)` is already optional.
   - `createRigControl` builds the control's node name from a base name by removing `_jnt` and adding `_ctrl` and the side suffix. For an ik control with no joint, pass `<usage>` as the base (`spineIK_00` gives `spineIK_00_ctrl_c`). The tweakers still use their bind joint's name.
   - Sockets (`index_0` ...), the `start` and `end` child plugs, the channels on the first control, `ikControlCount`, `knotU`, `bindU` and the rest offsets are unchanged. Their names are public API.
6. **The inverse** (`RibbonModuleNode::inverse`). Step 1 solves the two end controls from their joints, and those joints no longer exist. The derived end frames equal the first and last bind joints' frames, so take the ends' locals from the first and last bind joints (`ModuleInverse::localFromWorld`, the same call). The interior ik controls have no joint: they keep their fallback (sidecar or rest) and the tweakers carry every bind joint's difference, as step 3 already does for a missing control joint. **This is a design choice the author hasn't confirmed:** say so in the summary. Rejected so far: solving interior controls from the nearest bind joint, which the old design did only when a control joint was present.
7. **Mirroring.** `expandMirrors` swaps every `[side, usage]` selector in `moduleArgs`, so `joints` mirror as before. `controlName` is a plain string and isn't swapped. No mirror branch in `planFromJson` (D28).

## 3. Tests and fixtures to change

Write pose expectations by hand (`tests/README.md`). Most existing numbers stay valid: the 3-control fixtures already had their controls at 0, 4, 8 over bind joints 0, 2, 4, 6, 8, which is where `placeAlongChain` puts them.

- **Templates** in `tests/maya/fixtures/templates/`: `ribbon_basic`, `ribbon_five`, `ribbon_aimed`, `ribbon_arm`, `ribbon_arm_swing`, `ribbon_driven_ends`, `ribbon_mirror`, `ribbon_negx`. Replace `controls` with `controlCount` (or leave the default) and a `controlName` where the old usage was meaningful. Check each against its skeleton: `ribbon_five` needs 5 controls, and `ribbon_arm` has two ribbons whose controls were named `upCtrl*` and `foCtrl*`.
- **Skeletons** in `tests/maya/fixtures/skeletons/`: `ribbon_basic`, `ribbon_five`, `ribbon_arm`, `ribbon_arm_tilted`, `ribbon_mirror`, `ribbon_negx`. Remove the `*Ctrl*_jnt` control joints.
- **Pose cases** (`tests/maya/fixtures/poses/ribbon/*.json`, 27 files including the `negx_*` ones): every `set` key and `expect` entry that names a control (`tailCtrlB_ctrl_c`) or a control joint (`tailCtrlB_jnt`) needs the new name or deletion. The control joints won't exist, so their expectations go. Keep the bind joint expectations.
- **`test_ribbon.py`:** the `FIRST`, `INTERIOR`, `LAST` and `TWEAKERS` constants, `JOINT_OWNERS` (the control joints leave it), the "marker joints follow their controls" test (delete it) and every test that names a control.
- **`tests/maya/golden/ribbon_basic.json`:** regenerate with `RigTests.cmd --update-golden golden` and review the diff. It must show the new usages and nothing else.
- **`test_docs_examples.py`** (`RibbonDocExamples`, the `## Ribbon` entry and the shoulder snippet) follows the docs change below.
- **New tests, from the phase 1 probe:**
  - A curved bind chain (a quarter circle of radius 10 through 5 joints, 3 controls): the middle ik control sits at (7.0711, 7.0711, 0) to about 0.05.
  - Uneven bind spacing (joints at 0, 1, 5, 8, 3 controls): the middle control is at x = 4.
  - Orientation: bind joints at x = 0, 4, 8 turned 0, 0 and 90 about X, 5 controls: the control at x = 6 is turned 45 about X.
  - Validation, each with the joint named in the message: a joint's X turned 65 degrees off the chain (an error) and 55 degrees (builds), a joint turned 180 about Y (the direction disagrees), two joints on each other, `controlCount` of 1 and 9, a misspelled key.
  - `controlName`: an override gives `armIK_00`, the default strips `_NN`, a base with no `_NN` is kept whole.
  - The update step: a version 1 template with `controls` reads and gives `controlCount`.
  - The -X chain (`ribbon_negx`) with derived controls, and the inverse contract on it.
- **Contract:** `RibbonContract`, `RibbonAimedContract` and `RibbonNegXContract` (`ModuleContract` checks the interface, naming, channels and the inverse round trip).

## 4. Docs to change (the docs pass, CLAUDE.md)

`docs/modules.md` (the `## Ribbon` section: the args table, the marker-joint paragraph, the controls row, the examples), `docs/templates.md`, `docs/architecture.md` (the Ribbon description), the `RibbonModule` row of the repo map in `CLAUDE.md`, `design/ribbon.md` (section 5's `moduleArgs` line, with a reversal note and a pointer to D57), `design/derived-controls.md` (mark phase 2 done), `tests/README.md` (the Ribbon test list, but the author has uncommitted edits in that file, so commit only your lines or ask) and D57's entry. Then search every doc for `controls` as a Ribbon key and for the old control usages (`tailCtrlA`, `spineCtrlA`, `upCtrlA`).

## 5. Callers

- **mayadev:** a search of `py/scripts`, `toolsets` and `tests` for `Ribbon` found no call to the module's arguments. `bmaya/maya/assettools.py:860` creates joints "for twist and ribbon joints and a ribbon's control joints": it keeps working, and its control joint use goes away. Nothing breaks. Propose no mayadev edit.
- **`tools/perf/perf_scenes.py` and `check_perf_scene.py`** generate Ribbon templates with `controls`. The update step keeps them building, and the unclaimed control joints in the generated skeleton are harmless. Cleaning them up is phase 4. The suite doesn't run them, but run `check` once (`mayapy tools\perf\perf_scenes.py check`) to be sure they still build.
- **`RigTemplates/` and `README.md`** hold no Ribbon template, but confirm with a search.

## 6. What cost time before, and how to avoid it

- **The author's Maya locks the `.mll`.** A build fails at link with `LNK1104` while their Maya has the plugin loaded. Ask them to unload it or close Maya, and never kill it. A redirected `OutDir` is rejected (stale `.mll`).
- **Line endings.** Source files are CRLF. Edit with the Edit tool, or write the file and run `unix2dos`. A Python script that edits a file must read and write bytes and use `\r\n`. New fixtures written with the Write tool are LF and git warns, which is harmless.
- **The probe approach for `core/`:** `cl` from `vcvars64.bat`, compiling `core/ControlPlacement.cpp` with a `main` that holds hand-worked cases, gave a result in seconds. Phase 1's probe was in the scratchpad and is gone: rewrite the cases from section 3 if a `core/` change needs one.
- **A bind joint's end twist** in `RibbonMath` has two assignments that overwrite the loop (`detail.twist[0]` and `[count - 1]`). Anything that changes a twist must change both. It cost a build in phase 0.
- **Before a commit:** the full suite, then `bench` on `complex_x20`, `complex_x50` and `floor_x50` (CLAUDE.md, working style). Compare with the numbers in section 1.
- **Commit message** shape and the docs list: CLAUDE.md, "Working style". Do not add the author's `--durations` files.

## 7. Done when

- The Ribbon builds from `joints`, `controlCount` and `controlName`, with no control joints in any fixture, and `controls` still reads through the update step.
- The full suite passes on the build that holds the change, with the section 3 tests in it, and the golden diff is reviewed.
- The docs in section 4 are updated and a search finds no stale Ribbon `controls` example.
- The inverse decision in section 2 (6) is stated to the author.
- `bench` is run and reported, and the commit message lists what was verified and what was not (viewport drawing, a real asset).

## 8. Open points for the author

1. The inverse's interior controls (section 2, item 6).
2. The collision rule for two Ribbons whose base name is the same (section 2, item 4).
3. Whether the Solomon asset's template should be rewritten by a one-off script (D57's "not in the phases" note), after phases 2 and 3.
