# Handoff: build markers (D73) and ReverseFoot (D74)

**Status:** Handoff, written 2026-11-24 at commit `a86fd47`. Both designs are signed off and nothing is built. Delete this page when phase 7's decision is made (the phases below are committed or dropped), as `design/README.md` says of handoffs.

You are building two things in order: **markers** (a new file kind, phases 0 and 1) and the **`ReverseFoot` module** that uses them (phases 2 to 6). Phase 7 is a separate gate and is not part of this job.

## Read first, in this order

1. `CLAUDE.md` (loaded for you) and `.claude/rules/shell.md`. Source files are CRLF: use the Edit tool, never `sed -i`, never a heredoc over about 7 KB.
2. `design/markers.md` and `design/reverse-foot.md`, whole. They are the spec. Each build phase table (`reverse-foot.md` section 16) is the order of work.
3. `design/decisions.md` Index, then only the entries D73 (`decisions/data.md`), D74 and D62 (`decisions/modules.md`), D57, D54, D41 and D52.
4. `design/limb.md` sections 8 and 8.1 (`ikTarget`, `blindSockets`, why the sockets are separate arrays) and `design/inverse-solve.md` sections 4, 5 and 8.
5. For the code to copy: `FkHierarchy` and `Limb` (`cpp/maya/modules/`), `core/SkeletonFile` and its hash (the marker hash reuses its FNV code), `maya/commands/JointMarkupCmd` (the command shape, D14), `maya/shared/RigInverse`, `maya/shared/TwoBoneIkSolve.h`.

Use the agents in `.claude/agents/`: `module-explorer` for Limb and TwoBoneIk, `decisions-lookup` for the decisions, `maya-runner` for every build and suite run, `limb-coupling-check`, `compute-reviewer`, `convention-reviewer`, `docs-auditor` and `api-surface-diff` before each phase's commit.

## Rules that apply to every phase

- **One commit per phase,** after the build and the full suite pass on it. The message follows CLAUDE.md (grouped bullets, the decision id, public API and its impact on poses, animation and exports, the suite TOTAL line, what was not verified).
- **Docs pass in the same commit** (CLAUDE.md steps 1 to 6). The pages to update per phase are in `reverse-foot.md` section 17 and `markers.md` section 11. A worked example gets a test in `tests/maya/concepts/test_docs_examples.py`.
- **Tests with the code.** Pose expectations are written by hand, never recorded from the rig. Give every new test file a tier in `TIERS` in `tests/run_tests.py`. A throwaway probe becomes a suite test or is listed in the summary as a proposed test.
- **Do not rerun the suite** when it passed on the current build and nothing has changed. **Do not run `bench`** except once, in phase 6.
- **mayadev:** propose edits to the author, never make them. All changes here are additive, so no call site breaks, but search `py/scripts`, `toolsets` and `tests` before touching a command that already exists (`createRigFromTemplate`, `getRigInfo`, `loadRigPose`, `loadRigAnimation`, `setRigIkFkBlend`).
- **New files:** add to `cpp/RigModules.vcxproj` and `.filters` and `docs/source-map.md`. `core/` has no Maya headers and includes by bare name. Check `MTypeId`s in CLAUDE.md before picking the foot's (the next free one after `0x002193`).
- **Names are public API.** Use exactly the names in the design pages. A change to one is a decision to flag, not a quiet edit.
- **Commands:** long flag names only, `maya.cmds` examples, no import aliases in Python.
- Say "Needs local build and Maya verification" on anything not built and run, and report the TOTAL line plus any tier that failed or crashed.

## Phase 0: `core/MarkerFile` and its hash

- A Maya-free file read and write for `markers.json` (format version 1), the hash (rounding step 0.0001, order independent, reusing the FNV code that `core/SkeletonFile` uses: extract a shared function rather than copy), every parse error, a version step table (D33).
- **Done when:** a core test module in the layout tier covers parse errors, the hash (rounding, order independence, a one-line change changes it, the stored hash is recomputed and a mismatch is a warning), and a version step. No public change.

## Phase 1: markers in Maya

- The commands `rigMarkerMarkup`, `saveRigMarkers`, `loadRigMarkers`, `mirrorRigMarkers` and `getRigMarkers` (D14 shape, `JointMarkupCmd` the reference), `MRK` in `core/NamingConvention` and `cmds.renameRigNodes`, `createRigFromTemplate markers=`, marker resolution in the read phase (joint rest world times `offsetLocal`, every error at once, named), the `markersHash` stamp on the rig root, in `getRigInfo` and in `LibraryMeta` (additive: an old file reads "unknown"), and the stale status on `loadRigPose` and `loadRigAnimation` (a status, never an error, D70).
- The **axis visualizer** (cylinder and cone per axis, red, green, blue, with a display size) in the generated control shape library, drawn on the editing locators `loadRigMarkers` makes. Export-marker nodes are phase 7.
- `mirrorRigMarkers` reflects the world position across X = 0 and writes the other side's offset in that side's joint frame. It is an authoring command and the build never mirrors markers.
- **Done when:** commands, resolution (missing joint, missing marker, no file passed), the build with and without the flag, the stamps and the stale status are tested. `docs/markers.md` (new), `commands.md`, `skeletons.md`, `naming.md`, `templates.md`, the source map and the README list are updated. The skeleton hash is unchanged by markers.
- **Proposed to the author, not made:** the mayadev Markers tab, the `markers` asset kind, `build_rig` passing the flag.

