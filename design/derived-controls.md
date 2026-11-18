# Derived controls: placement from the bind joints

**Status:** D57. Signed off by the author on 2026-11-17. Nothing built. It reverses part of D52 and part of D56, so the reversals are listed first. Build phases are in section 10.

## 1. Problem

Controls take their place from joints in the skeleton. `skeleton.json` of the `solomon` test asset holds 95 joints, and 35 of them are `skin: false`. Every one of the 35 has `export: true`, so all of them reach the engine as animation for nothing. They also share the hierarchy and the skeleton display with the bind joints, and each one changes the skeleton hash (D54: "every marked joint counts"), so adding one invalidates the skin weights.

The 35, by what they carry:

| Group | Joints | Information beyond the bind joints |
|---|---|---|
| Spine FK and IK | `spineFK_00-03`, `spineIK_00-03` (8) | None: even spacing from root to neck |
| Shoulder ribbon | `shoulderRibbon_00-02` ×2 (6) | None: even thirds of shoulder to elbow |
| Leg IK, pole, settings | `legIk_00`, `legPole_00`, `legSettings_00` ×2 (6) | A pole direction and distance, a settings position |
| Foot pivots | `heel_00`, `bankIn_00`, `bankOut_00` ×2 (6) | Yes: authored pivots |
| Tips | `head_01`, `index_03`, `mid_03`, `thumb_02` ×2, `toeTip_00` ×2 (9) | End joints, unchanged by this design |

A joint is something the rig drives. A control's placement joint is something the rig only reads, so it is the wrong thing to author as a joint when the bind joints already say where it goes.

## 2. What the author decided (2026-11-17)

1. **No new input type** (no locators). Rejected: a `markers` array in `skeleton.json`, with its own node type. The D54 hash decision stands.
2. **Derive control placement from the bind joints** wherever it carries no information of its own.
3. **Foot pivots stay marker joints** (`export: false`, `skin: false`). Nothing else does.
4. **Counts.** A ribbon has 3 controls: start, mid, end, spread evenly. A longer chain such as a tail can have more, up to the cap, which stays at 8.
5. **Orientation comes from the driving joints**, and a build fails with the joint named when it is wrong. In a mirrored arm or leg, -X points down the chain, and that is accepted.
6. **Names.** `IK` is appended to the floating controls, `Tweak` to the controls that sit on bind joints, `FK` to FK controls the module creates.

## 3. Reversals

| Was | Now | Where |
|---|---|---|
| D52 decision 1: `ik`, `pole`, `settings` are three marker joints per limb; a computed pole position was rejected | The IK handle, pole and settings are derived from the three limb joints | `two-bone-ik.md` |
| D56 / `ribbon.md` section 5: `controls` is a list of control joint selectors | `controlCount` replaces it | `ribbon.md` |

Everything else in D52 and D56 stands. The hash rule (D54) stands.

## 4. Ribbon

### 4.1 moduleArgs

| Key | Was | Now |
|---|---|---|
| `joints` | bind joint selectors | unchanged |
| `controls` | control joint selectors | **removed** |
| `controlCount` | | optional integer, default 3, 2 to the cap |
| `controlName` | | optional string, the base of the control usages (section 4.4) |

