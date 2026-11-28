# Rig templates

A rig template is a JSON file that lists a rig's modules, how they attach to each other, and optional space-switch constraints. `createRigFromTemplate` reads it (see [commands.md](commands.md)). [`RigTemplates/LayoutRig.json`](../RigTemplates/LayoutRig.json) is a full example.

Templates never name joints. Modules pick joints by their markup (`[side, usage]`), so one template builds on any skeleton marked the same way. Templates don't name an asset either: `createRigFromTemplate` takes it as `asset`, so one template builds many assets.

## Example

```json
{
  "rigVersion": 1,
  "rigTemplateName": "SpineArm",
  "rigAuthor": "you",
  "rigModules": [
    {
      "moduleType": "FkChain",
      "name": "spine_00_M_MOD",
      "moduleArgs": { "joints": [["Center", "spine1"], ["Center", "spine2"]] }
    },
    {
      "moduleType": "FkChain",
      "name": "arm_00_L_MOD",
      "side": "Left",
      "moduleArgs": { "joints": [["Left", "arm"]] },
      "parentStructure": { "start": "spine_00_M_MOD.end" }
    }
  ],
  "constraints": [
    {
      "name": "armSpace_00_L_CON",
      "target": "arm_00_L_MOD.start",
      "drivers": [
        { "socket": "spine_00_M_MOD.end", "label": "Chest", "weight": 1.0 },
        { "socket": "layout_00_M_MOD.end", "label": "World" }
      ]
    }
  ]
}
```

## Top level

| Key | Required | Value |
|---|---|---|
| `formatVersion` | no | Integer: the version of the template format. Defaults to 1, which is the current one. A template with a newer version than this build reads is an error. Older versions are updated when the template loads, so old templates keep building |
| `rigVersion` | yes | Integer, 1 or more |
| `rigTemplateName` | yes | Non-empty string |
| `rigAuthor` | yes | String |
| `rigModules` | yes | Array of modules (below) |
| `rigName` | no | Names the rig: its folder in the asset (`body`, `face`, `cine`), and with the asset's name the default namespace, `{asset name}_{rigName}` (D54). Lowercase letters, digits and `_`, because it names a folder in the asset: `Human` is an error, not converted. Defaults to `rigTemplateName` in snake_case (`HumanRigTest` becomes `human_rig_test`). A namespace holds one rig, whatever its name |
| `constraints` | no | Array of constraints (below) |
| `collisions` | no | Array of [push outs](push-out.md): a control kept out of a capsule. Built after every constraint, so each one runs after its target's constraint. The only `type` is `PushOut` |

## Markers

