# FkHierarchy module: design

**Status:** signed off by the author on 2026-11-23 (D71) and built the same day on branch `fk-hierarchy`: the `core/` planner, the module, the inverse and the perf rig instances. Section 16 lists where the build differs from the text below. The `bench` numbers are in D71.

Read with: `decisions/modules.md` (D71), `docs/modules.md` (FkChain, the reference), D25 (control markup), D28 (mirroring), D58 (naming), D70 (a skeleton has one root).

## 1. The problem

FkChain takes an ordered list of joints, so it describes one chain. A prop or weapon skeleton is usually a root with many children, and often deeper branches below them. The tommygun asset has `root_00` with five children (`safety_00`, `mag_00`, `trigger_00`, `handle_00`, `grip_00`). Today that is one FkChain per child, five modules for one skeleton, each with its own `start` child plug that the template must attach by hand.

The animator-facing need: every joint under a root gets an FK control, with no template entry per branch, and a joint added to the skeleton later builds without editing the template. This is also the natural generation target for the open item on a generated starting template for a branching skeleton (D29, D30).

## 2. Decision: a new module, not an extension of FkChain

**FkChain stays as it is. The new module is `FkHierarchy`.**

Why not extend FkChain (the gate in `CLAUDE.md` asks):

- **Its interface is positional.** `joints` is an ordered list, each control parents to the previous one, sockets are `index_<n>`, and `start` and `end` are aliases for the first and last. A tree has no single last joint and no meaningful `index_<n>`. Supporting both in one `moduleType` means two interface shapes behind one name, and every saved pose, shape file and template that names FkChain sockets depends on today's shape (names are public API).
- **Its compute is a straight loop.** The loop carries one `mParentDelta` forward. A tree needs a per-control parent. Adding that to FkChain's loop puts a branch in the hottest and most-used module for the sake of a case it does not serve.
- **The two modules still share almost everything else:** control creation, `wireJoint`, `wireControl`, `solveControlStep`, the rotate order, the inverse step. The new module is thin because of that.

Decided in conversation (author, 2026-11-23):

1. The module takes one root joint and an optional list of terminator joints. A terminator is included, and its descendants are not. With no terminators, the walk takes every joint below the root.
2. Sockets are named from joint markup, not position: the side lowercased, then the usage, for example `left_spine_00`. The author prefers this over positional names in general.
3. Every joint and control must have unique markup, as always.
4. A joint added under the root builds, and its control inherits its parent's transform. Props and weapons with many joints under one root are the main use.
5. One root per module. A second root is a second `FkHierarchy`.
6. It mirrors (D28). The mirror may have a different hierarchy below the root, and mirrored terminators that do not exist on the other side are dropped. To get specific terminators on the mirror side, the template defines that module explicitly with its own `terminators`.
7. The joints are found with `MItDag`.

## 3. Template

```json
{
  "moduleType": "FkHierarchy",
  "name": "gun_00_M_MOD",
  "moduleArgs": {
    "root": ["Center", "root_00"],
    "terminators": [["Center", "mag_00"]]
  },
  "parentStructure": { "start": "layout_00_M_MOD.end" }
}
```

| `moduleArgs` key | Required | Value |
|---|---|---|
| `root` | yes | Selector for the top joint. Its control is the module's only child plug. |
| `terminators` | no | Selectors for joints that end a branch. Each is included, and its descendants are not. Default: none, so the walk goes to every leaf. |

Both keys are selectors `[side, usage]` (D15), so a mirror swaps them by the existing rule with no template change (D28).

## 4. Which joints are in the hierarchy

The module builds a control for `root` and for every joint reached from it, in one `MItDag` depth-first walk starting at the root joint's path, filtered to `MFn::kJoint`.

