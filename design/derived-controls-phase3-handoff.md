# Derived controls, phase 3 (TwoBoneIk): handoff

**Status:** written 2026-11-18, after commit `815910f` (D57 phase 2, the Ribbon). Nothing of phase 3 is built. Delete this page when phase 3 is committed.

## 1. Where things stand

- **Design:** `derived-controls.md` (signed off 2026-11-17), sections 2, 3, 5, 7 to 10, and D57. Then `design/two-bone-ik.md` (D52) for what stays, and `core/ControlPlacement.h`.
- **Done and committed:** phase 0 (`chainSign`), phase 1 (`core/VectorMath.h`, `core/ControlPlacement`: `checkChain`, `describeChainProblem`, `placeAlongChain`, `placeLimb`) and phase 2 (the Ribbon: `controlCount`, `controlName`, `moduleVersion` 2, no control joints, the inverse change). `placeLimb` has no caller yet: phase 3 is its first.
- **Suite on `815910f`:** TOTAL 1220 passed, 8 skipped. `bench` was **not** run for phase 2 (author's decision, 2026-11-18), so the last numbers are the phase 1 ones: `complex_x20` 21.8 fps, `complex_x50` 1.72 times the floor. Phase 3 changes no evaluation, so only the commit rule applies (CLAUDE.md: run `bench` before a commit unless the author says not to).
- **The Ribbon's pattern to copy:** `RibbonModule.cpp`. `planFromJson` claims the real joints, runs the check, derives the frames, names the controls and passes them on; `createModule` takes a slot with a null `oJoint` and skips `wireJoint` for it; `argUpdateTable()` is the module's `UpdateTable`, which `ModuleRegistry.cpp` passes as the descriptor's `argUpdates` (the fifth field, `{}` for TwoBoneIk today).
- **Another session works in this tree** (it stashed uncommitted work once, on 2026-11-18). Run `git status` and `git stash list` before you start, and commit only your own files.

## 2. What phase 3 builds

TwoBoneIk derives its IK handle, pole and settings control from the three limb joints. `ik`, `pole` and `settings` leave `moduleArgs`.

1. **moduleArgs.** `joints` (three selectors) stays. Remove `ik`, `pole`, `settings`. Add optional `controlName` (string), `poleOffset` and `settingsOffset` (optional `[x, y, z]`, world space). `TwoBoneIkArgs` is in `TwoBoneIkModule.h`: `std::optional<std::array<double, 3>>` or a plain array with a flag, whatever `nlohmann` reads cleanly with the `_WITH_DEFAULT` macro, and `readModuleArgs` gets `{ "joints" }` as the known keys and the other three as optional.
2. **`moduleVersion` 2** (`argUpdateTable()`, as the Ribbon's). The step erases `ik`, `pole` and `settings` when present. It cannot turn the marker joints into offsets (it has no scene), so an old template builds with the derived defaults, and its poles and settings controls move. Say so in the docs and the commit message. The update is tolerant: leave anything it does not recognise for validation.
3. **`planFromJson`** (`TwoBoneIkNode::planFromJson`, about line 950 of `TwoBoneIkModule.cpp`):
   - Claim only the three limb joints.
   - Keep the existing degenerate checks that still apply: bones with length (`kMinBoneLength`). Drop the `ik` offset check (`kMaxIkEndOffset`) and the pole-on-the-line check: the handle is at the end by construction, and a straight limb is **not** an error (the author, 2026-11-17: mocap skeletons have straight limbs).
   - `ControlPlacement::placeLimb(root, mid, end, poleOffset, settingsOffset)` gives the pole and settings **positions** (`LimbPlacement`). The handle sits at the end joint.
   - Build the three marker slots' rest matrices. Orientation is yours to decide and the design says: handle = the end joint's orientation. For the pole and settings, use the root joint's orientation too, or the end's. **Decide, state it, and test it**, because the pole's and settings controls' rotation is locked, so it only affects how their translate channels read. The existing fixtures' marker joints are unrotated (world axes) unless noted, so world axes may be the least surprising for those two. Check what `computeRest` and the pole's channels assume before choosing.
   - **Mirroring (D28):** `poleOffset` and `settingsOffset` are world vectors for the Left or Center module and negate X for the other side. Do it through `ctx.moduleData.isMirror()` in `planFromJson`, not in `createModule`. Check how `expandMirrors` treats `moduleArgs` first: it swaps `[side, usage]` selectors, so the offsets arrive unswapped.
   - Straight-limb fallback and its tolerance are in `placeLimb` (`StraightLimbTolerance`, 0.1 percent): the pole goes out along the root joint's **-Y**. The author wrote "-Z" and the design reads it as -Y (derived-controls.md section 5, "To confirm"). Ask the author to confirm before the commit, and say what you assumed.
4. **Names.** The base is `controlName`, else the first limb joint's usage without a trailing `_NN` (`StringUtils::stripNumberSuffix`; `upperArm` stays `upperArm`, so an arm wants `controlName: "arm"`). Usages: `<base>IK`, `<base>Pole`, `<base>Settings`. The three FK controls keep their joints' markup. `StringUtils::numberedName` is for numbered names only: these three have no number.
5. **`createModule`.** Today every one of the six slots goes through `wireJoint` and `createRigControl(..., joint name, ...)`. For slots `Ik`, `Pole` and `Settings` (`TwoBoneIkSlot`), pass no joint: no `wireJoint`, and the control's node name from the usage (`armPole_ctrl_l`), as the Ribbon's ik controls do (`RibbonSlotInput::baseName`). Check that `compute`, `jointOPM` (written for all six slots today at line ~509), `jointRestWorld`, `RigInverse::readJoints` and the match outputs (`computeMatch`, `fkMatchLocal`, `ikMatchLocal`) tolerate the holes. `writeOutputMatrix` skips a missing element, and `readJoints` walks the physical elements, so they should. Confirm, do not assume.
6. **The inverse.** The design says it already ignores the marker joints, so it should not change. Verify it with the contract: the IK handle and the pole are solved from the limb joints (`MathUtils::computeTwoBoneIkInverse`), not from markers.
7. **Sockets, child plugs, channels, `ikFkBlend` and the rest.** Unchanged, names are public API: `index_0`..`index_2`, `start`, `end`, `ik`, `pole`, `settings`, `roll`, `pin`, `space`, `stretch`, `soft`.

## 3. Tests and fixtures to change

Write pose expectations by hand (`tests/README.md`). **The existing pose cases must pass unchanged** (design section 10). The way to keep them: give each fixture template the offsets that reproduce its old marker joints.

- A derived pole is `mid + poleOffset` and the settings control is `end + settingsOffset`. Take each old marker joint's world position, subtract the mid or end joint's, and put the result in the template as `poleOffset` or `settingsOffset`. For `twoboneik_basic`: mid (1.8, 2.4, 0), old pole (2.5, 5, 0), so `poleOffset` is (0.7, 2.6, 0); end (5, 0, 0), old settings (0, 8, 0), so `settingsOffset` is (-5, 8, 0). The handle was already at the end.
- Mind the orientation: a marker joint with a rotation (`twoboneik_hand`, the tilted `ribbon_arm_tilted`, and any fixture with `"rotate"` on a marker) gave its control that rotation, and the derived control takes the choice in section 2 (3). The poses that read a handle's or pole's rotation or its local channels may need their expected numbers rechecked by hand. Do not record them from the rig.
- **Templates** in `tests/maya/fixtures/templates/`: `twoboneik_basic`, `twoboneik_hand`, `twoboneik_mirror`, `ribbon_arm`, `ribbon_arm_swing`. Also `tests/maya/fixtures/validation/twoboneik_four_joints.json`.
- **Skeletons** in `tests/maya/fixtures/skeletons/`: `twoboneik_basic`, `twoboneik_hand`, `twoboneik_ik_off_end`, `twoboneik_mirror`, `twoboneik_offplane`, `twoboneik_pole_on_line`, `twoboneik_zero_bone`, `ribbon_arm`, `ribbon_arm_tilted`. Remove the `handIk`, `armPole` and `armSettings` style joints. `twoboneik_ik_off_end` and `twoboneik_pole_on_line` test the two checks you are dropping: replace them with the straight-limb case (a new fixture), and delete or repurpose their tests.
- **Pose cases** (`fixtures/poses/twoboneik/*.json`, 31 files): every key naming an old control (`handIk_ctrl_l`, `armPole_ctrl_l`, `armSettings_ctrl_l`) needs the new node name, `armIK_ctrl_l`, `armPole_ctrl_l` (unchanged when `controlName` is `arm` and the old usage was `armPole`), `armSettings_ctrl_l`. Choose `controlName` values that keep most of the old node names, as phase 2 did with `up` and `fo`. Remove any expectation on a marker joint.
- **Python tests** naming the old controls: `tests/maya/modules/test_two_bone_ik.py` (11 mentions, and its `joints_cannot_give` parses names), `commands/test_two_bone_ik_cmds.py`, `commands/test_two_bone_ik_inverse.py`, `commands/test_constraint_types.py`, `concepts/test_docs_examples.py` (7), `modules/test_ribbon.py` (2, through the `ribbon_arm` fixtures).
- **`tests/maya/golden/twoboneik_basic.json`:** regenerate with `RigTests.cmd --update-golden golden` and review the diff. It must show the new usages, the dropped marker joints and nothing else.
- **New tests:**
  - Placement: the handle sits on the end joint; the default pole of a bent limb is `mid + bend * limb length`; the default settings control is a quarter of the limb's length above the end (+Y).
  - `poleOffset` and `settingsOffset` move them, and on a mirrored (Right) module the X is negated.
  - Straight limb: no error, and the pole goes out along the root joint's -Y, for a Left and a mirrored Right limb (the Right root's Y is mirrored, so check the sign by hand).
  - A limb of three joints on a line with a bone of no length is still an error.
  - `controlName`: the override, the default from the first joint's usage, `armIK`, `armPole`, `armSettings`.
  - The update step: a version 1 template with `ik`, `pole` and `settings` reads, builds, and the controls are at the derived places.
  - The inverse contract and `setRigIkFkBlend` on the new build (`TwoBoneIkContract` is in `test_two_bone_ik.py`).
  - `jointOPM` has no elements at the three marker slots, as `RibbonDerivedControls` checks for the Ribbon.

## 4. Docs to change (the docs pass, CLAUDE.md)

`docs/modules.md` (the `## TwoBoneIk` section: the args table, the marker-joint paragraph, the controls and sockets rows, the examples and the restore note that says the marker joints are not read), `docs/templates.md`, `docs/commands.md` (it names `handIk` style controls in three places), `docs/architecture.md` (the `TwoBoneIkNode` row says "six joints, three marker"), `docs/source-map.md` (the `TwoBoneIkModule` description, and the `ControlPlacement` row, which says `placeLimb` is unused), `design/two-bone-ik.md` (a reversal note on decision 1 and a pointer to D57; the page is in `design/`, with an archive beside it, so look first), `design/derived-controls.md` (mark phase 3 done) and D57's entry. Then search every doc for `"ik":`, `"pole":`, `"settings":` as TwoBoneIk keys and for the old control names (`handIk`, `armPole`, `armSettings`, `footIk`, `legPole`). Also check `README.md` and `docs/python.md` (`find_ik_fk_limb`, which finds the settings control by its `ikFkBlend` channel, not by name, so it should not change).

## 5. Callers

- **mayadev:** the repo has a `mayadev-callsites` agent for this. Search `py/scripts`, `toolsets` and `tests` for the `TwoBoneIk` arguments and for the old control usages (`handIk`, `armPole`, `armSettings`, `legIk`, `legPole`, `legSettings`). Phase 2's search found no caller of the Ribbon's arguments. Propose no mayadev edit. List what it breaks in the summary.
- **`tools/perf/perf_scenes.py` and `check_perf_scene.py`** generate 12 TwoBoneIk limbs with their marker joints and `ik`, `pole`, `settings` keys. The update step keeps them building (the unclaimed joints are harmless) and phase 4 cleans them up. Run `mayapy tools\perf\perf_scenes.py check` once.
- **`RigTemplates/`** (`Human.json` and `LayoutRig.json`): grep for `TwoBoneIk`. None found in the Ribbon's search, but confirm.
- **The `solomon` asset** is the author's, not yours (D57, "not in the phases").

## 6. What cost time before, and how to avoid it

- **The author's Maya locks the `.mll`.** A build fails at link with `LNK1104` while their Maya has the plugin loaded. Ask them to unload it, never kill it. A redirected `OutDir` is rejected.
- **Line endings.** Source files are CRLF. A Python script that edits a file must read and write bytes and use `\r\n`. `sed -i` keeps CRLF on a one-line substitution but is easy to misuse.
- **The hidden pole rule.** `createRigControl` builds a control's node name from a joint name by removing `_jnt` and adding `_ctrl` and the side suffix. For a control with no joint, pass the usage, as the Ribbon does.
- **A restore cannot give back a control with no joint.** Phase 2 had to rework the Ribbon contract for it (`_RibbonRestore` in `test_ribbon.py`, and `ModuleContract.controls_lost_with_joint`). TwoBoneIk's inverse already computes the handle and pole from the limb, so this should be easier, but read `TwoBoneIkContract.joints_cannot_give` first.
- **`bench`.** Run it before the commit unless the author says not to (CLAUDE.md). Do not run it per change.
- **Commit message** shape and the docs list: CLAUDE.md, "Working style". Do not add files other sessions changed.

## 7. Done when

- TwoBoneIk builds from `joints`, `controlName`, `poleOffset` and `settingsOffset`, with no marker joints in any fixture, and `ik`, `pole` and `settings` still read through the update step.
- The existing pose cases pass with their numbers unchanged (apart from names and any rotation case you recheck by hand), and the new tests are in.
- The golden diff is reviewed, the full suite passes on the build that holds the change, and `perf_scenes.py check` builds.
- The docs in section 4 are updated and a search finds no stale TwoBoneIk key or control name.
- The pole's orientation choice, the straight-limb direction (-Y or -Z) and the update step's loss of old marker positions are stated to the author.

## 8. Open points for the author

1. The straight-limb direction: the root joint's -Y, where the author said "-Z" (derived-controls.md section 5 and 9).
2. The straight-limb tolerance (0.1 percent of the limb's length) is the assistant's number.
3. The orientation of the derived pole and settings controls (section 2, item 3).
4. Whether the update step should instead refuse an old template that has `ik`, `pole` or `settings` joints that the derived places would move far from, rather than build it silently.
