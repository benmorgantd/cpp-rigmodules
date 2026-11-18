# Attachments: a rig that follows another rig (D54 phase 7)

Status: signed off by the author and built 2026-11-17 (D55). The mayadev side (section 3.6) is built and committed on mayadev branch `attachments-phase8` as `01a3d55` (stacked on `rigs-and-looks-phase5`), not yet merged. Read `design/rigs-and-looks.md` section 7 first. This page is the design the gate in `CLAUDE.md` asks for. The user-facing description is `docs/attachments.md`.

**As built, where it differs from the proposal below:**
- `RigModuleNodeBase::findSocketById` is the one cross-rig lookup, and `RigConstraintNode::resolveSocket` is now a wrapper over it. The read phase calls it quietly and lists every miss.
- The DAG reparent skip is `RigModuleDagReparentArgs::reparentDag`.
- Hiding is `DagUtils::queueHideAndLockVisibility` plus the existing `queueLockAndHideTransform`. "Zeroed" needed no step: a new control is at rest.
- A control the parent drives alone is found by `RigTemplateValidation::isDrivenByParent`, so a constraint with a `parent:` driver and another driver stays a free space switch.
- Open point 1 (how the face models overlapping controls) and 2 (skip the DAG reparent) were built as proposed: the author signed off without changing either.
- Perf: 20 complex rigs, parallel evaluation, 25.9 fps alone and 25.6 fps with 80 attachments (4 per rig, 24 joints each). Both are below the 30 fps target: the shortfall was there before this change (`control-node-perf-handoff.md`). Attachments cost about 0.3 fps for 26% more joints.

## 1. The animator-facing problem

A face, cape or prop has its own skeleton and its own controls, but it must ride on the body. Today the only way to join two rigs is to build them as one template. That ties the face to one body and makes the face impossible to build, version, pose-save or export on its own.

## 2. Does something existing cover it?

Mostly. Two pieces already cross rigs or modules:

- `RigConstraintNode::resolveSocket` takes `"namespace:module.socket"`, so a constraint driver can already name a module in another rig.
- The parenting pass (`parentModuleControls`, `RigRoot.cpp`) connects a module's child plug to a parent socket, but only through the rig's own module map.

The gap is therefore small: the parenting pass and the template validation only know one rig. So the proposal is **no new node** (author, 2026-11-17). It extends two existing things. This also avoids a new node's evaluation cost, which matters because the 20-rig target of 30 fps is already missed (22 to 28 fps, `control-node-perf-handoff.md`).

## 3. Design

### 3.1 Template: the `parent:` prefix

An id of the form `parent:module.socket` names a socket on the parent rig. It is valid in two places:

- A `parentStructure` value. The module's child plug follows the body: `"parentStructure": { "start": "parent:head.end" }`.
- A constraint `drivers` entry. `"drivers": ["parent:chest.end"]` is the way an attachment's own control is driven by the body.

The attachment's template therefore declares everything it needs from the parent, and the list of required sockets is derived from those ids. There is no separate mapping block and no separate file. A template with a `parent:` id needs a parent rig to build. Older templates cannot contain the prefix (it fails today as a missing module), so no D33 update step is needed.

### 3.2 Build: `parentRig`

`cmds.createRigFromTemplate(*joints, template=path, asset=id, namespace=ns, parentRig=rigRoot)`.

- Read phase: `parentRig` must be a rig root, in another namespace, and not itself an attachment of this rig. A template with `parent:` ids and no `parentRig` is an error, and so is the reverse (a `parentRig` for a template with no `parent:` id). Every required socket is checked against the parent's modules, and every missing one is listed in the same report (D17). Core gets a Maya-free `RigTemplateValidation::parentSocketIds(rigData)`; the scene check is plugin side.
- Write phase: the parenting pass resolves a `parent:` id through `RigConstraintNode::resolveSocket` (rename or extract it to `RigModuleNodeBase` so both callers share it) and connects the same child plug input it connects today. Constraint drivers resolve the same way.
- The rig root gets a `parentRig` message attribute connected from the parent's rig root. `getRigInfo` returns `parentRigRoot`. `RigTraversal` stays per rig (an attachment is a rig root of its own, one namespace each).
- The dependency is one way, parent to attachment. A parent never reads an attachment, so there is no DG cycle.

