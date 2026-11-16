# Handoff: rigs and looks, phase 3 (the RigModules scene side)

Written 2026-11-16, after phase 2. Read `CLAUDE.md`, then D54 and D29 in `design/decisions.md`, then `design/rigs-and-looks.md` (the plan: section 4 is the scene structure, section 5 the hash, section 10 what it touches, section 11 the phases). Delete this file when phase 3 is committed.

## Where things stand

Branch `rigs-and-looks-phase2`, three commits on top of `main` (`eda12bc`), not pushed, not merged:

| Commit | What |
|---|---|
| `0a55b0b` | D54 and its plan. Phase 2: lowercase asset ids and `rigName`, `core/SkeletonFile`, `core/Hash64`, `saveRigSkeleton` |
| `abc80d8` | The FkChainModule rename (D53), the author's change |
| `5877510` | `design/control-node-perf-handoff.md`, unrelated to this work |

The build is clean and the suite is **TOTAL 1030 passed** at `abc80d8` (the third commit is text only).

**Built and usable now:**
- `AssetId::isValidName` (`core/AssetId`): the lowercase name rule. Use it for every rig, skeleton and look name.
- `skeletonHash(const Skeleton&)` and `SkeletonScene::readSkeleton(joints, outSkeleton, report)`: the hash of a set of marked joints as they are now. Phase 3's stamp calls these on the build's input joints. Nothing reads a skeleton file back yet.
- `Hash64::format` and `Hash64::parse`: the 16-hex-digit text for any stored hash.
- `CommandUtils::readJoints`: a command's joint names as paths.

## Phase 3 scope (from the plan, with what the code looks like today)

One change, with its tests and docs. Per `CLAUDE.md`, propose the design and get sign-off before writing code: this phase removes a node type and renames public keys.

1. **One rig per namespace.** `RigRoot.cpp`'s read phase (`findAssetRoot`, from line 256) fails today only when a rig root with the same `rigName` is already under the namespace's asset root. It should fail when the namespace holds any rig root.
2. **Merge the asset root into the rig root** (author, 2026-11-16).
   - The rig root gets `assetId` (the name is already in `RigAttr`). `assetRootModule` goes: `maya/nodes/AssetRoot.h/.cpp`, its `ModuleRegistry` entry, the `assetRoot.children → rigRoot.assetRoot` connection, `RigTraversal::getAssetRoot`, the "reuse the namespace's asset root" step, and the vcxproj and filters entries.
   - Its `MTypeId` 0x00219 is retired, not reused. Say so in `CLAUDE.md`'s id list.
   - Files that mention it now: `core/Constants.h`, `RigInfoCmd.cpp`, `ModuleRegistry.cpp`, `RigRoot.h/.cpp`, `RigDataFileScene.cpp`, `RigTraversal.h/.cpp`, `python/rigmodules/query.py`, and the tests `test_create_rig.py` (class `AssetRoots`), `test_rig_info.py`, `test_plugin.py`, `rigtest/config.py` (`RIG_NODE_TYPES`).
   - Open point to settle with the author: a scene saved with an `assetRootModule` node. No rigs need to survive (memory: personal project), so the likely answer is no migration, but ask.
