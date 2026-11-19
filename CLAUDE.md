# CLAUDE.md: cpp-rigmodules

Maya 2026+ C++ rigging plugin with a Python front end. Repo: https://github.com/benmorgantd/cpp-rigmodules

The author is a senior rigging technical artist (AAA background, advanced Maya, Python and C#). This is their first large C++ project. Skip basic rigging and Maya explanations. Explain C++ idioms only when a choice is non-obvious. Be direct, and avoid clichés and flattery.

Goal: excellent, efficient, scalable rig modules for Autodesk Maya 2026+, synthesizing the author's lessons from complex rigging systems at large AAA studios. Emphasize planning before writing code.

The rigmodules PDF (`design/Rigging System Planning.pdf`) holds the author's design instructions, and the repo holds the actual code. The PDF may be slightly out of date. When it disagrees with the code, the code wins. Flag the discrepancy instead of silently picking one.

Design decisions and their history are in `design/decisions.md`. Before proposing architectural changes, read its Index (one line per decision) and then the entries for the area you are changing. Do not read the whole file: it is about 200 KB.

Documentation has two homes (D21). `design/` is for people working on the system: why it is built the way it is. `README.md` and `docs/` are for people using it: what it does now. The process is in `docs/README.md`.

## Where the instructions live

This file is what every session loads, so it holds only what every task needs. The rest is split so it loads when it applies:

- `.claude/rules/cpp.md` loads for `cpp/`: reuse, formatting, naming, Maya API rules.
- `.claude/rules/mayadev-boundary.md` loads for commands, `python/` and the mayadev pages.
- `.claude/agents/`: `maya-runner` (build and suite; the full procedure is in its file), `docs-auditor` (the docs pass on a diff), `convention-reviewer` (the review checklist on a diff, not a bug hunt) and `mayadev-callsites` (the call-site search). Agents start with a clean context, so give each a self-contained brief with the diff range or file list.
- `docs/source-map.md` (per-file reference), `design/README.md` (which design page to read and its state), `docs/architecture.md` are read on demand.

## Working style

- Plan before code. For any new module or architectural change, propose the design first (inputs, outputs, child plugs, sockets, attributes, affects relationships, failure modes) and wait for sign-off before writing implementation.
- When a design decision is made, changed, or reversed, add or update its entry in `design/decisions.md` in the same change.
- **Docs change with the code, in the same change.** Every change ends with a docs pass:
  1. List what the change touches that a user or maintainer can see: a command or flag, a template key, a module, its `moduleArgs`, child plugs, sockets or controls, a validation rule, a build or test step, the repo layout.
  2. Update the owning page for each one (`docs/README.md` maps topics to pages): `README.md`, a `docs/` page, `tests/README.md`, or this file.
  3. If something new has no owning page (a new module, command, workflow or tool), add it to the existing page that fits. If none fits, write a new page and add it to the table in `docs/README.md` and the README's documentation list. A new module or command is never done without its docs.
  4. Search the docs for the old names and behaviour and fix every stale mention, including examples.
  5. A new or changed worked example gets a test in `tests/maya/concepts/test_docs_examples.py`.
  6. In the change summary, list the docs updated or added, or say why none were needed.
- Prefer small, reviewable diffs. Do not refactor unrelated code in the same change.
- **Commit messages are documentation.** Write them more fully than the repo's older one-line history. Use an imperative summary line under about 72 characters, a blank line, then bullet points that cover:
  - what changed, grouped by area (core, plugin, tests, docs, tools)
  - why, including the alternative that was rejected when there was a real choice, and the decision id (`D26`)
  - what is public API or user-visible: renamed or added names, changed shapes or defaults, and the impact on saved poses, animation, shape files and exports
  - what was verified (the build and the suite TOTAL line) and what was not, such as the viewport gallery or a GUI check
- If a requirement is ambiguous, ask. Do not guess at rig behavior.
- Never claim code compiles or behaves correctly in Maya unless you actually built and ran it. Say what was verified and what was not.
- **Every build is followed by the full test suite** (see "Tests" under Local build and test). A change is not done until the suite passes on the build that contains it. Report the TOTAL line, and report any tier that failed, crashed or was blocked.
  - **Do not rerun the suite when it already passed recently** (author, 2026-11-18): if it passed on the current build and no source, test or build file has changed since, say so and cite that run's TOTAL line instead of running it again. This includes before a commit.
- **Benchmarks (`bench`, the fps and floor checks in `tools/perf/`) are slow and are not the test suite.** Run them only (a) once when a new module or constraint type is created, and (b) when the author asks (author, 2026-11-18: never before a commit unless asked). Never after each build or each change: the mayapy suite is the per-change check. For a change that could affect evaluation cost, say so in the summary instead of benchmarking, and offer to run it.
- **Verification lives in the suite, not in temp scripts.** Every change adds or updates tests in `tests/`. Throwaway mayapy probes are fine while exploring. Before finishing, either turn each check a probe made into a suite test, or list it in your summary as a proposed test (what it checks, which tier, data case or code) for the author to accept. Never leave a check only in the scratchpad.

## Product context and priorities

This is a full animation and export pipeline, not only a rig builder. Animators are the primary users of what this system produces.

Priority order when trade-offs arise:
1. **Animator experience and preference.** Predictable controls, behavior animators expect, nothing surprising when they key, pose, or mirror.
2. **Simplicity.** Fewer concepts, fewer attributes, fewer special cases. If a design needs a long explanation, look for a simpler one.
3. **Stability and maintainability.** Rigs and animation must keep working as the system grows.
4. **Cleverness and flexibility.** Last. Do not add generality nobody has asked for.

Design every module with likely future needs in mind, even when they are not built yet:
- Reanimation and retargeting (animation data must survive rig changes and re-sourcing)
- Export (game-engine skeletons, baked animation, clean joint and control separation)
- Pose libraries and animation libraries (stable, predictable control and attribute names; data that can be saved, loaded, and mirrored)
- Rig versioning and migration (an older rig or older animation should have a defined path forward)

Name stability matters: renaming a control, attribute, child plug, or socket can break saved poses, animation, and exports. Treat names as public API.

### Gate for new modules

Do not add a new module casually. Before proposing one:
1. State the animator-facing problem it solves.
2. Check whether an existing module, or a small extension of one, already covers it.
3. Write the design (inputs, outputs, child plugs, sockets, attributes, JSON schema impact, export impact, pose/animation library impact, tests) and get sign-off before implementing. The tests are a `ModuleContract` subclass, pose cases and a golden snapshot (see `tests/README.md`).
4. Name what it adds to the long-term maintenance surface.
5. Add many instances of it to the complex performance rig (`tools/perf/perf_scenes.py`, `chain_specs`, `aim_specs` and `limb_specs`) in the same change, and run `bench` on it once, as part of creating the module (a manual run, never part of the suite: D36). The target (D36, amended 2026-11-11) is 30 fps (1:1 playback) for 20 of the complex rig in parallel evaluation, and on the 50-rig scene a cost within 1.5 times the native floor. The same goes for a new constraint type. Report the fps, and say so when it is below the target.

## Architecture

- Custom module nodes are the rig's brain. All transform math happens inside the nodes. `inheritsTransform` is OFF for every control and joint, so DAG parenting does not drive transforms. Never rely on DAG inheritance for results.
- Joints are the main input. A JSON file is the rig template and the main user input. It is read in C++ with nlohmann json. A simple Python command builds the rig from that JSON.
- Module inputs are control world-space matrices and their parent world-space matrices.
- Modules define **child plugs** (child-side attachment inputs) and **sockets** (parent-side attachment outputs). Sockets are world-space matrices, and child plugs are the inputs those matrices connect to. Both are named by the module at creation (for example `start`, `end`).
- The rig is built in two passes:
  1. Create every module separately.
  2. Parent them as defined by the JSON `parentStructure`, which maps child plug to parent socket.
- **Limb is coupled to TwoBoneIk and Ribbon (D62, `design/limb.md`).** It is not a subclass, but it runs their solves, channels, control derivation and inverses. Changing the IK/FK solve, the ribbon solve, `core/RibbonMath`, `core/SwingTwist` or either module's channels changes Limb. Read `design/limb.md` first, and say in the summary what Limb does with the change.
- Existing modules include Layout, FkChain, Aim, TwoBoneIk and Ribbon. Use FkChain and Layout as the reference for new modules. TwoBoneIk is the reference for a module with custom control channels, controls with no joint (derived from the limb joints, D57), an internal child plug (`ModulePlugInfo::followsPlug`) and a module-owned space. The Ribbon's ik controls are derived too: `controlCount` of them, spread along its bind joints, with no control joints in the skeleton.
- Mirroring is template data (D28): a `"mirror": true` entry is copied to the other side in the read phase, so modules never see it. A module whose arguments change on the other side (a negated default, say) handles it in `planFromJson` through `ctx.moduleData.isMirror()`, never with a mirror branch in `createModule`.
- Every joint, control, module and constraint is named `<camelCaseName>_##_<L|R|M>_<JNT|CTRL|MOD|CON>` (D58, `docs/naming.md`). The rules are `core/NamingConvention`: never build or parse a name with string code elsewhere. The layout module is `layout_00_M_MOD`, and a joint outside the convention fails the build. `cmds.renameRigNodes` renames into it.
- A control's animation interface is its keyable, writable channels (D34). A custom channel sets its default to its rest, follows the naming rules in D34, and declares `mirrorCopy` or `mirrorNegate` through `RigPoseScene::setMirrorMode`. `ModuleContract` checks every control's channels.
- Every control carries `controlMarkup` (side, usage), unique per rig (D25). A module's plan declares it in `ModuleInterface::controls` and passes the same values to `createRigControl`. Tools find controls through `RigTraversal`, never by name.
- Control shapes are meshes from a generated library, all fillable (D26). Rings face +X, pointers point down +X, round solids run along +X.

### Shared constants and attribute lookup

- Shared attribute names are string constants in `core/Constants.h` (namespace `RigAttr`). Node type names live in the `NodeCreateCommands` namespace.
- Module-level code uses the module's own static `MObject` attributes. Shared rigmodule code looks attributes up by name (`findAttr`) and uses string-based `findPlug`.
- Do not duplicate attribute name strings. Add them to `RigAttr`.
- Structured data in a string attribute is JSON, read and written with `JsonUtils`. Do not invent delimited formats.
- Open question: whether node creation names should live only in the `ModuleRegistry`. Do not move them without asking.

## Repo map

All C++ lives under `cpp/`, with its Visual Studio project (D23). Inside it, source is split by dependency (D13): `core/` holds code with no Maya headers, and `maya/` holds code that uses the Maya API, grouped by role (D22). **C++ paths in this file and in includes are relative to `cpp/`**: `maya/shared/RigModule.h` means `cpp/maya/shared/RigModule.h`. Tests live in `tests/` (see `tests/README.md`). Read these tables before searching the repo.

```
cpp/                 the C++ root and include root: RigModules.vcxproj + .filters, .clang-format
  core/              Maya-free, flat
  maya/              PluginMain.cpp, ModuleRegistry, CommandRegistry
    modules/         one file pair per module node (FkChain, Layout, Aim, TwoBoneIk, Ribbon)
    commands/        one file pair per MPxCommand (or per closely related set)
    nodes/           every other node: roots, constraint, control
    shared/          code used across those folders: module base, utilities, scene helpers, enums
  include/           vendored third-party headers (nlohmann json)
  x64/Release/       build output (ignored)
python/rigmodules/   the Python package that wraps the commands (D24)
RigModules.mod       Maya module file: puts python/ on PYTHONPATH
tests/               mayapy suite, plus the manual viewport gallery
tools/               developer scripts (the control shape generator)
docs/, design/       user docs, design docs
RigTemplates/        example template
```

**Where a new file goes:** no Maya header needed → `core/`. A module node → `maya/modules/`. A command → `maya/commands/`, and its node-side logic stays in the node or `shared/` (the command parses flags and delegates, D14). Any other node → `maya/nodes/`. Used by more than one of those folders → `maya/shared/`. A new folder is a layout decision: add it to `SOURCE_FOLDERS` in the layout test and to this map.

**Include rules** (checked by `tests/maya/concepts/test_source_layout.py`):
- `core/` includes only other `core/` headers (by bare name, `"Constants.h"`), the standard library and `<nlohmann/json.hpp>`. Never Maya, never `maya/`. Bare names keep `core/` self-contained if it becomes its own library.
- `maya/` includes every project header by its path from `cpp/` (`"core/Constants.h"`, `"maya/shared/RigModule.h"`), including headers in its own folder. The include line says where the header lives.
- `cpp/` (the project dir) and `cpp/include/` are on the include path. There is no separate core library yet: both folders build into the one `.mll`.

**Per-file reference:** [`docs/source-map.md`](docs/source-map.md) lists what every file in `core/` and `maya/` holds, the Python package, the `design/` pages and the tools. Read it before searching the repo. `docs/architecture.md` gives the layers and the build phases. `docs/README.md` maps every topic to its page. Every new file is added to `docs/source-map.md`, `cpp/RigModules.vcxproj` and its `.filters`.


**Node types and MTypeIds:** `rigRootModule` 0x00218, `fkChainModule` 0x00218B, `layoutModule` 0x00218C, `aimModule` 0x00218D, `rigConstraint` 0x00218E, `twoBoneIkModule` 0x00218F, `lookRoot` 0x002190, `ribbonModule` 0x002191, `rigControlNode` 0x0013B5C0. Check this list before picking an id for a new node. Retired, never reuse: 0x00219 (`assetRootModule`, merged into the rig root by D54).

Command naming: name API functions and commands in verb pairs: get/set for scene data (`getRigControls`, `setControlShape`), save/load for files this system owns (`saveRigControlShapes`, `loadRigControlShapes`), import/export for data going to or from other tools or formats, and `mirror` for copying one side onto the other (`mirrorRigControlShapes`). Every command follows the shape in D14, with `JointMarkupCmd` as the reference. The full command list is in `docs/commands.md`.


## C++ rules (summary)

The full rules are in `.claude/rules/cpp.md`, which loads when you touch `cpp/`. Read it before planning or reviewing C++ if it has not loaded. The short form:

- **Reuse first.** Search `maya/shared/Utilities.h`, `maya/shared/RigModule.h` and `core/` before writing a helper. Extend a near match backward-compatibly. A helper that isn't about one feature's own concepts goes in the shared layer when written. Name what you reused or added in the summary.
- **Structure.** One home per concept, thin modules, data over branching, no dead or speculative code, no new dependency without asking. If logic can live in `core/`, it goes there.
- **Format and names.** Allman braces (including empty bodies), tabs. `o` / `p` / `fn` / `a` prefixes for `MObject`, `MPlug`, function sets and module attributes. Module files are `<Name>Module`.
- **Maya API.** Check every `MStatus`. No temporaries for non-const `MObject&` parameters. Every attribute gets create, add and `attributeAffects`. `compute()` has no allocation and no string lookup.
- **State complexity you add** (a new shared function, attribute, JSON key or special case) and justify it.

## Conventions that apply everywhere


Use long flag names everywhere: in code that reads flags, in comments, in docs and in examples (`-usage`, never `-us`). Short names get confusing when reading code. `MSyntax::addFlag` still requires a short name, so short names appear only inside `newSyntax()` and nowhere else. Keep long flag names in file-local constants and read them through those constants.

**Command examples are Python `maya.cmds`, never MEL.** This applies to docs, `design/decisions.md`, comments, commit messages and replies to the author: `cmds.rigJointMarkup("skel:arm_00_M_JNT", usage="arm", side="Left", export=True, skin=True)`, not `rigJointMarkup -usage "arm" ...`. Keywords are the long flag names, and a multi-use flag takes a list. Flag tables name the keyword (`usage`), without the leading `-`.

**No import aliases in Python.** Write `import bmaya.core.asset` and call `bmaya.core.asset.Asset.create(...)`, never `import bmaya.core.asset as ba`. The full path says where each name lives. The one exception is `import maya.cmds as cmds`, the established convention. This applies to code, tests, docs and replies.


## Sibling repo: mayadev

The author's Python pipeline is a separate repo (`C:\Users\bmorgan\Documents\dev\maya\mayadev`, package `bmaya`). mayadev depends on RigModules, never the reverse. The full boundary, what mayadev holds and the legacy builder notes are in `.claude/rules/mayadev-boundary.md` (loads for commands, `python/` and the mayadev pages), and `design/mayadev-integration.md` and `docs/mayadev.md`.

**Before renaming, removing or reshaping a command, flag, return value or `rigmodules` function, search mayadev's `py/scripts`, `toolsets` and `tests` for it**, and list the call sites it breaks in the change summary. Propose mayadev edits to the author instead of making them. "Layout" and "FkChain" here always mean the RigModules ones.

## Environments

- **Cloud sessions** cannot run or build against Maya (license, devkit, restricted network). In the cloud, work on the Maya-free core, tests, schema and fixtures, planning docs, Python tooling, and convention audits. Never state that Maya-dependent code works.
- **Local sessions** can build against the devkit and run `mayapy` or `maya -batch` against test JSON. Verification of `compute()` behavior, plug evaluation, and undo/redo happens here.
- Any change touching Maya-dependent code should end with a note: "Needs local build and Maya verification."
- Stub-based syntax checks are a lint only. Stubs drift from real signatures.


### Build and test (Windows)

The full procedure (building, mayapy scripts, GUI Maya, crash windows, reading results) is in `.claude/agents/maya-runner.md`. Delegate build and suite runs to the `maya-runner` agent, which returns the TOTAL line and failures, or read that file before running Maya yourself. The essentials:

- **Build** (from the repo root): `& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" cpp\RigModules.vcxproj /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal`. Output is `cpp\x64\Release\RigModules.mll`.
- **Suite:** `cmd /c tests\RigTests.cmd` (pass a filter for part of it). Run it after every build. Both stop at the first failed tier, so fix the earliest failure first.
- **Never** start Maya or write a `maya.exe -command` string by hand: use `python/mayalaunch` (D48). Set `MAYA_DISABLE_CER=1` for your own mayapy runs and end them with `maya.standalone.uninitialize()` then `os._exit(0)`.
- **Build speed (D60):** compiles are parallel (`/MP`) and each of `core/` and `maya/` has a precompiled header (`CorePch.h`, `MayaPch.h`), force-included by the project. A full rebuild takes about 20 s. After editing a PCH header expect its folder to recompile.
- **Copy the .mll** before loading it, because a loaded plugin locks the file.
- **Line endings:** source files are CRLF. Prefer the Edit tool, since `sed -i` in Git Bash rewrites a file as LF.
- **Adding tests:** assign every new test file a tier in `TIERS` in `tests/run_tests.py`, follow `tests/README.md`, and write pose expectations by hand, never recorded from the rig.
- Stop only the processes you started, by PID. Never kill `maya.exe` by name: the author's own Maya may be open.

## Review checklist

Before declaring work done, check:

- [ ] Allman braces everywhere
- [ ] `o` / `p` / `fn` / `a` naming followed
- [ ] No temporaries passed to non-const `MObject&` parameters
- [ ] New attributes have create, add, and `attributeAffects`
- [ ] Attribute name strings come from `RigAttr`, not literals
- [ ] `MStatus` checked on Maya API calls
- [ ] Pure logic placed in `core/`, not the plugin layer
- [ ] Searched `maya/shared/Utilities.h`, `maya/shared/RigModule.h` and `core/` first; reused or minimally extended existing code
- [ ] Every new file-local helper is about this feature only; anything shareable went to the shared layer
- [ ] New files placed by dependency: `core/` if no Maya header is needed, otherwise `maya/`
- [ ] No duplicated logic, constants, or strings; shared code in the shared layer
- [ ] No names (controls, attributes, child plugs, sockets) changed without flagging the pose, animation, and export impact
- [ ] Animator-facing behavior is simple and predictable
- [ ] Complexity added by this change is stated and justified
- [ ] A new module or constraint type has many instances in the complex perf rig, and `bench` was run once for it. Otherwise `bench` runs only when asked, never per change or per commit
- [ ] Tests added or updated for the change, and the full suite passes on the build that contains it
- [ ] Every check a temp mayapy probe made is now a suite test, or is listed in the summary as a proposed test
- [ ] Docs pass done: every visible change is in its owning page, new topics have a page, no stale mentions remain, and the summary lists the docs touched
- [ ] Command examples use Python `maya.cmds`, not MEL
- [ ] Stated clearly what was and was not verified

## Known open items

These go stale quickly. Verify against the repo before acting on them.

- None recorded. The `compute()` review closed on 2026-11-11 (see "Open items" in `design/decisions.md`).
