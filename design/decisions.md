# Design decisions: cpp-rigmodules

Running record of design decisions for the Maya C++ rig system. Read this before proposing architectural changes. Update it when a decision is made, changed, or reversed.

Source: compiled on 2026-11-04 from the Project memory for "Maya Rigging Modules". It records what was decided, and only records a reason where one was stated. Entries marked **Rationale not recorded** need the author to fill in the why. Where this file and the code disagree, the code wins. Flag the difference.

Status key: **Decided** (in use), **Leaning** (direction chosen, not built), **Open** (unresolved).

---

## Architecture

### D1. Nodes are the rig's brain, DAG inheritance is off
**Status:** Decided

All transforms are calculated inside the custom module nodes. `inheritsTransform` is off for every control and joint, so DAG parenting never drives results.

- **Why:** The nodes own all transform math, so the DAG can't be a second source of truth.
- **Consequence:** DAG parenting is organizational only. Never rely on DAG inheritance for results.

### D2. Joints are the main input, JSON is the template
**Status:** Decided

Joints are the primary input to the rig. A JSON file is the rig template and the main user input. It is read in C++ with nlohmann json. A simple Python command builds the rig from that JSON.

- **Rationale not recorded** for choosing JSON as the template format.

### D3. Module inputs are world-space matrices
**Status:** Decided

Modules take control world matrices and their parent world matrices as inputs. Outputs are also world-space.

### D4. Child plugs and sockets define module attachment
**Status:** Decided

- **Child plugs** are child-side named inputs.
- **Sockets** are parent-side named world-space matrix outputs.
- Both are defined by the module at creation, with names like `start` and `end`.
- A child plug connects to a socket to attach a child module to a parent module.

Names are public API. Renaming a child plug or socket can break saved poses, animation, and exports. Flag the impact before changing one.

- **Terminology (2026-11-04):** these docs used to call child plugs "pins", but the code always said `childPlugs`. The docs now use "child plug" to match the code. Only the documentation term changed, not any names in code or templates.

### D5. Two-pass rig build
**Status:** Decided

1. Create every module separately.
2. Parent them as defined by the JSON `parentStructure` (child plug to parent socket).

- **Rationale not recorded** for the two-pass split, though it implies modules must exist before their relationships can be resolved.

### D6. DAG parenting works
**Status:** Decided, verified by the author

DAG parenting is working. Earlier review items 1 and 2 were addressed. The specific contents of those items were not recorded in memory.

---

## Shared code and constants

### D7. Shared attribute names live in `Constants.h` (`RigAttr`)
**Status:** Decided

- Shared attribute names are string constants in `core/Constants.h`, namespace `RigAttr`.
- Module-level code keeps using the module's own static `MObject` attributes.
- Shared rigmodule code looks attributes up by name (`findAttr`) and uses string-based `findPlug`.
- This fixed the socket-to-child-plug connection (switching `findPlug` to strings).
- Do not duplicate attribute name strings. Add them to `RigAttr`.

### D8. Reuse before writing new helpers
**Status:** Decided (working rule)

Before writing a helper, check `maya/shared/Utilities.h`, `maya/shared/RigModule.h` and `core/`. Reuse what exists, or make a small backward-compatible edit. Follow code-sharing and structural reuse practices to limit complexity growth. Full rules are in `CLAUDE.md`.

### D9. Where node creation names live
**Status:** Open

`Constants.h` also holds a `NodeCreateCommands` namespace (node type names). The open question is whether creation names should live only in the `ModuleRegistry`. Do not move them without asking.

---

## Product direction

### D10. Priorities for trade-offs
**Status:** Decided

This is a full animation and export pipeline, not only a rig builder. Order when trade-offs arise:

1. Animator experience and preference
2. Simplicity
3. Stability and maintainability
4. Cleverness and flexibility

### D11. Design for future needs before they are built
**Status:** Decided

Plan for reanimation and retargeting, export, pose and animation libraries, and rig versioning and migration, even though none are built yet. Name stability follows from this (see D4).

### D12. New modules are gated
**Status:** Decided

Plan carefully before adding modules. A new module needs a stated animator-facing problem, a check against existing modules, a written design with sign-off, and a named maintenance cost. Full gate is in `CLAUDE.md`.

---

## Structure

### D13. Maya-free core plus thin plugin layer
**Status:** Decided (folder split, 2026-11-05). Separate library and core tests still open.

The code splits into a Maya-free core and a thin Maya plugin layer, matching how the author structures their Python code. The intended split is in `CLAUDE.md` ("Structure").

