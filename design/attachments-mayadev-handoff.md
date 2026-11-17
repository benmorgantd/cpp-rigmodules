# Handoff: attachments in mayadev (D55 follow-up, phase 7b)

Written 2026-11-17. Read `CLAUDE.md` (the "Sibling repo: mayadev" section), then `design/attachments-mayadev.md` (the approved design: this is what to build), `design/attachments.md` and `docs/attachments.md` (what RigModules already does), and D55 in `design/decisions.md`. In mayadev read `docs/assets.md`, `docs/rigmodules.md` and `docs/tools.md`. Delete this file when the mayadev side is committed.

## Where things stand

RigModules phase 7 is built and committed: `c7b7868`, on branch `rigs-and-looks-phase5`, unpushed and unmerged (phases 3 to 7 are stacked on `main`). Full suite: TOTAL 1100 passed. mayadev is on a branch of the same name, clean, latest commit `1ea6385`. Nothing in mayadev has been changed for attachments. Ask which mayadev branch to build on (a new branch stacked on `rigs-and-looks-phase5` is the default) and do not merge or push unasked.

The design, `design/attachments-mayadev.md`, is approved and uncommitted in RigModules (with this file). Commit both with the first RigModules docs change, or on their own.

mayadev results against the phase 7 `.mll`: `tests/asset` 129 + 212 OK, `tests/core` 115 (7 skipped) + 138 OK. **Not run in phases 5 to 7:** every `tests/gui/*` check (one library browser check failed once in phase 4) and `tests/rigging/rigmodules_menu`. Run them at the start so a failure isn't blamed on this phase.

## What RigModules gives you

- `cmds.createRigFromTemplate(*joints, template=, asset=, namespace=, parentRig=rigRoot)`. A template with `parent:module.socket` ids needs `parentRig`, and one without refuses it. A missing socket fails the read phase with every miss listed, and nothing is built.
- `getRigInfo` returns `parentRigRoot`, and `rigmodules.query.RigInfo.parent_rig_root` is `None` or the parent's rig root name. `RigInfo.skeleton_hash` is what the look partition compares weight files against.
- Test fixtures to copy for mayadev tests: `tests/maya/fixtures/templates/attach_face.json` with `skeletons/attach_face.json` (a body from `fkchain_three` and `fk_three`), and `attach_ears` for mirrored `parent:` ids.

## What to build (the approved design, short)

1. **`rig.json` `parent`** (`bmaya/core/asset/records.py`, `asset.py`): `RigRecord.parent: RigRef | None`, an optional key (no format bump), `Asset.add_rig(..., parent=None)`, `Asset.parent_of(rig)`, and the self-parent and ancestor-loop checks across assets.
2. **`new_rig`** takes `parent`, and `New Rig...` asks "Attaches to a rig?". No template parsing.
3. **`build_rig`** (`bmaya/maya/assettools.py`): `parent_rig_root=None`. Find the parent by `RigInfo.asset_id` and `rig_name`: one is used, none builds the parent first with its default look (recursively), several is an error naming them. Pass `parentRig=`. A failed attachment build leaves the parent and says so.
4. **`apply_look`**: keep only the weight files whose `skeletonHash` is the rig's (a file with no hash belongs to the rig only on a one-skeleton look, an error on a spanning one), import the model, keep the meshes whose `meshId` is in those files, delete the rest, then `setRigLook`. `build_rig` applies the look to the parent and the attachment through it.
5. Tests, then docs (list in section 7 and 8 of the design).

## Points to check as you go

- `RigRecord` is read with `_read(path, required, optional)`, which refuses unknown keys: `parent` must join the optional set.
- `apply_look` currently imports into `info.namespace` and passes every file. Today's single-skeleton looks must behave exactly as before (the tests in `tests/asset` cover this: keep them green).
- `RigRef` already exists for `source`, with its `{asset, rig}` form: reuse it, don't add a second type.
- Search before changing a name: `createRigFromTemplate` in `bmaya/maya/assettools.py` (`build_rig`), `RigInfo(` in `tests/core/test_maya_rigpanel_view.py`, and the registered tool calls (`check_calls` over `toolsets/master_tool_registry.json`) for the changed `New Rig` form.
- `py/scripts/docs/guides/Asset Layout.md` in mayadev still describes variants: ask whether to rewrite or delete it. The author's test asset (`C:\Users\bmorgan\Documents\Art\Maya\Projects\test\character\biped`) is in the old layout: never touch it.

## Things that cost time before

- **Bash heredocs eat backslashes and trip on quotes** here. Write scripts and commit messages to the scratchpad with the Write tool and run them (`git commit -F file`).
- **The Edit tool needs the exact tabs.** A multi-line `old_string` failed once on indentation: use fewer lines of context.
- **Line endings are mixed.** RigModules C++ and most docs are CRLF, some tests LF. The Write tool writes LF.
- **mayadev's Maya tests skip silently** without `RIGMODULES_PLUGIN`, so read the skip count. Copy the `.mll` to a temp folder (recopy after every RigModules build), set `RIGMODULES_PLUGIN` to the copy, `PYTHONPATH` to RigModules' `python/` folder, `MAYA_DISABLE_CER=1`, `MAYA_SKIP_USERSETUP_PY=1` and `QT_QPA_PLATFORM=offscreen`. Run the plain-Python tests with `python -m unittest discover` and the `test_maya_*.py` ones with `mayapy.exe -m unittest discover` from inside each `tests/` folder, as the `run.bat` files do. `tests/asset` uses `test_asset_*.py` (plain) and `test_maya_*.py` (mayapy); `tests/core` uses `test_core_*.py` and `test_maya_*.py`. Filter output for `^Ran`, `OK`, `FAIL`, `ERROR`, `skipped`.
- **`cmds.undo()` undoes the last undoable thing, which can be a selection change.** An undo chunk must contain every step.
- **Commit only your own paths** in both repos (the author may have uncommitted work), and follow the commit message rules in `CLAUDE.md` (the RigModules ones are the model). mayadev has its own conventions: its docs and tests rules apply there, not this repo's Allman and docs pass.
- **Propose mayadev edits to the author:** the go-ahead for this build was given for the design above. Anything beyond it needs asking.

## Done looks like

- `rig.json` carries `parent`, old files read as before, and the self-parent and loop checks fail with clear errors.
- `build_rig` builds a body and a face in one call, with the face following the body (`RigInfo.parent_rig_root` set, a chest-up control moving the face), reuses an existing parent, refuses several candidates and a wrong `parent_rig_root`, and leaves the parent after a failed attachment.
- A look that spans both skeletons gives each rig its own meshes and weights, and single-skeleton looks behave as before.
- mayadev's `tests/asset` and `tests/core` pass against the current `.mll` with no unexpected skips, the GUI checks are run or listed as not run, and the docs are updated: mayadev `docs/assets.md`, `docs/rigmodules.md`, `docs/tools.md`, `docs/status.md`, and here `docs/mayadev.md`, `design/attachments.md` (status), `design/rigs-and-looks.md` (section 7 and phase 7) and `CLAUDE.md`.
- The summary lists every author data folder affected and says what was and wasn't verified.