`moduleVersion` goes to 2, and its `argUpdates` step turns `controls` into `controlCount` (the array's length), so an old template still reads.

### 4.2 Placement

A smooth spline runs through the bind joints' rest positions (author, 2026-11-17: not a polyline). Proposed: a chordal Catmull-Rom spline with clamped ends, so it passes exactly through every joint. Control `k` of `c` sits at the share `k / (c - 1)` of the spline's arc length, from a dense sampled table. The table is built once in the read phase, so `compute()` pays nothing. The derivation lives in `core/` as pure math (positions and matrices in, matrices out) and the plan passes the result where it passed the claimed joints' rests.

`RibbonMath::buildSetup` then takes each bind joint's `u` from the polyline through the derived controls, as it does today. A mid control sits on the spline, not on a chord between joints, so on a curved tail the parameters shift by a small amount compared with a straight chain. That is expected.

Orientation: a control's rest basis is the rotation of the two bind joints on either side of its point, interpolated by the same share (a slerp). X comes from the joints' own X axes, so the sign follows them. It is not taken from the chord, because the author asked for orientation to come from the driving joints.

### 4.3 Validation (D17, read phase)

An error that names the bind joint and its selector when:

- a joint's X axis, dotted with the direction to the next joint (the last uses the direction from the previous), is not within 60 degrees of its line, so the axis doesn't run along the chain (author accepted 60 degrees);
- the sign of that dot differs from the first joint's. All joints must agree, so a chain is +X-down or -X-down, never mixed;
- `controlCount` is outside 2 to the cap.

A -X-down chain is accepted, so the ribbon needs a chain sign that flips the tangent. `RibbonMath` has no sign handling today (a search for one found none), and `ribbon-phase2-handoff.md` records that a joint whose X points away builds but gives a looped base curve. The author believes the built Ribbon already copes with a mirrored chain. Phase 0 (section 10) checks that with a probe on a -X-down chain before anything is built on it. The sign touches the base curve's tangent, the aim blend and the inverse.

The cap stays `RibbonMath::MaxControls`, 8 (author, 2026-11-17).

### 4.4 Names

The base is `controlName` when given, else the first bind joint's usage without its trailing `_NN`. `spine_00_M_JNT` as the first bind joint gives the base `spine`. A module whose first joint is a shoulder sets `controlName: "arm"` to get `arm`.

| Control | Markup usage | Example (side `Center`) |
|---|---|---|
| Floating ik controls | `<base>IK_<nn>` | `spineIK_00` |
| Tweakers on bind joints | `<base>Tweak_<nn>`, `nn` the bind joint's index | `spineTweak_00` |
| FK controls (`fkStyle`, not built) | `<base>FK_<nn>` | `spineFK_00` |

I changed one thing from the earlier draft: the tweaker name is `<base>Tweak_<nn>` and not the bind joint's usage plus `Tweak`, so the three layers read the same way. The control usages must stay unique per rig (D25), and the plan declares them in `ModuleInterface::controls` as today. The default base is the joint's markup usage, which can differ from its Maya name (a joint named `spine_00_M_JNT` may have the usage `spineTwist_00`), so a template sets `controlName` when the usage isn't what it wants.

## 5. TwoBoneIk

`ik`, `pole` and `settings` leave `moduleArgs`. The three FK controls still come from `joints`. Derived from the three joints' rest worlds:

- **IK handle:** at the end joint, with the end joint's orientation (D52 already says it is normally equal to it).
- **Pole:** the mid joint plus `poleOffset`, an optional world-space vector (author, 2026-11-17: the direction isn't consistent between limbs). Without it, the pole is pushed from the mid joint along the bend direction, the component of (mid - root) perpendicular to the root-to-end line, by the limb's length. **A straight limb is not an error** (author, 2026-11-17: mocap skeletons have straight limbs). When the bend is under 0.1 percent of the limb's length and there is no `poleOffset`, the pole goes straight out in the -Y direction of the root joint's frame, which is the front of a knee and the back of an elbow. The author said "-Z orientation". The root joint's -Y is what gives that result on the `solomon` skeleton (the left hip's Y is world -Z, so -Y is front; the left shoulder's Y is world +Z, so -Y is back), so I read it as -Y. **To confirm.**
- **Settings:** the end joint plus `settingsOffset`, an optional world-space vector. Default: a quarter of the limb's length along world +Y (author, 2026-11-17), so it doesn't sit on the IK handle.

Both vectors are world-space for the Left or Center module and mirror by negating X for the other side, in `planFromJson` through `ctx.moduleData.isMirror()` (CLAUDE.md, mirroring).

**moduleArgs:** `joints` (three selectors), `controlName` (optional, the base of the three names, default the first joint's usage), `poleOffset` and `settingsOffset` (optional `[x, y, z]`). **Names:** `<base>IK`, `<base>Pole`, `<base>Settings` (`armIK`, `armPole`, `armSettings`; `legIK`, `legPole`, `legSettings`). The three FK controls keep the names of their joints.

The solve, the match outputs and the inverse already treat these as three controls with a rest, so they don't change. The inverse ignores the marker joints today, which keeps it correct.

## 6. Foot pivots

`heel_00`, `bankIn_00` and `bankOut_00` stay as they are: marker joints per D52's convention. I found no built module that reads them (reverse foot is "not this module" in D52). They stay in the skeleton until a foot module claims them, and then that module's design decides the rest.

## 7. Impact

- **Skeleton and hash.** 20 of the 35 joints can be deleted from a skeleton: the spine's 8, the ribbon's 6 and the legs' 6. 15 remain: the 6 foot pivots and the 9 tips. The skeleton hash changes once, and weight files and looks need a re-stamp. The influence set doesn't change, because none of the 20 skin anything.
- **Control names.** Every moved control changes its markup usage, so saved poses and animation that name them stop matching. The author's standing position (no rigs need to survive) means a note, not a migration.
- **Rest orientation.** A derived control has the orientation of the joints, not the old marker joint's (the spine FK markers have swapped axes). Control rest matrices change, and so do their channel values.
- **Public API.** `controls` leaves Ribbon's `moduleArgs`, and `ik`, `pole` and `settings` leave TwoBoneIk's. Both modules bump `moduleVersion`.
- **Evaluation.** Fewer joints are wired into modules. Not measured, and `bench` is not required (no new module or constraint).

## 8. Tests and docs

Tests (write expectations by hand, per `tests/README.md`):

- Core: placement at even shares on a straight, a bent and a curved chain; orientation slerp; the validation errors (axis off the line, mixed signs, count out of range); a -X-down chain; the straight-limb fallback (the pole goes out along the root joint's -Y, for a left and a mirrored right limb).
- `ModuleContract` and golden snapshots for Ribbon and TwoBoneIk regenerate.
- Pose cases for TwoBoneIk with a derived pole, hand-worked.
- `argUpdates` steps: an old template reads and gives the new args.
- `perf_scenes.py` and `check_perf_scene.py` drop their control joints.
- `test_docs_examples.py` covers the new examples.

Docs: `docs/modules.md` (both module sections), `docs/templates.md`, `docs/skeletons.md`, `docs/controls.md` (control name suffixes), `docs/architecture.md` if a file is added, `design/ribbon.md`, `design/two-bone-ik.md`, `README.md` if it lists the arguments.

## 9. Open questions

Answered by the author on 2026-11-17: the name base is an optional `controlName` (Ribbon and TwoBoneIk), the TwoBoneIk names are `<base>IK`, `<base>Pole` and `<base>Settings`, the offsets are optional world-space vector args, the placement is a smooth spline, the cap stays 8, and the tolerances are accepted.

Also answered (2026-11-17): the settings default is a quarter of the limb's length, and a straight limb is not an error. The last three are closed (2026-11-18):

1. **The straight-limb direction.** The root joint's -Y, where the author said "-Z" (section 5). Confirmed by the author in phase 3.
2. **The straight-limb tolerance**, 0.1 percent of the limb's length. Built in phase 3 and accepted with it.
3. **The -X chain check.** Answered by phase 0: the probe failed and `chainSign` fixed it.

## 10. Build phases

One commit per phase. Each ends with the build and the full suite, and the commit message follows the CLAUDE.md shape. Nothing in a phase leaves the suite red.

| Phase | Work | Verified by |
|---|---|---|
| 0 | **Done 2026-11-18: the probe failed (3 of 5 cases), `chainSign` fixed it** (`ribbon.md` section 14). **Probe the -X-down chain.** Build a Ribbon on a mirrored chain (a Right arm or tail whose joints' -X runs down the chain) and check the base curve, the aim blend and the inverse against hand-worked numbers. If it loops, add the chain sign to `RibbonMath` (tangent, aim, inverse) and its tests here. Tests only when it already works. | A `test_ribbon.py` case with a -X fixture |
| 1 | **Done 2026-11-18 (code, not yet committed).** `core/VectorMath.h` (the helpers `RibbonMath` had file-local, now shared), `core/ControlPlacement`. A throwaway probe checked 24 hand-worked cases and all passed; the suite cannot reach `core/` on its own (D20), so those cases become suite tests through the phase 2 and 3 modules. **`core/` placement, no Maya types.** The spline through the bind joints, the arc-length table, even shares, the orientation slerp, the axis-and-sign validation, the pole derivation with the straight fallback, the settings default. In `core/` with a bare-name include set. | Hand-worked cases in the suite's core tier (straight, bent, curved chain; -X chain; mixed signs; straight limb) |
| 2 | **Done 2026-11-18.** **Ribbon.** `controlCount` and `controlName`, the `controls` key removed with a `moduleVersion` 2 step, names (`<base>IK_<nn>`, `<base>Tweak_<nn>`), derived rests where the claimed control joints were, the read-phase errors. The Ribbon fixtures lose their control joints. Goldens and pose cases regenerate by hand. | `ModuleContract`, pose cases, validation fixtures, update-table test, `test_docs_examples.py` |
| 3 | **Done 2026-11-18.** **TwoBoneIk.** `controlName`, `poleOffset` and `settingsOffset`, `ik`, `pole` and `settings` removed with a `moduleVersion` step, names (`<base>IK`, `<base>Pole`, `<base>Settings`), mirror negation of the offsets. The solve, the match outputs and the inverse are unchanged and the existing pose cases must pass unchanged. | The same set, with the straight-limb case |
| 4 | **Done 2026-11-18.** **Perf scenes.** `tools/perf/perf_scenes.py` and `check_perf_scene.py` drop their control joints. One `bench` run on the 50-rig scene to check nothing got slower. It isn't part of the suite, and no target applies (no new module). | `bench` numbers in the commit message |
| 5 | **Done 2026-11-18 (docs verified, mayadev call sites reported, nothing in mayadev edited).** **Docs, repo map and callers.** `docs/` pages (section 8), `CLAUDE.md`'s repo map and module descriptions, a search of mayadev (`py/scripts`, `toolsets`, `tests`) for the removed keys and the old usages, and a list of the call sites it breaks. Not an edit of mayadev. | The docs pass checklist |

Phases 2 and 3 are independent of each other once phase 1 is in. They are sequential only because both touch `docs/modules.md` and `tests/run_tests.py`.

**Not in the phases, and the author's to do:** the `solomon` asset's own files (its template and `skeleton.json`), which need the 20 joints removed and the `moduleArgs` rewritten, and a weight re-stamp. I can write a one-off script that lists the joints and rewrites the template, if you want it, but I would not edit the asset in place.