- **Folder split (2026-11-05):** source moved off the repo root into `core/` (no Maya headers: `Constants.h`, `JsonUtils.h`, `StringUtils.h`, `JointMarkup`, `RigJsonStructs`, `RigTemplateValidation`) and `maya/` (everything else). Done before adding more modules, so new files have an obvious home. Only files that were already Maya-free moved to `core/`. Nothing was rewritten to qualify.
- **Include rule:** `core/` includes only `core/` headers, the standard library and nlohmann. `maya/` includes core headers as `"core/X.h"`, so a core dependency is visible at the include line and a stray Maya include in `core/` is easy to find. `tests/maya/concepts/test_source_layout.py` enforces both, in the registration tier. D22 extended the path rule to every header `maya/` includes.
- **Why:** a likely benefit is testing the core without a Maya install, and a hard boundary keeps Maya types from leaking into code that does not need them.
- **Rejected (at the time):** a `src/core` + `src/maya` tree (one more level with nothing else under `src/`). D23 later added that level as `cpp/`. Adding `core/` and `maya/` to the include path so includes stay bare (`"Constants.h"`), because that hides which side a header is on. Building `core/` as its own static library now, because nothing but the plugin links it yet. Revisit when the GoogleTest harness lands.
- **Stayed in `maya/` though close:** `AssetType.h` is Maya-free but `AssetType.cpp` reports errors through `MGlobal`. `Side.h` and `RigType.h` use `MColor` and `MString`. Moving them needs a small rewrite first.

---

## Conventions

### D14. Code style and naming
**Status:** Decided

- Every curly bracket starts on a new line.
- **Enforced and tooled (2026-11-06).** `cpp/.clang-format` uses tabs (`UseTab: ForIndentation`, matching the code), Allman braces, and one parameter per line when a list does not fit (`BinPackParameters: false`, `AlignAfterOpenBracket: BlockIndent`), as the author prefers. The brace violations that existed were fixed by hand: empty constructor and destructor bodies, K&R braces and one-line bodies in `RigControlNode`, `operator bool` in `RigModule.h`, and two initializer blocks. `test_source_layout.py` now fails on a block brace that shares its line with code. One-line braced init lists stay allowed. Includes are sorted within each blank-line-separated block (`SortIncludes: CaseInsensitive`, `IncludeBlocks: Preserve`), so the hand-written groups and their comments stay. Rejected: `IncludeBlocks: Regroup`, which would rebuild the groups from category rules and drop the section comments. 27 files were re-sorted with clang-format's own order, and the layout test checks it. **Not done:** a whole-tree clang-format pass. It would change about 2,000 more lines, mostly re-aligning hand-formatted continuation lines and comments, so the code is not yet clang-format clean.
- `MObject`: `oMyName`. `MPlug`: `pMyPlug`. `MFn*`: `fnMyMfn`. Module attributes: `aMyAttributeName`.
- Pass lvalue `MObject`s to any function taking a non-const `MObject&` (for example `MPlug::child`). Look the attribute up on its own line into a named variable first. Details in `CLAUDE.md`.
- Command flags are read and documented by long name only (`-usage`, not `-us`). `MSyntax::addFlag` requires a short name and rejects anything over 3 letters (verified in Maya 2026: `-skin` and `-usage` fail and the flag silently never registers), so short names appear only inside `newSyntax()`, wrapped in `CHECK_MSTATUS`. Added 2026-11-04.
- Commands share one shape (2026-11-05, every command migrated). Long flag names are file-local `constexpr` constants. A file-local helper reads the flags into a struct (`RigBuildInputs`, `ConstraintBuildArgs`) or a value before anything is queued. `doIt` only parses, delegates and returns. The nodes a command acts on are string objects passed by name, never the selection (`useSelectionAsDefault(false)`). When a helper already displays its error, the caller returns its status with a plain `if`, not `CHECK_MSTATUS_AND_RETURN_IT`, which would print a second "API error detected" line.
  - **Signature changes:** `createRigParentConstraint` takes the control as its one object instead of `-control`: `cmds.createRigParentConstraint("rig:up_ctrl_r", driver="rig:layoutModule.end", label="World")` (since D19, `driver="layout.end"`). `setRigConstraintSpace` takes the constraint as an object, which is call-compatible with the old positional argument, and now fails when both `-space` and `-label` are given. A negative `-initial` is now an error instead of being clamped to 0.
  - **Removed:** `RigModuleCommandUtils` (`-name`, `-joints`, `-parentModule`, `-parentSocketIndex`, `-rigRoot` helpers). It had no callers and belonged to the per-module creation commands that the template build replaced.

---

## Joints

### D15. Joint markup replaces joint names in templates
**Status:** Decided (2026-11-04). Built, not yet verified in Maya.

