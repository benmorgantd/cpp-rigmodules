# Design decisions: cpp-rigmodules

Running record of design decisions for the Maya C++ rig system. Before proposing architectural changes, read the Index below and then the entries it points to for the area you are changing. The file is long, so do not read all of it. Update it when a decision is made, changed, or reversed.

Source: compiled on 2026-11-04 from the Project memory for "Maya Rigging Modules". It records what was decided, and only records a reason where one was stated. Entries marked **Rationale not recorded** need the author to fill in the why. Where this file and the code disagree, the code wins. Flag the difference.

Status key: **Decided** (in use), **Leaning** (direction chosen, not built), **Open** (unresolved).

---

## Index

One line per decision, in number order. Read the entry itself before changing anything it covers. Status is the entry's own **Status:** line. The entries are in `design/decisions/`, one file per area, in number order. The File column says which.

| D | Decision | Status | File |
|---|---|---|---|
| D1 | Nodes are the rig's brain, DAG inheritance is off | Decided | [foundations](decisions/foundations.md) |
| D2 | Joints are the main input, JSON is the template | Decided | [foundations](decisions/foundations.md) |
| D3 | Module inputs are world-space matrices | Decided | [foundations](decisions/foundations.md) |
| D4 | Child plugs and sockets define module attachment | Decided | [foundations](decisions/foundations.md) |
| D5 | Two-pass rig build | Decided | [foundations](decisions/foundations.md) |
| D6 | DAG parenting works | Decided | [foundations](decisions/foundations.md) |
| D7 | Shared attribute names live in `Constants.h` (`RigAttr`) | Decided | [foundations](decisions/foundations.md) |
| D8 | Reuse before writing new helpers | Decided | [foundations](decisions/foundations.md) |
| D9 | Where node creation names live | Open | [foundations](decisions/foundations.md) |
| D10 | Priorities for trade-offs | Decided | [foundations](decisions/foundations.md) |
| D11 | Design for future needs before they are built | Decided | [foundations](decisions/foundations.md) |
| D12 | New modules are gated | Decided | [foundations](decisions/foundations.md) |
| D13 | Maya-free core plus thin plugin layer | Decided | [foundations](decisions/foundations.md) |
| D14 | Code style and naming | Decided | [foundations](decisions/foundations.md) |
| D15 | Joint markup replaces joint names in templates | Decided | [foundations](decisions/foundations.md) |
| D16 | String attributes on nodes store JSON | Decided | [foundations](decisions/foundations.md) |
| D17 | Read-phase validation before any scene change | Decided | [foundations](decisions/foundations.md) |
| D18 | createRigFromTemplate takes its joints by name and owns the rig namespace | Decided | [foundations](decisions/foundations.md) |
| D19 | Commands take the same socket ids as templates | Decided | [foundations](decisions/foundations.md) |
| D20 | mayapy test suite with data-driven expectations | Decided | [foundations](decisions/foundations.md) |
| D21 | User docs in `docs/`, design docs in `design/` | Decided | [foundations](decisions/foundations.md) |
| D22 | `maya/` grouped by role: modules, commands, nodes, shared | Decided | [foundations](decisions/foundations.md) |
| D23 | All C++ under `cpp/`, with its project | Decided | [foundations](decisions/foundations.md) |
| D24 | mayadev depends on RigModules, never the reverse | Decided | [foundations](decisions/foundations.md) |
| D25 | Controls carry markup, and tools find them by traversal | Decided | [controls](decisions/controls.md) |
| D26 | Every control shape is a fillable mesh, with one orientation convention, and per-rig shape files | Decided | [controls](decisions/controls.md) |
| D27 | Space switches match through the control's values, with no stored offsets | Decided | [controls](decisions/controls.md) |
| D28 | Modules and constraints mirror through the template, by side swap | Decided | [controls](decisions/controls.md) |
| D29 | The asset owns its namespace, its rig variants and its files | Decided | [assets](decisions/assets.md) |
| D30 | mayadev stays the pipeline repo, and rigs build anywhere before an asset exists | Decided | [assets](decisions/assets.md) |
| D31 | Orient and Point constraints take one component from the driver, on one `rigConstraint` node | Decided | [controls](decisions/controls.md) |
| D32 | Shots reference published rigs, which are read only | Decided | [data](decisions/data.md) |
| D33 | The release version and format versions are separate, and updates never need a bake | Decided | [data](decisions/data.md) |
| D34 | The animation interface is the keyable channels, with rests set by the build | Decided | [controls](decisions/controls.md) |
| D35 | Weight files are keyed by joint markup, mesh id and topology hash | Decided | [data](decisions/data.md) |
| D36 | Rigs are optimized for parallel evaluation, and the goal is set against Maya's native floor | Decided | [foundations](decisions/foundations.md) |
| D37 | Control drawing stays an MPxDrawOverride, with cached bounds and a callback-kept selection set | Decided | [controls](decisions/controls.md) |
| D38 | The build makes three display layers, found by message attributes on the rig root | Decided | [assets](decisions/assets.md) |
| D39 | The build makes selection set hierarchies for controls and skin joints, found by markup | Decided | [assets](decisions/assets.md) |
| D40 | A module owns its controls' rotate orders, and the build locks them | Decided | [controls](decisions/controls.md) |
| D41 | Controls are restored from joints by a module-owned inverse, with USD joint files and a pose-format sidecar | Decided | [data](decisions/data.md) |
| D42 | Control data is the primary pose and animation library, and joint data is for remaps and the engine | Decided | [data](decisions/data.md) |
| D43 | The build makes `rig_all` over the rig and `mesh_grp` for the model, found by message attributes on the rig root | Decided | [assets](decisions/assets.md) |
| D44 | The art stages before the rig are file types with a status, not a second container | Leaning | [assets](decisions/assets.md) |
| D45 | Face expressions come from blend shapes, and blend shape targets are asset data | Leaning | [assets](decisions/assets.md) |
| D46 | After the IK limb: a pose-driver with a marker spec, then the face module, and overlap only on request | Leaning | [modules](decisions/modules.md) |
| D47 | mayadev menu tools name a function by readable id, and their files are checked and versioned | Decided | [pipeline](decisions/pipeline.md) |
| D48 | One launcher starts and ends Maya: `python/mayalaunch`, beside `rigmodules` | Decided | [pipeline](decisions/pipeline.md) |
| D49 | mayadev UIs are thin Qt views over headless view-models and workflows | Decided | [pipeline](decisions/pipeline.md) |
| D50 | The active rig panel is a launcher over the existing pose tools, with one new query for spaces | Decided | [pipeline](decisions/pipeline.md) |
| D51 | The library browser is a searchable view over control data, with sources, masks and file metadata | Decided | [pipeline](decisions/pipeline.md) |
| D52 | Two-bone IK: a `TwoBoneIk` limb with a matched IK/FK switch and module-owned spaces | Decided | [modules](decisions/modules.md) |
| D53 | Module source files and classes end in `Module`: `FkChainModule` | Decided | [modules](decisions/modules.md) |
| D54 | An asset holds skeletons, rigs and looks; a look replaces the variant | Decided | [assets](decisions/assets.md) |
| D55 | An attachment follows another rig through `parent:` ids, with no attach node | Decided | [assets](decisions/assets.md) |
| D56 | Ribbon module: a virtual ribbon with ik controls, tweakers and stateless twist | Leaning | [modules](decisions/modules.md) |
| D57 | Control placement is derived from the bind joints, with no control joints in the skeleton | Decided | [modules](decisions/modules.md) |
| D58 | One naming convention for joints, controls, modules and constraints: `<camelCaseName>_##_<L|R|M>_<JNT|CTRL|MOD|CON>` | Decided (meshes added by D69) | [assets](decisions/assets.md) |
| D59 | TwoBoneIk and Ribbon template keys for defaults, orientation and an FK layer | Decided | [modules](decisions/modules.md) |
| D60 | The build is faster through parallel compiles and two precompiled headers, not by splitting the project | Decided | [foundations](decisions/foundations.md) |
| D61 | The Asset Editor is a dockable asset context over files, markup and shapes | Decided | [assets](decisions/assets.md) |
| D62 | Limb: one module that runs the TwoBoneIk and Ribbon solves with one continuous ribbon | Decided | [modules](decisions/modules.md) |
| D63 | `constraintWeight`: a baked 0..1 blend from "follow the parent" to the full constraint | Decided | [controls](decisions/controls.md) |
| D64 | `smoothSkinWeights`: neighbor-average smoothing on a working set, built to be a brush later | Decided | [skinning](decisions/skinning.md) |
| D65 | The skin weight brush: one context for every skin painting command, falloff over the mesh surface | Decided | [skinning](decisions/skinning.md) |
| D66 | Rest-match brushes: paint weights in a pose so the mesh goes back towards its rest deltas or edge lengths | Decided | [skinning](decisions/skinning.md) |
| D67 | Attribute and node type names: RigAttr, Maya's static MObjects, MayaAttr, MayaNodeType; the rig root's children attribute removed | Decided | [foundations](decisions/foundations.md) |
| D68 | A look build never fails over weights: marked meshes with no file bind rigidly to the topmost skin joint | Decided | [assets](decisions/assets.md) |
| D69 | Meshes are named by the convention too, with the suffix `GEO`: `body_00_M_GEO` | Decided | [assets](decisions/assets.md) |

