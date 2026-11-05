# CLAUDE.md: cpp-rigmodules

Maya 2026+ C++ rigging plugin with a Python front end. Repo: https://github.com/benmorgantd/cpp-rigmodules

The author is a senior rigging technical artist (AAA background, advanced Maya, Python and C#). This is their first large C++ project. Skip basic rigging and Maya explanations. Explain C++ idioms only when a choice is non-obvious. Be direct, and avoid clichés and flattery.

Goal: excellent, efficient, scalable rig modules for Autodesk Maya 2026+, synthesizing the author's lessons from complex rigging systems at large AAA studios. Emphasize planning before writing code.

The rigmodules PDF (`Rigging System Planning.pdf`) holds the author's design instructions, and the repo holds the actual code. The README and the PDF may be slightly out of date. When they disagree with the code, the code wins. Flag the discrepancy instead of silently picking one.

Design decisions and their history are in `docs/decisions.md`. Read it before proposing architectural changes.

## Working style

- Plan before code. For any new module or architectural change, propose the design first (inputs, outputs, child plugs, sockets, attributes, affects relationships, failure modes) and wait for sign-off before writing implementation.
- When a design decision is made, changed, or reversed, add or update its entry in `docs/decisions.md` in the same change.
- Prefer small, reviewable diffs. Do not refactor unrelated code in the same change.
- If a requirement is ambiguous, ask. Do not guess at rig behavior.
- Never claim code compiles or behaves correctly in Maya unless you actually built and ran it. Say what was verified and what was not.

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
3. Write the design (inputs, outputs, child plugs, sockets, attributes, JSON schema impact, export impact, pose/animation library impact) and get sign-off before implementing.
4. Name what it adds to the long-term maintenance surface.

## Architecture

- Custom module nodes are the rig's brain. All transform math happens inside the nodes. `inheritsTransform` is OFF for every control and joint, so DAG parenting does not drive transforms. Never rely on DAG inheritance for results.
- Joints are the main input. A JSON file is the rig template and the main user input. It is read in C++ with nlohmann json. A simple Python command builds the rig from that JSON.
- Module inputs are control world-space matrices and their parent world-space matrices.
- Modules define **child plugs** (child-side attachment inputs) and **sockets** (parent-side attachment outputs). Sockets are world-space matrices, and child plugs are the inputs those matrices connect to. Both are named by the module at creation (for example `start`, `end`).
- The rig is built in two passes:
  1. Create every module separately.
  2. Parent them as defined by the JSON `parentStructure`, which maps child plug to parent socket.
- Existing modules include Layout, FkChain and Aim. Use FkChain and Layout as the reference for new modules.

### Shared constants and attribute lookup

- Shared attribute names are string constants in `Constants.h` (namespace `RigAttr`). Node type names live in the `NodeCreateCommands` namespace.
- Module-level code uses the module's own static `MObject` attributes. Shared rigmodule code looks attributes up by name (`findAttr`) and uses string-based `findPlug`.
- Do not duplicate attribute name strings. Add them to `RigAttr`.
- Structured data in a string attribute is JSON, read and written with `JsonUtils`. Do not invent delimited formats.
- Open question: whether node creation names should live only in the `ModuleRegistry`. Do not move them without asking.

## Repo map

Flat layout: every source file sits at the repo root, and there are no `src/` or `tests/` folders yet. Read this table before searching the repo.

| File | Contents |
|---|---|
| `PluginMain.cpp` | Plugin entry. Loops over `ModuleRegistry` and `CommandRegistry`, and registers `rigControlNode` plus its VP2 draw override. |
| `ModuleRegistry.cpp/.h` | The only file that includes every module header. Holds each node's type name, JSON `moduleType`, `MTypeId` and `planFromJson`. Add new nodes here. |
| `CommandRegistry.cpp/.h` | The only file that includes every command header. Add new commands here. |
| `RigModule.h/.cpp` | Shared module layer: `RigModuleNodeBase` (plug helpers, socket/child plug lookup, `planModules`, `buildModules` pass 1, `readModuleArgs`/`claimJoint` plan helpers), `RigModuleNode<T>`, `RigModuleWithJointsNode<T>`, `PlanContext`, `ModulePlan`, `RigModuleBuildResult`, `ModuleDescriptor`. |
| `Utilities.h/.cpp` | `MathUtils`, `DagUtils`, `DagPlugAndSocketUtils` (`setParentSocketNames` writes a socket's names as a JSON list), `StringUtils`. |
| `JsonUtils.h` | Maya-free, header-only `readJsonString` / `writeJsonString`. Every string attribute that holds structured data (socket names, joint markup) goes through these (D16). |
| `Constants.h` | `RigAttr`, `JointMarkupKey`, `PlugAndSocketNames`, `ConstraintAttr`, `SideNames`, `RigNaming`, `RigControlNodeAttr`. It has no Maya headers, because the Maya-free core includes it. Keep it that way. |
| `Colors.h` | `Colors`, the Maya `MColor` constants, split out of `Constants.h`. |
| `RigJsonStructs.h/.cpp` | Maya-free. Template structs (`RigRootData`, `RigModuleData`, constraint data) and `loadRigTemplate` (key checks, then parse). |
| `RigTemplateValidation.h/.cpp` | Maya-free core: `ValidationReport`, `ModuleInterface`, and the read-phase template checks (D17). |
| `StringUtils.h` | Maya-free, header-only `splitIdentifier` for `"module.name"`. Included by `Utilities.h`. |
| `RigRoot.h/.cpp` | `RigRootNode`, `createRig` (read phase: parse, scan markup, `planModules`, validate. Write phase: roots, `buildModules`, parenting pass, constraints) and `CreateRigCmd`. |
| `AssetRoot`, `AssetType`, `RigType`, `Side` | Root node and enum/string helpers. |
| `LayoutModule`, `FkChain`, `AimModule` | Module nodes. FkChain is the reference pattern. |
| `Constraints.h/.cpp` | `ParentConstraintNode`, `createRigParentConstraint`, `setRigConstraintSpace`. |
| `RigControlNode.h/.cpp` | Custom control transform and its draw override. |
| `JointMarkup.h/.cpp` | Maya-free core: markup schema, `JointSelector`, `JointMarkupIndex`. |
| `JointMarkupScene.h/.cpp` | Plugin side of markup: namespace joint scan, attribute read, `claimJoint`. |
| `JointMarkupCmd.h/.cpp` | `rigJointMarkup` command. |
| `RigTemplates/LayoutRig.json` | The only example template. Its joints use `[side, usage]` selectors. |
| `include/nlohmann/json.hpp` | nlohmann json 3.12, vendored. |
| `docs/decisions.md` | Decision log (D1–D19). |
| `RigModules.vcxproj` + `.filters` | Add every new `.h`/`.cpp` to both files. |

**Maya commands:** `createRigFromTemplate -template <path> [-namespace ns] <joint>...`, `rigJointMarkup`, `createRigParentConstraint -driver module.socket ... <control>`, `setRigConstraintSpace (-space n | -label l) <constraint>`. Every command follows the shape in D14, with `JointMarkupCmd` as the reference.

**Node types and MTypeIds:** `rigRootModule` 0x00218, `assetRootModule` 0x00219, `fkChainModule` 0x00218B, `layoutModule` 0x00218C, `aimModule` 0x00218D, `bmParentConstraint` 0x00218E, `rigControlNode` 0x0013B5C0. Check this list before picking an id for a new node.

Known discrepancies with the text above:
- There is no `NodeCreateCommands` namespace in `Constants.h`. Node type names live only in `ModuleRegistry.cpp`.
- There is no Python in the repo yet. The build entry point is the `createRigFromTemplate` command.

## Reuse and complexity control

Complexity grows through duplication and one-off special cases. Resist both.

### Look before you write

1. Before writing any helper, search `utilities.h` and `rigmodule.h` (and their implementations) for existing functionality.
2. If something close exists, **reuse it**. If it nearly fits, **make a small, backward-compatible edit** (new overload, optional parameter, clearer name) instead of writing a parallel version.
3. Only write a new shared function when nothing close exists. Put it in the right shared location, not inside a module.
4. When you reuse or edit a shared function, check its other callers so you do not change their behavior.
5. In your change summary, state what existing code you reused or extended and why a new function was needed, if one was added.

### Structural practices

- **Rule of three.** Tolerate duplication twice. On the third occurrence, extract it into a shared function. Do not extract speculatively before there are real call sites.
- **One home per concept.** Each piece of logic, constant, and string lives in exactly one place. Shared code goes in the shared layer. Module-specific code stays in the module.
- **Modules stay thin.** A module declares its attributes, child plugs, and sockets, then delegates to shared code for common behavior. If two modules share behavior, move it up to `rigmodule.h` or `utilities.h` instead of copying.
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
Every curly bracket goes on its own new line (Allman), with no exceptions. This includes functions, control flow, namespaces, classes, lambdas, and initializer blocks.

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

### Command flags

Use long flag names everywhere: in code that reads flags, in comments, in docs and in examples (`-usage`, never `-us`). Short names get confusing when reading code. `MSyntax::addFlag` still requires a short name, so short names appear only inside `newSyntax()` and nowhere else. Keep long flag names in file-local constants and read them through those constants.

### Maya API rules

- **Pass lvalue MObjects to non-const `MObject&` parameters.** `MPlug::child(MObject& attr, MStatus*)` does not accept a temporary, so `pElement.child(fnModule.attribute("name"))` fails with E0304. Look the attribute up on its own line into a named `oAttrName` variable, then pass that variable. This applies to any Maya API function taking a non-const `MObject&`, including the result of `fnNode.attribute(...)` and `MFnAttribute::create(...)`. Check the header signature before nesting a call as an argument.
- Always capture and check `MStatus`. Use the project's existing error-handling macros or patterns rather than inventing new ones.
- In `initialize()`, every attribute needs create, add, and `attributeAffects` handled together. Missing `attributeAffects` is a common silent bug, so check it on every new attribute.
- In `compute()`, group plug reads and writes by purpose and avoid re-querying the same plug.
- Keep `compute()` free of allocations and string lookups. Resolve plugs and attributes outside the hot path where possible.

## Planned structure: Maya-free core plus thin plugin layer

Studio-style split, matching how the author structures their Python code:

- **Core library (no Maya headers):** child plug and socket resolution, `parentStructure` ordering and cycle detection, JSON parsing and validation, matrix math. Testable with GoogleTest and no Maya install.
- **Plugin layer (Maya API):** nodes, attribute definitions, plug I/O, and the commands that bridge to the core.
- **Python:** the build command, JSON validation, and user-facing error messages.

Rule: if logic can live in the core, it goes in the core. The plugin layer should be thin adapters.

## Environments

- **Cloud sessions** cannot run or build against Maya (license, devkit, restricted network). In the cloud, work on the Maya-free core, tests, schema and fixtures, planning docs, Python tooling, and convention audits. Never state that Maya-dependent code works.
- **Local sessions** can build against the devkit and run `mayapy` or `maya -batch` against test JSON. Verification of `compute()` behavior, plug evaluation, and undo/redo happens here.
- Any change touching Maya-dependent code should end with a note: "Needs local build and Maya verification."
- Stub-based syntax checks are a lint only. Stubs drift from real signatures.

### Local build and test (Windows)

- **Visual Studio project:** `RigModules.vcxproj`. The devkit path comes from the `MAYA_DEVKIT_LOCATION` env var (`C:\Users\bmorgan\Documents\dev\maya\devkit\devkitBase`).
- **Build:** `& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" RigModules.vcxproj /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal`
  - Output: `x64\Release\RigModules.mll`.
  - Warnings that already existed and can be ignored: nlohmann C5260, `RigControlNode.cpp` C4244, `Utilities.cpp` C4018.
- **Run Maya headless:** `C:\Program Files\Autodesk\Maya2026\bin\mayapy.exe script.py`. Start the script with `maya.standalone.initialize(name="python")`, then `cmds.loadPlugin(path)`.
- **Copy the .mll before loading it.** A loaded plugin locks the file, so a Maya session that has it loaded makes the next build fail. Copy the .mll to the scratchpad and load the copy.
- **Reading results in standalone:**
  - `MGlobal::displayError` text surfaces as the `RuntimeError` message from the `cmds` call.
  - Not enough command objects raises `TypeError`.
  - The user's `userSetup.py` prints startup noise, so filter the output.
  - Turn undo on with `cmds.undoInfo(state=True, infinity=True)` before testing undo/redo.
- **Line endings:** source files are CRLF. Git Bash `sed -i` rewrites a file as LF, so prefer the Edit tool. Otherwise run `unix2dos` afterwards.
- **Locked files:** `RigRoot.cpp` and other files may be open and locked in Visual Studio, so writing them from a Python script fails with a permission error. The Edit tool still works.
- **Short flag names:** `MSyntax::addFlag` rejects short names longer than 3 letters, and the flag then silently never registers. Wrap each `addFlag` in `CHECK_MSTATUS`.
- **No unit tests:** no GoogleTest harness exists yet. Verify changes with ad hoc mayapy scripts.

## Review checklist

Before declaring work done, check:

- [ ] Allman braces everywhere
- [ ] `o` / `p` / `fn` / `a` naming followed
- [ ] No temporaries passed to non-const `MObject&` parameters
- [ ] New attributes have create, add, and `attributeAffects`
- [ ] Attribute name strings come from `RigAttr`, not literals
- [ ] `MStatus` checked on Maya API calls
- [ ] Pure logic placed in the core library, not the plugin layer
- [ ] Searched `utilities.h` and `rigmodule.h` first; reused or minimally extended existing code
- [ ] No duplicated logic, constants, or strings; shared code in the shared layer
- [ ] No names (controls, attributes, child plugs, sockets) changed without flagging the pose, animation, and export impact
- [ ] Animator-facing behavior is simple and predictable
- [ ] Complexity added by this change is stated and justified
- [ ] Stated clearly what was and was not verified

## Known open items

These go stale quickly. Verify against the repo before acting on them.

- Connect-time jump on the child when a child plug connects to a non-identity socket.
- Reviewing `compute()` plug grouping and the layout `end` socket to first FkChain `start` child plug connection.