Each joint carries one JSON string attribute, `rigMarkup`: `{"markupVersion":1, "usage":"upperArm", "side":"Left", "export":true, "skin":true}`. All keys are required. Markup says what the joint is, and never names a module. Template `moduleArgs` pick joints with `[side, usage]` selectors (`"joints": [["Left","upperArm"], ...]`, `"aim": ["Left","eye"]`). Each selector must match exactly one joint. Joint names no longer appear in templates, and there is no mixed mode.

- **Why:** Joint markup is a separate pass from rig building, and joint identity should not depend on naming. The same template then builds on any skeleton with matching markup.
- **Rejected:** markup naming the module and role it belongs to, because that ties markup to module usage. Modules declaring joint roles in `ModuleDescriptor`, for the same reason. Usage groups such as `"arm"` that expand to several joints. Matching many joints by usage and ordering them by DAG depth. A `mirror` key: mirrors will be found from markup by pairing `usage` across `Left`/`Right`.
- **Rules:** `(side, usage)` is unique across the joints passed to one build. Only one module can claim a joint, because its OPM has one driver. `side` is checked strictly against `SideNames`, with no silent `Center` fallback. Markup is read only from the joints passed to `createRigFromTemplate` (D18). Until 2026-11-05 it was read from every joint in one namespace.
- **Consequence:** `usage` strings and the markup keys are public API, and changing them breaks every marked skeleton. `export` and `skin` are stored, but nothing reads them yet. The markup core (`JointMarkup.h/.cpp`) is the first Maya-free code (see D13), so `Colors` moved out of `Constants.h` into `Colors.h`.
- **Command:** `rigJointMarkup` has create, `-edit`, `-query` and `-remove` modes. Joints are always passed by name, never taken from the selection.

---

## Data storage

### D16. String attributes on nodes store JSON
**Status:** Decided (2026-11-04). Built, not yet verified in Maya.

Any string attribute that holds structured data stores JSON text, read and written through `JsonUtils::readJsonString` / `writeJsonString` (`JsonUtils.h`, Maya-free). `socketNames[i]` is a JSON string list of every name for socket `i`, for example `["index_0","start"]` or `["aim"]`. Joint markup (D15) goes through the same helpers via `from_json`/`to_json` on `JointMarkup`.

- **Why:** one storage format and one parser instead of a mix of JSON and ad hoc delimiters. JSON handles escaping, and nothing needs a list-specific split or join helper.
- **Rejected:** `;` separated socket name strings (the previous format, with a trailing `;`), and `StringUtils::joinStringArray` / `nameListContains`, which were removed. Putting the helper in `Utilities.h`, because that header includes Maya and `JointMarkup.cpp` must stay Maya-free (D13).
- **Not changed:** `"module.socket"` identifiers in templates, which are already JSON strings split by `StringUtils::splitIdentifier`. `childPlugName`, which holds one plain name.
- **Consequence:** scenes with rigs built before this change hold `;` strings, so socket lookups (`setRigConstraintSpace`, `createRigParentConstraint`) on them report an invalid `socketNames` error. There is no migration. Rebuild those rigs from their templates. Socket names themselves did not change.

---

## Build

### D17. Read-phase validation before any scene change
**Status:** Decided (2026-11-04). Built and run in mayapy 2026 against valid and broken templates.

`createRig` runs in two phases. The read phase loads the template, reads joint markup, plans every module and validates the whole template. It writes nothing, not even the `rig` namespace. Only a report with no errors reaches the write phase, which creates nodes, parents, runs `doIt` and builds constraints. Errors are collected and all printed at once, each prefixed with its JSON path, for example `rigModules[1] 'fkchain_2'.parentStructure.start`.