### 3.3 What attached means (hidden, locked, zeroed)

An attachment's own controls for the overlapping region stay in the rig, so it can be built and animated alone. A control whose constraint is driven only by `parent:` sockets is **driven by the body**. At build time it is hidden, its transform channels are locked and its values are zeroed. Keys on it can't surprise an animator later. The rule is implicit (no new JSON key), and it is stated in the user docs.

The controls use `queueLockAndHideTransform` and the existing constraint code. The same constraint is also what a prop's hand to hip to world switch uses later (section 7.2), so one change serves both.

### 3.4 Detach, re-attach, animation

Attaching is build time: the connections are written by `createRigFromTemplate`. To detach or re-attach to a different body, rebuild the attachment. Its animation survives because the control names and attributes are unchanged and the driven controls carry no keys. A `detach` command is not part of this phase.

### 3.5 Libraries, info, export, looks

- **Poses and animation:** an attachment is a rig, so it has its own `controlposes` and `controlanims` (D42, D51). The `meta` block gets no new field: `getRigInfo` names the parent, and `rig.json` names it on disk.
- **Export:** the attachment's skeleton is its own file and hash (D54), so the engine export stays separate. I did not read `design/engine-rig.md` for this, so treat that as unchecked.
- **Looks:** `setRigLook` works on one rig root. A look that spans body and face is mayadev's `apply_look` calling it once per rig. Not handled in RigModules.
- **Mirroring:** `TemplateMirror` swaps the side in `parentStructure` and driver ids, and must keep the `parent:` prefix while swapping the module name. This needs a test.

### 3.6 mayadev (proposed, needs the author's go-ahead)

- `RigRecord` gains an optional `parent` (`asset` and `rig`). **As built, no D33 update step:** the key is optional and additive, so every old file reads as before and a step would add nothing.
- `build_rig` builds the parent first and passes its root as `parentRig`.
- `apply_look` partitions a look that spans several skeletons by the weight file's `skeletonHash`. The design is `attachments-mayadev.md`.

## 4. Names and public API added

| Name | Where |
|---|---|
| `parent:` id prefix | template `parentStructure` values and constraint `drivers` |
| `parentRig` flag | `createRigFromTemplate` |
| `parentRig` attribute | rig root (message) |
| `parentRigRoot` | `getRigInfo` result |

Names of existing controls, attributes, child plugs and sockets don't change, so saved poses and animation are unaffected. Existing rigs and templates behave as before.

## 5. Failure modes

- No `parentRig`, or a wrong one (a body from another template): the read phase lists every missing socket and builds nothing.
- `parentRig` is not a rig root, is in the same namespace, or is already built as an attachment of this rig: error.
- A `parent:` id inside a mirror pair whose other side doesn't exist on the parent: reported as a missing socket.

## 6. Maintenance surface

One reserved prefix, one build flag, one rig root attribute and one info key. No node type, no MTypeId, no compute code. The shared `resolveSocket` moves from `RigConstraintNode` to `RigModuleNodeBase`, so there is one cross-rig lookup.

## 7. Tests and perf

- `RigTemplateValidation`: `parentSocketIds`, and the `parent:` forms (a prefix with no parent rig, a `parentRig` with no prefix).
- Scene: build a body, attach a face. The face follows the body's chest-up sockets, a missing socket fails in the read phase with all problems listed, the driven controls are hidden, locked and zeroed, `getRigInfo` shows the parent, and undo and redo work.
- Mirror: a mirrored `parent:` id.
- A pose case and a golden snapshot for the attachment (D20, `tests/README.md`).
- Perf: add many attachment instances to `tools/perf/perf_scenes.py` and run `bench`. Report the fps, and say so if it is below 30.

## 8. Open points

1. How are the overlapping controls modelled in the face template? I assume each is a control with a constraint whose only driver is a `parent:` socket (section 3.3). Say so if the face template should do it another way.
2. DAG parenting: the parenting pass also reparents control transforms (outliner organisation only, since `inheritsTransform` is off). For a cross-rig socket I propose to skip the reparent, so the attachment stays under its own rig root. Confirm.
