# Handoff: rigs and looks, phase 6 (listing looks that aren't synced)

Written 2026-11-17, after phase 5. Read `CLAUDE.md` (the "Sibling repo: mayadev" section), then D54 in `design/decisions.md` (the phase 5 built note), `design/rigs-and-looks.md` (section 6 is this phase, section 11 the phases) and `docs/looks.md`. In mayadev read `docs/assets.md` (the "Source control" and "Publishes" parts) and `tests/README.md`. Delete this file when phase 6 is committed.

## Where things stand

Both repos are on branch `rigs-and-looks-phase5`, unpushed and unmerged. Phases 3, 4 and 5 are stacked on `main` in each. Ask which branch to start from, and whether to merge first.

| Repo | Phase 5 commits |
|---|---|
| RigModules | `a04e080` (your `Filled` default, committed alone), `8367ce6` (looks in the scene), `1777609` (the control test and docs follow `Filled`) |
| mayadev | `a371bac` (`apply_look`, recorded looks, the session's look API) |

Results: RigModules suite **1081 expected passed** (1080 measured plus the test updated in `1777609`, run on its own, not as a full suite). mayadev against the phase 5 `.mll`: `tests/asset` 123 + 208, `tests/rigging/rigmodules_menu` 44, `tests/core` 112 + 138, no skips. **Not run:** every `tests/gui/*` check, including the library browser one that failed once in phase 4 and was never rerun.

mayadev has the author's uncommitted deletion of `tests/basic/startup_test/`: leave it out of commits. `py/scripts/docs/guides/Asset Layout.md` still describes variants (an old copy of `docs/assets.md`): ask whether to rewrite or delete it. The author's test asset (`C:\Users\bmorgan\Documents\Art\Maya\Projects\test\character\biped`) is still in the old layout: never touch it.

## What phase 5 left that phase 6 builds on

- **Looks on disk and in the scene.** `Asset.looks` (folders holding a `look.json`), `Asset.look_fits(look, rig)`, `assettools.get_rig_looks`, `assettools.apply_look(node, look)` and `cmds.setRigLook`. A rig records its look on a `lookRoot` (`RigInfo.look`).
- **A session API with no window.** `LibrarySession.looks()`, `worn_look()` and `apply_look(name)` list the looks the active rig's skeleton can wear and apply one. `looks()` reads `Asset.looks`, which is a local directory scan: this is exactly what phase 6 changes. The library window has **no Looks tab**: it is the unbuilt half of phase 5 and the natural place for "not local" to show.
- **Source control is a stub.** `bmaya/core/asset/sourcecontrol.py` has `SourceControl` with `checkout(path)` and `add(path)`, both no-ops, and `set_current(adapter)`. There is no Perforce adapter in the repo.
- **The registry** (`bmaya/core/asset/registry.py`) indexes assets by a directory walk, so it also sees only what is synced. `latest_publish`, `find` and `ids` are the questions it answers.

## Phase 6 scope (from the plan, section 6)

"Source-control listing of unsynced looks (with the Perforce adapter, when a project uses Perforce)." The plan chose: **ask source control for the folders**, with the do-nothing default falling back to the local scan. Rejected: a master list per asset (a shared-file edit, D29) and, for now, a farm-built index.

1. **A "list folders" call on `SourceControl`** (mayadev). Something like `list_folders(path) -> list[str] | None` returning the child folder names under a depot path whether or not they are synced, `None` for the default adapter, which means "scan the disk". Settle: the name, whether it returns names or paths, and what an adapter failure does (a Perforce server that is down must not make the library empty: fall back to the local scan and say so).
2. **`Asset.looks` and the registry use it.** Today `Asset.looks` is a disk scan of folders that hold a `look.json`. Decide whether the listing becomes a new method (`Asset.all_looks()`, say) that returns every look with a `local` flag, leaving `Asset.looks` as the local scan, or changes `Asset.looks`. A new method keeps the callers that assume a look exists on disk (`apply_look`, `build_rig`) honest. Names only: `look.json` isn't readable for an unsynced look, so the skeleton a look fits can't be known until it is synced. Settle what the browser shows for an unsynced look (name, "not local") and how `look_fits` is answered for it.
3. **Syncing on choice.** The plan says "choosing it syncs it". That needs a `sync(path)` call on the adapter (a second addition to the interface), and `apply_look` calls it for a look that isn't local, then re-reads `look.json` and checks the skeleton fits before importing. Settle the failure cases: a sync that fails, and a synced look that turns out not to fit the rig.
4. **The Perforce adapter.** The plan says "when a project uses Perforce". Ask whether it is in this phase. If it is, settle P4Python or the `p4` command line, how a depot path maps from a workspace path (`p4 where`), the ignorable cost of `p4 dirs`, and how to test without a server (a fake adapter in the tests, and a manual check against the author's server). The package must stay importable without P4Python, as `bmaya.core` is plain Python.
5. **The library browser's Looks tab** (mayadev, `design/library-browser.md`, D51) if it is brought in here: the unbuilt half of phase 5. It lists `looks()` with a "not local" marker and applies through `apply_look`. Settle whether a look gets `meta` (tags, description): the author said no in phase 5, and `look.json` would need a D33 update step if that changes.
6. **Docs, tests and perf.** mayadev `docs/assets.md` (the Source control section, the `Asset` table), `tools.md`, `rigmodules.md`, `status.md`; RigModules `docs/looks.md` ("Not here yet"), `docs/mayadev.md`, the D54 built note and the phase line in `rigs-and-looks.md`, and `CLAUDE.md` if the repo map changes. **No RigModules C++ is expected:** if the design finds a need for one (a flag on `setRigLook`, say), raise it first. No perf check unless a node is added.

Not in phase 6: attachments and the attach node (7), the asset browser (8), the parameters node (9), looks from another asset in `build_rig` (needs a look reference as two fields, like a skeleton reference; phase 5 left it), and removing a previous look's materials (a known gap of `setRigLook`).

## Points to settle with the author before designing

- Which branch to start from, and whether to merge phases 3 to 5 first.
- Whether the Perforce adapter is in this phase, and whether a server is available to test against.
- Whether the Looks tab is in this phase.
- The shape of the listing (a new method with a `local` flag, or a changed `Asset.looks`) and the name of the adapter calls.
- What to do when the adapter fails (the recommendation: fall back to the local scan, with a warning).
- Whether an unsynced look can be applied at all before it is synced (the plan: choosing it syncs it).

## Things that cost time before

- **Bash heredocs eat backslashes and trip on quotes** in this environment. Write edit scripts to the scratchpad with the Write tool and run them. A small exact-text replace helper that opens files with `encoding="utf-8"` and `newline=""`, and writes each file's own line ending back, was the reliable way to edit many files. Use raw strings for Windows paths and `\n` in replacement text, or the edit script's own `\n` handling will bite.
- **Line endings are mixed.** RigModules C++ and most docs are CRLF, some tests are LF. The Write tool writes LF: convert new C++ files with a read-and-rewrite to CRLF afterwards.
- **mayadev's Maya tests skip silently** without `RIGMODULES_PLUGIN`, so read the skip count. Copy the `.mll` to the scratchpad (recopy after every build), set `RIGMODULES_PLUGIN` to the copy, `PYTHONPATH` to RigModules' `python/` folder, `MAYA_DISABLE_CER=1` and `MAYA_SKIP_USERSETUP_PY=1`, then run `tests\asset\run.bat`, `tests\rigging\rigmodules_menu\run.bat` and `tests\core\run.bat`. The output is very noisy: filter it for `^Ran`, `OK`, `FAIL`. The `internalTimeUnitStringToDisplayFPSString` lines are old noise.
- **`cmds.undo()` undoes the last undoable thing, which can be a selection change.** An undo chunk must contain every step, including a closing `cmds.select(clear=True)`. This failed a test in phase 5.
- **`MObjectHandle` has no `operator<`**, so it can't go in a `std::set`. Use a vector and `std::find`.
- **The perf tool had been broken since phase 2** (an uppercase `rigName`), fixed in phase 5. Scenes saved before D54 phase 3 have an `assetRoot` attribute that the rig root no longer has, so their `bench` numbers are not valid: rebuild them with `perf_scenes.py complex --count N`. The measured 20-rig figure is 22 to 28 fps against a 30 fps target (`design/control-node-perf-handoff.md`).
- **The library index caches by modified time.** Two writes in one clock tick look unchanged. A test that edits a library file behind the session calls `session._index.forget(path)`.
- **Commit only your own paths in mayadev and RigModules** (both have the author's uncommitted work), and use the commit message rules in `CLAUDE.md`.
- **Probes are not tests.** Each check must end up in the suite, or in the summary as a proposed test.

## Done looks like

- The design is signed off, with the points above answered.
- With a source-control adapter that lists folders, the session lists every look of the asset, local or not, and a look that isn't local says so. With the default adapter nothing changes from phase 5.
- Choosing an unsynced look syncs it, checks that it fits the rig, then swaps it in. A failed sync or a look that doesn't fit changes nothing.
- A failing adapter falls back to the local scan with a warning, and never leaves the library empty.
- The Maya suites pass against the current `.mll` with no skips, the GUI checks are run (or listed as not run), and RigModules' suite passes with its TOTAL line reported. The docs list above is updated.
- The summary names every author data folder the change would affect.
