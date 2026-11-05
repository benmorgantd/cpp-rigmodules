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

- Shared attribute names are string constants in `Constants.h`, namespace `RigAttr`.
- Module-level code keeps using the module's own static `MObject` attributes.
- Shared rigmodule code looks attributes up by name (`findAttr`) and uses string-based `findPlug`.
- This fixed the socket-to-child-plug connection (switching `findPlug` to strings).
- Do not duplicate attribute name strings. Add them to `RigAttr`.

### D8. Reuse before writing new helpers
**Status:** Decided (working rule)

Before writing a helper, check `utilities.h` and `rigmodule.h`. Reuse what exists, or make a small backward-compatible edit. Follow code-sharing and structural reuse practices to limit complexity growth. Full rules are in `CLAUDE.md`.

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
**Status:** Leaning

The author is interested in splitting the code into a Maya-free core library and a thin Maya plugin layer. It matches how they structure their Python code. The intended split is in `CLAUDE.md` ("Planned structure"). Not yet implemented in the repo as far as the recorded notes show.

- **Rationale not recorded** beyond consistency with their Python structure. A likely benefit is testing without a Maya install, but that is not a recorded decision.

---

## Conventions

### D14. Code style and naming
**Status:** Decided

- Every curly bracket starts on a new line.
- `MObject`: `oMyName`. `MPlug`: `pMyPlug`. `MFn*`: `fnMyMfn`. Module attributes: `aMyAttributeName`.
- Pass lvalue `MObject`s to any function taking a non-const `MObject&` (for example `MPlug::child`). Look the attribute up on its own line into a named variable first. Details in `CLAUDE.md`.
- Command flags are read and documented by long name only (`-usage`, not `-us`). `MSyntax::addFlag` requires a short name and rejects anything over 3 letters (verified in Maya 2026: `-skin` and `-usage` fail and the flag silently never registers), so short names appear only inside `newSyntax()`, wrapped in `CHECK_MSTATUS`. Added 2026-11-04. Older commands (`CreateParentConstraintCmd`, `SetConstraintSpaceCmd`, `RigModuleCommandUtils`) still use short names and have not been migrated.

---

## Joints

### D15. Joint markup replaces joint names in templates
**Status:** Decided (2026-11-04). Built, not yet verified in Maya.

Each joint carries one JSON string attribute, `rigMarkup`: `{"markupVersion":1, "usage":"upperArm", "side":"Left", "export":true, "skin":true}`. All keys are required. Markup says what the joint is, and never names a module. Template `moduleArgs` pick joints with `[side, usage]` selectors (`"joints": [["Left","upperArm"], ...]`, `"aim": ["Left","eye"]`). Each selector must match exactly one joint. Joint names no longer appear in templates, and there is no mixed mode.

- **Why:** Joint markup is a separate pass from rig building, and joint identity should not depend on naming. The same template then builds on any skeleton with matching markup.
- **Rejected:** markup naming the module and role it belongs to, because that ties markup to module usage. Modules declaring joint roles in `ModuleDescriptor`, for the same reason. Usage groups such as `"arm"` that expand to several joints. Matching many joints by usage and ordering them by DAG depth. A `mirror` key: mirrors will be found from markup by pairing `usage` across `Left`/`Right`.
- **Rules:** `(side, usage)` is unique within a namespace. Only one module can claim a joint, because its OPM has one driver. `side` is checked strictly against `SideNames`, with no silent `Center` fallback. Markup is read from exactly one namespace (`createRigFromTemplate -namespace`, default `:`), and nested namespaces are excluded.
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
- **Checks:** required and unknown keys at every level (`moduleArgs` included). `rigType`, `assetType`, `side`, `shapeType` and selector sides are checked strictly, with no silent fallback. `rigVersion` must be at least 1. `moduleVersion` must be between 1 and the descriptor's `currentVersion`. Module names must be unique, valid Maya node names (so no `.`), not `layout`, and not already present as `:rig:<name>`. Selectors must resolve, and each joint can be claimed only once. A joint whose `offsetParentMatrix` is already driven fails. An Aim rest pose with target or up on the aim joint, or up on the aim line, fails. Every `parentStructure` key must be a child plug, and every value must be `module.socket` naming an existing socket. Constraint names must be unique, `type` must be `Parent`, the target must be a child plug with a control that no other constraint targets, there must be at least one driver, driver sockets must exist, labels must be non-empty and unique, and weights must be at least 0. No cycles are allowed through `parentStructure` plus constraint drivers, and a self-reference counts as a cycle, since every module input affects every output.
- **Warnings:** a marked joint with `export: true` that no module claims, and several drivers that tie for the highest weight.
- **Behaviour changes:** unknown keys are errors. A bare socket name in `parentStructure` (`"end"`) is an error, so write `layout.end`. Child plugs a `parentStructure` does not list now parent to `layout.end`, the same as a module with no `parentStructure`. Before, they were left unconnected. `rigName` is now read (optional, defaults to `rigTemplateName`) and names the rig root. Before, the rig root always took `rigTemplateName`.
- **Why:** a broken template used to fail partway through, after the namespace existed and after nodes were queued, or worse, built a half-parented rig that only logged errors. Template authors also got one error per attempt.
- **Rejected:** a separate `describe` function per module that only returns names, which would parse args twice and let names drift from `createModule`. A `std::variant` of every module's plan type, which would make `RigModule.h` know every module.
- **Consequence:** every new module needs a plan step that declares its full interface. Validation lives in the C++ core, not Python as CLAUDE.md's planned structure says, because module interfaces come from the C++ registry. Python can call the core later.

---

## Open items

- **Connect-time jump:** the child jumps when a child plug connects to a non-identity socket. Still to do.
- **`compute()` review:** plug grouping in `compute()`, and the layout `end` socket to first FkChain `start` child plug connection. In progress.
- **D9:** where node creation names should live.
- **D13:** whether and when to split out the Maya-free core. `JointMarkup.h/.cpp` is the first Maya-free file, but it still sits at the repo root with no separate library or tests.

## Adding an entry

Use this shape:

```
### Dn. Short title
**Status:** Decided | Leaning | Open
What was decided, in one or two sentences.
- **Why:** the reason, including what was rejected.
- **Consequence:** what this constrains.
```