3. **Default namespace `{asset name}_{rig}`.** Today `CreateRigCmd.cpp` defaults to `AssetId::name(assetId)` before the template is read, so the rig name isn't known at that point. The default has to move into `createRig`'s read phase, after `loadRigTemplate`. The suite's `config.ASSET_ID` is `tests\rig` with namespace `rig`, and almost every test resolves names in `rig:`, so decide first how the tests keep a stable namespace (pass `namespace=` in `RigTestCase.build`, most likely). The README and `docs/commands.md` examples change too (`hero:` becomes `hero_<rig>:`), with their tests in `test_docs_examples.py` (`DOC_NAMESPACE`).
4. **`skeletonHash` on the rig root.** A new string attribute (name in `RigAttr`), set by the build from `SkeletonScene::readSkeleton` on `inputs.joints` and `skeletonHash`. Read the joints in the read phase, before anything moves them. The joints must all be marked already, so `readSkeleton`'s checks repeat the build's own: reuse the `MarkedJointSet` the build has if that is cleaner than reading markup twice.
5. **`getRigInfo` and `RigInfo`.** The JSON's `variant` key (`RigAttr::Variant`) becomes `rig`, the asset root key goes, and `skeletonHash` is added. `rigmodules.query.RigInfo.variant` becomes `rig`, and `asset_root` goes.
6. **Library file `meta` (D51).** `variant` becomes `rig`, and the caller-passed `skeletonId` becomes `skeleton` (still the caller's) plus `skeletonHash` (filled by the rig, in `RigDataFileScene::fillRigMeta`). Pose and ControlAnim formats go to version 3 with an update step each (`poseUpdateTable`, `controlAnimUpdateTable`, D33). `core/LibraryMeta` holds the field table.
7. **Weight files (D35).** Add `skeletonHash` next to `topologyHash`, written at save. The format has no update table yet (`SkinWeightFileFormat::CurrentVersion` is 1 and the reader accepts only that), so version 2 needs the table added, as `ControlShapeFile` has. Decide with the author whether a load checks the hash now or only records it: the plan puts the check with the look swap (phase 5).

Not in phase 3: the disk layout (phase 4, mayadev), the look node and swap (phase 5), attachments (phase 7).

## mayadev call sites this breaks

Search again before changing anything (`py/scripts`, `toolsets`, `tests` in `C:\Users\bmorgan\Documents\dev\maya\mayadev`). Found on 2026-11-16, mayadev on `main`:

- `RigInfo.variant`: `bmaya/maya/assettools.py` (lines 126, 157 to 161, 177 to 195, 205, 286 to 303), `bmaya/maya_ui/rigpanel.py` (151, 158), `tests/asset/test_maya_asset_tools.py` (161).
- `skeletonId` in `meta`: `assettools.py` (206, where it passes it), `bmaya/core/asset/library.py` (373), `bmaya/maya_ui/librarybrowser.py` (440), and tests `test_maya_asset_handlers.py`, `test_maya_asset_tools.py`, `test_maya_library_session.py`, `test_asset_library.py`.
- Nothing in mayadev was found reading `asset_root`, but check again.
- Still open from phase 2: mayadev's `bmaya.core.asset.assetid` accepts uppercase and compares with `casefold` (`docs/mayadev.md`, "Known mismatches").

List the breaks in the change summary and propose the mayadev edits to the author. Do not make them without a go-ahead.

## Things that cost time in phase 2

- **The suite needs the build that contains the change**, and a filtered run (`RigTests.cmd skeleton`) is quick for iterating. Run the full suite before finishing.
- **Line endings are mixed across the repo.** Many `.md`, `.h` and `.cpp` files are CRLF, and some are LF. The Edit tool keeps a file's endings. A Python or `sed` rewrite must open with `newline=""` and write the file's own ending, or `file <path>` will show "CRLF, LF".
- **New `.h`/`.cpp` files go in both `cpp/RigModules.vcxproj` and `.filters`.** A removed file comes out of both.
- **A new test file needs a line in `TIERS`** in `tests/run_tests.py`, or the run fails.
- **Doc examples are tested.** `test_docs_examples.py` reads the README and docs pages, so changing an example or a namespace there changes a test.
- **The author's test asset** (`C:\Users\bmorgan\Documents\Art\Maya\Projects\test\character\biped\variants\human\template.json`) still has `"rigName": "Human"`, which the build now rejects. It is the author's data: ask before editing it. `tools/perf/perf_scenes.py human` fails until it is lowercased.
- **Committing next to the author's uncommitted work:** build the commit in a temporary index (`GIT_INDEX_FILE`), then `git reset -q -- <paths>` in the real one. The working tree was clean at the end of phase 2, so this may not be needed.

## Done looks like

- The design for items 1 to 7 signed off, with the two open points answered (old scenes with an asset root, and whether a weight load checks the hash).
- Build clean and the full suite passing, with tests for: a second rig in a namespace failing, the default namespace, the `skeletonHash` stamp matching `saveRigSkeleton` on the same joints, the `getRigInfo` shape, and the version 2 to 3 update of a pose and a ControlAnim file.
- Docs pass: `commands.md`, `python.md`, `library.md`, `skinning.md`, `architecture.md`, `mayadev.md`, the README quick start, `CLAUDE.md` (repo map, node ids, commands), `asset-model.md`, D54's built notes and the plan's phase list.
- The summary lists every mayadev call site the change breaks.
