# Handoff: rigs and looks, phase 5 (looks in the scene)

Written 2026-11-17, after phase 4. Read `CLAUDE.md` (the "Sibling repo: mayadev" section), then D54 in `design/decisions.md`, then `design/rigs-and-looks.md` (section 4 is the scene structure, 4.1 the swap, 5 the hash, section 11 the phases) and `design/rigs-and-looks-phase4.md` (what phase 4 settled). In mayadev read `docs/assets.md`, `docs/rigmodules.md` and `tests/README.md`. Delete this file when phase 5 is committed.

## Where things stand

Both repos are on branch `rigs-and-looks-phase4`, unpushed and unmerged. Phase 3 and phase 4 are on top of `main` in each, so start from phase 4 (or merge first, if the author has). Ask which.

| Repo | Commits since main |
|---|---|
| RigModules | phase 3 `8a7d41c`, `dbecc28`; phase 4 `ad45564` (skeleton reader and `loadRigSkeleton`), `73947cb` (docs) |
| mayadev | phase 3 `10c0b1e`; phase 4 `866f7ae` (lowercase ids), `f81b84b` (model and tools), `689e675` (docs) |

Results at those commits: RigModules suite **TOTAL 1050 passed**. mayadev against the `ad45564` `.mll`: `tests/asset` 123 + 201, `tests/rigging/rigmodules_menu` 44, `tests/core` 112 + 138, no skips. `tests/gui/rigpanel` passes. `tests/gui/librarybrowser` failed once, on the rig label only, and its check was updated and **not rerun**: run it first.

RigModules has an uncommitted change to `cpp/maya/nodes/RigControlNode.cpp` (the `Filled` default, true to false) that is the author's. Leave it out of every commit and ask before building on it. mayadev has the author's uncommitted deletion of `tests/basic/startup_test/`: leave it out too. `py/scripts/docs/guides/Asset Layout.md` in mayadev still describes variants (an old copy of `docs/assets.md`): ask whether to rewrite or delete it.

## What phase 4 left that phase 5 builds on

- **`skeleton.json` is the only skeleton format.** `cmds.saveRigSkeleton` and `cmds.loadRigSkeleton` (`core/SkeletonFile`: `deserializeSkeleton`, `skeletonHash`, `findSkeletonJoint`). Nothing yet compares a file's hash with anything.
- **A model file is only meshes** (author, 2026-11-16): no joints, no skinCluster, no history. `ModelHandler.save` in `bmaya/maya/assethandlers.py` exports undeformed copies. A look is its model plus its weight files, so the swap in `rigs-and-looks.md` section 4.1 was rewritten: check the weight files' `skeletonHash`, import the meshes, load the weights onto the rig's joints, delete the previous look's nodes. There is no skinCluster re-pointing.
- **The weight file carries `skeletonHash`** (D35, version 2) and the rig root stamps `skeletonHash` (phase 3). `loadRigSkinWeights` records the hash and **does not check it**: that check is phase 5.
- **A built rig does not record its look.** `build_rig(asset_id, rig, look=None, namespace=None)` imports the look but leaves no trace, so `save_skin_weights` and `load_skin_weights` take a `look` argument (the one look the rig can wear, an error when there are several). The `lookRoot` node removes that workaround.
- **Library:** the browser lists a rig's Approved, Shared (the source rig's) and User files. There is no Looks kind.
- **mayadev has looks on disk** (`Asset.looks`, `look.json` naming skeletons, `Asset.look_fits(look, rig)`), `new_look`, and `rigmodules.asset.new_look` in the menu.

## Phase 5 scope (from the plan, section 11)

"Looks in the scene. The `lookRoot` node, the swap command, and the library browser's Looks kind." Propose the design first and get sign-off, as in phase 4: the author reviewed it before any code. The node and command are RigModules C++ (new node: the gate in `CLAUDE.md` applies in spirit, though a look root is not a module); the Looks kind is mayadev.