A module can select a point that no joint marks, a [marker](markers.md), with a `[side, usage]` pair in a `moduleArgs` key that names markers. The markers come from the file passed to [`createRigFromTemplate`](commands.md#createrigfromtemplate) as `markers`, and a template that selects a marker when no file was passed is a read-phase error naming the marker. A mirrored module swaps the selector's side, as it does a joint selector. The [ReverseFoot](modules.md#reversefoot) takes no marker selectors: it finds its six pivot markers by role on its own side (`[Left, heel]`), and its `ikFkDriver` module id swaps its side when the foot is mirrored.

## Modules

| Key | Required | Value |
|---|---|---|
| `moduleType` | yes | A module type from [modules.md](modules.md): `FkChain`, `FkHierarchy`, `Aim`, `TwoBoneIk`, `Ribbon` or `ReverseFoot` |
| `name` | yes | The module's id, in the [naming convention](naming.md): `<camelCaseName>_##_<L|R|M>_MOD`, such as `arm_00_L_MOD`. Unique in the template, its side letter must match `side`, and it is not `layout_00_M_MOD`. The module node gets this name in the rig namespace. |
| `side` | no | `Center` (default), `Left` or `Right`. Sets the controls' side letter (`M`, `L`, `R`) and color, and must match the side letter in `name`. |
| `shapeType` | no | Control shape for the module's controls: `Box` (default) or any shape in [controls.md](controls.md#shapes) |
| `moduleVersion` | no | Integer from 1 to the module's current version. Defaults to 1. Layout, FkChain, FkHierarchy, Aim and ReverseFoot are at version 1, and TwoBoneIk and Ribbon at version 2. A `moduleArgs` written for an older version is updated before the module reads it. |
| `moduleArgs` | no | Module-specific arguments, mostly joint selectors. See [modules.md](modules.md). |
| `parentStructure` | no | Maps this module's child plugs to their DAG parent sockets (below) |
| `matrixParentStructure` | no | Maps child plugs to the socket whose matrix they follow, where that differs from the DAG parent (below) |
| `mirror` | no | `true` also builds this module on the other side. Only `Left` and `Right` modules. See [Mirroring](#mirroring). |

### Joint selectors

A selector is a `[side, usage]` pair matched against each joint's `rigMarkup` attribute, for example `["Left", "upperArm"]`.

- A selector must match exactly one of the joints passed to the build.
- A joint can be claimed by only one module.
- A joint whose `offsetParentMatrix` is already connected cannot be claimed.

### parentStructure

```json
"parentStructure": { "start": "spine_00_M_MOD.end" }
```

Each key is one of this module's child plugs. Each value is a socket id, `module.socket`, where `module` is another module's `name` or `layout_00_M_MOD`.

- A bare socket name (`"end"`) is an error. Write `layout_00_M_MOD.end`.
- Child plugs that are not listed attach to `layout_00_M_MOD.end`, as do all child plugs of a module with no `parentStructure`. The exceptions are a [`Ribbon`](modules.md#ribbon)'s `end`, which takes the same parent as its `start` unless it is listed, and a [`TwoBoneIk`](modules.md#twoboneik)'s `ikTarget`, which stays unconnected when it is not listed. An [anchored Ribbon](modules.md#the-anchored-form)'s `start`, `mid`, `end` and `settings` have no default: leaving one out fails the build.
- A module cannot parent to itself, directly or through a loop of modules.
- `parent:module.socket` names a socket of the rig this one is attached to, in a `parentStructure` value or a constraint driver. See [attachments.md](attachments.md).
- **A `parentStructure` value is a DAG parent.** For a child plug that has a control, the control is reparented under the socket's control, and the build fails when the socket has none (a matrix-only socket, below). The error says a valid `dagParent` must be given. A ReverseFoot's `ik` and `ankle` carry the foot control and the toe FK control, so they follow this rule too. A child plug with no control (an Aim's `aim`, a TwoBoneIk's `ikTarget`, an anchored Ribbon's anchors) has nothing to reparent, so its entry only says where its matrix comes from and any socket is valid. The exception is an anchored Ribbon's `settings`, which names the control its channels go on, so its socket must have a control.
- Every control is a socket with a transform, except where it shares its joint's socket: a TwoBoneIk FK control is the transform of `start`, `index_<n>` and `end`. The sockets with no control are **matrix-only**: an Aim's `aim`, and a ReverseFoot's `ankle`, `toe`, `toeTip` and pivot sockets. They are valid `matrixParentStructure` values and constraint drivers, never DAG parents.
- The DAG parent decides the outliner and nothing else, because no control inherits a transform (`inheritsTransform` is off). Visibility does inherit, so a module hides its ik/fk controls through the control's `shown` attribute and not its `visibility`, and a control under a hidden one stays visible.

### matrixParentStructure

```json
"parentStructure": { "start": "arm_00_L_MOD.settings" },
"matrixParentStructure": { "start": "arm_00_L_MOD.end" }
```

The same keys as `parentStructure`, each a child plug, and the same `module.socket` values (or `parent:module.socket`), but any socket is valid, with or without a control. The plug's matrix, and the parent a [constraint](#constraints) leaves it, comes from this socket. The DAG parent stays the `parentStructure` entry.

- A plug with no `matrixParentStructure` entry follows its DAG parent, so most templates need only `parentStructure`.
- The build does not compare the two. A rigger who names a control that hides in IK as the matrix parent gets a rig that is wrong in IK.
- A loop of matrix parents is an error, and so is a loop of DAG parents (Maya refuses it at the reparent).
- Mirroring swaps sides in both keys, as it does for `parentStructure`.

## Constraints

A constraint gives a control a choice of parent spaces. With more than one driver, the control gets a `space` enum attribute listing the driver labels.

| Key | Required | Value |
|---|---|---|
| `name` | yes | The constraint node's name, in the [naming convention](naming.md): `<camelCaseName>_##_<L|R|M>_CON`, such as `armSpace_00_L_CON`. Unique in the template. |
| `target` | yes | `module.childPlug`. The child plug must have a control. Only one constraint can target a child plug. |
| `drivers` | yes | At least one driver (below) |
| `type` | no | `Parent` (the default), `Orient`, `Point`, `Swing` or `Twist`. See [Constraint types](#constraint-types). |
| `constraintWeight` | no | 0 to 1, default 1. See [Constraint weight](#constraint-weight). Not the driver's `weight`, which only picks the starting driver. |
| `mirror` | no | `true` also builds this constraint on the other side. The target must be a `Left` or `Right` module. See [Mirroring](#mirroring). |

Drivers:

| Key | Required | Value |
|---|---|---|
| `socket` | yes | Socket id, `module.socket` |
| `label` | no | Enum label shown on the control. Defaults to the socket id. Unique within the constraint. |
| `weight` | no | 0 or more, default 0. The driver with the highest weight starts active. With a tie, the first of them starts active (and the build warns). With no weights, the first driver starts active. |

A module cannot drive its own child plug, and no loop may run through `parentStructure` and constraint drivers together. One kind of loop is allowed: a plug may declare sockets its input does not reach, and a path that enters a module by that plug cannot leave it by one of them. A [`TwoBoneIk`](modules.md#twoboneik)'s `ikTarget` declares `ik`, `pole` and `settings`, so a foot that hangs from `ik` and feeds `ikTarget` is fine, and the same foot hanging from `end` is a cycle. A [`ReverseFoot`](modules.md#reversefoot) declares the same for its own `ankle` plug (the TwoBoneIk's `end` socket) and its `ankle` socket, so a foot that takes `end` in that plug is fine and one that takes `end` in its `ik` plug is a cycle. Neither of its plugs has a control, so a constraint on either is an error.

A module that follows another's ik/fk blend (the ReverseFoot's `ikFkDriver`) has three more checks: the driver must be a module of the template with a blend to follow (a TwoBoneIk), every child plug of the follower must hang from one of its sockets (a plug with no `parentStructure` entry takes `layout_00_M_MOD.end`, which is not), and the driver must read the follower's output where the follower needs it (the foot's `ankle` socket as the TwoBoneIk's `ikTarget`). The error names the `parentStructure` line to add.

A module can own a child plug's space, or take its parent from another plug of its own. [`TwoBoneIk`](modules.md#twoboneik)'s `pole` and `settings` take no constraint, and its `settings` takes no `parentStructure` entry either.

### Constraint types

The active driver gives only its own part of the motion. Everything else comes from the child plug's matrix parent, the socket it attaches to in `matrixParentStructure`, or `parentStructure` when that has no entry (or `layout_00_M_MOD.end`), as if there were no constraint.

| Type | Translate (pivot) | Rotate | Scale |
|---|---|---|---|
| `Parent` | driver | driver | driver |
| `Orient` | parent | driver | parent |
| `Point` | driver | parent | parent |
| `Swing` | driver | driver, without its twist about X | parent |
| `Twist` | driver | driver's twist about X, on the parent's swing | parent |

`Swing` is for the start of a [Ribbon](modules.md#ribbon) at a shoulder or a hip. The driver's rotation is a twist about the child's X (the bone's axis) and then a swing that turns X to where it points; `Swing` keeps the swing and drops the twist. The twist is the part of the rotation that spins about the bone, so with a rotate order of xyz it is the rotateX channel and rotateY and rotateZ are the swing. It is read about the child's own X, whichever way the bone points in the world. A twist is read as a half turn on either side (200 degrees is -160) and a swing of exactly half a turn from rest has no twist, so there the twist flips as the arm passes through. Give both ribbon plugs the same parent as the driver (below and [the Ribbon's shoulder example](modules.md#ribbon)).

`Twist` is the other half, for the end of a [Ribbon](modules.md#ribbon) at a wrist or an ankle. The rotation is the driver's twist about the child's X turned by the swing of the plug's parent: the swing of the driver never arrives, and the parent's own twist is replaced by the driver's. Parent the plug on the elbow or the knee, drive it from the wrist or ankle, and the hand's flop can't bend the ribbon while its twist still eases in ([the wrist example](modules.md#ribbon)). The twist is read about the child's own X, as for `Swing`, with the same wrap at a half turn. The driver's translation is taken too, so one constraint places the end at the wrist and no second constraint is needed.

In the README's quick start rig, the arm's `start` attaches to `spine_00_M_MOD.end`. With this `Orient` constraint, the arm keeps the world's rotation while its shoulder pivot swings with the chest. A leg on `spine_00_M_MOD.start` works the same way, with the hip as its pivot.

```json
{
  "name": "armSpace_00_L_CON",
  "type": "Orient",
  "target": "arm_00_L_MOD.start",
  "drivers": [
    { "socket": "layout_00_M_MOD.end", "label": "World", "weight": 1.0 },
    { "socket": "spine_00_M_MOD.end", "label": "Chest" }
  ]
}
```

A space switch keeps the control in place whatever the type. On an `Orient` constraint it changes only the control's rotate channels while its translate is zero, and on a `Point` constraint only its translate while its rotate is zero.

### Constraint weight

`constraintWeight` takes a share of any type's effect, such as 20% of a wrist's twist: `"type": "Twist", "constraintWeight": 0.2`. At 1 (the default) the control gets the full constraint. At 0 it gets none, and just follows its parent as if the constraint were not there, for every type, `Parent` included. In between, the pivot moves part of the way and the rotation turns part of the way (a slerp), so a `Twist` at 0.5 is close to half the twist. The weight is part of the rig, set here or with `setAttr` on the constraint's `constraintWeight`, and is not an animation channel, so it is in no pose or animation. It applies to the whole constraint, whichever driver is active, and it copies to the other side with `mirror`. A `Parent` constraint below 1 reads its parent plug, so that plug needs a parent.

## Collisions

The `collisions` list holds [push outs](push-out.md): `{ "name": ..., "type": "PushOut", "target": "module.childPlug", "capsule": { ... } }`. It is a list of its own because collisions come after constraints: a push out on a plug runs after that plug's constraint. A plug takes at most one push out. `"mirror": true` copies it to the other side like a constraint (see [Mirroring](#mirroring)), with the capsule reflected across X for the copy. The keys, the capsule, the solve and the temporary display are on [push-out.md](push-out.md).

## Mirroring

`"mirror": true` on a `Left` or `Right` module builds a second module on the other side from the same entry. The joints already carry their own behaviour or orientation mirror, so the mirror module builds from them like any other module.

```json
{
  "rigVersion": 1,
  "rigTemplateName": "Arms", "rigAuthor": "you",
  "rigModules": [
    { "moduleType": "FkChain", "name": "spine_00_M_MOD", "moduleArgs": { "joints": [["Center", "chest"]] } },
    { "moduleType": "FkChain", "name": "clavicle_00_L_MOD", "side": "Left", "mirror": true,
      "moduleArgs": { "joints": [["Left", "clavicle"]] }, "parentStructure": { "start": "spine_00_M_MOD.end" } },
    { "moduleType": "FkChain", "name": "arm_00_L_MOD", "side": "Left", "shapeType": "Circle", "mirror": true,
      "moduleArgs": { "joints": [["Left", "upperArm"], ["Left", "lowerArm"]] },
      "parentStructure": { "start": "clavicle_00_L_MOD.end" } }
  ],
  "constraints": [
    { "name": "armSpace_00_L_CON", "target": "arm_00_L_MOD.start", "mirror": true,
      "drivers": [ { "socket": "clavicle_00_L_MOD.end", "label": "Clavicle", "weight": 1.0 }, { "socket": "layout_00_M_MOD.end", "label": "World" } ] }
  ]
}
```

This builds `clavicle_00_R_MOD`, `arm_00_R_MOD` and `armSpace_00_R_CON` as well.

A [`TwoBoneIk`](modules.md#twoboneik) limb mirrors the same way. Its three selectors swap sides, its `poleOffset` and `settingsOffset` have their X negated, and the constraints that target its `start` and `ik` child plugs mirror like any other:

```json
{
  "rigVersion": 1,
  "rigTemplateName": "Arms", "rigAuthor": "you",
  "rigModules": [
    { "moduleType": "TwoBoneIk", "moduleVersion": 2, "name": "arm_00_L_MOD", "side": "Left", "mirror": true,
      "moduleArgs": {
        "joints": [["Left", "upperArm"], ["Left", "lowerArm"], ["Left", "hand"]],
        "controlName": "arm" },
      "parentStructure": { "start": "layout_00_M_MOD.end", "ik": "layout_00_M_MOD.end", "pole": "layout_00_M_MOD.end" } }
  ],
  "constraints": [
    { "name": "armIkSpace_00_L_CON", "target": "arm_00_L_MOD.ik", "mirror": true,
      "drivers": [ { "socket": "layout_00_M_MOD.end", "label": "World", "weight": 1.0 }, { "socket": "layout_00_M_MOD.index_0", "label": "Hips" } ] }
  ]
}
```

The mirror is the source entry with:

- **`side`** swapped, `Left` and `Right`.
- **`name`** with its side letter swapped: `arm_00_L_MOD` becomes `arm_00_R_MOD`. A mirrored module's or constraint's name must be in the naming convention with a side of `L` or `R`.
- **Every selector side** in `moduleArgs` swapped (a module that has world-space vectors in its `moduleArgs`, as TwoBoneIk does, mirrors those itself): `["Left", "upperArm"]` becomes `["Right", "upperArm"]`. `Center` selectors stay `Center`.
- **Socket ids** in `parentStructure`, `target` and `drivers` pointing at the other side's module when the named module is `Left` or `Right`: `clavicle_00_L_MOD.end` becomes `clavicle_00_R_MOD.end`. Ids on `Center` modules and `layout_00_M_MOD` stay as they are. The socket or child plug name never changes, with one exception: a socket named from joint markup (an [FkHierarchy](modules.md#fkhierarchy)'s `left_upperArm`) swaps its leading `left_` or `right_`, so a mirrored module attached to `arm_00_L_MOD.left_hand` attaches to `arm_00_R_MOD.right_hand`. This holds for ids on `Center` modules too. Such a `Left` or `Right` module needs a side letter in its name.
- **Driver labels** unchanged, except a label left out, which defaults to the mirror's own socket id.
- **`mirror`** removed. Everything else is copied.

The mirror then goes through every check an entry written by hand does. A mirror's errors name the entry it came from: `rigModules[2] 'arm_00_L_MOD' (mirror 'arm_00_R_MOD').moduleArgs: ...`.

- **A missing joint fails the build.** If no joint matches a mirrored selector, the pair is asymmetric.
- **Positions are not checked by default**, because a skeleton can be asymmetric where the rig is not. Pass `checkMirrorPositions=True` to `createRigFromTemplate` to also fail a mirrored joint that is more than 0.001 from its source joint reflected across X = 0.
- **Name clashes** fail like any duplicate: a mirror named `arm_00_R_MOD` and an entry named `arm_00_R_MOD` cannot both exist.

## Validation

`createRigFromTemplate` checks the template and the skeleton before it changes the scene. If anything is wrong it builds nothing (not even the namespace) and reports every problem at once. Each error starts with its location in the template:

```
rigModules[1] 'arm'.parentStructure.start: ...
```

Besides the rules in the tables above:

- **Unknown keys are errors** at every level, including inside `moduleArgs` and drivers, so a typo fails instead of being ignored.
- **Enum values are strict.** `side`, `shapeType` and selector sides must match exactly. There is no fallback.
- **Names must be free.** Every module's and joint's name must be unused in the rig namespace.
- **Module-specific checks** are listed in [modules.md](modules.md).

Warnings do not stop the build:

- A joint marked `export: true` that no module claims.
- Several drivers of one constraint tied for the highest weight.