## Phase 2: `core/FootSolve`

- Maya-free: the foot frame from marker positions (forward = heel to toe tip projected on the ground, up = world +Y), the peel split (`heel = max(-footRoll,0)`, `ball = clamp(footRoll,0,breakAngle)`, `toeTip = max(footRoll-breakAngle,0)`, raises added after), the bank pivot choice from lateral position, the chain fold, and the inverse decomposition (total pitch into `footRoll`, total roll into `bank`, the toe joint into `toeTap`, the rest to the handle).
- **Hand-work the numbers first** (`reverse-foot.md` section 15 lists the cases), then a standalone check, then the suite. `core/` has no harness of its own (D20), so the pose cases run through the module in phase 3.
- **Done when:** the cases are written and the functions pass them. Watch the sign of each pitch channel against the table in section 8.

## Phase 3: `ReverseFoot` stage 1

- The module and node (`reverseFootModule`), `moduleArgs` (`joints`, `reverseHierarchy`, `ikFkDriver`, `controlName`), the role table and its validation (unknown role, repeated role, `bankOut` without `bankIn`, a marker the file lacks, the ankle driven by two modules), the channel control `<base>Foot` (derived from the `ball` marker, world aligned, translate locked and hidden, the 11 channels with their mirror modes), and the `ankle` socket wired to the Limb's `ikTarget`.
- **The cycle:** two child plugs, `ik` and `ankle`, in separate attribute groups (the D62 phase 4 lesson: Maya orders the graph by attribute, a socket and a plug on one attribute is a cycle in DG). Add `ModulePlugInfo::blindSockets` for the `ankle` plug against the `ankle` socket. Test the whole leg cold in DG and parallel mode, and that a foot wired to `end` for stage 1 still reports the loop.
- **Done when:** a `ModuleContract` subclass (with a mirrored build and a -X chain), the hand-worked pose cases that need no stage 2 (rest, each peel regime, a roll exactly at the break, `ballRaise` not starting the toe tip, bank both signs on both sides, swivels, `ballRoll`, the ankle pushed up), the validation cases, the cycle test, a golden snapshot, `docs/modules.md` section and a worked example with its test.

## Phase 4: stage 2, the toe FK control, `ikFkDriver`

- Stage 2: the toe and toe tip from `end`, the length-preserving pull toward the pivot, the toe FK control (rotate only), the blend by `ikFkBlend` with TwoBoneIk's blend function, the visibility output.
- `ikFkDriver`: a connection from the driver module's settings control `ikFkBlend` channel to a locked, non-keyable input on the foot, made in build pass 2. Add the blend source plug to `ModuleInterface` (TwoBoneIk and Limb implement it) and the validator rules (the module exists, is a Limb or TwoBoneIk, and is the module `ik` and `ankle` hang from). A mirrored foot swaps the id.
- **Done when:** the blend 0, 0.5, 1 cases, the leg that cannot reach, the validation cases for `ikFkDriver`, and the contract still pass. **Run `limb-coupling-check`:** this phase touches Limb's interface.

## Phase 5: the inverse and the match

- `RigInverse` pre-pass over modules that feed an `ikTarget`, `ModuleInverseContext::ikTargetOffset`, the foot's `inverseChannels` (`footRoll`, `bank`, `toeTap` derived, the rest read as scalars), the Limb's inverse using the offset (`handle = end * offset^-1`), and `setRigIkFkBlend` running the same inverse.
- **Done when:** the round trip comes back to 1e-4 on the ankle and toe, the FK to IK match with a raised foot has no pop, the pre-pass order is tested, and the missing-joint and sidecar-precedence cases pass. `test_limb_matches_parts` gets the foot-shaped case (Limb alone, with an identity foot, and with a foot at rest give the same joints). This closes the open item in `limb.md` section 8: say so in the commit and update the page.

## Phase 6: perf rig and `bench`

- Add many feet to `tools/perf/perf_scenes.py` (`foot_specs`, on the existing `limb_module_specs` limbs) and run `bench` **once** (D36). Report the fps and the ratio to the floor in the commit message, and say so when they are below the target (30 fps for 20 rigs, 1.5 times the floor on 50). Run `compute-reviewer` on the node.

## Phase 7: not part of this job

Aim to markers (`moduleVersion` step) and the export of `export: true` markers (`MRK` nodes, the axis visualizer on them) are their own gates. Do not start them. Leave a note in the final summary if the phases above change what they need.

## Watch at build time

- The sign of every pitch channel, against the hand-worked cases.
- Whether the world-aligned `<base>Foot` control is easy to select next to the foot (only the viewport gallery shows this: ask the author to look).
- The `ModuleInterface` change for the blend source touches every module: keep it a default no-op.
- `RigInverse`'s pre-pass must not change the order for rigs with no foot. The golden interface snapshots of existing modules must not change.

## When you report

For each phase: the commit, the TOTAL line, any tier that failed or was blocked, the docs touched, the mayadev proposals, what was not verified (the viewport gallery, GUI Maya), and the complexity added. Stop and ask on any ambiguity about rig behavior: do not guess.
