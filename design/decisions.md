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
**Status:** Decided (working rule). Extended 2026-11-09.

- **Shared when written (author, 2026-11-09):** a scene or utility helper (finding nodes, namespaces, DAG walks, components, file IO, flag parsing) goes into the shared layer from its first use, not after its third copy. The rule of three now covers only logic about one feature. Why: a few days of new commands had left near-identical file reading, report printing and name lookups in several files. A sharing pass is planned ("Before adding more modules").

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
- Commands share one shape (2026-11-05, every command migrated). Long flag names are file-local `constexpr` constants. A file-local helper reads the flags into a struct (`RigBuildInputs`, `ConstraintBuildArgs`) or a value before anything is queued. `doIt` only parses, delegates and returns. The nodes a command acts on are string objects passed by name, never the selection (`useSelectionAsDefault(false)`). Exceptions: `setRigMeshId` and `pruneToMaxInfluences` with no meshes named act on the selected meshes (D35). When a helper already displays its error, the caller returns its status with a plain `if`, not `CHECK_MSTATUS_AND_RETURN_IT`, which would print a second "API error detected" line.
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
- **The export skeleton is the joints whose markup has `export: true`** (author, 2026-11-10). Nothing else defines it: no naming rule, no separate hierarchy, no per-module list. A module's rig-internal joints are marked `export: false`, or aren't joints in the skeleton at all. Validation already warns about an export joint no module claims. Nothing exports yet, so no code reads the key for that.
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
- **Checks:** required and unknown keys at every level (`moduleArgs` included). `rigType`, `assetType`, `side`, `shapeType` and selector sides are checked strictly, with no silent fallback. `rigVersion` must be at least 1. `moduleVersion` must be between 1 and the descriptor's `currentVersion`. Module names must be unique, valid Maya node names (so no `.`), not `layout`, and not already present in the rig namespace (D18). Selectors must resolve, and each joint can be claimed only once. A joint whose `offsetParentMatrix` is already driven fails. An Aim rest pose with target or up on the aim joint, or up on the aim line, fails. Every `parentStructure` key must be a child plug, and every value must be `module.socket` naming an existing socket. Constraint names must be unique, `type` must be `Parent` (D31 adds `Orient` and `Point`), the target must be a child plug with a control that no other constraint targets, there must be at least one driver, driver sockets must exist, labels must be non-empty and unique, and weights must be at least 0. No cycles are allowed through `parentStructure` plus constraint drivers, and a self-reference counts as a cycle, since every module input affects every output.
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
- **Changed by D29 (2026-11-08):** the command takes a required `asset` flag, and `namespace` defaults to the asset's last id token instead of `rig`.

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
- **Shared module tests:** `ModuleContract` gives every module the same tests: builds, rest pose kept, joints driven by the module, nothing inherits transform, and undo/redo round trip. It also builds the module mirrored (D28, 2026-11-11): every Left or Right control pairs with its mirror and has the same channels, mirrored joints keep their rest, and `mirrorRigPose` and `flipRigPose` copy or negate each channel as it declares. A new module supplies data, not copies of these tests.
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

### D26. Every control shape is a fillable mesh, with one orientation convention, and per-rig shape files
**Status:** Decided (2026-11-07). Built and tested in Maya 2026: mayapy suite, plus a VP2 check with `tests/ViewportGallery.cmd`.

- **Meshes, not draw manager primitives (author, 2026-11-07):** every shape is points, faces and lines, drawn with `MUIDrawManager::mesh`: the unique face edges plus lines as `kLines` (the wire), and the faces, triangulated by ear clipping, as `kTriangles` when `filled` is on. So every shape can be filled, every shape scales linearly on every axis, and the bounds are the points through `shapeTransform`. Rejected: the draw manager's `box`, `sphere`, `capsule` and `cylinder`. Only six shapes could fill, round ones ignored some scale axes, and their size rules had to be measured.
- **Fill look (revised 2026-11-10, author):** `filled` defaults to on, and the fill draws at 0.75 of the wire's alpha with the wire on top, so a filled control keeps its outline. `xray` (default on) wraps the draw in `MUIDrawManager::beginDrawInXray`, so controls always show in front of meshes. `xray` is a display preference and is not in the shape file (D26 format unchanged). VP2 doesn't cull the back of a fill.
- **Data:** generated by `tools/generate_control_shapes.py` (mayapy) into `core/ControlShapeLibrary.cpp`, compiled in, with nothing read at plugin load. Box, Sphere, Cylinder, Capsule and Dome come from Maya's poly commands read through `MFnMesh`. Rings and arrows are outlines in the generator, several from mayadev's legacy `controlTypes.json`. `validateShapeLibrary` (core) runs at plugin load and refuses to load if any face doesn't triangulate or any index is out of range.
- **Orientation:** every shape fits the -1..1 cube. Rings (Circle, Square, Triangle, PointedCircle, HalfCircleArrow, CircleArrow) lie in YZ, facing +X, so they wrap a bone with +X down it. Pointers (SingleArrow, FourArrow, CircleStick, ArchedDoubleArrow) point down +X, and the flat ones lie in XZ (author: arrows point down +X). Round solids run along +X: Cylinder is 2 long with radius 1, and Dome bulges toward +X. Capsule fills the cube like Box: a 2-long, radius-0.5 round-capped cylinder stretched x2 in Y and Z, so its caps are half-ellipsoids (author, 2026-11-07: at radius 0.5 it read as much smaller than the other solids, and a uniform scale would have broken the -1..1 rule).
- **Rebuilt rather than copied:** the legacy 3D curves had no faces. Diamond is an octahedron, Pivot is three flat double arrows (one per axis), ArchedDoubleArrow is a strip over +Y with triangle heads, CircleArrow is two HalfCircleArrows, and CircleStick is a stick line and a disc. Not copied: the legacy `cube`, `boundingBox`, `sphere`, `circle`, `square` and `cylinder`, which the generated solids replace.
- **Enum:** the 17 names and their stored order are unchanged, so templates and saved scenes keep working. `ShapeType` lives in `core/ControlShapes.h` and static_asserts its count against `RigControlNodeAttr::ShapeTypes::All`.
- **History:** the first version of this change kept draw manager primitives for six shapes beside a curve library. Its viewport check found that the primitives' bounds didn't scale linearly. The author then asked for meshes only.
- **Viewport checks need a GUI session:** batch renders (`mayabatch` with `ogsRender`) draw meshes but not UI drawables or curves. `tests/ViewportGallery.cmd` starts `maya.exe -command` and playblasts every shape at unit scale, scaled, filled with a selection, mirrored, and filled from behind.
- **Shape file:** one JSON file per rig, `{"formatVersion":2, "controls": {side: {usage: {shapeType, shapeTransform, filled, wireframeAlpha}}}}` (version 1 had no `wireframeAlpha`, see below), written by `saveRigControlShapes` and applied by the undoable `loadRigControlShapes`. Nested by side then usage, so no delimited key (D16). Load validates the whole file first and applies nothing on any error. Missing and extra entries are warnings. The serializer is hand-written so each matrix is one line and a shape edit is a one-line diff.
- **Not in the file:** color. Color follows the module a control is built on (author, 2026-11-07).
- **Alpha is in the file, format version 2 (author, 2026-11-10):** the build sets color from the module's side but nothing sets `wireframeAlpha`, so a rigger's alpha was lost on rebuild. Each entry now has a required `wireframeAlpha` (0 to 1) beside `filled`, and `formatVersion` is 2. Version 1 files still load: `controlShapeUpdateTable()`, the file's D33 table, has one step that adds `wireframeAlpha: 1.0` (the attribute's default), so the parser reads only version 2. The serializer rounds alpha to four places, because the plug is a float. `mirrorRigControlShapes` copies the source's alpha to the mirror with the rest of the shape. Rejected: making the key optional, which would leave two shapes of one version.
- **C++ commands, not Python:** answers the open question in `mayadev-integration.md`. The format lives in `core/` (Maya-free), and the load is one undo chunk.
- **Draw fixes in the same change:** VP2 draw data is now read whenever VP2 hands over no cached data (a new panel or instance used to draw a default red box), the dead `aChildControlPlugs` / `aParentControlSockets` declarations and the unused `moduleNode` parameter of `createRigControl` were removed, `initialize()` checks every `MStatus`, and shape points are transformed once per shape change instead of every draw.
- **Not built:** mirroring shapes between Left and Right (pair `usage` across sides). Filtering save and load by module or markup.
- **Consequence:** shape names, the shape file keys and `formatVersion` are public API. The shapes look different from earlier builds: Capsule and Cylinder sizes changed, and SingleArrow, FourArrow and CircleStick now point down +X.

## Constraints

### D27. Space switches match through the control's values, with no stored offsets
**Status:** Decided (2026-11-07). Built and run in mayapy 2026: create and switch on posed rigs (flat and rotated skeletons, non-default rotate order), zero after both, every space at rest, undo, locked channels, no keys.

`bmParentConstraint` outputs the active driver's motion since rest, `rest^-1 * world`, with nothing stored per driver. Every driver's delta is identity at rest, so every space gives the rest pose. When `setRigConstraintSpace` switches on a posed rig, or `createRigParentConstraint` runs on one, the control keeps its world matrix because its own translate, rotate and scale are rewritten: `L' = L * OPM * D_new^-1 * D_old * OPM^-1`, using the control's live offsetParentMatrix (`rest * D_old`).

- **Why:** the old driver offset was hidden state captured at the moment of a switch. Zeroing every control no longer gave the rest pose, a keyed `space` enum used the last captured offset on every frame, and a pose's control values didn't describe the rig. Now a pose is exactly control values plus the `space` enum, which pose and animation libraries, mirroring and retargeting need.
- **Author's choices (2026-11-07):** creation on a posed rig compensates through control values like a switch (rejected: letting the control follow its new driver). The command sets no keys. Keying the switch frame belongs in the animation tools. Only the control on the constrained child plug is matched. The rest of an FkChain follows it.
- **Rejected:** keeping per-driver offsets but capturing them only at rest. It still stores state outside the controls, and a switch would jump.
- **Rules:** the rotation keeps the control's rotate order and is the euler closest to its current values, so curves don't flip. Locked channels, and channels driven by anything but an anim curve, keep their values, with a warning naming them. Shear that the channels can't hold gives a warning. A switch writes nothing when both deltas are equal, so creating at rest and template constraints change no values. The control is found through the space enum that drives `activeDriver`. A constraint with no enum has one driver, so it never needs a match.
- **Removed:** `driverOffset` on `bmParentConstraint` and `ConstraintAttr::DriverOffset`. No existing rigs need to survive, so there is no migration.
- **Consequence:** setting the `space` enum directly (or through keys) does no matching, so the control moves unless the rig is at rest. That is the usual behaviour animators expect from a keyed space without a match tool. Switching on an animated control writes values without keying them, so they hold until the frame changes, like `setAttr`.