- **Only joint-to-joint edges count.** A joint is reached when its parent is an included joint. A joint under a non-joint transform (a locator or group between two joints) is not reached. Rejected: reaching through non-joint nodes, which would make the skeleton's DAG shape matter in a way no one would expect.
- **Terminators prune.** On reaching a terminator the walk includes it and calls `MItDag::prune()`, so its subtree is skipped.
- **Order.** Depth-first, in the order `MItDag` visits the hierarchy (a joint's children in their DAG order), author's answer 2026-11-23. A parent always comes before its children (pre-order), which the solve in section 8 relies on. Slot numbers follow that order and so change if someone reorders joints in the outliner. That is harmless: poses and animation are keyed by markup (D25), and the sockets are named from markup. Rejected: sorting siblings by name, which adds a rule for a stability nobody needs.
- **Every reached joint must carry valid, unique markup.** The walk goes through `JointMarkupScene` (`claimJoint`), so a missing markup, a duplicate `(side, usage)` and a joint already claimed by another module are existing errors that name the joint. An unmarked joint in the walk is an error, not a skip: the hierarchy is automatic, so a skipped joint would leave a silent hole.
- **A joint another module already claimed is an error** (for example a limb hanging off the prop's root). It names the joint and says to end the walk above it with a terminator, and to give the claimed joint's branch its own module. There is no `exclude` list (author, 2026-11-23): a second `FkHierarchy` rooted below the first covers every case the author expects, and skipping joints in the middle of a branch is not one of them. A terminator cuts every child of that joint, so a sibling that should stay in this module and a sibling that belongs to another module cannot share a parent here. The other sibling's branch becomes its own module.
- **Each terminator must be reached.** A terminator that is not at or below the root, or that the walk never meets, is an error, to catch a typo. Mirrors are the exception (section 7).
- **Joint count.** At least one: a root alone is valid (one control).

## 5. Controls

- One control per joint, named from the joint by D58 (`mag_00_M_JNT` gives `mag_00_M_CTRL`), with the joint's markup, exactly as FkChain does it.
- The module's `shapeType` applies to every control. A rigger changes shapes per control afterwards through the shape files (D26).
- The control hierarchy in the DAG mirrors the joint hierarchy (DAG parenting works but drives nothing, D1, D6).
- Rotate order XYZ, the same as FkChain and for the same reason (D40).
- **Channels:** the same as FkChain's controls (the standard transform channels). No custom channel is added, so `ModuleContract`'s channel check and the mirror modes are the FkChain ones.

## 6. Child plugs and sockets

| | |
|---|---|
| Node type | `fkHierarchyModule` (MTypeId `0x002193`, not in the list in `CLAUDE.md`, to be added) |
| Child plugs | `start`: the root control's parent |
| Sockets | One per joint, named `<side>_<usage>`: the markup side lowercased (`left`, `right`, `center`), then the usage, as written. The root's socket is also `start`. |

- **No `index_<n>`, no `end`.** Positions shift when a joint is added, and a tree has no last joint. Markup is the name the pose, shape and weight files already use.
- **Added joints add sockets and never rename one.** That is the stability property the author asked for.
- **Validity.** A socket id is `module.socket`, so a usage containing `.` is an error at plan time. Uniqueness follows from unique markup: the side prefix is one of three fixed words, so no two joints give the same name.
- **Usage case.** The side is lowercased and the usage is kept as written (a camelCase usage such as `upperArm` stays `left_upperArm`), author's answer 2026-11-23.
- **`start` is the root's alias and there is no `end`.** To attach to anything else, a template uses the joint's markup socket.
- **Interface query.** `ModuleInterface` (`makeInterface`) is built in the plan step, from the walk, so the template validator and `getRigModuleInterface` see the real socket list before any scene change (D17).

## 7. Mirroring

The mirror is an ordinary template entry after the read phase (D28), so the node never sees it. `planFromJson` reads `ctx.moduleData.isMirror()` for one rule:

- **A mirrored module drops any terminator that matches no joint** instead of failing. The mirror hierarchy may differ, and the author does not require the terminators to match. With every terminator dropped, the mirror walks to every leaf below its root.
- The module adds a **warning** to the report for each dropped terminator, naming the source terminator and the mirror module, so a mirror that grew an unwanted branch is visible (the report supports warnings: `ValidationReport::addWarning`).
- **To get specific terminators on the mirror side**, the template defines the other-side module explicitly (no `"mirror": true`) with its own `terminators`.
- The root must exist on the mirror side. A missing root is the existing selector error (D28: asymmetric pairs fail the build).
- Everything else follows D28: the name's side suffix, `parentStructure` ids pointing at the other side, and socket names, which come from the mirror's own markup (`right_spine_00`).

This is the one mirror-specific branch, and it lives in `planFromJson`, as D28 requires.

## 8. Compute and inverse

**The solve stays one linear pass over the controls.** It is not necessary to split the tree into chains. Controls are stored in pre-order, so each control's parent has a lower index than the control. Today's loop carries one `mParentDelta` forward. For a tree, each control reads its parent's delta by index.

```
for i in 0 .. n-1:
    parentDelta = (parentIndex[i] < 0) ? delta of the module's parent socket : delta[parentIndex[i]]
    step = solveControlStep(local[i], restWorld[i], parentDelta)
    write control OPM, joint OPM, socket
    if hasChildren[i]: delta[i] = computeDeltaFromRest(restWorld[i], step.world)
```

- **One matrix inverse per parent, not per child.** A prop root with 40 children inverts once. A leaf skips the inverse (FkChain already skips the last control's).
- **Splitting into chains was rejected.** Each chain's first control would still need its parent's delta from another chain, so the same cross-reference exists with more bookkeeping and several arrays.
- **Scratch storage.** `delta[i]` needs per-evaluation storage that `compute()` must not allocate. Proposal: a node member vector, resized when the control count changes (the first evaluation after a build or load) and not otherwise. A node evaluates on one thread at a time, so the scratch is not shared. `compute-reviewer` checks this when it is built (D36).
- **A new attribute, `controlParentIndex`:** an int array, one per control, `-1` for the root. It is written at build and read in `compute()`. `hasChildren` is derived once from it, not stored. It is the module's one new attribute, with create, add and `attributeAffects` to the control OPM, joint OPM and socket outputs.
- **A separate function, not a flag on `solveFkChain`.** The new function (`solveFkHierarchy`, in `maya/shared/RigModule.h` beside `solveFkChain`) uses the same `solveControlStep`, `computeDeltaFromRest`, `readControlInput` and `writeOutputMatrix`. FkChain's loop is untouched, so its goldens and its D36 numbers cannot move. The duplicated part is the loop of about fifteen lines. Rejected: one function with a null-or-array parent argument, which puts a branch in every FkChain evaluation. Revisit if the benchmark shows no cost.
- **The inverse (D41).** `ModuleInverse::solveChain` walks the controls with one running `mParentDelta`. A hierarchy needs the same parent-index lookup. `ModuleInverseContext` gets an optional parent-index vector (empty means a chain), and the loop is shared. This touches code FkChain and Layout use, so FkChain's inverse tests must pass unchanged. I have read `solveChain` but not its callers.

## 9. Failure modes

| Case | Result |
|---|---|
| Root selector matches no joint | existing selector error |
| A reached joint has no markup, or duplicates one | existing markup errors, naming the joint |
| A reached joint is claimed by another module | error naming the joint and both modules, with the terminator hint |
| Terminator not at or below the root | error naming the terminator (not on a mirror) |
| Terminator is the root | allowed: a one-control module |
| A usage contains `.` | error at plan time |
| A joint is outside the naming convention (D58) | existing error |
| A mirror's terminator matches nothing | dropped, with a warning |
| The root already has an incoming `offsetParentMatrix` | existing `claimJoint` error |

## 10. Impact

- **Poses and animation.** Controls are keyed by markup, so a joint added to the skeleton adds a control with no animation, and nothing else changes. A joint removed leaves its keys with no control, which is true of every module. A joint renamed in markup is a rename of the control key.
- **Export.** None: the joints are the same joints. No control joints are added.
- **Pose and animation libraries.** The same control channels as FkChain, so existing library tools and mirror modes apply.
- **Versioning (D33).** A new module has no older shape to migrate. A rig that moves from five FkChains to one `FkHierarchy` keeps its control names and control markup, because both come from the joints, so saved poses and animation carry over. The module names and socket ids change, which matters to anything that named them (`parentStructure` entries, constraint drivers).
- **mayadev.** Nothing breaks: this adds a module and no command, flag or return shape. The asset editor's generated starting template (open item) can use it.

## 11. What it adds to the maintenance surface

- One module file pair, one registry entry, one node type and id.
- One shared solve function and one optional field on the inverse context.
- One attribute, `controlParentIndex`.
- One mirror rule in `planFromJson`.
- One shared Maya helper for the walk (`maya/shared`, with `JointMarkupScene`, since it finds joints), per the "shareable helpers are shared when written" rule. The ordering and pruning rule has no Maya dependency, so the part that takes the visited joints (name, parent position) and a terminator set and returns the order and parent indices goes in `core/` (testable with no Maya), author's answer 2026-11-23. The `MItDag` walk stays in the Maya layer and calls it.

## 12. Tests

- `ModuleContract` subclass for `FkHierarchy`, with a tommygun-shaped skeleton (a root with five children) and a deeper one (branches two levels down).
- Pose cases written by hand (D20): rotate the root and check every descendant's world matrix, rotate a middle joint and check its subtree only and its siblings unmoved, a translated child.
- Golden snapshot with controls, markup and socket names.
- Walk cases: no terminators, terminators that cut one branch, a terminator that is a leaf, a terminator that is the root, and the depth-first order with several children at several levels.
- Failure cases: every row of section 9.
- Mirror: the source with a terminator, the other side with a different hierarchy, a dropped terminator with its warning, and an explicit other-side module with its own terminator. Compared with the same rig written by hand, as `test_mirror_matches_template_written_by_hand` does.
- Skeleton edit: build, add a child joint with markup, rebuild, and check the new control and that the existing socket names and control markup are unchanged.
- Inverse: restore control values from joints on a posed branching rig, and a round trip.
- FkChain regression: its goldens and inverse tests pass unchanged.
- A worked example in `docs/modules.md` with its test in `tests/maya/concepts/test_docs_examples.py`.
- Every new test file gets a tier in `TIERS` in `tests/run_tests.py`.

## 13. Performance

Per the new-module gate: add many `FkHierarchy` instances to the complex perf rig (`tools/perf/perf_scenes.py`) and run `bench` once at creation, reporting the fps against the 30 fps target for 20 rigs and the 1.5 times native floor on the 50-rig scene (D36). Say so when it is below target. The cost per evaluation is one inverse per joint that has children, so a flat prop is cheaper than the same joints as an FkChain.

## 14. Questions answered (author, 2026-11-23)

1. **A joint another module owns, below the root:** an error in the template, avoided with a terminator. No `exclude` list: a second `FkHierarchy` rooted below the first covers it (section 4).
2. **Socket case:** the side is lowercased, the usage is kept as written (section 6).
3. **Where the walk lives:** the `MItDag` walk in the Maya layer, the ordering and parent-index rule in `core/` (section 11).
4. **`start`:** the root's socket alias. There is no `end`; use the joint's markup socket (section 6).
5. **Order:** depth-first, in `MItDag` order, not sorted by name (section 4).

Nothing is open. The remaining step is the author's sign-off on the whole page.

## 15. Build phases

1. Sign-off on this page (answers to section 14).
2. `core/` ordering function and its tests (no Maya).
3. The module, `solveFkHierarchy` and the registry entry, with the contract tests, goldens and the failure and mirror cases.
4. The inverse generalization, with the FkChain regression run.
5. Perf rig instances and one `bench` run.
6. Docs: `docs/modules.md` (a section beside FkChain), `docs/source-map.md`, `docs/templates.md` if a key is added, the README module list, `tests/README.md`, `CLAUDE.md` (the module list and the MTypeId list), the D71 entry, and removing the "FkChain (branching)" open item.
7. Optional, separate: the asset editor's generated starting template uses `FkHierarchy` from the skeleton's root (mayadev).

## 16. Where the build differs from the text above

- **The forward solve is in `FkHierarchyModule.cpp`**, a private member (`computeHierarchyOutputs`), not `solveFkHierarchy` in `RigModule.h`. Only this module uses it, and it composes the shared `solveControlStep`, `readControlInput`, `readChildPlugParentDelta`, `openSolvedOutputs`, `writeSolvedSlot` and `commitSolvedOutputs`. FkChain's loop is untouched, as section 8 required.
- **`HierarchyPlanner::plan` takes the walked joints and does the keep/cut work.** `JointMarkupScene::collectJointSubtree` returns every joint below the root in depth-first order (with `MItDag`), and the planner drops what sits below a terminator. Pruning is done in `core/`, not with `MItDag::prune()`, so the rule is tested without Maya.
- **The child order is `MItDag` order, with no sort** (section 4, author's answer).
- **Mirrored sockets needed a mirror rule the draft missed.** A template entry that names a markup-named socket (`clavicle_00_L_MOD.left_upperArm`) has to point at `right_upperArm` on the other side, so `core/TemplateMirror` now swaps a leading `left_` or `right_` on a socket name, on every id it rewrites, `Center` modules included. No other module names a socket that way, so no existing template changes.
- **A claim error gets a second line** naming the fix (a terminator, or a second `FkHierarchy`), added once per module when any joint failed to claim.
- **Tests:** `modules/test_fk_hierarchy.py` (contract with the inverse round trips, six hand-worked pose cases, the behaviours in section 12), six validation cases, two golden snapshots, and `FkHierarchyDocExample` in `concepts/test_docs_examples.py`.
- **Perf rig:** 4 `hierarchy_specs` (6 modules built, 8 joints each, three levels deep) in `tools/perf/perf_scenes.py`, so the complex rig is 408 joints and 524 controls.