---

## Open items

Checked against the repo on 2026-11-18. Items that were finished are not listed: the `compute()` review (2026-11-11), the pre-module checklist (weight commands, animation interface and tools, version update tables, channel naming, the IK limb: D33, D34, D35, D52), mirroring poses and animation (D34, D51), the selection highlight cost (D37), and the `python/` package and `.mod` files (D24, D30).

- **D9:** where node creation names should live.
- **D13:** `core/` and `maya/` are split by folder, but `core/` is not yet its own library and has no tests that run without Maya.
- **D29, D30:** the template authoring UI (after a module interface query), building a second variant into a namespace, the publish step, the out-of-date check, and a Perforce adapter when a project uses Perforce.
- **D29 (potential):** asset GUIDs for references between assets, if renames and moves become common. Not needed now: ids are relative to the projects root, so they survive branching. See "Potential TODO: asset GUIDs" in `asset-model.md`.

### Design now, build later

- **Format version callbacks** (D33): the open, import and reference load callbacks that offer an update.
- **Referenced rigs** (D32): the namespace strip in Save Rig, `getRigInfo` in a referenced file, the alias list and the shot update tool.
- **Rebuild from template keeping rigger tweaks.** Shape files (D26) and weight files (D35) cover shapes and weights. Nothing covers other tweaked values.
- **Plugin deployment** per Maya version (the `.mod` files in D24).

## Adding an entry

Use this shape:

```
### Dn. Short title
**Status:** Decided | Leaning | Open
What was decided, in one or two sentences.
- **Why:** the reason, including what was rejected.
- **Consequence:** what this constrains.
```
