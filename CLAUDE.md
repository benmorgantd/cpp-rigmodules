# CLAUDE.md: cpp-rigmodules

Maya 2026+ C++ rigging plugin with a Python front end. Repo: https://github.com/benmorgantd/cpp-rigmodules

The author is a senior rigging technical artist (AAA background, advanced Maya, Python and C#). This is their first large C++ project. Skip basic rigging and Maya explanations. Explain C++ idioms only when a choice is non-obvious. Be direct, and avoid clichés and flattery.

Goal: excellent, efficient, scalable rig modules for Autodesk Maya 2026+, synthesizing the author's lessons from complex rigging systems at large AAA studios. Emphasize planning before writing code.

The rigmodules PDF (`design/Rigging System Planning.pdf`) holds the author's design instructions, and the repo holds the actual code. The PDF may be slightly out of date. When it disagrees with the code, the code wins. Flag the discrepancy instead of silently picking one.

Design decisions and their history are in `design/decisions.md`. Read it before proposing architectural changes.

Documentation has two homes (D21). `design/` is for people working on the system: why it is built the way it is. `README.md` and `docs/` are for people using it: what it does now. The process is in `docs/README.md`.

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
- Existing modules include Layout, FkChain, Aim, TwoBoneIk and Ribbon. Use FkChain and Layout as the reference for new modules. TwoBoneIk is the reference for a module with custom control channels, marker joints, an internal child plug (`ModulePlugInfo::followsPlug`) and a module-owned space.
- Mirroring is template data (D28): a `"mirror": true` entry is copied to the other side in the read phase, so modules never see it. A module whose arguments change on the other side (a negated default, say) handles it in `planFromJson` through `ctx.moduleData.isMirror()`, never with a mirror branch in `createModule`.
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


## Reuse and complexity control

Complexity grows through duplication and one-off special cases. Resist both.

### Look before you write

1. Before writing any helper, search `maya/shared/Utilities.h`, `maya/shared/RigModule.h` and `core/` (and their implementations) for existing functionality.
2. If something close exists, **reuse it**. If it nearly fits, **make a small, backward-compatible edit** (new overload, optional parameter, clearer name) instead of writing a parallel version.
3. Only write a new shared function when nothing close exists. Put it in the right shared location, not inside a module.
4. When you reuse or edit a shared function, check its other callers so you do not change their behavior.
5. In your change summary, state what existing code you reused or extended and why a new function was needed, if one was added.
6. **Shareable helpers are shared when written, not after their third copy** (author, 2026-11-09). A helper that isn't about one feature's own concepts goes straight into the shared layer, even with one caller: finding nodes, joints or meshes, namespaces, DAG and history walks, components, file IO, flag parsing, report display. It goes in `maya/shared/Utilities.h` under the class for its topic (`DagUtils`, `MeshUtils`, `CommandUtils`, ...), or in the shared file that owns the topic (`JointMarkupScene` for joints by markup, `RigTraversal` for walking a rig), or `core/` if it needs no Maya. Only logic about the feature itself (a skinCluster's undo state, a module's math) stays file-local. Before finishing, list every anonymous-namespace helper the change added and say why each one isn't shareable.

### Structural practices

- **Rule of three, for feature logic only.** Logic about one feature may be duplicated twice before it is extracted. Scene and utility helpers never are: see step 6 above. Do not invent shared APIs nobody calls.
- **One home per concept.** Each piece of logic, constant, and string lives in exactly one place. Shared code goes in the shared layer. Module-specific code stays in the module.
- **Modules stay thin.** A module declares its attributes, child plugs, and sockets, then delegates to shared code for common behavior. If two modules share behavior, move it up to `maya/shared/RigModule.h` or `maya/shared/Utilities.h` (or `core/` when it needs no Maya) instead of copying.
- **Prefer composition and data over inheritance and branching.** Avoid deep class hierarchies and long `if (moduleType == ...)` chains. Extend through shared helpers and the `ModuleRegistry`.
- **Consistent patterns.** A new module should look like the existing ones (FkChain, Layout). If you need to break the pattern, say why. If the pattern itself is wrong, propose changing it everywhere rather than diverging once.
- **Small, single-purpose functions** with clear inputs and outputs. Avoid functions that need many flags to behave differently.
- **Minimize surface area.** Keep things private or file-local by default. Every new public function, attribute, or JSON key is a long-term commitment.
- **No dead or speculative code.** No unused parameters, "just in case" options, or commented-out blocks. Delete what is not used.
- **Avoid new dependencies** unless they clearly pay for themselves. Ask first.
- **Document why, not what.** Comment non-obvious decisions, invariants, and Maya quirks. Do not narrate obvious code.
- **Refactor with the change, not after.** If adding a feature exposes duplication or awkward structure, fix the structure in the same effort or flag it explicitly. Do not leave it for "later."

### Complexity checks

When a change adds notable complexity, call it out and justify it: a new shared function, a new attribute on an existing module, a new JSON key, or a new special case. If a simpler path exists, take it or present it as an alternative.

## Code conventions

### Formatting
Every curly bracket goes on its own new line (Allman), with no exceptions. This includes functions, control flow, namespaces, classes, lambdas, empty bodies (`X::X()` then `{` and `}` on their own lines), and multi-line initializer blocks. A braced init list that fits on one line is a value, not a block, and stays inline: `int v{ 1 }`, `foo(a, {}, b)`, `{ attr, 0.0 }`. `test_source_layout.py` checks this.

Indentation is tabs. `cpp/.clang-format` encodes these rules (Allman, tabs, a parameter list that doesn't fit on one line goes one parameter per line, sorted includes).

**Includes** are grouped in blank-line-separated blocks: the file's own header first, then project headers, the standard library and Maya, in whatever groups the file already uses. Each block is sorted alphabetically, ignoring case. `test_source_layout.py` checks the order. Select the include lines and run Format Selection to sort them. Do not run it over whole files yet: it would also re-align hand-formatted code. Use it on new code or a selection.

```cpp
if (status != MS::kSuccess)
{
    return status;
}
```

### Naming

| Kind | Convention | Example |
|---|---|---|
| `MObject` | `o` prefix | `oMyName` |
| `MPlug` | `p` prefix | `pMyPlug` |
| `MFn*` function sets | `fn` prefix | `fnMyMfn` |
| Module attributes | `a` prefix | `aMyAttributeName` |
| Module files and classes | `<Name>Module.h/.cpp`, class `<Name>ModuleNode` (D53). The template `moduleType` stays `<Name>`. | `AimModule.cpp`, `AimModuleNode`, `"Aim"` |

### Command flags

Use long flag names everywhere: in code that reads flags, in comments, in docs and in examples (`-usage`, never `-us`). Short names get confusing when reading code. `MSyntax::addFlag` still requires a short name, so short names appear only inside `newSyntax()` and nowhere else. Keep long flag names in file-local constants and read them through those constants.

**Command examples are Python `maya.cmds`, never MEL.** This applies to docs, `design/decisions.md`, comments, commit messages and replies to the author: `cmds.rigJointMarkup("skel:arm_jnt", usage="arm", side="Left", export=True, skin=True)`, not `rigJointMarkup -usage "arm" ...`. Keywords are the long flag names, and a multi-use flag takes a list. Flag tables name the keyword (`usage`), without the leading `-`.

**No import aliases in Python.** Write `import bmaya.core.asset` and call `bmaya.core.asset.Asset.create(...)`, never `import bmaya.core.asset as ba`. The full path says where each name lives. The one exception is `import maya.cmds as cmds`, the established convention. This applies to code, tests, docs and replies.

### Maya API rules

- **Pass lvalue MObjects to non-const `MObject&` parameters.** `MPlug::child(MObject& attr, MStatus*)` does not accept a temporary, so `pElement.child(fnModule.attribute("name"))` fails with E0304. Look the attribute up on its own line into a named `oAttrName` variable, then pass that variable. This applies to any Maya API function taking a non-const `MObject&`, including the result of `fnNode.attribute(...)` and `MFnAttribute::create(...)`. Check the header signature before nesting a call as an argument.
- Always capture and check `MStatus`. Use the project's existing error-handling macros or patterns rather than inventing new ones.
- In `initialize()`, every attribute needs create, add, and `attributeAffects` handled together. Missing `attributeAffects` is a common silent bug, so check it on every new attribute.
- In `compute()`, group plug reads and writes by purpose and avoid re-querying the same plug.
- Keep `compute()` free of allocations and string lookups. Resolve plugs and attributes outside the hot path where possible.

## Structure: Maya-free core plus thin plugin layer

Studio-style split, matching how the author structures their Python code. The folders exist (D13); the rest of the split is still planned.

- **Core (`core/`, no Maya headers):** JSON parsing and validation, joint markup, shared constants. Still to move here: child plug and socket resolution, `parentStructure` ordering, matrix math. Meant to be testable with GoogleTest and no Maya install (no harness yet).
- **Plugin layer (`maya/`, Maya API):** nodes, attribute definitions, plug I/O, and the commands that bridge to the core.
- **Python (started, D30):** a thin package in `python/rigmodules`. Built: the rig query (`get_rig_info`, `get_rigs`), `template.get_rig_name` and `skinweights`. Planned: a build wrapper, the rest of the rig query API (modules, controls, sockets, rest values), template validation passed through to the core, shape IO and pose data read and write, and user-facing error messages. Placement rule: Python goes here only if it would have to change when a RigModules node, attribute, flag or template key changes. It imports no Qt or `bmaya` and reads no environment variables or asset paths (D24).

## Sibling repo: mayadev

The author's Python pipeline lives in a separate repo: https://github.com/benmorgantd/mayadev, local checkout `C:\Users\bmorgan\Documents\dev\maya\mayadev`, package `bmaya` under `py/scripts/bmaya`. The author uses it for the asset file model, the menus and tools, the active rig, skinning, animation and GUI tools. Read `design/mayadev-integration.md` before touching either side of the boundary. The boundary is decided (D24).

**mayadev code is visible from here.** Its documentation is in its own `docs/` folder (`docs/README.md` there maps the pages; `docs/rigmodules.md` is the list of every command and `rigmodules` function it calls, and `docs/status.md` its known problems). `docs/mayadev.md` in this repo is the reverse view for someone changing RigModules. In a cloud session `benmorgantd/mayadev` is in the repository scope: attach it with `add_repo` when it isn't cloned, and read it there. **Before renaming, removing or reshaping a command, flag, return value or `rigmodules` function, search `py/scripts`, `toolsets` and `tests` in mayadev for it**, and list the call sites it breaks in the change summary. The menu tools in `toolsets/master_tool_registry.json` are a module, a function and keyword arguments (D47), so a `rigmodules` function named directly there is checked by mayadev's `check_calls`. A command called inside a mayadev function is not, so a name search is still the only check for it. Propose mayadev edits to the author instead of making them.

- **Dependency direction:** mayadev depends on RigModules, never the reverse. mayadev tools reach rig internals only through the RigModules Python query API, never by walking node connections or parsing names.
- **Legacy rig builder:** mayadev has its own builder (`maya/rigging/base/` with `Rig`, `Module`, `Layout`, plus `maya/rigging/modules/`). It uses cmds constraints, DAG offset groups, name-based joints and `L/R/M` sides. It is deleted on mayadev main (branch `asset-tools`, merged), along with `rigManager` and the legacy `bmaya.maya.asset`. The tag `legacy-rig-builder` keeps it, and an untracked copy sits in mayadev's ignored `legacy_reference/` folder for reading. mayadev stays the pipeline repo, and its asset tools (`bmaya.maya.assettools`) use `rigmodules.query` (D30). No rigs built with it need a migration path. Do not extend it, port its code, or copy its conventions into RigModules. Read its modules only as a list of animator-facing requirements when a new module goes through the gate.
- **What mayadev holds now:** `bmaya.core.asset` (Maya-free: asset ids of any depth with the same rules as `core/AssetId` here, `project.json`, `asset.json`, skeletons, rigs and looks (D54), the registry, publishes, local versions, file handlers: D29), `bmaya.maya.assethandlers` and `assettools` (the only code that calls RigModules, with `activerig` and `posetools`; menu entry points are `*_interactive` and ask through a `Prompter`, D49), the rest of `core/` (plain Python: log, progress manager, `Event`, prompts, tool registry, batch framework), `maya/startup.py` and `maya_ui/startup.py` (the registration passes, D49), `maya/eventsregistry.py`, `maya_ui/` (Qt base classes, the Qt prompter and forms, the editors), the tool menus (`toolsets/`), and older skinning, joint and utility code in `maya/rigging/` and `maya/util/` that is not yet on the query API. The D49 `ui-phase1` branch is merged into mayadev `main`. D54 phase 4 (skeletons, rigs and looks: `Asset.skeletons`, `rigs`, `looks`, `rig.json` and `look.json`) is on mayadev branch `rigs-and-looks-phase4`. The attachment side of D55 (`rig.json` `parent`, `build_rig` with a parent, `apply_look` for a look that spans several skeletons) is on mayadev branch `attachments-phase8`, uncommitted.
- **Same words, different meanings:** both repos have `Layout`, `FkChain` and `LayoutRig.json`. In this repo they always mean the RigModules ones. Call mayadev's versions "legacy".
- mayadev is a different repo with its own conventions. This file's rules (Allman, docs pass, suite) apply only to this repo. Changes to mayadev need the author's go-ahead and a commit in that repo.

Rule: if logic can live in the core, it goes in `core/`. The plugin layer should be thin adapters. A new file goes in `core/` when it needs no Maya header, including through what it includes.

## Environments

- **Cloud sessions** cannot run or build against Maya (license, devkit, restricted network). In the cloud, work on the Maya-free core, tests, schema and fixtures, planning docs, Python tooling, and convention audits. Never state that Maya-dependent code works.
- **Local sessions** can build against the devkit and run `mayapy` or `maya -batch` against test JSON. Verification of `compute()` behavior, plug evaluation, and undo/redo happens here.
- Any change touching Maya-dependent code should end with a note: "Needs local build and Maya verification."
- Stub-based syntax checks are a lint only. Stubs drift from real signatures.

### Local build and test (Windows)

- **Visual Studio project:** `cpp\RigModules.vcxproj`. The devkit path comes from the `MAYA_DEVKIT_LOCATION` env var (`C:\Users\bmorgan\Documents\dev\maya\devkit\devkitBase`).
- **Build** (from the repo root): `& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" cpp\RigModules.vcxproj /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal`
  - Output: `cpp\x64\Release\RigModules.mll`. That is the runner's default `--plugin`.
  - **Then always run the tests**, even for a change that looks unrelated. Run `cmd /c tests\RigTests.cmd`, or the suite at the end of your own mayapy script (see Tests below).
  - Warnings that already existed and can be ignored: nlohmann C5260, `maya/shared/Utilities.cpp` C4018.
- **Run Maya headless:** `C:\Program Files\Autodesk\Maya2026\bin\mayapy.exe script.py`. Start the script with `maya.standalone.initialize(name="python")`, then `cmds.loadPlugin(path)`.
- **No crash handler windows from your own scripts.** Set `MAYA_DISABLE_CER=1` before running mayapy or maya.exe yourself, and end a standalone script with `maya.standalone.uninitialize()` then `os._exit(0)`, as `tests/run_tests.py` does (Maya 2026 crashes at process exit without it). After a run that crashed, close any `senddmp.exe` or `cer_dialog.exe` it left open.
- **Viewport (VP2) checks need a GUI session.** mayapy, `mayabatch` and `ogsRender` don't draw UI drawables or curves, so a draw override can't be checked there. After changing control drawing or shapes, run `cmd /c tests\ViewportGallery.cmd` and look at the images (see `tests/README.md`). It starts `maya.exe -command` with a one-line `python(...)` bootstrap (see the next bullet for how to write one). The script runs its work through `maya.utils.executeDeferred` (so the UI exists), sets the camera on `cmds.playblast(activeEditor=True)` (not the first model panel, which playblast may not capture), playblasts with `offScreen=True`, then calls `cmds.quit(force=True)`. `exec` gives it no `__file__`, so paths come through env vars.
- **Starting a script in a GUI Maya: use `mayalaunch.run_gui(script, env, timeout)` (D48), which applies every rule below.** Never write the `-command` string by hand. The rules are kept here because they explain what it does. `-command` takes MEL, so MEL parses the string before Python sees it.
  - `-command` takes MEL, so `python("...")` is a MEL string literal first. Rules, with the failure each one caused here:
  - **A backslash in a MEL string is an escape.** `\n`, `\t`, `\r`, `\"` and `\\` are escapes, and any other `\x` is mangled or dropped. `python("exec(open('C:\Users\me\x.py').read())")` reached Python as `C:Users` plus a stray control character (`Invalid argument`), and the script never ran. Never put a Windows path in the string.
  - **Only the double quote delimits a MEL string.** A single quote is plain text to MEL, so Python's `'...'` survives. A literal double quote inside needs `\"`, and a literal backslash needs `\\`. Avoid both: use single quotes in the Python and no backslashes at all.
  - **Pass paths through environment variables, not the command string.** Set `PERF_SCRIPT` (and the plugin and output paths) before launching, and bootstrap with `python("exec(open(__import__('os').environ['PERF_SCRIPT'], encoding='utf-8').read())")`. The string then has no path, no backslash and no escaped quote. Forward slashes also work, but the variable removes the question.
  - **Keep the bootstrap free of spaces when launching from PowerShell.** `Start-Process -ArgumentList` joins the array with spaces and adds no quotes, so a space inside the `python(...)` string splits it into two arguments and MEL reports `Unterminated string` (seen here at `environ['X'], encoding=...`). Write `open(p,encoding='utf-8')`, not `open(p, encoding='utf-8')`.
  - **From PowerShell, `Start-Process -ArgumentList` hands the string to Windows unquoted.** The inner double quotes must reach the process as `\"`, which in a PowerShell double-quoted string is a backslash, a backtick, then the quote. Check the result in `Get-CimInstance Win32_Process` `CommandLine` before waiting on a run.
  - **Write the script as UTF-8 without a BOM.** Windows PowerShell 5.1's `Set-Content -Encoding utf8` and `Out-File` add a BOM, and Python's default `open()` then fails with `invalid character '»' (U+00BB)`. Write scripts with the Edit and Write tools, or `[IO.File]::WriteAllText(path, text, (New-Object System.Text.UTF8Encoding($false)))`, and open them with `encoding='utf-8'`.
  - **A bootstrap that fails does not exit.** Maya stays open on `Error: ...` in the script editor, so a runner that waits for exit waits out its whole timeout. Give every run a short timeout (a few minutes), have the script write a first log line at the top, and look for that line within a minute of launching.
  - **Stop only the processes you started, by PID.** Never kill `maya.exe` by name or start time: the author's own Maya may be open.
- **Copy the .mll before loading it.** A loaded plugin locks the file, so a Maya session that has it loaded makes the next build fail. Copy the .mll to the scratchpad and load the copy.
- **Reading results in standalone:**
  - `MGlobal::displayError` text surfaces as the `RuntimeError` message from the `cmds` call.
  - Not enough command objects raises `TypeError`.
  - The author's `userSetup.py` is mayadev's custom startup (logging, tool registry, menus), and it's wanted. It prints startup messages in mayapy, so filter them out of results rather than disabling it. The suite skips it on purpose (`MAYA_SKIP_USERSETUP_PY=1`), so tests see the plugin alone.
  - Turn undo on with `cmds.undoInfo(state=True, infinity=True)` before testing undo/redo.
- **Line endings:** source files are CRLF. Git Bash `sed -i` rewrites a file as LF, so prefer the Edit tool. Otherwise run `unix2dos` afterwards.
- **Locked files:** `maya/nodes/RigRoot.cpp` and other files may be open and locked in Visual Studio, so writing them from a Python script fails with a permission error. The Edit tool still works.
- **Short flag names:** `MSyntax::addFlag` rejects short names longer than 3 letters, and the flag then silently never registers. Wrap each `addFlag` in `CHECK_MSTATUS`.
- **Tests:** run them after every build, in one of two ways:
  - **Already running a mayapy script to check a change:** end that script with the suite instead of starting Maya again (about 4 s, no second startup). It must be the last thing the script does, because every test starts with `file -new`. It has no crash protection, so if Maya crashes, rerun with `RigTests.cmd` to find the test.
    ```python
    sys.path.insert(0, r"C:\Users\bmorgan\Documents\dev\maya\cpp\repos\RigModules\RigModules\tests")
    import run_tests
    ok = run_tests.run_in_current_maya()
    ```
  - **Otherwise:** run `cmd /c tests\RigTests.cmd`. It runs 1207 tests in 6 tiers in one watched mayapy process (stopped by `--timeout`, default 900 s), plus the files in `PLAIN_PYTHON` (no plugin, no scene) in a plain Python process beside it, in about 35 s. `--durations` shows where the time went, and reports crashes with the test that caused them.
  - **Reading results:** both stop at the first failed tier, so fix the earliest failure first.
  - **Adding tests:**
    - Every new test file must be assigned a tier in `TIERS` in `tests/run_tests.py`.
    - Follow `tests/README.md`.
    - Write pose expectations by hand. Never record them from the rig.
  - **Filtering:** pass a filter to run part of the suite, for example `RigTests.cmd fkchain`. Run the full suite before calling a change done.
  - **Not covered:** there is no GoogleTest harness for the Maya-free core yet (D20).

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