1. **`lookRoot` node** (RigModules). Connected to the rig root, it records the look's asset id, look name and `publishVersion`, so tools find the look by traversal, never by name. Settle: its `MTypeId` (0x00219 is retired and must not be reused: check the list in `CLAUDE.md`), attributes in `RigAttr`, the connection to the rig root and what happens when a rig has none, how `RigTraversal` finds it, and whether `getRigInfo` and `rigmodules.query.RigInfo` gain `look` fields (public API for mayadev: search `py/scripts`, `toolsets` and `tests` in mayadev for `RigInfo` use, `docs/rigmodules.md` lists them).
2. **The swap command** (RigModules): something like `cmds.setRigLook(rigRoot, ...)`. Open points to put to the author:
   - **What it does and does not do.** The plan's steps are in section 4.1. The command can't import a file from mayadev's asset model (D24: RigModules knows no asset paths), so decide the split: mayadev imports the meshes and passes the nodes, or the command takes a model file and a weights folder. Whichever it is, the `skeletonHash` check against the rig root and the error naming the differing joints (from the two `skeleton.json` files) belong in RigModules.
   - **Undo.** Importing a file through `cmds.file` is not cleanly undoable in a command. D14's command shape and the existing undoable load commands (`loadRigSkinWeights`, `loadRigSkeleton`) are the reference. Decide whether the swap is one undo step or not undoable, and say so in the docs.
   - **Deleting the previous look's nodes.** The `lookRoot` must know which nodes belong to a look (a set, or the connection graph) so the swap can delete them and so a mesh the animator added is never removed.
   - **What else a look carries** (the face's blend shape wiring from the parameters node, section 7.3) is later: record it as out of scope.
3. **The weight hash check** in `loadRigSkinWeights`: compare the file's `skeletonHash` with the rig root's. Settle the severity (error, warning, or an error unless a flag) and the case of a file with `""` (made on joints no rig drove, or a version 1 file): the phase 3 note says it is recorded and `""` is possible.
4. **`build_rig` records the look** through the new node, and the weight tools stop taking a `look` (or keep it as an override: ask).
5. **The library browser's Looks kind** (mayadev, D51 `design/library-browser.md`): a third kind beside poses and animations, listing the looks whose skeleton matches the active rig's, with tags and search, and applying one through the swap command. Not-local looks wait for phase 6 (source-control listing). The browser's session is `LibrarySession` in `bmaya/maya/librarybrowser.py` and its window `bmaya/maya_ui/librarybrowser.py`. Decide whether a look has `meta` (tags, description) and where it is stored (`look.json` is the natural place, but it is a format change with an update step: D33).
6. **Docs, tests and perf** as always: `docs/commands.md`, `docs/architecture.md`, `docs/skinning.md`, `docs/mayadev.md`, `CLAUDE.md` (the repo map, the command list, the node list and MTypeIds), mayadev's `docs/assets.md`, `tools.md`, `rigmodules.md`, `status.md`, `tests/README.md`, and D54's built notes. A new command gets a docs example test in `tests/maya/concepts/test_docs_examples.py`. A new node needs a perf check: the node is one per rig and does no `compute`, so say so, and still run `bench` once to confirm it adds nothing.

Not in phase 5: source-control listing of unsynced looks (6), attachments and the attach node (7), the asset browser (8), the parameters node (9), a hash in the bone map, looks from another asset in `build_rig` (needs a look reference as two fields, like a skeleton reference; the library browser's Looks kind will probably force this, so raise it).

## Points to settle with the author before designing

- Which branch to start from (above), and whether to merge phases 3 and 4 first.
- The `lookRoot` shape: its attributes, how it reaches the rig root, how it owns its nodes.
- The command's split with mayadev (above, point 2) and its undo behaviour.
- The severity of the weight hash check, and the `""` case.
- Whether a look gets `meta` and where.
- Whether to run the author's own asset through it. The test asset (`C:\Users\bmorgan\Documents\Art\Maya\Projects\test\character\biped`) is still in the old layout: the author moves it by hand (section 9 of `rigs-and-looks-phase4.md`). Ask whether they have, and never touch it.

## Things that cost time before

- **Bash heredocs eat backslashes** in this environment (a Windows id or path, a `\n` in a string). Write edit scripts to the scratchpad with the Write tool and run them, or use the Edit tool. A scratch helper that does exact-text replacement and keeps each file's line ending was the reliable way to edit many docs.
- **Line endings are mixed.** Most files are CRLF, some tests are LF. Open with `newline=""` and write each file's own ending back.
- **mayadev's Maya tests skip silently** without `RIGMODULES_PLUGIN`, so read the skip count. Copy the `.mll` to the scratchpad and set `RIGMODULES_PLUGIN` to the copy, `PYTHONPATH` to RigModules' `python/` folder, `MAYA_DISABLE_CER=1` and `MAYA_SKIP_USERSETUP_PY=1`, then run `tests\asset\run.bat`, `tests\rigging\rigmodules_menu\run.bat` and `tests\core\run.bat`. The GUI checks (`tests\gui\*\run.bat`) also need `RIGMODULES_PYTHON`.
- **A copied `.mll` goes stale.** Recopy after every build, or mayadev runs against the old plugin.
- **The library index caches by modified time.** Two writes in one clock tick look unchanged. A test that edits a library file behind the session calls `session._index.forget(path)`.
- **`maya.cmds.ls(type=..., exactType=...)` are mutually exclusive.** Use one.
- **`cmds.duplicate(..., upstreamNodes=True)` names the copies in the current namespace**, which is how `ModelHandler` keeps material names without the rig's namespace. A `shadingEngine` can't be duplicated with upstream nodes (it drags in every assigned mesh), so it is created new.
- **Commit only your own paths in mayadev and RigModules** (both have the author's uncommitted work).
- **Probes are not tests.** A mayapy probe is fine while exploring. Each check must end up in the suite, or in the summary as a proposed test.

## Done looks like

- The design is signed off, with the points above answered.
- A rig can be put in a look through the swap: the weights' hash is checked, the previous look's nodes go, the `lookRoot` is updated, and the controls and animation are untouched. Undo behaves as documented.
- `build_rig` leaves a `lookRoot`, and the weight tools no longer need to be told the look (or keep an override, as decided).
- The library browser lists looks for the active rig and applies one. The Maya suites pass against the phase 5 `.mll` with no skips, and the GUI checks are run (or listed as not run).
- RigModules' suite passes on the build that contains the change, with the TOTAL line reported, and the docs list above is updated.
- The summary names every author data folder the change would affect.