- **Plan step per module.** `ModuleDescriptor::build` became `plan` (`ModulePlanFn`). A module's plan step parses `moduleArgs`, claims joints, reads rest matrices and returns a `ModulePlan`: its `ModuleInterface` (child plug and socket names, and whether each child plug has a control) plus a `build` closure that captures the resolved inputs. The layout goes through the same path. `buildModules` checks each built result against its plan's interface, so a plan and `createModule` that disagree fail before `doIt`. FkChain and Layout get their socket names from one `socketNamesAt` function each, used by both sides.
- **Maya-free core.** `RigTemplateValidation.h/.cpp` checks keys, header enums, module names and fields, `parentStructure`, constraints and cycles. `RigJsonStructs.h` and `StringUtils.h` became Maya-free so the core can use them.
- **Checks:** required and unknown keys at every level (`moduleArgs` included). `rigType`, `assetType`, `side`, `shapeType` and selector sides are checked strictly, with no silent fallback. `rigVersion` must be at least 1. `moduleVersion` must be between 1 and the descriptor's `currentVersion`. Module names must be unique, valid Maya node names (so no `.`), not `layout`, and not already present in the rig namespace (D18). Selectors must resolve, and each joint can be claimed only once. A joint whose `offsetParentMatrix` is already driven fails. An Aim rest pose with target or up on the aim joint, or up on the aim line, fails. Every `parentStructure` key must be a child plug, and every value must be `module.socket` naming an existing socket. Constraint names must be unique, `type` must be `Parent`, the target must be a child plug with a control that no other constraint targets, there must be at least one driver, driver sockets must exist, labels must be non-empty and unique, and weights must be at least 0. No cycles are allowed through `parentStructure` plus constraint drivers, and a self-reference counts as a cycle, since every module input affects every output.
- **Warnings:** a marked joint with `export: true` that no module claims, and several drivers that tie for the highest weight.
- **Behaviour changes:** unknown keys are errors. A bare socket name in `parentStructure` (`"end"`) is an error, so write `layout.end`. Child plugs a `parentStructure` does not list now parent to `layout.end`, the same as a module with no `parentStructure`. Before, they were left unconnected. `rigName` is now read (optional, defaults to `rigTemplateName`) and names the rig root. Before, the rig root always took `rigTemplateName`.
- **Why:** a broken template used to fail partway through, after the namespace existed and after nodes were queued, or worse, built a half-parented rig that only logged errors. Template authors also got one error per attempt.
- **Rejected:** a separate `describe` function per module that only returns names, which would parse args twice and let names drift from `createModule`. A `std::variant` of every module's plan type, which would make `RigModule.h` know every module.
- **Consequence:** every new module needs a plan step that declares its full interface. Validation lives in the C++ core, not Python as CLAUDE.md's planned structure says, because module interfaces come from the C++ registry. Python can call the core later.

### D18. createRigFromTemplate takes its joints by name and owns the rig namespace
**Status:** Decided (2026-11-05). Built and run in mayapy 2026: build, undo, redo, nested and root namespaces, and each failure case below.

`cmds.createRigFromTemplate(*joints, template=path, namespace=ns)`, where `namespace` is optional. Joints are string objects with their namespace in the name (`skel:upperArm_jnt`), and the selection is never used, the same as `rigJointMarkup`. `-template` replaced the positional path. `-namespace` is the rig's final namespace, `rig` by default. `:` is the root, nested paths such as `char:rig` work, and an existing namespace is reused. Every node the build creates goes there, and every input joint is moved there with its name unchanged.

- **Why:** the caller says exactly which joints are rig inputs instead of the build scanning a namespace, and the finished rig (joints included) lives under one namespace.
- **Rules (read phase, D17):** every passed joint must carry valid markup, so an unmarked joint is an error, not skipped. Each joint's and module's target name in the rig namespace must be free in the scene and unique within the build. For example, `skel:a` and `other:a` cannot both be passed. A joint already in the rig namespace keeps its place. Joint name resolution fails before the read phase, like `rigJointMarkup`.
- **Rejected:** skipping unmarked joints (it hides a missed markup pass), and leaving joints in their own namespace.
- **Fixed with this change:** redo ran the modifier with the user's current namespace, so redone controls and modules landed in the root namespace. `doIt` and `redoIt` now both make the rig namespace current through `ScopedRigNamespace`. Control names strip the joint's namespace, so `skel:a_jnt` gives `a_ctrl_c` in the rig namespace, not `skel:a_ctrl_c`.
- **Consequence:** the joint rename is part of the command's undo, but namespace creation is not, so undo leaves an empty rig namespace. Callers that passed a positional path or relied on the namespace scan must change. `JointMarkupScene::collectMarkedJoints` now takes a joint list and adds its errors to the `ValidationReport`.

---

### D19. Commands take the same socket ids as templates
**Status:** Decided (2026-11-05). Built and run in mayapy 2026 with rig namespaces `rig`, `char:rig` and the root: create, space switch, undo, redo, and each failure case below.

`cmds.createRigParentConstraint(control, driver=["layout.end", "fkchain_1.start"])` uses the same `module.socket` ids as a template's `parentStructure` and `constraints`.