### D31. Orient and Point constraints take one component from the driver, on one `rigConstraint` node
**Status:** Decided (2026-11-09).

A constraint's `type` is `Parent`, `Orient` or `Point`. The driver gives only its own component: rotation for `Orient`, translation for `Point`, all three for `Parent`. Everything else, scale included, comes from the child plug's `parentStructure` parent, as if the constraint weren't there. With `C` the plug's control rest world, `Dp` the parent's motion since rest and `Dd` the active driver's, `Cp = C * Dp` and `Cd = C * Dd`. `Orient` builds `M` from the translation, scale and shear of `Cp` with the rotation of `Cd`. `Point` builds it from `Cp` with the translation of `Cd`. The output stays a delta, `C^-1 * M`, so modules don't change and an FkChain's whole chain follows as before. A `leg_l.start` Orient-constrained to `layout.end` swings its hip pivot with `spine.start` but keeps the layout's rotation.

- **Why (author, 2026-11-09):** a limb that holds its world orientation while its pivot rides the body is the usual space animators expect for legs, heads and arms. A Parent constraint can't give it.
- **One node, a `constraintType` enum** (rejected: a node type per constraint type). The driver array, the space enum and D27 matching are shared, and the type changes only how the output is built. The node takes the plug's parent on `parentMatrix` and `parentRest`, rewired from what the parenting pass connected, and stores `childRest`. A Parent constraint gets the same inputs and ignores them, so every constraint is wired the same way.
- **Renamed** (author, 2026-11-09): the node type `bmParentConstraint` becomes `rigConstraint` (`RigConstraintNode`, same MTypeId 0x00218E), and the command `createRigParentConstraint` becomes `createRigConstraint`, with a `type` flag defaulting to `Parent`. `setRigConstraintSpace` is unchanged.
- **Rules:** an `Orient` or `Point` constraint on a child plug with no parent connected fails (author's choice, rejected: treating a missing parent as the world). A template can't produce one, because every child plug has a parent, `layout.end` when `parentStructure` doesn't list it, so this only guards the command on a hand-edited rig. The enum index is stored in scenes, so `Parent`, `Orient`, `Point` keep their order and new types are appended. Space matching (D27) uses the node's own output for each driver, so the control keeps its world matrix on a switch whatever the type.
- **Consequence:** rigs built before this change have `bmParentConstraint` nodes, which no longer load. No rigs need to survive, so there is no migration: rebuild them. Template `"type": "Parent"` entries and `setRigConstraintSpace` calls are unchanged. Controls, sockets and child plugs keep their names, so poses and animation are unaffected.
- **IK controls (author, 2026-11-09):** a constraint can only target a child plug with a control. An IK hand is usually the effector's parent and a pole is a control, so the IK module will expose both as child plugs, and they get spaces through `rigConstraint` with no new mechanism. The space list is fixed when the constraint is created. Adding a space means editing the template and rebuilding. Adding a space inside a shot is out of scope: shots can't change rigs (D32), so it would be a separate tool that bakes or rebuilds.

## Mirroring

### D28. Modules and constraints mirror through the template, by side swap
**Status:** Decided (2026-11-07). Built and run in mayapy 2026: a mirrored template against the same rig written by hand, every validation error, the position check flag, both commands, undo.

`"mirror": true` on a `Left` or `Right` module or constraint adds a copy on the other side in the read phase, before any check, so the copy is an ordinary template entry from then on. The copy is the source with `side` swapped, the name's `_l`/`_r` suffix swapped, every `[side, usage]` selector in `moduleArgs` swapped, and every `module.x` id in `parentStructure`, `target` and `drivers` that names a `Left` or `Right` module pointed at the other side's module. Everything else is copied. `core/TemplateMirror` does it, Maya-free.

- **Why (author, 2026-11-07):** module mirroring should be simple. Modules always build from their joints, and the joints already carry the behaviour or orientation mirror, so a mirror needs nothing but the other side's joints. A mirror's JSON is exactly its source's, minus `mirror`.
- **Author's choices (2026-11-07):**
  - The existing `RigNaming` side suffixes name the mirror (rejected: a `mirrorName` key).
  - A mirrored constraint resolves from template data in the read phase (rejected: finding the mirror plug and sockets by traversing the built rig). So a bad mirror fails before any scene change (D17).
  - Asymmetric pairs fail the build: a mirrored selector that matches no joint is the existing selector error. Positions are not checked, because a skeleton can be asymmetric where the rig is not. `createRigFromTemplate(checkMirrorPositions=True)` adds the check: a mirrored joint more than 0.001 from its source joint reflected across X = 0 fails.
  - Shapes mirror with `mirrorRigControlShapes(rigRoot, leftToRight=True)`, in world space, using both controls' rest world matrices: `S_mirror = S_source * W_source * R * W_mirror^-1`, where `R` reflects X.
  - Mirror-specific module behaviour goes in the module's `planFromJson`, which reads `ctx.moduleData.isMirror()` (rejected: an `isMirror` parameter on `createModule`). The plan step parses the arguments, so `createModule` keeps one build path with no mirror branch. No module needs it yet: FkChain and Aim build entirely from joint positions and orientations, and Layout is `Center`.
- **Rules:** only `Left` and `Right` modules mirror, and a mirrored module's name must end in its side's suffix. A mirrored constraint needs a `Left` or `Right` target module and a name ending in `_l` or `_r`. `Center` selectors stay `Center`, so a mirror that names one fails as a joint claimed twice. A referenced `Left` or `Right` module without a side suffix is an error. A driver label left out defaults to the mirror's own socket id. Mirrors are appended after every source entry, and a mirror's errors name its source entry: `rigModules[2] 'arm_l' (mirror 'arm_r')`.
- **Scene side:** `getRigMirrorControl(control)` returns the control with the same `usage` and the other side, found by control markup through `RigTraversal` (a `Center` control returns itself). The pose and animation tools will pair controls through it.
- **Consequence:** the template key `mirror`, the suffix rule, `checkMirrorPositions`, `getRigMirrorControl` and `mirrorRigControlShapes` with `leftToRight` are public API. A generated module's name is public as a socket id, like any module name. Nothing is stored on nodes, and a mirrored rig is identical to the same rig written out by hand (checked by `test_mirror_matches_template_written_by_hand`).

---

## Assets

### D29. The asset owns its namespace, its rig variants and its files
**Status:** Decided (2026-11-08). The scene side is built and run in mayapy 2026. The file model, versions, registry and handlers are built on mayadev branch `asset-model` (not merged), as described in `asset-model.md`.

An asset is the top-level container for a character, prop, vehicle, vfx element or environment piece. In a scene, one `assetRootModule` per flat namespace owns every rig variant built into that namespace. On disk, the asset is a folder under `RIGGING_PROJECT_DIR` holding an `asset.json`, and its pipe-separated id is that folder's relative path. Full design: `asset-model.md`.

- **Author's answers (2026-11-08):**
  - Keep `|` in asset ids, because existing tools depend on it (rejected: `/`). Depth varies (rejected: always `project|type|name`, which is idealistic once assets end up everywhere). Ids compare case-insensitively.
  - The asset comes from `createRigFromTemplate(asset=...)`, so templates can be shared between assets (rejected: an `assetId` template key, which ties a template to one asset and goes stale when a template is copied).
  - Namespaces are flat, one per asset instance (rejected: nested `asset:variant`). The default is the id's last token, and `namespace` overrides it (`hero_damaged`). Variants share the namespace, and the variant is the rig root's `rigName`.
  - `rigType` is removed, and asset types come from the project's `project.json` and are stored in `asset.json`. The C++ keeps no asset type list (rejected: the `Mesh`/`Rig`/`Other` enum, which described file kinds and repeated what the project defines).
  - Variants share one skeleton. A variant may add joints, never remove them. Meshes, weights, textures and shaders are per variant.
  - Each rig root stamps what built it (`rigVersion`, `pluginVersion`, `publishVersion`). Tools check the stamps against the latest publishes to flag out-of-date imports.
  - Mainline only: one stream, no release-stream tooling, because one person maintains it.
  - Under Perforce, nothing shared is written on a routine save. Depot files change only when someone deliberately changes that asset: `asset.json` holds only type and skeleton, variants are folders, publishes are new folders with `<UTC time>Z_<user>` ids, the registry is an in-memory scan, and local versions live outside the workspace. Saves go through a source-control adapter that does nothing by default. Rejected: a shared `registry.json` written on every save (constant checkouts, merge conflicts on every integrate), sequential publish numbers (collide), changelist numbers as publish ids (renumbered on submit), and a per-machine registry cache file (still needs the walk, adds staleness).
  - The file model is Python in mayadev, on a new branch, because it departs from mayadev's current asset package. D24 stands: RigModules C++ never resolves a path from an asset id.
  - Where a file lives is kept apart from how it is saved and loaded. Save and load are per-file-type handler stubs.
  - Local version copies are wanted alongside source control. Artists go days between submits, and personal work has no Perforce.
  - Asset data must be readable without opening Maya: per-asset `asset.json`, a `project.json` holding the asset types and file types, and a registry cache. The registry tracks shared skeletons, so assets that can share poses and animations, and the dependents of a changed skeleton, come from one query.
  - The environment variable is `RIGGING_PROJECT_DIR`.
- **Built (2026-11-08):**
  - `createRigFromTemplate` needs `asset`, checks it (pipe-separated tokens of letters, digits and `_`), and defaults `namespace` to its last token.
  - The build reuses the namespace's asset root or creates one. The read phase fails when the namespace holds more than one asset root, when the asset root's `assetId` differs from `asset` (case is ignored), or when a rig root under it already has the template's `rigName`.
  - Before this change, the asset root's id came from `rigTemplateName` by mistake, and every build made a new asset root.
  - Rig roots get `pluginVersion` (set by the build from `PluginInfo::Version`, which `MFnPlugin` also registers) and `publishVersion` (a string holding the publish id, empty until the publish step sets it).
  - Removed: the template keys `assetId`, `assetType` and `rigType`; the `assetType`, `assetData` and `rigType` attributes; and `AssetType` and `RigType` in `maya/shared/`.
- **Consequence:**
  - Every template with `assetId`, `assetType` or `rigType` now fails as having unknown keys. No legacy templates need to survive.
  - Every `createRigFromTemplate` call needs `asset`, and the default namespace moves from `rig` to the asset's name.
  - Two templates with the same `rigName` can't be built into one namespace.
  - Pose and animation data aren't affected: no control, attribute or socket names changed.
- **Amendment (2026-11-11, author):** the `rig` file type ends with `_rig`, `variants/{variant}/{name}_{variant}_rig.ma`, like `_skeleton` and `_model`, so every Maya scene's role reads from its name. It is the default in mayadev's `Project.create`. An existing `project.json` keeps the layout it was written with until edited, and a rig saved under the old name needs renaming by hand.

### D30. mayadev stays the pipeline repo, and rigs build anywhere before an asset exists
**Status:** Decided (2026-11-08). Steps 1 to 5 are built, except the template authoring UI: RigModules side run in the mayapy suite, mayadev side in its mayapy tests. Branch `asset-tools` is merged into mayadev main (checked 2026-11-09).

The author considered starting a new pipeline repo, because most of mayadev changes with the new asset and rigging systems. Decided: keep mayadev, tag its legacy state, delete the legacy code, and build the new tools there. A rig can be built and iterated on anywhere, before its asset folder exists. Tools that know about assets use the asset's paths when the rig's asset folder exists, and fall back to a file dialog when it doesn't.

- **Why keep the repo:** the parts that survive (`core/`, the events registry, `qtbase`, the tool registry and menus, the startup, skinning) keep their history and blame, and `mayadev.mod`, `MAYADEV_ROOT`, `userSetup.py` and the RigModules menu keep working unchanged. Deleting the legacy code gives the same clean slate a new repo would, and the tag keeps it readable as reference.
- **Rejected:** a new repo. It would lose that history and rewire the whole startup. It stays an option if a public portfolio repo without the legacy history is wanted, started from the cleaned-up tree. A rename of mayadev is also left for later.
- **Why build anywhere (author, 2026-11-08):** riggers iterate on a template and skeleton long before an asset is worth creating. The build already needs only an asset id (D29), never a folder.
- **Order:**
  1. In mayadev, commit or stash the uncommitted legacy work, then tag main `legacy-rig-builder`.
  2. Merge branch `asset-model`.
  3. On a cleanup branch, delete the legacy builder, legacy modules, `rigManager`, the legacy `bmaya.maya.asset`, and the legacy constants that read `RIG_TEMPLATES_DIR`, and fix startup and menus so nothing imports them.
  4. Build the RigModules `python/` query from any rig node to its asset id, variant (`rigName`), namespace and stamps (D24, D29).
  5. Build the new mayadev tools on it: New Asset and New Variant (replacing the legacy AssetCreator), asset-aware shape save and load, Save Rig, Build Rig from Asset, then the template authoring UI that replaces `rigManager`.
- **New Variant writes the variant's `template.json`.** A variant is a folder holding its template (D29), so creating one is a real write, not an empty folder, and the rule that folders appear only on write holds. `Asset.create` still writes only `asset.json`.
- **Built (2026-11-08):**
  - mayadev: main tagged `legacy-rig-builder`, `asset-model` merged. On branch `asset-tools`, the legacy code is deleted (builder, modules, shapes, templates, `rigTools`, `activerig`, `rigManager`, legacy `bmaya.maya.asset`, the `RIG_TEMPLATES_DIR` constants, their menu tools and docs). A copy of it sits in mayadev's untracked, ignored `legacy_reference/` for reading.
  - RigModules: `getRigInfo` (any rig root, module or control to its rig root, asset root, asset id, variant, namespace and stamps, as JSON), on new `RigTraversal::getOwningRigRoot` and `getAssetRoot`. `getMirrorControl` now uses `getOwningRigRoot`.
  - RigModules `python/rigmodules`: `query.get_rig_info`, `query.get_rigs(namespace)`, the `RigInfo` dataclass, and `template.get_rig_name`. `RigModules.mod` puts `python/` on `PYTHONPATH`. It adds no plugin path yet, because a loaded `.mll` locks the build output.
  - mayadev `bmaya.maya.assettools`: New Project, New Asset, New Variant, Save Skeleton, Build Rig from Asset, Save Rig, and asset-aware Save and Load Control Shapes, in the RigModules menu. A `template` handler copies a template into the variant and checks its `rigName` is the variant.
- **Author-facing choices made while building (2026-11-08), open to change:**
  - Once a rig's asset folder exists, the shape and rig tools always use the asset's paths, and loading a shape file the asset doesn't have is an error, not a dialog. A file never lands in two places.
  - Save Rig needs the variant's template in the asset first (New Variant). Without it the rig file would sit in a folder that isn't a variant.
  - Build Rig from Asset refuses a namespace that already holds a rig. Variants share one skeleton, so a second variant should reuse the joints already there rather than import them again, and that isn't built.
  - The rig tools act on the active rig (author, 2026-11-08), so nothing has to be selected. mayadev's `bmaya.maya.activerig.ActiveRigTracker` follows it, modelled on the legacy tracker: selecting any control, rig-driven joint, module or rig root makes its rig active, and it stays active while other things are selected. File new and open take the scene's first rig. Startup starts the tracker when the plugin loads. Without a tracker, the selection's rig, then the scene's first rig. The asset id dialogs always open, filled in with the active rig's asset id.
  - `getRigInfo` accepts joints, through the module that drives the joint's `offsetParentMatrix`, and returns `""` with no error for any node outside a rig, so a selection callback stays quiet. Rejected: a Python pre-check by node type in mayadev, which would copy RigModules node names (D24).
- **Not built:** the template authoring UI (it needs a module interface query command first, D24). Building a second variant into a namespace. (Importing the variant's model and loading its weights in Build Rig was built 2026-11-09, D35.)
- **Consequence:** `getRigInfo`'s keys, `RigInfoKey::Namespace`, and the `rigmodules.query` and `rigmodules.template` names are public API for mayadev. The template authoring UI validates through RigModules `python/`, never with its own copy of the template rules (D24).

---

## Shots, versions and component data

### D32. Shots reference published rigs, which are read only
**Status:** Decided (2026-11-09). Nothing built yet.

Animators reference a published rig file into their shots and never import it. A rig in a shot is read only and exists only to be animated. Rig changes go through the asset and a new publish, never through a shot. Updating a shot's rig means moving its reference to a newer publish. The rig is regenerable: the template and its component files (shapes, weights) are the source of truth, and a rig can be deleted and rebuilt from the published version at any time.

- **Why (author, 2026-11-09):** importing rigs was a studio's way to keep shots working, however far out of date their rigs fell, because no update path was trusted to be non-destructive. Here the update path is a commitment: a strong name API plus tested updates (D33). Rig edits made in a shot would be reference edits on top of the publish, and for a game they can never reach the export.
- **Rejected:** importing rigs into shots. Rig edits in a shot.
- **Names are hard API.** A shot's anim curves reach the rig through reference edits, which store node and attribute names, not markup. Control names and channel names are as binding as markup: markup (D25) keys tools and files, and names key shots. A rename comes with an entry in an alias list (old name to new name), and a shot update tool uses that list to rewrite the reference edits in a set of shots.
- **The namespace is stripped on save.** The build keeps the rig's inputs and context in its asset namespace (D18, D29), so the scene shows exactly what the rig is made of. The published rig file moves everything to the root namespace, and the reference namespace becomes the instance namespace (`hero:`, `hero1:`). Rejected: keeping the namespace in the file, which nests it under the reference namespace (`hero1:hero:arm_ctrl_l`).
- **Rest is the build pose.** Rest matrices are stored on the rig at build time (D27), never read from a posed scene, so creating a constraint on a posed rig doesn't change any rest. A film pipeline might one day want "moveable pins" whose rest changes per shot. That is a rig change in a shot, so it isn't planned.
- **Consequence:** Save Rig, `getRigInfo` and the rig namespace rules need checking against a root-namespace rig inside a referenced file. A rename of any control or channel needs an alias entry and a shot update before it ships, so renames should be rare.

### D33. The release version and format versions are separate, and updates never need a bake
**Status:** Decided (2026-11-09). The update runner, the template `formatVersion` and the module tables are built (2026-11-10), with every table empty. The shape file is wired to it (2026-11-10: version 2 adds alpha, D26). The weight file, `markupVersion` and the callbacks aren't wired to it yet.

- **Release version:** `PluginInfo::Version`, a label with integer parts (`0.1`, `0.2` … `0.11` … `1.0`), starting at `0.1`. `1.0` is the first release. No update code is tied to it, so it can change as often as development needs.
- **Format versions:** one integer per saved format: a new template key `formatVersion`, the shape file's `formatVersion` (D26), the weight file (D35), each module's `moduleVersion` and `markupVersion`. A format version goes up only when saved data changes shape or meaning, so most releases bump none of them.
- **Update steps:** each format keeps a table from version N to its update function, run in order. Template steps run in `core/` in the read phase, before `expandMirrors`. A minimum supported version per format lets old steps be deleted, which keeps the table short before `1.0`.
- **Built (2026-11-10, author signed off):** `core/FormatUpdate` holds `UpdateTable` (`minVersion` and a list of steps, `steps[i]` taking version `minVersion + i` to the next, so the current version is `minVersion + steps.size()`) and `FormatUpdate::runUpdates`, which errors for a version below `minVersion` or above the current one. A step is a function on the raw JSON and is tolerant: what it doesn't recognise is left for validation. The template table is `templateUpdateTable()`, run at the top of `loadRigTemplate` on the `formatVersion` key (optional, 1 when missing, an integer). Each module's table is `ModuleDescriptor::argUpdates`, run on a copy of `moduleArgs` in `planModules` before the plan step, so a module's plan reads only its current shape. This replaced `ModuleDescriptor::currentVersion` and the range check in `validateModuleFields`, and the old "migration goes here" comment in `readModuleArgs`. The error for a bad `moduleVersion` is unchanged ("N is not supported, this build reads versions 1 to M"). Steps are plain function pointers named for what they do, with no naming scheme yet since none exist.
- **Not built:** wiring the weight file and `markupVersion` to the runner, and the stamp callbacks. The shape file's table (`controlShapeUpdateTable()`) has the first real step, tested through `loadRigControlShapes` on a version 1 file. The template and module tables are still empty. Proposed test, for when `core/` gets a harness (D20): a two-step fake table, versions 0 to 4, checking the steps run in order and the errors name the path.
- **Non-destructive:** an update never needs a bake. Animation lives only on control channels (D27). A shot's referenced rig updates by moving to a newer publish (D32), and its anim curves follow by channel name. A rig open in a scene without a reference (rigging work) updates by rebuilding from the current template.
- **Detection:** scene open, import and reference load callbacks compare the rig root's stamps (`rigVersion`, `pluginVersion`, `publishVersion`, each module's `moduleVersion`) with the current versions and the latest publish, and offer the update.
- **Why (author, 2026-11-09):** rapid development means many releases. Tying updates to release numbers would mean an update path per release, while format versions give one per real change. A `moduleVersion` change must have an update path too, not a forced rebuild of shots.
- **Rejected:** float versions (`0.1` equals `0.10`, and `0.2` sorts after `0.11`). One update step per release.

### D34. The animation interface is the keyable channels, with rests set by the build
**Status:** Decided (author, 2026-11-10). Channels, zero, mirror and flip are built. Phase 2 below (control info query, Python query, selection scope for the pose commands) is signed off and built (2026-11-10, suite 453 passed; not checked in a GUI session). Phase 3 is built in part (2026-11-10): `saveRigPose`, `loadRigPose` and `rigmodules.pose` (key all, key modules). Not built: the shared capture and apply path for the older pose commands, mirror on load, animation files and the keyframe command.

A control's animation interface is its keyable channel-box attributes: transforms, plus the module's custom attributes (IK/FK blend, stretch, bend bow, sine values, `space`). Each channel's rest value is the attribute's default, which the module sets when it creates the attribute, so rests of 1 or 0.5 come from the build itself. Ranges are the attribute's min and max. A query reads all of this from the built rig, with no stored list. Pose saving, animation save and load, reset, mirroring, bake and reanimate all iterate it.

- **Mirroring:** every transform channel copies its value, and the joint orientation decides whether that gives a behavior or an orientation mirror (D28). Every custom attribute must declare copy or negate when the module creates it. There is no default (author, 2026-11-10, replacing "copy as the default"): a missing declaration fails `getRigControlChannels` and every pose command, and the `ModuleContract` runs that query on every control, so a channel can't ship with an unconsidered mirror. The build itself doesn't check (changed while building, 2026-11-10: a check at the end of the write phase would fail after the scene had changed).
- **The declaration is an attribute category** (author, 2026-11-10): `mirrorCopy` or `mirrorNegate`, set through `RigPoseScene::setMirrorMode`. Checked in mayapy 2026: categories survive save, reference, import and duplicate, in ascii and binary files. The constraint's `space` enum is `mirrorCopy`. Rigs built before this have no category on `space`, so they need a rebuild before the pose commands work on them.
- **Built 2026-11-10:** `getRigControlChannels`, `zeroRigControls`, `mirrorRigPose` (Center controls keep their values) and `flipRigPose` (sides swap; a Center control flips through its channels when one rest axis lies along world X, and is left alone with a warning otherwise).
- **Build order (author, 2026-11-10):** the channel contract and the query command, then zero all and mirror pose as the first two tools, all before the IK limb. The other tools (key all, pose and animation files, keyed space switch) follow.
- **Channel naming rules** (author, 2026-11-10), for every custom channel a module adds:
  - camelCase, with the long name as the only name. No module name, side or control name in it (`stretch`, not `arm_l_stretch`), because the control already says which it is and mirroring pairs channels by name.
  - The same concept has the same name in every module: `ikFkBlend`, `stretch`, `space`. Look for an existing channel before inventing one, and put a new one's name in `docs/controls.md` the first time a module uses it.
  - A blend runs 0 to 1 with 0 at the first-named end (`ikFkBlend`: 0 is FK).
  - Enum labels are API too, since scenes store the index and tools show the label. Append labels, never reorder.
  - A rename needs an alias entry (D32). Prefer a new channel and a deprecation.
  - Every channel sets its default to its rest, and declares `mirrorCopy` or `mirrorNegate` through `RigPoseScene::setMirrorMode` before the attribute is added.
- **Why:** one interface for every module, so the animation tools need no per-module code. Rests read from attribute defaults can't drift from the build.
- **Rejected:** a transform-only interface, because modules have custom channels. A stored channel list (JSON on the control), a second copy that can drift from the attributes.
- **Consequence:** every module sets each custom attribute's default to its rest. Channel names are hard API (D32).

**Phase 2: query and selection scope** (author signed off, 2026-11-10). The gaps it closes: no command gave a control's markup or module type to Python (so tools would parse names or the attribute, against D24), `rigmodules.query` had no controls list or mirror lookup, and the pose commands only worked on a whole rig, where an animator mostly works on a selection.

- **`getRigControlInfo(*controls)`** returns JSON keyed by control name: `moduleType`, `module` (the module node), `side`, `usage` and `mirror` (the mirror control's name, `null` when it has none, the control itself for Center). Keys are in `ControlInfoKey`. A query, so not undoable. `getRigControls`, `getRigMirrorControl` and `getRigControlChannels` are unchanged.
- **`rigmodules.query`:** `RigControl` (`node`, `module`, `module_type`, `side`, `usage`, `mirror`), `get_rig_controls(rig_root, module_type=None, side=None, usage=None)`, `get_control(node)` and `find_control(node)` (raise or `None`, like `get_rig_info` and `find_rig_info`), and `get_channels(controls)`, one command call for many controls. `get_control_channels` is unchanged.
- **Scope of the pose commands.** `zeroRigControls`, `mirrorRigPose` and `flipRigPose` take either one rig root (every control, as before) or one or more controls from one rig (those only). The commands never read the selection (D14): the UI or Python passes it. Controls from two rigs, or a mix of a rig root and controls, is an error.
  - **zero** resets the given controls.
  - **mirror** writes each source control's values onto its mirror. With the rig root, the source side is `leftToRight` (default Left). With controls, an explicit `leftToRight` wins. Without it, the source side is the side of the given controls when they all share one, and Left otherwise (author's rule). Center controls are never a source. When both controls of a pair are given and the source side is Left, the Left one is the source and the Right one is overwritten.
  - **flip** swaps each given Left or Right control with its mirror (a pair given from both ends swaps once), and flips each given Center control through its channels.
  - A control with no mirror gets a warning and is skipped, as in the rig-wide form.
- **Rejected:** an error when both controls of a pair are selected (the mixed-side rule already says what happens). A separate `*Selection` command per tool (the object list already says which scope).

**Phase 3: pose data and key all** (designed, author agrees, 2026-11-10; built after phase 2).

- **One pose data path.** In memory a pose is `(side, usage) -> channel -> value`, in UI units. Every tool is capture and apply: zero applies the rest values, mirror and flip capture and apply with the mirror transform, and a pose file is read and applied, optionally mirrored. The transform (copy or negate per channel, the Center axis flip) lives once, in `RigPoseScene`. The three built pose commands move onto it (author, 2026-11-10), so pose files can't drift from `mirrorRigPose`.
- **Files** are keyed by control markup, never by name, so a pose survives renames and retargeting (D25). Enums store the index (labels are append-only). A control or channel the file has and the rig lacks warns and is skipped, and one the file lacks keeps its value. Format in `core/`, with a `formatVersion` that goes through the D33 runner. Commands: `saveRigPose`, `loadRigPose(mirror=False)`.
- **An animation file is a sequence of pose data** (author, 2026-11-10): the same pose format per frame, so animation save and load, bake and reanimate share the pose reader and writer. Its frame, key and tangent layout is designed with the keyframe command below.
- **Key all** is Python, `rigmodules.pose.key_controls(controls)`: it reads `get_channels` and calls `cmds.setKeyframe` once with every plug, because one call is far faster than one per channel. It sets nothing else. Pose commands set values and never key, so auto-key stays the UI's job.
- **Built 2026-11-10:** `core/PoseFile` (format version 1, through the D33 runner), `RigPoseScene::getRigPose` / `setRigPose`, `saveRigPose` and `loadRigPose` (rig root or controls, `file`), and `rigmodules.pose` with `key_controls`, `key_rig` and `key_modules`. Pose files are keyed by markup, values are in UI units (angles in degrees by default), an enum is its index. A selection loads only the controls it names. A whole-rig load warns about file entries the rig lacks and rig controls the file lacks. Pose files live in the asset's `poses/` folder (D29), one `<name>.json` each, shared by every variant. mayadev's RigModules menu has a pose submenu (save and load pose, mirror, flip, zero, key all controls, key selected modules) that acts on the selected controls, or the whole active rig with none selected.
- **Deviation from the design above:** `zeroRigControls`, `mirrorRigPose` and `flipRigPose` were not moved onto the capture and apply path, and `loadRigPose` has no `mirror` flag. Doing both is the next step, so the transform lives once. Until then a mirror on load is a load followed by `mirrorRigPose`.
- **Keyframe command, built 2026-11-11** (author's request): `RigKeyScene::setRigKeys` writes every key of a call at once. A channel with no curve gets one through the `MDGModifier` (so undo deletes the node), then each channel's keys go in with one `MFnAnimCurve::addKeys` call recorded in an `MAnimCurveChange` (so undo and redo restore the keys, on a new curve or an existing one). `RigKeyEdit` holds both, and a command's `undoIt` is `curveChange.undoIt()` then `dgMod.undoIt()`. It is the path of `setRigKeys` and of `animateRigFromJointClip`, so animation restore is one undo. `getRigKeys` reads every channel's curve back. Both speak `{side: {usage: {channel: [[frame, value], ...]}}}` (`core/RigKeys`), by control markup like a pose, in UI time and UI units. It is a JSON string, not a file the system owns, so it has no `formatVersion`. A channel's new keys replace the curve's keys from its first new frame to its last, and keys outside that range stay: that is `addKeys` with `keepExistingKeys` false, and it is what a range restore wants (`test_the_new_keys_replace_what_lies_between_them`, `test_keys_an_animator_set_on_other_frames_survive`). Rejected: `keepExistingKeys` true, which would leave stale keys between a clip's frames. Enum, bool and integer channels key stepped and the rest use the global default tangents. Rejected: a `keyRigControls` command that keys the current values (key all stays `rigmodules.pose.key_controls` until someone needs it faster), and a keys file format with a version (nothing stores it yet). Bake is still to do, and it can use the same path.

### D35. Weight files are keyed by joint markup, mesh id and topology hash
**Status:** Decided (2026-11-09). Built and run in mayapy 2026: save, load, in place, undo and redo, every error below, binding at rest on a posed rig (`tests/maya/commands/test_skin_weight_cmds.py`).

Weight save and load are undoable C++ commands, like the shape commands (D26), with the file format in `core/`. mayadev's `bmaya.maya.rigging.weights.weightio` is the OpenMaya reference. Influences are keyed by joint markup `(side, usage)` (D15). Weights load onto a mesh only when both its mesh id and its topology match the file:

- **`meshId`:** a GUID string attribute on the mesh transform, stamped once when the mesh enters the asset. It is attribute data, so it survives import, reference, rename and reparent. Duplicating a mesh copies it, so a save fails when two meshes share one.
- **Topology hash:** 64-bit FNV-1a over the vertex count and `MFnMesh::getVertices`' polygon counts and connects, which is vertex order plus connection order. Positions and UVs are left out, so a sculpt or proportion change keeps its weights. A fixed algorithm, not `std::hash`, so the value is the same in every build.
- The file stores the mesh id, the vertex and face counts and the hash. A mismatched id or hash fails, and the error reports the counts.
- **Why (author, 2026-11-09):** joint markup alone can't tell meshes with different topology apart. Weights are shared only when the joints' side and usage and the mesh id all match.
- **Rejected:** Maya's node UUID as the mesh id. Storing the full counts and connects arrays, which costs hundreds of thousands of numbers per dense mesh for a better error message.
- **Commands (author, 2026-11-09):** `setRigMeshId(*meshes, newId=False)`, `saveRigSkinWeights(mesh, file=...)` and `loadRigSkinWeights(mesh, file=...)`, with the `Rig` prefix like every other command. One mesh per save and load. Looping over an asset's meshes and naming the files belongs in mayadev's `weights` handler (D24). mayadev's model publish can call `setRigMeshId`, so the weight commands never depend on mayadev. The attribute is `RigAttr::MeshId` (`meshId`).
- **`setRigMeshId` reads the selection** when no mesh is named, filtered to meshes (author, 2026-11-09). It and `pruneToMaxInfluences` are the exceptions to D14's rule that commands never use the selection. The flag is `newId`, not `new`: Maya rejects a three-letter long flag name.
- **Load updates an existing skinCluster in place** (author's choice): it adds the file's missing influences, removes ones the file doesn't list and sets every weight, so the cluster keeps its place in the deformer stack. Rejected: deleting and recreating it, as mayadev's `weightio` did. A mesh without one gets a new one. The skinCluster command creates it and edits influences, queued in an `MDGModifier` as MEL, because the API can't. Weights go through `MFnSkinCluster::setWeights`, and its old values are kept for undo.
- **New influences bind at rest** (author's choice). Each one's `bindPreMatrix` is the inverse of `RigTraversal::getJointRestWorld`, so a load on a posed rig binds the build pose (D32). That needed the joint's rest stored on its module: `RigModuleWithJointsNode` gained a `jointRestWorld` array, written by a new `wireJoint` helper that FkChain and Aim now share. `wireJoint` replaced the four joint wiring steps both modules had copied. A joint no rig drives uses its current world matrix.
- **Save and load never change a weight** (author, 2026-11-09, reversing the 6-decimal rounding and the normalize on load from the first version). Weights are written with `std::to_chars`, the shortest text that reads back as the same double, so a round trip is exact. Neither command rounds, prunes or normalizes: a vertex whose weights don't sum to 1 within 1e-5 fails the save, and fails the load of a hand-edited file, naming the vertices. Pruning is separate: Maya's Prune Small Weights covers small weights, and `pruneToMaxInfluences` limits influences per vertex (below). `normalizeWeights` is still switched off for the length of each `setWeights` call and put back, because interactive normalization warns partway through a vertex even when the values sum to one.
- **Clean diffs** (author, 2026-11-09): one line per vertex, and one per blend weight. Influences are sorted by (side, usage), so the skinCluster's own influence order never reaches the file. Changing two vertices' weights changes two lines (checked on a 102,082-vertex mesh). Adding an influence renumbers the ones after it, which is accepted.
- **Speed** (author asked for very fast, 2026-11-09). Measured on a 102,082-vertex sphere with 60 influences, 4 per vertex, 11 MB file: save 0.3 s (Maya's `getWeights` 0.19 s of it), load in place 0.75 s (JSON parse 0.25 s, `setWeights` 0.44 s), load onto a new skinCluster 1.0 s (the `skinCluster` command adds 0.31 s). Numbers are written with `to_chars` instead of a json value each, the parser builds an error path only on an error, and a load that creates the skinCluster skips `setWeights`' old-value copy, since undo deletes the node. The rest is Maya's own calls and nlohmann's parse. A hand-written parser for the weights list could cut most of the parse time, at the cost of a second JSON reader: not done.
- **Whole weight tables only** (author, 2026-11-09): weights are read and written for every vertex and every influence in one `getWeights` or `setWeights` call, never one influence or one vertex at a time. The one smaller write is a set of whole rows chosen by the data (the vertices a prune changed), still one call. Tables are copied out of `MDoubleArray` once, because its `operator[]` is an exported call per element. Scans for meshes and joints use `MItDag` filtered to `kMesh` or `kJoint`, which hands over each path, instead of listing a namespace and looking up a path per node.
- **`pruneToMaxInfluences(*meshes, maxInfluences=n)`** (author, 2026-11-09). On each vertex with more than `n` non-zero weights it keeps the largest `n` and divides them by their sum. Equal weights keep the lower influence index. Vertices within the limit aren't written, so they keep their exact weights. It ignores locks, as Maya's own prune does, and leaves the skinCluster's `maxInfluences` settings alone. The pruning is a Maya-free function on the whole table (`core/SkinWeightTable`). It reads the table in one call and writes only the changed rows in one call, keeping the old rows for undo. 102,082 vertices with 60 influences, pruned from 4 to 3 per vertex: 0.75 s, undo 0.31 s, a run that changes nothing 0.29 s. Almost all of it is Maya's `getWeights` and `setWeights`. Small weights are left to Maya's Prune Small Weights.
- **Binary file format: not adopted yet.** The author allowed a binary format, since nobody hand-edits weight files. It would also give up clean diffs, which the author asked for, and the text parse it would replace is about a third of a load (0.25 s of 0.75 s on the mesh above). Open: binary, a faster text parser, or keep JSON.
- **Other choices:** an influence marked `skin: false` fails the save (author), the first code that reads that key. The format is JSON (rejected: binary, which can't be read or diffed). The plugin now links `OpenMayaAnim.lib` for `MFnSkinCluster`.
- **Consequence:** the weight file keys, `formatVersion`, the `SkinMethods` names, `meshId` and the three commands are public API. Modules have a new stored attribute, `jointRestWorld`. Rigs built before this change don't have it, so a load that binds a new influence to one of their joints fails until the rig is rebuilt.
- **Pipeline (2026-11-09, author asked for menu tools and weights in the build):** RigModules `python/rigmodules/skinweights` holds what a tool needs to loop over meshes without hard-coding RigModules names: meshes by `meshId`, skinned meshes in a namespace, and a weight file's `meshId`, read from the file's top lines. mayadev's `weights` handler saves one file per skinned mesh in the variant's `weights` folder, named after the mesh, and loads by the `meshId` inside each file, as one undo step. A file left from a mesh's old name is removed on save. The folder is versioned before each save. `build_rig` imports the variant's model into the asset namespace and then loads its weights. `meshId` lives in the model file, so meshes are marked (Mark Meshes for Skinning) in the model scene before Save Model, which saves the open scene as the variant's model and warns about unmarked meshes. Rejected: naming files by `meshId` (unreadable), and matching files to meshes by name (breaks on a rename). Choice made while building, open to change: Save Model saves the whole open scene, like Save Skeleton, rather than exporting the selected meshes, which would also carry the rig namespace into the file.
- **Not built:** mirroring weights, a closest-point transfer for changed meshes, and weights for other deformers. Each would be a separate tool.

---

### D36. Rigs are optimized for parallel evaluation, and the goal is set against Maya's native floor
**Status:** Decided (author, 2026-11-10). The goal was amended on 2026-11-11 and the author signed the amendment off the same day. The original goal stays below as history.
Animators play rigs back in Maya's parallel evaluation manager, so that is the mode we design, write and measure for.
- **Original goal (superseded 2026-11-11):** 30 fps (1:1 playback) for a scene of 100 heavy rigs.
- **Goal (author, 2026-11-11):**
  1. **Shot scale:** 20 of the complex rig run at 30 fps or better in parallel evaluation (a 33 ms frame, 1.7 ms per rig). The measured edge on the author's machine is about 25 rigs, so 20 leaves margin for the run-to-run noise (up to about 30% between sessions).
  2. **Rig layer cost:** on the 50-rig scene (default since 2026-11-11, because 100 took too long and too much of the machine; the ratio does not depend on rig count, the measurement below was on 100), the full rig costs no more than 1.5 times the native floor (below). Measured 2026-11-11: 1.25 to 1.45. This is the regression guard for a new module or constraint type.
  3. **Stress benchmark:** the 50-rig scene (was 100), and its fps is reported with no pass or fail.
- **Measured (2026-11-10, 8 physical cores and 16 logical, headless, no viewport):** 100 Human rigs (1,600 joints) run at 148 fps in parallel (6.8 ms/frame). 100 of the generated complex rig (200 joints, 52 modules and 51 constraints each, 20,000 joints in all) run at 8.3 fps in parallel (121 ms/frame), about 4 times short of the goal. Serial evaluation was 1.1 fps and plain DG 2.0 fps on the same scene. In a Maya window, with the viewport and UI, the earlier complex scene ran at 1.1 fps.
- **Where the time goes (2026-11-11, Maya profiler, 8 frames of the 100-rig complex scene):** about 510 ms of thread CPU per frame. `rigControlNode` 38%, `animCurveTA` 25%, `joint` 20%, `fkChainModule` 12%, `rigConstraint` 5%, `aimModule` 0.5%. Our modules, constraints and aim nodes are about 18%, so removing them entirely would give about 1.2 times.
- **Scaling (2026-11-11):** frame time is linear in rig count, about 1.2 to 1.4 ms per rig from 1 to 100 rigs, so the scene is bound by total work, not by dependency depth. By thread count (25 rigs): 249 ms at 1 thread, 110 at 2, 60 at 4, 44 at 8, 48 at 16. Scaling stops at the 8 physical cores, and the 8 SMT threads add nothing.
- **Native floor (2026-11-11, 100 rigs, no pulls, two alternating rounds):** 194 native `transform` controls per rig with 3 rotate curves each, plus 200 native joints driven 1:1 by control world matrices, no module, constraint or custom node: 76 and 84 ms/frame. The full rig on the same runs: 110 and 105 ms. Controls and curves alone: 43 and 63 ms. The floor is already 2.3 times the 33 ms a 30 fps frame allows, so the original goal could not be met by any change to the rig layer. The scenes are flat (no DAG hierarchy, shapes, viewport or skinning), so the floor is approximate. A `rigControlNode` in place of the native transform costs about 18 to 21 ms more in the graph step at 100 rigs. Paired runs in one session, three rounds: native 51 to 56 ms, custom 71 to 78 ms.
- **Chain solves (2026-11-11, DG mode, 1 rig, every joint and control world matrix pulled):** `fkChainModule` computes 2.17 times per frame per module and `aimModule` 2.0. Each output array (`controlOPM`, `jointOPM`, `outputSocketMatrix`) is its own compute and solves the whole chain, and the sockets are only consumed on 8 of 46 modules. All three outputs are dirtied by the same inputs, so one solve that writes all of them would do. The saving is estimated at 6 to 8% of node CPU. It is not measured in parallel evaluation, where about 0.76 compute events per module evaluation were seen, and not built. **Done 2026-11-11:** `fkChainModule`, `aimModule` and `layoutModule` each solve once per evaluation and write and clean every output array together (`computeChainOutputs`, `commitOutput` in `RigModule.h`). Measured in DG mode on the 100-rig scene, one pull of every joint: 1.00 computes per module (4,600 FkChain, 600 Aim). In parallel evaluation the frame time did not change measurably (127.7 and 129.2 ms against 130.2 ms before, inside the session noise), so the saving only shows where compute is called per output. The last control of a chain also skips its unused child delta. Pinned by `test_contract_one_solve_fills_every_output` (compute count by `dgtimer`).
- **Why:** parallel evaluation is the only mode that gets close, and it is the only one that scales with the core count. A rig that is only fast in DG or serial mode is not fast.
- **Consequence for modules and constraints:** `compute()` must be safe to run on any thread and independent of every other node's `compute()`: no shared or static mutable state, no `MGlobal` or MEL calls, no scene queries, and no allocation or string lookup in the hot path (the existing rule). Declare every dependency with `attributeAffects` and connect every input as a plug, because the evaluation graph is built from connections. A node should not need a pull from another node that isn't an input. Fewer node evaluations is the lever: cost is linear in node count, and Maya's per-node overhead is a large part of it. Depth was not the limit in the 2026-11-11 measurements.
- **Not yet done:** no node overrides `schedulingType`. The native floor has no tool yet: `tools/perf/perf_scenes.py` has no `floor` command, so the 1.5 times check is a manual comparison. The cause of the `rigControlNode` premium is unknown: neutralizing its `setDependentsDirty` override changed nothing (probe, reverted). Reading a joint's world matrix costs the same with either control type, about 17 to 25 ms for 5,600 pulls. Each pull computes the joint serially on the main thread, so `bench`, which pulls the chain tips, is pessimistic by about 10 to 20% on the 100-rig scene. Any change to the control node type needs its own decision, because the type name is public API (D33).
- **Process:** a new module or constraint type adds many instances to the complex perf rig in the same change, and `bench` is run on it, on the 20-rig scene against 30 fps and on the 50-rig scene against the floor (D12, "Gate for new modules"). The perf run is manual and never part of the suite, because it takes too long: `tools/perf/perf_scenes.py`, documented in `tests/README.md`. `check` is its quick validity check.

### D37. Control drawing stays an MPxDrawOverride, with cached bounds and a callback-kept selection set
**Status:** Decided for the cheap fixes (author go-ahead, 2026-11-11). Built and tested in Maya 2026: mayapy suite, `ViewportGallery.cmd` and `perf_scenes.py profile`. Whether to replace the draw override is **Open**, see below.
`rigControlNode` keeps its retained `MPxDrawOverride`. Its bounds are cached on the node, its draw data is versioned by a generation counter, the selection lookup is a hash set kept by an `MModelMessage::kActiveListModified` callback (`ActiveSelection`), and its callbacks report to Maya's Profiler in a `RigModules` category (`ProfilerUtils`).
- **Measured (2026-11-11, GUI, parallel evaluation, cached playback off, `perf_scenes.py profile`):** during playback the override's `prepareForDraw`, `addUIDrawables` and `boundingBox` are not called at all. VP2 keeps each control's UI drawables and moves them with its matrix. They run on first draw, on a shape edit and when a control's display status changes. So the bounds cache and the selection set don't change playback speed (frame-time differences between builds were within session noise). Selecting every control of 100 Human rigs (1,400) went from 478 to 151 ms per select or clear with its redraw, because the old code copied and scanned the whole active list once per re-prepared control (quadratic). Selecting 14 controls: 30 and 27 ms.
- **Multi-viewport fix:** the dirty flag lived on the node and the first override to draw cleared it, so another panel's cached data could keep an old shape. Each `ControlDrawData` now keeps the node generation it read.
- **Where draw time goes:** 100 Human rigs (1,400 controls): drawn 29 ms/frame, controls hidden 27, suspended 8. 50 complex rigs (9,850 controls, chains about 30 deep): drawn 276 ms/frame, controls hidden 78, suspended 80, joints hidden 268. So the controls cost about 2 µs each on Human and about 20 µs each on the complex rig, all inside VP2 (`Vp2UpdateScene`, `xrayUIList`), not in plugin code. The per-control difference points at hierarchy depth or the transparent, xray queue, not at the shape count. Not measured yet.
- **Evaluation:** `rigControlNode worldMatrix` is the largest evaluation row: about 16 µs per control per frame against about 6 µs per joint (Human), and 230 ms of thread time per frame on the 50-rig complex scene. That is D36's open `rigControlNode` premium, and this change doesn't touch it.
- **Rejected for now:** an `MPxSubSceneOverride`. It would need world-space render items updated per frame, it has no xray (a shader pre-draw callback would have to turn off the depth test), and a per-rig version needs a DAG host node and reimplements visibility, layers and isolate select. Decide only after the cheap draw experiments below. Also rejected: `MPxLocatorNode` (a shape needs a transform parent, so twice the nodes).
- **Draw experiments (2026-11-11, 50 complex rigs, 9,850 `Box` controls, playback ms/frame, a throwaway probe and a throwaway build, another Maya session open so absolute numbers are noisy):**

  | Variant | transparent (current) | `isTransparent()` false |
  |---|---|---|
  | drawn | 249, 251 | 221, 226 |
  | xray off | 296 | 255 |
  | controls unparented (flat) | 276 | 233 |
  | flat, xray off | 308 | 261 |
  | controls hidden | 76 | 73 |

  A separate run with the current build: drawn 282 and 270, `filled` off 205, hidden 75. So the transparent queue costs about 27 ms, xray saves about 40 ms (turning it off is slower), hierarchy depth isn't the cost (flat is slower), and the fill is about 70 ms. The wire alone is about 130 ms, about 13 µs per control for an 8-point mesh. The cost is VP2's fixed overhead for each UI drawable on each object, not geometry. Unexplained: Human controls cost about 2 µs each in the same kind of test.
- **Open, needs a design proposal and the author's sign-off:** a retained, batched draw. VP2's per-object overhead is the cost, so only drawing many controls per render item removes it: an `MPxSubSceneOverride` with instanced render items per shape type. The per-rig version (one host DAG node per rig, `getInstancedSelectionPath` back to each control) removes the most, but costs the animator-facing behaviour listed above. Prototype a per-control version with consolidation first (each control keeps its own DAG drawing, so visibility, layers and isolate select keep working) and measure it on this scene. Also possible without a rewrite: `isTransparent()` false when `wireframeAlpha` is 1, for about 10%, if VP2 reads it again when alpha changes (not checked).
- **Consequence:** shape attributes are data that is set, not driven by an animated connection: parallel evaluation doesn't call `setDependentsDirty`, so a driven shape wouldn't refresh its bounds or drawing (`docs/controls.md`). Profiler event names (`rigControl ...`) are developer-facing and may change.

### D38. The build makes three display layers, found by message attributes on the rig root
**Status:** Decided (author, 2026-11-11). Built and tested in Maya 2026 (`DisplayLayers` in `test_create_rig.py`).
After the modules are built, `createRig` replaces `jointsLayer`, `meshesLayer` and `controlsLayer` in the rig namespace (`RigNaming`) with new layers holding the input joints and every control, and an empty mesh layer (`DisplayLayerScene::replaceLayers`). Each layer's `message` connects to one of the static `jointDisplayLayer`, `meshDisplayLayer` and `controlDisplayLayer` message attributes on the rig root (`RigAttr`). There is no array of all layers: a tool wants a layer by role, and the three attributes give that (author, 2026-11-11).
- **Why:** animators and tools toggle or lock a rig's parts as a group, and the layers are the lookup a tool uses, not their names. The array and the three attributes both exist because the author asked for both: the array lists the layers, the attributes name the role. Layers are plain `displayLayer` nodes made in the same `MDagModifier` as the rig, so undo and redo cover them. Rejected: MEL `createDisplayLayer`, whose undo would run apart from the modifier's.
- **Deletion is by name,** only the three layers, so an animator's own layers in the namespace survive. A second rig variant built into the namespace replaces the first variant's layers, because the names are per namespace.
- **Meshes come from the model import, not the build** (author, 2026-11-11). The model file is imported after the build (mayadev `build_rig`), so the build can't see it, and scanning the namespace would also catch meshes that aren't the model. The mesh layer starts empty, and the importing tool calls `rigmodules.displaylayers.add_meshes(rig_root, new_nodes)` with the import's new nodes. Python, not a command: it only reads the `meshDisplayLayer` attribute and calls `editDisplayLayerMembers`, which is undoable. Rejected: a C++ namespace scan at build time.
- **Members:** a node already in another layer is moved to the rig's layer. An empty layer is still made, so the lookup always works.
- **Consequence:** the layers are public API (names and attributes). The layer manager is used the way `createDisplayLayer` does: `identification` and `displayOrder` are the first free id, and the layer connects to `layerManager.displayLayerId[id]`.

### D39. The build makes selection set hierarchies for controls and skin joints, found by markup
**Status:** Decided (author, 2026-11-11, restructured the same day). Built and tested in Maya 2026 (`commands/test_selection_sets.py`).
After the display layers, `createRig` replaces the rig namespace's selection sets (`objectSet`, `SelectionSetScene::replaceSets`) with two hierarchies, `controls_set` and `joints_set` (the skin joints only). Each holds `<kind>_left_set`, `<kind>_right_set`, `<kind>_center_set` and `<kind>_modules_set`, and the modules set holds one `<module>_<kind>_set` per module that owns members. `SelectionSets::planSets` in `core/` lays out each kind from `{module, side}` items, so the layout is Maya-free. Sets carry a JSON `setMarkup` (`markupVersion`, `kind`, `role`, `side`, `module`) and connect to `rigRoot.selectionSets`.
- **Why:** animators and skinners select a rig's parts as groups, and skinning tools need "the skin joints of this module or side" as data. Sets keep that in the scene, where an animator can also use them in the Outliner.
- **First version rejected (author):** it made 28 sets on the five-module test rig, with an "all" set per kind (controls, every joint, skin joints) holding members both directly and through module subsets, so a node could be in a set twice. Now there is one top set per kind that holds only other sets, and the items sit in exactly one side set and one module set. Selecting the top set selects each item once. The all-joints set is gone: the joints hierarchy is the skin joints, which is what skinning and animator selection need.
- **Markup, not a message attribute per set:** one array plus per-set markup follows D25 (D38 has three attributes), and `rigmodules.selection.find_set` filters it. `role` tells the top set, the modules container and a module set apart, since none of them has a side.
- **Module and joint ownership:** a control's module is its `controlMessage` owner, and a joint's module is the one driving its `offsetParentMatrix` (`RigTraversal::getJointModule`, now public). A skin joint no module drives is in its side set only. A module subset exists only when it has members, so Layout has controls but no joints set. The side sets always exist, even empty, so a lookup never has to handle a missing set.
- **Skinning helpers are Python** (`rigmodules.selection`): `get_skin_joints`, `select_skin_joints`, `get_missing_influences` and `get_extra_influences`. No new command: they read sets and markup only.
- **Build detail:** the `setMarkup` attribute is added by an internal `doIt`, because its value can only be queued once the plug exists. Sets are made in the same `MDagModifier` as the rig, so undo and redo cover them. Deletion is by name and covers only these sets, like D38, so a rebuild into a namespace whose modules changed leaves the old module sets behind.
- **Consequence:** set names, the `selectionSets` and `setMarkup` attributes and the `kind` and `role` values are public API.

### D40. A module owns its controls' rotate orders, and the build locks them
**Status:** Decided (author, 2026-11-11). Built; see the build and suite result in the change summary.
`createRigControl` takes the rotate order as a required argument, so every module states it. After the first `doIt`, `createRig` locks `rotateOrder` on every control (`queueRotateOrderLocks`, MEL queued on the modifier so undo and redo replay it). FkChain uses XYZ, and Aim and Layout pass XYZ explicitly, which is what they already had.
- **Why XYZ for FkChain (+X down the bone, +Z bend):** the middle axis of the order is the one that gimbal locks at 90 degrees (checked in mayapy 2026 for all six orders). Y, the side swing, is the axis that rarely goes far, so it goes in the middle. In XYZ, Z is also the outer axis, so the bend curve stays in parent space, and X, the twist, turns about the bone. The author suggested ZXY, which puts the twist in the middle: a forearm or shoulder twist near 90 degrees would lock. A one-line change in `FkChain.cpp` if the author prefers another order.
- **Lock in one place, not in each module:** a module can't lock its own plug while its node is still queued in the modifier. One pass after the build covers every module, and a new module only has to pick its order.
- **Consequence:** a new module must choose a rotate order for its controls. Animators and tools can't change it by `setAttr`, so Maya's rotate order switching and `Constraints.cpp`'s read of it see a fixed value. A different order for an existing rig is a module change and a rebuild. Saved poses are in euler values, so changing an existing module's order later changes what they mean: treat it as a format break (D33).

### D41. Controls are restored from joints by a module-owned inverse, with USD joint files and a pose-format sidecar
**Status:** Decided (author, 2026-11-11). Built 2026-11-11: the file types, the `inverse` descriptor field, the JointClip reader (`deserializeJointClip`), the JSON capture (`rigmodules.jointclip.capture_joint_clip`), the animation sidecar reader, `RigInverse::solve` with the inverse of FkChain, Aim and Layout, the constraint delta function (`RigConstraintNode::computeDelta`), both commands and the keyframe command (D34). Round trips pass for every module, mirrored builds, space switches and an Orient constraint. Not built: USD IO, the sidecar writer, mayadev's `anims/` folder, IK and retargeting. Full design, formulas, formats and tests in `inverse-solve.md`. Build order: core and driver with a single pose, then clips and the keyframe command, then USD IO, then mayadev, then IK, then retargeting. It comes before the IK limb.
Joints (and animated joints) become the main pose and animation storage. A solve turns joint matrices into control values, and a plain control-value library stays for the simple same-rig case.
- **Why (author, 2026-11-11):** control-value libraries restore 1:1 only on the rig they came from and fail on different proportions. Joints are the rig-independent carrier, and they let animation come from outside the rig.
- **Modules own the inverse.** `ModuleDescriptor` gets an `inverse` beside `plan`, built on the same helpers as `compute()`. A module that can't be inverted says so explicitly, so a new module can't ship without deciding. A shared driver sweeps modules parents-first in memory, running the constraint delta for the `space` value.
- **Modules only match world matrices.** They know nothing about source skeletons or proportions. Retargeting is a layer above that manufactures target world matrices.
- **Precedence (P4):** per channel, the joint-derived value, else the sidecar, else rest. The sidecar fills only what joints can't give. A missing joint is a warning.
- **Files:** USD (`UsdSkel`) for joints, with the joint markup JSON in `rigmodules:jointMarkup`, and a separate sidecar file with the same basename. A pose's sidecar is the pose file format. A clip's sidecar adds one entry per frame. C++ never links USD: Python reads and writes it with pxr and hands C++ a transient JSON JointClip (a list of JointPoses).
- **Keys (amended 2026-11-11, author):** every channel is baked every frame, derived and sidecar alike, and no tangent data is stored. Joints are samples, so one rule covers all channels. Curve simplification is a separate tool.
- **Commands:** `animateRigFromJointClip` (keys) and `poseRigFromJointPose` (values only). The terms are JointClip and JointPose.
- **Proportions:** `mode` accepts only `match` for now, and the source's rest is kept in the files, so a retarget layer can later manufacture target world matrices above the module functions. The calibration pose, the offset delta and its composition order were dropped (author, 2026-11-11) and are not reserved anywhere.
- **Rejected:** linking USD into the plugin. FBX. Sparse sidecar keys and tangent data. Per-module restore code in Python. Setting attributes and pulling plugs once per frame.
- **Requirements on the IK module:** a closed-form inverse, FK and IK controls both solved from joints whatever `ikFkBlend` is (which also gives IK/FK match), and a declared list of derived custom channels.
- **JointClip details settled while building the reader (2026-11-11):** the file has a required `startFrame` (the frame number of its first pose), so a clip can be keyed where it was captured. Translations are in Maya's internal units (centimeters), not the scene's, because the solver works on `MMatrix` values. A joint's local transform is its world matrix relative to its nearest captured ancestor, so capture doesn't depend on rig joints inheriting transforms. A rest comes from the build pose stored on the driving module. Rejected: transforms in scene units (the solver would convert on every read), and the DAG parent as the hierarchy (a captured set can skip joints).
- **Built as designed, with these settled while building (2026-11-11):**
  - **The module contract is matrices only.** A module's inverse takes world matrices and returns control *local matrices*, never channel names or units. `ModuleInverseInput` holds `jointWorld`, `parentDelta` and `fallbackLocal` (the control's local from the sidecar, or rest, for a joint that is missing), all by the module's slot, the logical index its `controlData`, `jointOPM`, `outputSocketMatrix` and `childPlugs` arrays share. `ModuleInverseResult` holds `controlLocal` (empty where the joints gave nothing), `socketWorld`, `solved`, `reason` and `warnings`. The driver alone turns a local matrix into channel values (translate, the euler nearest the previous frame in the control's own rotate order, scale, a shear warning) and back. This replaced the skeleton's `derivedChannels` list and `ModuleInverseContext::jointRestWorld`, which nothing needed, and made `ModuleInverseFn` a plain function pointer.
  - **FkChain and Layout share one chain walk** (`ModuleInverse::solveChain`): `L = W * D^-1 * rest^-1` for a control whose joint is present, `fallbackLocal` otherwise, and the forward step through `RigModuleNodeBase::solveControlStep`. Layout is the same walk with no joints. Aim solves target and up the same way and builds its aim joint with the helper `compute()` uses (`computeAimJointWorld`, `computeAimRestOffset`, factored out of `solveAim` and `createModule`), so a source aim joint that disagrees by more than 1e-3 is a warning.
  - **The Layout is solved first, because the sweep orders every module after the modules that feed its child plugs.** Its controls come only from the sidecar. Without a sidecar entry they are at rest, and the controls below it take up the difference, so the joints still match (tests `test_without_a_sidecar_the_layout_is_at_rest_and_the_controls_below_carry_the_move`).
  - **The space switch uses the constraint's own function.** `RigConstraintNode::computeDelta(type, childRest, parentDelta, driverDelta)` was the file-local `constraintDelta` and is now shared by `compute()`, `createConstraint`, `matchSwitchSpace` and the sweep. The sweep picks the driver from the sidecar's `space`, else the constraint's current driver.
  - **What the solve writes** (clarifying P4): a control's transform channels always get a value (joints, else sidecar, else rest). A custom channel, `space` included, gets one only when the sidecar has it, because nothing derives it and rest would flip a space the sweep solved in. So a Layout control is keyed at rest on every frame of an animation without a sidecar. That is the design's rule (every channel, every frame), and it is the surprising part for an animator who keyed the layout by hand: the keys on frames inside the range are replaced.
  - **Frame rates are errors.** A clip must run at the scene's frame rate (the keys land on its own frame numbers) and a sidecar at the clip's. `animateRigFromJointClip` takes `startFrame` and `endFrame` inside the clip.
  - **The source check** compares each matched joint's rest with the rig's `jointRestWorld` and warns above 0.01 cm.
  - **Rejected while building:** a `solved` flag per control (the module reports one flag and a reason, and the driver names the missing joints), and per-frame warnings (each distinct warning shows once per call).
- **Consequence:** every new module needs an inverse and a round-trip contract test (`ModuleContract.test_contract_inverse_round_trip`, its mirrored version and the missing-joint test, which every module gets by subclassing). Markup keys are load-bearing in a second file type. No existing name changes. New permanent surface: `getRigKeys`, `setRigKeys`, `core/RigKeys`, `RigKeyScene`, `ModuleInverse.cpp`.
## Open items

- **`compute()` review:** done 2026-11-11, no open item. Plug grouping: every module and constraint `compute()` reads its plugs once, by `MObject` attributes resolved at `initialize()`, with no string lookup, allocation or `MGlobal` call, and writes in place into existing output elements. The layout `end` socket to FkChain `start` connection: `connectSocketMatrixToChildPlug` stores the socket's rest on the child plug and connects its live matrix, so the delta is identity at build time. Layout rests are identity, so the delta is the socket's world, and an FkChain on a rotated layout follows rigidly (pose cases `child_chain_rest_on_rotated_socket`, `rotated_parent_carries_child_chain`). Removed five uncalled `RigModuleNodeBase` helpers (`getInputMatrix` x2, `getInputCompoundMatrix`, `setOutputMatrix` x2): two called `MGlobal::displayError` and one built array elements from compute, both against D36. Not changed: `commitOutput` builds a `MPlug` per output per compute (three per module), which is cheap but unmeasured against D36's node overhead; `solveFkChain` assumes dense control indices 0 to n-1, which every module guarantees.
- **D9:** where node creation names should live.
- **D13:** `core/` and `maya/` are split by folder, but `core/` is not yet its own library and has no tests that run without Maya.
- **D24:** the TODOs in `mayadev-integration.md` (the `python/` package, `.mod` files), plus control sets. Asset folders moved to D29.
- **D29, D30:** the template authoring UI (after a module interface query), building a second variant into a namespace, the publish step, the out-of-date check, and a Perforce adapter when a project uses Perforce.
- **D29 (potential):** asset GUIDs for references between assets, if renames and moves become common. Not needed now: ids are relative to the projects root, so they survive branching. See "Potential TODO: asset GUIDs" in `asset-model.md`.
- **D29:** a later pass moves asset ids from `|` to `/` separators (author, 2026-11-08). It has to be done together with the existing tools that read `|` ids, and with a migration for existing rigs whose `assetId` uses `|`.
- **D28:** mirroring poses and animation, built on `getRigMirrorControl` with the animation tools.
- **D26:** the selection highlight still reads the active selection list once per control per `prepareForDraw`, which scales with control count times selection size.

### Before adding more modules

Work that every new module would inherit, so it is cheaper to settle before the module count grows. Checked against the repo on 2026-11-07. The control registry and naming (D15, D25), template validation (D17), space switching (D19, D27), module mirroring (D28), undo, the mayapy suite (D20) and the folder split (D13) are already done. The connect-time jump no longer reproduces: a chain on a rotated socket keeps its rest and follows rigidly (2026-11-07, pose cases `child_chain_rest_on_rotated_socket` and `rotated_parent_carries_child_chain`).

The asset root, asset folders, the `python/` package and the `.mod` files are done (D29, D30). Updated 2026-11-09, in this order:

1. **Weight save and load commands** (D35). Done 2026-11-09.
2. **Build Rig from Asset… in the menu (mayadev).** Done, works (author, 2026-11-10). A file browser on the project root, filtered to `asset.json`, that builds a variant (the active rig's by default) and then loads its shapes and weights. Shapes and weights save to the active rig's asset with no file browsing, as shapes already do.
3. **Dev reload tool (mayadev).** Not in the menu yet (2026-11-10). The author has their own unload code to start from. Tear down everything tied to the plugin (the active rig tracker's callbacks, the RigModules menu), start a new scene, unload the plugin, load a fresh copy of the built `.mll` so the build output stays unlocked, then rebuild the menu and tracker. Keeps Maya open across builds.
4. **Sharing pass** (author, 2026-11-09). A lot of code landed in a few days, and some of it repeats in near-identical forms. Go through `maya/` and move every shareable helper to the shared layer (rule in `CLAUDE.md`, "Look before you write", step 6), merging near-duplicates into one. `Utilities.h` stays one file in `maya/shared/` (author, 2026-11-10; rejected: a `maya/shared/utils/` folder with a file per topic). Known candidates, found 2026-11-09:
   - Name lookups. Done 2026-11-10 (author's shape): two functions, `DagUtils::getMObjectFromName` (any node, quiet) and `DagUtils::getDagPathFromName` (a DAG node, with an optional `NodeKind` of joint or mesh, reports). They replace `getJointDagPathFromName`, `getJointObjectFromName`, `MeshUtils::getMeshTransformFromName` and the old unchecked `getDagPathFromName`, which had no callers. The messages are now the same for every kind: `No DAG node named <name>` and `<name> is not a joint.` or `... mesh.`.
   - Two namespace conventions: `DagUtils::getAbsoluteNamespace` gives `":rig"` or `":"`, `ConstraintCmds` builds `":rig"` or `""` for socket ids, and `RigRoot.cpp` has its own `qualifiedName`. One helper for "a node's namespace" and one for "a name in a namespace".
   - Selector text in messages: `formatJointSelector` (core) gives `[Left, upperArm]`, while `RigModule.cpp` and `JointMarkupIndex` errors still write `side, usage` their own way.
   - "Exactly one object" argument parsing: `readRigRoot` in `RigControlCmds.cpp` and the mesh reading in `SkinWeightCmds.cpp`.
   - `readTopology` in `SkinWeightScene.cpp` (a mesh's counts, connects and hash) belongs in `MeshUtils` once a second tool needs a topology check.
   - Already moved on 2026-11-09, with the weight commands: the required file flag, text file read and write and `displayReport` (from `RigControlCmds`, `SkinWeightCmds` and `RigRoot`), the mesh lookups, reading meshes named or selected, the all-vertices component, MEL quoting, selector formatting, the nearest upstream node, a node's namespace, and the namespace joint scan (now `JointMarkupScene::collectNamespaceJoints`, on `collectMarkedJoints` and `JointMarkupIndex`).
5. **Animation interface query and animation tools** (D34), planned separately: key all, zero all (go to bind pose), save and load pose, save and load animation, mirror pose and animation, and a keyed space switch (match on the switch frame, key the frame before). Built on `getRigControls` (D25) and `getRigMirrorControl` (D28). A pose is control values plus the `space` enum (D27). Their files carry a `formatVersion`, like the shape file (D26). The contract, the query, zero all and mirror pose come before the next module, the rest after (D34, 2026-11-10).
6. **Template `formatVersion` and its update table** (D33), before the next module (author, 2026-11-10).
7. **`moduleVersion` update table and methods** (D33), before the next module (author, 2026-11-10). `moduleVersion` is already a template key, checked against the module's current version, with a comment in `RigModule.h` marking where migration goes. No update step exists yet.
8. **Channel naming rules** (D32, D34), written before the next module.

**The next module is the IK limb** (author, 2026-11-10). It goes through the gate (D12) after items 5 to 8.

Design for these now, build them later:

- **Format version callbacks** (D33): the open, import and reference load callbacks that offer an update.
- **Referenced rigs** (D32): the namespace strip in Save Rig, `getRigInfo` in a referenced file, the alias list and the shot update tool.
- **Rebuild from template keeping rigger tweaks.** Shape files (D26) and weight files (D35) cover shapes and weights. Nothing covers other tweaked values.
- **Plugin deployment** per Maya version (the `.mod` files in D24).
- **Empty namespace after undo** (D18): undoing a build leaves the rig namespace behind.

## Adding an entry

Use this shape:

```
### Dn. Short title
**Status:** Decided | Leaning | Open
What was decided, in one or two sentences.
- **Why:** the reason, including what was rejected.
- **Consequence:** what this constrains.
```
