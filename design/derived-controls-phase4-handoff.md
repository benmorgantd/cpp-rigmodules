# Derived controls, phase 4 (perf scenes): handoff

**Status:** written 2026-11-18, after commit `f230915` (D57 phase 3, TwoBoneIk). Nothing of phase 4 is built. Delete this page when phase 4 is committed. Phase 5 (below) is mostly done already.

## 1. Where things stand

- **Done and committed:** phases 0 to 3 (the -X chain, `core/ControlPlacement`, the Ribbon, TwoBoneIk). Design: `derived-controls.md` (sections 8 to 10) and D57 in `decisions.md`, which records what phase 3 decided.
- **Suite on `f230915`:** TOTAL 1234 passed, 8 skipped. `perf_scenes.py check` passes (11 tests). A GUI Maya run drew the derived TwoBoneIk controls correctly (rest, IK posed, straight limb, both sides).
- **`bench`** has not been run since phase 1 (author's decision, 2026-11-18: not per change). Last numbers: `complex_x20` 21.8 fps, `complex_x50` 1.72 times the floor. D36's target is 30 fps for 20 rigs and 1.5 times the floor on 50, so both are already below target. Phase 4 is the one place D57 asks for a `bench` run, so run it once at the end and report the numbers.
- **Another session may work in this tree** (a stash named "D57 phase 2 in progress" exists and is not yours). Run `git status` and `git stash list` first, and commit only your own files.

## 2. What phase 4 builds

`tools/perf/perf_scenes.py` and `tools/perf/check_perf_scene.py` still write the old shape: control joints for the Ribbons and marker joints for the TwoBoneIk limbs. The update steps drop the old keys, so the scene builds, but the generator is out of date and carries joints the rig no longer reads. Make it write the D57 shape.

1. **TwoBoneIk limbs** (`limb_specs()`, `joint_layout()` about line 270, `build_template()` about line 363).
   - Stop writing the `ik`, `pole` and `settings` marker joints (about line 273) and the `ik`, `pole`, `settings` keys of `moduleArgs` (about line 374).
   - Write `"moduleVersion": 2` and `"controlName": <limb base>`. The limb joints' usages end in a digit (`limbA1`), so without `controlName` the base would be `limbA1` and the usages `limbA1IK`.
   - The spec's `ik`, `pole`, `settings` offsets become `poleOffset` and `settingsOffset` in the template: the pole is the mid joint plus `poleOffset`, the settings control the end joint plus `settingsOffset`, both world vectors for the Left module (the other side negates X, which `_flip` already does for the joints). The `ik` offset is always (0, 0, 0): the handle sits at the end joint.
   - The pole spec is a world vector from the mid joint, so it can be used as `poleOffset` directly. The settings spec is relative to the **root** joint today (`anchor` is `root`), so the new `settingsOffset` is `root + spec - end`.
2. **Ribbons** (`ribbon_specs()`, `joint_layout()` about line 288, `build_template()` about line 383).
   - Stop writing the `<base>Ctl` control joints. Write `"moduleVersion": 2`, `"controlCount": <the spec's controls>` and `"controlName"`, and drop the `controls` key.
   - Keep `controlCount` equal to the spec's `controls` so the control count (348) does not change.
   - The names change: the ik controls are `<base>IK_<nn>` and the tweakers `<base>Tweak_<nn>` (`nn` is the bind joint's index).
3. **The floor scene** (`build_floor_instance()`, about line 725) builds one native control per **joint**, so it loses the controls that no longer have a joint: the 12 limbs' IK, pole and settings, and the Ribbons' ik controls. It must create those native controls without a joint, so the floor keeps the same control count and the 1.5 times ratio still compares like with like. Give each a position (the generator already computes them), and keep the limb channels (`ikFkBlend`, `stretch`, `soft`, `roll`, `pin`, `space`) on the right controls so `key_rom` keys the same curves. `LIMB_ROLES` lookups already use the `IK` suffix after the phase 3 fix.
4. **`check_perf_scene.py`:** the counts change.
   - Joints: 354 today. Remove 36 limb markers and the Ribbons' 3 control joints on each of 6 arm ribbons and 4 on the tail (`36 + 2*3*3 + 4` in `test_limbs_are_twelve_with_six_joints_each`), so 354 - 58 = 296. Check the arithmetic against `joint_layout()` rather than trusting this number.
   - `test_limbs_are_twelve_with_six_joints_each` counts marker joints. It becomes "no marker joints on limbs or ribbons", or the test is dropped.
   - Controls stay 348, constraints 81, modules 40 (8 Center chains, 19 Left chains, 3 Aim modules, 6 limbs, 1 Center and 3 Left ribbons).
   - Aim modules keep their `target` and `up` marker joints (D57: marker joints stay for Aim and the foot pivots).
5. **Saved scenes:** `tools/perf/out/` is not tracked. The old `complex_template.json` and `.mb` files there are stale after this change. Regenerate them, never edit them.
6. **`bench`, once.** Build `complex --count 50` and `floor --count 50`, run `bench` on both in one session (`perf_scenes.py` header, about line 20), and report fps for 20 rigs and the ratio on 50. A `bench` takes about 3 minutes. Say so when it is below D36's target (it already was). The change removes joints and wires, so a small gain is expected, not guaranteed.

## 3. Tests

- `mayapy tools\perf\perf_scenes.py check` must pass and its counts reflect the new layout.
- The suite does not run the perf tool, so also run `cmd /c tests\RigTests.cmd` once on the final build and cite its TOTAL line. No C++ should change in this phase, so the build need not change either. If the `.mll` is the one phase 3 built, say so instead of rebuilding.
- Pose expectations and counts come from the generator's arithmetic, written by hand, never recorded from a scene.

## 4. Docs (phase 4's own)

`tests/README.md` (the perf section, about line 205 to 215, if it names the joint counts), the header comment of `perf_scenes.py`, and `docs/source-map.md`'s `tools/perf/` row (it says "12 TwoBoneIk limbs, 7 Ribbons ..." and may mention the generated rig's joint count). Mark phase 4 done in `derived-controls.md` section 10 and add a "Built, phase 4" line to D57. The `decisions.md` D57 status line should then say phases 0 to 4. Delete this page and its row in `docs/source-map.md`.

## 5. Phase 5 (docs, repo map, callers): what is left

Most of it was done with phases 2 and 3. Left:

- Re-read `CLAUDE.md`'s repo map and module descriptions against the code and fix anything stale. Phase 3 changed only the "TwoBoneIk is the reference for..." sentence.
- **mayadev.** Phase 3's call-site search found breakage in three test files, not in shipped code: `tests/asset/test_maya_rigpanel.py` (`LIMB_TEMPLATE`, `LIMB_JOINTS`, the `self.left["handIk"]` lookups), `tests/gui/rigpanel/run.py` (`LIMB_TEMPLATE`) and `tests/gui/rigpanel/check.py` (the marker joints, `usage="handIk"`). The fix is `"controlName": "arm"` in the template, the three marker joints dropped, and `handIk` renamed `armIK`. **Do not edit mayadev**: propose the edits to the author. The agent did not read `tests/README.md` or `docs/tools.md` in mayadev, where TwoBoneIk is mentioned in long lines.
- **The `solomon` asset** is the author's, not yours (D57, "not in the phases"): its template needs the 20 joints removed and its `moduleArgs` rewritten, and a weight re-stamp. A one-off script that lists the joints and rewrites the template can be offered, but the asset is not edited in place.
- **`RigTemplates/`** (`Human.json`, `LayoutRig.json`): grep for `TwoBoneIk` and `Ribbon`. The Ribbon's search found none, and phase 3 did not re-check.

## 6. What cost time before, and how to avoid it

- **The author's Maya locks the `.mll`.** A build fails at link with `LNK1104` while their Maya has the plugin loaded. Ask them to unload it, never kill it. No redirected `OutDir`.
- **Line endings.** Source files are CRLF. A Python script that edits a file must read and write bytes, and convert `\n` to `\r\n` for text it inserts. A pattern copied from a CRLF file that already ends in `\r\n` doubles the CR.
- **Heredocs.** The Bash tool failed on a heredoc that held an apostrophe in a Python string. Write an edit script to a file with the Write tool, then run it.
- **Moving the floor scene's counts.** The ratio is only meaningful if the floor and the rig have the same controls, joints, hierarchy and keys.

## 7. Done when

- The generator writes no limb marker joints and no ribbon control joints, and uses `moduleVersion` 2, `controlName`, `controlCount`, `poleOffset` and `settingsOffset`. Aim's marker joints are untouched.
- `perf_scenes.py check` passes with the new counts, the floor scene has the same control count as the rig, and the suite's TOTAL line is cited.
- One `bench` on the 50-rig scene is reported (fps for 20 rigs, the ratio on 50), with a note on whether D36's target is met.
- The docs in section 4 are updated, and this page is deleted.