- **The layout node is named `layout`** (`RigNaming::LayoutModule`), not `layoutModule`. Every module's scene name is now its template id, so the command path needs no special case for the reserved name. The read phase checks that `<rigNamespace>:layout` is free, like every other module name.
- **Drivers resolve in the constrained control's namespace.** A module name without `:` is looked up as `<control namespace>:<module>`, as an absolute name, so the user's current namespace does not matter. A name that already has a namespace (`other:layout.end`) is used as given. The default enum label stays exactly what was typed.
- **The constraint node goes in the control's namespace too.** The default name is `<control short name>_space`, and an unqualified `-name` gets the same namespace. A qualified `-name` is used as given. The name is absolute, so redo recreates the node under the same name whatever the current namespace is. Before this, the node landed in the current namespace and redo could put it somewhere else.
- **Why:** a socket id names a socket within one rig, and the control already says which rig that is. Templates and commands now share one vocabulary, which pose and animation tools can rely on later.
- **Rejected:** mapping `layout` to `layoutModule` in the command path, because it adds a special case that must stay in sync with the build. Resolving in the current namespace was rejected because it is fragile (the D18 redo bug). A `-namespace` flag was rejected because it adds a flag for something the command already knows.
- **Consequence:** rigs built before this change keep a `layoutModule` node until they are rebuilt. Only the module node's name changed. Controls, sockets, child plugs and attributes did not, so poses, animation and exports are unaffected. `setRigConstraintSpace` matches labels, not socket ids, and is unchanged.

---

## Testing

### D20. mayapy test suite with data-driven expectations
**Status:** Decided (2026-11-05). Built and run in mayapy 2026: 182 tests pass against the Release build. Tier gating, `--keep-going`, a crash through Maya's crash handler mid-tier and a plugin that fails to load were each forced and reported as intended. The forced crash left no processes, dialogs or recovery files behind.

`tests\RigTests.cmd` runs Python `unittest` tests in mayapy against a copy of the built `.mll` (details in `tests/README.md`). Tests are grouped by rig module (`modules/`), Maya command (`commands/`) and concept (`concepts/`: naming, sockets, undo/redo, template loading and validation, plugin registration). Known bugs go in `known/` as expected failures.

- **Tiers:** tests run in dependency order: registration, read only, build smoke, modules, constraints, then known bugs, which never block anything. Animation and import/export tiers will go after constraints. A tier runs only if every earlier tier passed (`--keep-going` overrides this), so a broken plugin or build is reported once at its root, not as a failure in every later test. `TIERS` in `run_tests.py` is the only place that assigns files to tiers. A file it doesn't list fails the run, and a file that mixes tiers is split. Constraint id resolution moved from `concepts/test_naming.py` to `commands/test_constraint_cmds.py` for this reason.
- **One watched mayapy process:** `run_tests.py` starts one mayapy child that runs every tier, and watches it. The child writes outcomes as they happen and records the running test before it starts. A crash then names the test, keeps the earlier results and blocks the later tiers (`--keep-going` starts a new child at the next tier), and a startup failure prints its error. A full run takes about 10 s.
  - **Rejected:** one process per tier. It isolated tiers from each other's crashes, but cost about 4 s of Maya startup per tier and gained nothing over the watched child, because a crash already stops the tiers after it.
- **Crash hygiene:** the child runs with `MAYA_DISABLE_CER=1`, because otherwise a crash starts `senddmp.exe` and an "Autodesk MAYA 2026 Error Report" dialog (`cer_dialog.exe`). Its `TEMP`/`TMP` point at the run folder, so crash dumps and recovery scenes stay out of the user's Temp folder. After a crash, the runner closes any crash reporter processes started by the child, matched by parent process id so the user's own Maya is never touched. The child calls `maya.standalone.uninitialize()` before exiting, because without it Maya 2026 crashes on exit. All of this was observed with Maya 2026.3 on 2026-11-05.
- **Data or code:** expected results that are data live in JSON case files. There are three types: pose cases (control values in, world transforms out), validation cases (JSON pointer edits to a template, expected error path and substring) and golden interface snapshots. Sequences of steps (undo/redo, namespaces, command errors) stay in code. Every case file rejects unknown keys.
- **Hand-written expectations:** pose expectations are written by hand and never recorded from the rig, because recording would turn a bug into the expected result. Golden snapshots are the one generated type (`--update-golden`). They capture public names only, and a change to them must come with a decision entry.
- **Shared module tests:** `ModuleContract` gives every module the same tests: builds, rest pose kept, joints driven by the module, nothing inherits transform, and undo/redo round trip. A new module supplies data, not copies of these tests.
- **Why:** past changes were verified with ad hoc scripts that drifted from the API (positional template path, `layoutModule`) and printed results for a person to judge. Data-driven cases keep most new tests as small, reviewable files.
- **Rejected:** pytest, which is not shipped with mayapy, and a compiled launcher `.exe`, because the `.cmd` gives the same double-click run.
- **Deferred:** GoogleTest for the Maya-free core (`JointMarkup`, `RigTemplateValidation`, `RigJsonStructs`). It needs a new dependency, so it waits for sign-off.
- **Running with every build:** the suite runs after every build. A script that already has mayapy running to check a change ends with `run_tests.run_in_current_maya()`, which reuses that Maya (about 4 s, no crash protection, refuses interactive Maya). Checks made by temp probe scripts become suite tests, or are proposed as tests, before the change is done.
- **Consequence:** every new module, command or bug fix comes with tests (see `tests/README.md`). The case-file formats are maintained surface, so a fourth case type needs a stated reason.

