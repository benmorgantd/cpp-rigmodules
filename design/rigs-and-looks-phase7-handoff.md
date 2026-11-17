# Handoff: rigs and looks, phase 7 (attachments)

Written 2026-11-17, after phase 6. Read `CLAUDE.md` (the "Gate for new modules" and "Sibling repo: mayadev" sections), then D54 in `design/decisions.md`, `design/rigs-and-looks.md` (section 7 is this phase, section 11 the phases), `design/two-bone-ik.md` (the reference for a module designed through the gate) and `design/module-directions.md` (D46). In mayadev read `docs/assets.md` and `docs/rigmodules.md`. Delete this file when phase 7 is committed.

## Where things stand

Both repos are on branch `rigs-and-looks-phase5`, unpushed and unmerged. Phases 3 to 6 are stacked on `main` in each. Ask which branch to start from, and whether to merge first.

| Repo | Latest commits |
|---|---|
| RigModules | `f632627` (phase 6 docs, this handoff's predecessor deleted), `1777609`, `8367ce6` (looks in the scene) |
| mayadev | `1ea6385` (removed `tests/basic/startup_test`), `14471a9` (phase 6: `SourceControl.list_folders` and `sync`, `Asset.all_looks`, `sync_look`, `apply_look` syncing first), `a371bac` (phase 5) |

Results at phase 6: mayadev `tests/asset` 129 + 212, `tests/core` 112 + 138 (1 skip) against the phase 5 `.mll`. RigModules' full suite was last run in phase 5 (1080 passed, plus one test fixed afterwards, expected 1081). **Not run in phases 5 and 6:** every `tests/gui/*` check (including the library browser one that failed once in phase 4) and `tests/rigging/rigmodules_menu`. Run what you can at the start so a failure is not blamed on phase 7.

`py/scripts/docs/guides/Asset Layout.md` in mayadev still describes variants: ask whether to rewrite or delete it. The author's test asset (`C:\Users\bmorgan\Documents\Art\Maya\Projects\test\character\biped`) is still in the old layout: never touch it. Not built from earlier phases: the Perforce adapter and the library window's Looks tab.

## What phase 7 is

An **attachment** is a rig with its own skeleton that follows another rig: a face, a cape, a prop. The plan (section 7) is: the attached rig's template lives in its own rig folder like any rig, `rig.json` says what it attaches to, and a new **attach node** carries the follow. The plan's captured requirements (author, 2026-11-16):

- It follows a hierarchy, not one socket: a head attaches from the chest up, so the face follows the body's chest, clavicle, neck and head.
- Controls of the attached rig for those regions are hidden and driven by the body while attached, and locked and zeroed, so keys left on a hidden control can't surprise an animator later.
- It reuses child plugs and sockets. A node probably holds the mapping (child plug to socket, several pairs); the child rig declares what it needs and the node checks the parent rig has those sockets.
- Rigs are in different namespaces (D54 phase 3: one rig per namespace), so every connection crosses namespaces.
- The attach command belongs in RigModules (it is about rig nodes). Building the body and face together and attaching them is mayadev tooling.

This is **a new node type, so it goes through the gate** (`CLAUDE.md`): the animator-facing problem, whether an existing module or a small extension (the constraint's driver sockets already cross modules: `RigConstraintNode::resolveSocket`) covers it, the full design, the maintenance surface it adds, and many instances in the perf rig with `bench` (target in D36). **Write the design and get sign-off before any code.** The design is the deliverable of the first session.

## Points to settle with the author before designing

- Branch, and whether to merge phases 3 to 6 first.
- Whether the attach node is really needed or a cross-namespace `rigConstraint` driver (D31, a socket named `"namespace:module.socket"`) plus a locking step covers it. Cost of each in evaluation (D36) and in what an animator sees.
- What "attached" means to an animator: are the face's overlapping controls hidden and locked, or removed? Can an attachment be detached, and what happens to its animation then? Does the attached rig's own animation (keys on its non-overlapping controls) survive re-attachment to a different body?
- Where the mapping lives: in the attached rig's template (the child rig declares what it needs, `rig.json` names the parent rig) or in a separate attach file. Name stability applies: every name is public API (poses, animation, export).
- What the parent must provide, and the failure mode when it doesn't (a body rig built from another template, a missing socket): the plan says the node checks, so the read phase of the attach command should report all problems at once, as `createRigFromTemplate` does (D17).
- Pose and animation libraries (D42, D51): do an attachment's controls have their own `controlposes` and `controlanims` (the plan says a rig is a rig), and how do `meta` and the rig info (`RigInfo`) name the parent?
- Export (D54, the skeleton files): an attachment's skeleton is its own file and its own hash. Does the engine export of body and face stay separate? Check `design/engine-rig.md` before assuming.
- Does `rig.json` gain a parent rig reference, as two fields like a skeleton reference (an `asset` and a `rig`), and a D33 update step? Check `RigRecord` in mayadev `bmaya/core/asset/records.py`: it holds `skeleton` and `source` today.
- Looks: a look can carry meshes for several skeletons (a body mesh on `body`, a head mesh on `face`). Does `apply_look` have to handle a look that spans a rig and its attachment (`setRigLook` works on one rig root)? Phase 5 left looks from another asset in `build_rig` open for the same reason.

## What to read in the code

- `cpp/maya/nodes/Constraints.h/.cpp`: `resolveSocket`, `createConstraint`, the cross-module socket handling and the D27 space-switch formula.
- `cpp/maya/shared/RigModule.h/.cpp`: child plugs and sockets, `planModules`, `buildModules`.
- `cpp/maya/nodes/RigRoot.h/.cpp`: `createRig`, the read and write phases, the namespace resolution (one rig per namespace) and the rig root's attributes (`assetId`, `skeletonHash`, `look`).
- `cpp/core/RigTemplateValidation.h/.cpp`: where an attach check would sit, and `ModuleInterface`.
- `maya/shared/RigTraversal`: finding a rig root by connections (`getRigInfo` for a node), which an attach must stay consistent with.
- `tests/README.md`: `ModuleContract`, pose cases and golden snapshots, and the perf scene tool (`tools/perf/perf_scenes.py`).

## Things that cost time before

- **Bash heredocs eat backslashes and trip on quotes** here. Write edit scripts to the scratchpad with the Write tool and run them. Use a small exact-text replace helper that opens files with `encoding="utf-8"` and `newline=""` and writes each file's own line ending back.
- **Line endings are mixed.** RigModules C++ and most docs are CRLF, some tests are LF. The Write tool writes LF: convert new C++ files to CRLF afterwards.
- **mayadev's Maya tests skip silently** without `RIGMODULES_PLUGIN`, so read the skip count. Copy the `.mll` to the scratchpad (recopy after every build), set `RIGMODULES_PLUGIN` to the copy, `PYTHONPATH` to RigModules' `python/` folder, `MAYA_DISABLE_CER=1` and `MAYA_SKIP_USERSETUP_PY=1`, then run the suites' `run.bat` files (or the `unittest discover` lines inside them directly, which is more reliable from Git Bash). Filter the noisy output for `^Ran`, `OK`, `FAIL`.
- **`cmds.undo()` undoes the last undoable thing, which can be a selection change.** An undo chunk must contain every step, including a closing `cmds.select(clear=True)`.
- **`MObjectHandle` has no `operator<`**, so it can't go in a `std::set`. Use a vector and `std::find`.
- **Perf scenes saved before D54 phase 3 are invalid** (they have an `assetRoot` attribute the rig root no longer has): rebuild with `perf_scenes.py complex --count N`. The measured 20-rig figure was 22 to 28 fps against the 30 fps target (`design/control-node-perf-handoff.md`), so a new node's cost matters: measure it, and report it below target.
- **A change to `mayalaunch` affects mayadev's batch framework**: run mayadev's `tests\core\run.bat` too.
- **Commit only your own paths** in both repos (they may hold the author's uncommitted work), and use the commit message rules in `CLAUDE.md`. Do not push or merge unasked.
- **Probes are not tests.** Each check must end up in the suite, or in the summary as a proposed test.

## Done looks like

- The design is signed off with the points above answered, and recorded in `design/decisions.md` (a D54 built note, or a new decision if the attach node is its own).
- The node (or the cross-namespace constraint path, if that is the answer) is built with a `ModuleContract` test, pose cases and a golden snapshot, many instances in the perf rig, and `bench` run with the fps reported.
- Building a body and attaching a face moves the face's skeleton with the body's chest-up controls, the overlapping face controls are hidden, locked and zeroed, and a missing socket or a wrong parent fails in the read phase with every problem listed.
- The mayadev side (`rig.json` parent reference, and the build-and-attach tool if the author brings it in) is done or listed as the next phase.
- The full RigModules suite passes with its TOTAL line reported, mayadev's suites pass against the new `.mll` with no skips, the GUI checks are run or listed as not run, and the docs pass is done (`docs/` pages for the new node, `docs/mayadev.md`, `docs/architecture.md`, `CLAUDE.md` repo map and MTypeId list).
- The summary names every author data folder the change would affect.