---

## Documentation

### D21. User docs in `docs/`, design docs in `design/`
**Status:** Decided (2026-11-05)

`README.md` (overview, install, quick start) and `docs/` (templates, commands, modules) describe what the system does now, for people using it. `design/` (this log and the planning PDF) records why, for people changing it. `docs/README.md` holds the process: each topic has one owning page, and a user-visible change updates that page in the same change. Docs are plain Markdown in the repo, with no generator.

- **Why:** `docs/` held only design material, so there was nowhere to put user documentation without mixing the two audiences.
- **Rejected:** moving the design docs into `.claude/`, because the decision log is for the author as much as for Claude. A doc generator (Doxygen, MkDocs), because it is a new dependency and nothing needs it yet.
- **Consequence:** reference pages repeat facts the code holds (keys, flags, socket names), so they can drift. The guards are the owning-page rule, the docs pass that ends every change (`CLAUDE.md`, Working style), and the README quick start running as a suite test. When a page and the code disagree, the code wins and the page is fixed.
- **Command examples are Python `maya.cmds`, never MEL** (author's call, 2026-11-05). This applies to every doc, this log, comments and replies. Flags are keywords named by the long flag name. The earlier MEL examples in D14, D18 and D19 were rewritten.

### D22. `maya/` grouped by role: modules, commands, nodes, shared
**Status:** Decided (2026-11-05)

`maya/` holds the plugin entry and the two registries. Under it, `modules/` holds module nodes, `commands/` holds `MPxCommand`s, `nodes/` holds every other node (rig and asset roots, the parent constraint, the control), and `shared/` holds code used across them (`RigModule`, `Utilities`, `JointMarkupScene`, the enum helpers, `Colors`). `core/` stays flat. Every `maya/` file includes project headers by path from the include root (`"maya/shared/RigModule.h"`), including headers in its own folder. Since D23 that root is `cpp/`.

- **Why:** more modules and commands are coming, and one flat folder mixed them with the base layer. Modules and commands are the two things that grow. Each now has its own folder, which matches `tests/maya/modules` and `tests/maya/commands`.
- **Command split:** `CreateRigCmd` moved out of `RigRoot.cpp` into `commands/CreateRigCmd`, and `CreateParentConstraintCmd` and `SetConstraintSpaceCmd` moved out of `Constraints.cpp` into `commands/ConstraintCmds`. Code was moved, not changed. `ScopedRigNamespace` was file-local in `RigRoot.cpp` and both `createRig` and `CreateRigCmd::redoIt` use it, so it moved to `shared/Utilities` unchanged.
- **Rejected:** subfolders in `core/` (9 files, so a folder per topic would hold one or two files each). Bare includes for headers in the same folder, because a file could then not move between `maya/` folders without breaking its includers' paths, and two rules are harder to check than one. `RigModule` in `modules/`, because `RigRoot` and the constraint commands use it too.
- **Consequence:** a new folder is a layout decision. It is added to `SOURCE_FOLDERS` in `test_source_layout.py` and to the repo map in `CLAUDE.md`, or the layout test fails.

### D23. All C++ under `cpp/`, with its project
**Status:** Decided (2026-11-06)

`cpp/` holds `core/`, `maya/`, the vendored `include/`, `RigModules.vcxproj` and `.filters`, and `.clang-format` (renamed from `clang-format`, which clang-format never read). The repo root keeps only non-code folders (`design/`, `docs/`, `tests/`, `RigTemplates/`) and the top-level docs. Build output moves to `cpp/x64/Release/RigModules.mll`, which is the test runner's default `--plugin`.

- **Why:** after D13 and D22, `core/` and `maya/` sat next to `docs/` and `tests/` with no single code root. A Python front end is planned and will want its own top-level folder beside `cpp/`.
- **Project file inside `cpp/`:** the project dir is the include root, so every include (`"core/X.h"`, `"maya/shared/X.h"`) and every item path in the project kept working with no edits. Only the build command, the output path and the runner's paths changed.
- **Rejected:** keeping the `.vcxproj` at the root and adding `cpp/` to the include path. The project would then reach into a folder it does not own, and the include root would differ from the project dir. Naming the folder `src/`, because the Python code will also be source.
- **Consequence:** C++ paths in `CLAUDE.md`, this log and includes are relative to `cpp/`. User-facing docs write the full `cpp/...` path. Visual Studio must reopen the project from `cpp\RigModules.vcxproj`. `tests/run_tests.py` checks for stale sources under `cpp/core` and `cpp/maya`. That check had silently found nothing since D13, because it still looked at the repo root.

---

## Python and the wider pipeline

### D24. mayadev depends on RigModules, never the reverse
**Status:** Decided (2026-11-06). The author signed off on the two-repo split, the placement rule and the migration steps in `mayadev-integration.md`. Nothing is built yet.

The author's Python pipeline repo, mayadev (package `bmaya`), has its own rig builder, which works completely differently. It will hold skinning, animation and GUI tools. Proposal: keep two repos. RigModules holds the plugin and a thin Python front end in `python/` (build wrapper, rig query API, template validation passthrough; no Qt, no asset paths). mayadev holds assets, build orchestration, skinning, animation tools and UI, and reaches rig internals only through the query API. mayadev's legacy rig builder (`rigging/base`, `rigging/modules`) is frozen, then deleted. Its modules are kept as requirements reference, not code to port. Full survey, conflict table and phases are in `design/mayadev-integration.md`.

- **Why:** RigModules names are public API for animation (D11), so rig behavior should version apart from fast-moving tools. The plugin must load headless without Qt or a pipeline environment. And mayadev's code would not meet this repo's tests, docs and conventions without a rewrite.
- **Rejected (for now):** one repo with all of `bmaya` under `python/`. It stays the fallback if two checkouts get painful.
- **Placement rule:** Python goes in `python/` if it would have to change when a RigModules node, attribute, flag or template key changes. Otherwise it stays in mayadev. `python/` never imports Qt or `bmaya` and reads no environment variables or asset paths.
- **Author's answers (2026-11-06):** no legacy rigs need to survive (personal portfolio work), so the legacy builder can be tagged and deleted with no migration. Riggers edit control shapes per asset through `shapeTransform`, so shape IO is a TODO on the RigModules side and the asset folder layout is a TODO on the mayadev side. Each repo ships a `.mod`.
- **Consequence:** every legacy conflict (cmds constraints, name-based joints, nested `children` templates, `L/R/M` sides, network-node metadata) resolves in RigModules' favour. Mirroring and control sets are still open.

---

## Controls

### D25. Controls carry markup, and tools find them by traversal
**Status:** Decided (2026-11-07). Built and tested in Maya 2026 (mayapy suite).

Every `rigControlNode` has a static JSON string attribute, `controlMarkup`: `{"markupVersion":1, "side":"Left", "usage":"upperArm"}`. A control made for a joint copies that joint's `(side, usage)` (FkChain controls, Aim target and up). Layout control *n* is `(Center, layout_<n>)`. Each module's plan declares its controls' markup in `ModuleInterface::controls` and hands the same values to `createRigControl`, and `RigTemplateValidation::validateControlMarkup` fails the read phase if two controls in a rig share a pair. `getRigControls` walks rig root `rigModules` -> modules -> `controlMessage[i]` -> controls and filters by `moduleType`, `side` and `usage`.

- **Why:** shape files, pose libraries and animation tools need a control key that survives renames and doesn't depend on parsing names (D24: tools reach rig internals through connections, never names). Joint markup is already that key for joints.
- **JSON, not two plain attributes:** the author chose one JSON attribute so the schema can grow (D16), at the cost of a parse on read. Its keys reuse `JointMarkupKey::MarkupVersion`, `JointMarkupKey::Usage` and `RigAttr::Side`. The attribute name is `RigControlNodeAttr::ControlMarkup`.
- **Color stays with the module:** a control's color and name suffix come from its module's `side`, not its markup. The two normally agree.
- **Rejected:** keying on control names (they change with joint names). Leaving layout controls unmarked (every control needs a key).
- **Naming:** the traversal command is a getter, `getRigControls`. New API functions and query commands prefer get/set names (author, 2026-11-07).
- **Order:** modules sorted by node name, then controls by `controlMessage` index, so results are stable between sessions.
- **Registry:** `ModuleDescriptor::isRigModule` marks the entries that connect to `rigModules`. Their `moduleType` names are the valid filter values.
- **Consequence:** `controlMarkup`'s keys, the `layout_<n>` usages and the `moduleType` filter names are public API. Golden snapshots now record each control's markup. Rigs built before this have no markup and must be rebuilt (no migration, see D24).

### D26. One shape axis convention, a curve shape library, and per-rig shape files
**Status:** Decided (2026-11-07). Built and tested in Maya 2026 (mayapy suite). The drawn result was not checked in an interactive viewport.

- **Axis:** +X is the normal of every flat shape and the long axis of Capsule and Cylinder. Scale X of `shapeTransform` sizes along that axis, Y and Z across it. Before this, Circle and Square faced X, Triangle faced Y, and Capsule and Cylinder ran along Y. +X matches joints with +X down the bone.
- **Library:** ten curve shapes from mayadev's legacy `controlTypes.json` were added after the seven existing ones (the enum is stored by index): `HalfCircleArrow`, `ArchedDoubleArrow`, `PointedCircle`, `Diamond`, `CircleArrow`, `Dome`, `SingleArrow`, `FourArrow`, `CircleStick`, `Pivot`. Triangle moved into the library. The points are compiled into `core/ControlShapes.cpp`, rotated from +Y normal to +X and scaled to unit extent. Not copied: the legacy `cube`, `boundingBox`, `sphere`, `circle`, `square` and `cylinder`, which duplicate primitives. The enum moved from `RigControlNode.h` to `core/ControlShapes.h` and static_asserts its count against `RigControlNodeAttr::ShapeTypes::All`.
- **Bounds:** the control's object-space bounds follow `shapeTransform` and are exact. Curve shapes push their points through the matrix. Primitives are built from the sizes the draw manager is given, because the round ones don't scale linearly (a capsule's caps follow its radius). The draw manager's size meanings were measured against a +-1 cube in VP2 playblasts: box and rect take half extents, sphere and circle a radius, capsule and cylinder a radius and a full length.
- **Viewport check (2026-11-07):** every shape was playblasted in interactive Maya (VP2) from front, top and perspective, at unit scale, at scale (2, 1, 0.5), filled with a selection, and mirrored. Axes, sizes, fills, selection colors and mirroring matched the design. The first version of the bounds pushed a unit box through the matrix, which under-bounded a short, wide capsule. That was fixed before commit. Batch renders (`mayabatch` with `ogsRender`) draw meshes but not UI drawables or curves, so this check needs a GUI session started with `maya.exe -command`. `tests/ViewportGallery.cmd` repeats it.
- **Shape file:** one JSON file per rig, `{"formatVersion":1, "controls": {side: {usage: {shapeType, shapeTransform, filled}}}}`, written by `saveRigControlShapes` and applied by the undoable `loadRigControlShapes`. Nested by side then usage, so no delimited key (D16). Load validates the whole file first and applies nothing on any error. Missing and extra entries are warnings. The serializer is hand-written so each matrix is one line and a shape edit is a one-line diff.
- **Not in the file:** color and alpha. Color follows the module a control is built on (author, 2026-11-07).
- **C++ commands, not Python:** answers the open question in `mayadev-integration.md`. The format lives in `core/` (Maya-free), and the load is one undo chunk.
- **Draw fixes in the same change:** VP2 draw data is now read whenever VP2 hands over no cached data (a new panel or instance used to draw a default red box), the dead `aChildControlPlugs` / `aParentControlSockets` declarations and the unused `moduleNode` parameter of `createRigControl` were removed, `initialize()` checks every `MStatus`, and curve points are transformed once per shape change instead of every draw.
- **Not built:** mirroring shapes between Left and Right (pair `usage` across sides). Filtering save and load by module or markup.
- **Consequence:** shape names, the shape file keys and `formatVersion` are public API. Existing scenes with non-uniform Circle, Square, Capsule or Cylinder scale draw differently.

---

## Open items

- **Connect-time jump:** the child jumps when a child plug connects to a non-identity socket. Still to do.
- **`compute()` review:** plug grouping in `compute()`, and the layout `end` socket to first FkChain `start` child plug connection. In progress.
- **D9:** where node creation names should live.
- **D13:** `core/` and `maya/` are split by folder, but `core/` is not yet its own library and has no tests that run without Maya.
- **D24:** the TODOs in `mayadev-integration.md` (asset folders, the `python/` package, `.mod` files), plus control sets and mirroring.
- **D26:** shape mirroring (Left to Right by `usage`). The selection highlight still reads the active selection list once per control per `prepareForDraw`, which scales with control count times selection size.

## Adding an entry

Use this shape:

```
### Dn. Short title
**Status:** Decided | Leaning | Open
What was decided, in one or two sentences.
- **Why:** the reason, including what was rejected.
- **Consequence:** what this constrains.
```
