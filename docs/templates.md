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
      "name": "spine",
      "moduleArgs": { "joints": [["Center", "spine1"], ["Center", "spine2"]] }
    },
    {
      "moduleType": "FkChain",
      "name": "arm",
      "side": "Left",
      "moduleArgs": { "joints": [["Left", "arm"]] },
      "parentStructure": { "start": "spine.end" }
    }
  ],
  "constraints": [
    {
      "name": "arm_space",
      "target": "arm.start",
      "drivers": [
        { "socket": "spine.end", "label": "Chest", "weight": 1.0 },
        { "socket": "layout.end", "label": "World" }
      ]
    }
  ]
}
```

## Top level

| Key | Required | Value |
|---|---|---|
| `rigVersion` | yes | Integer, 1 or more |
| `rigTemplateName` | yes | Non-empty string |
| `rigAuthor` | yes | String |
| `rigModules` | yes | Array of modules (below) |
| `rigName` | no | Names the rig root, and is the rig variant (`game`, `cine`). Defaults to `rigTemplateName`. Unique among the rigs built into one namespace |
| `constraints` | no | Array of constraints (below) |

## Modules

| Key | Required | Value |
|---|---|---|
| `moduleType` | yes | A module type from [modules.md](modules.md): `FkChain` or `Aim` |
| `name` | yes | The module's id. Unique in the template, a valid Maya node name (no `.`), and not `layout`. The module node gets this name in the rig namespace. |
| `side` | no | `Center` (default), `Left` or `Right`. Sets the control name suffix (`_c`, `_l`, `_r`). |
| `shapeType` | no | Control shape for the module's controls: `Box` (default) or any shape in [controls.md](controls.md#shapes) |
| `moduleVersion` | no | Integer from 1 to the module's current version. Defaults to 1. Every module is at version 1. |
| `moduleArgs` | no | Module-specific arguments, mostly joint selectors. See [modules.md](modules.md). |
| `parentStructure` | no | Maps this module's child plugs to parent sockets (below) |
| `mirror` | no | `true` also builds this module on the other side. Only `Left` and `Right` modules. See [Mirroring](#mirroring). |

### Joint selectors

A selector is a `[side, usage]` pair matched against each joint's `rigMarkup` attribute, for example `["Left", "upperArm"]`.

- A selector must match exactly one of the joints passed to the build.
- A joint can be claimed by only one module.
- A joint whose `offsetParentMatrix` is already connected cannot be claimed.

### parentStructure

```json
"parentStructure": { "start": "spine.end" }
```

Each key is one of this module's child plugs. Each value is a socket id, `module.socket`, where `module` is another module's `name` or `layout`.

- A bare socket name (`"end"`) is an error. Write `layout.end`.
- Child plugs that are not listed attach to `layout.end`, as do all child plugs of a module with no `parentStructure`.
- A module cannot parent to itself, directly or through a loop of modules.

## Constraints

A constraint gives a control a choice of parent spaces. With more than one driver, the control gets a `space` enum attribute listing the driver labels.

| Key | Required | Value |
|---|---|---|
| `name` | yes | The constraint node's name. Unique in the template. |
| `target` | yes | `module.childPlug`. The child plug must have a control. Only one constraint can target a child plug. |
| `drivers` | yes | At least one driver (below) |
| `type` | no | `Parent`, the only type so far |
| `mirror` | no | `true` also builds this constraint on the other side. The target must be a `Left` or `Right` module. See [Mirroring](#mirroring). |

Drivers:

| Key | Required | Value |
|---|---|---|
| `socket` | yes | Socket id, `module.socket` |
| `label` | no | Enum label shown on the control. Defaults to the socket id. Unique within the constraint. |
| `weight` | no | 0 or more, default 0. The driver with the highest weight starts active. With a tie, the first of them starts active (and the build warns). With no weights, the first driver starts active. |

A module cannot drive its own child plug, and no loop may run through `parentStructure` and constraint drivers together.

## Mirroring

`"mirror": true` on a `Left` or `Right` module builds a second module on the other side from the same entry. The joints already carry their own behaviour or orientation mirror, so the mirror module builds from them like any other module.

```json
{
  "rigVersion": 1,
  "rigTemplateName": "Arms", "rigAuthor": "you",
  "rigModules": [
    { "moduleType": "FkChain", "name": "spine", "moduleArgs": { "joints": [["Center", "chest"]] } },
    { "moduleType": "FkChain", "name": "clavicle_l", "side": "Left", "mirror": true,
      "moduleArgs": { "joints": [["Left", "clavicle"]] }, "parentStructure": { "start": "spine.end" } },
    { "moduleType": "FkChain", "name": "arm_l", "side": "Left", "shapeType": "Circle", "mirror": true,
      "moduleArgs": { "joints": [["Left", "upperArm"], ["Left", "lowerArm"]] },
      "parentStructure": { "start": "clavicle_l.end" } }
  ],
  "constraints": [
    { "name": "arm_space_l", "target": "arm_l.start", "mirror": true,
      "drivers": [ { "socket": "clavicle_l.end", "label": "Clavicle", "weight": 1.0 }, { "socket": "layout.end", "label": "World" } ] }
  ]
}
```

This builds `clavicle_r`, `arm_r` and `arm_space_r` as well. The mirror is the source entry with:

- **`side`** swapped, `Left` and `Right`.
- **`name`** with its side suffix swapped: `arm_l` becomes `arm_r`. A mirrored module's name must end in its side's suffix (`_l` or `_r`), and a mirrored constraint's name must end in `_l` or `_r`.
- **Every selector side** in `moduleArgs` swapped: `["Left", "upperArm"]` becomes `["Right", "upperArm"]`. `Center` selectors stay `Center`.
- **Socket ids** in `parentStructure`, `target` and `drivers` pointing at the other side's module when the named module is `Left` or `Right`: `clavicle_l.end` becomes `clavicle_r.end`. Ids on `Center` modules and `layout` stay as they are. The socket or child plug name never changes. Such a `Left` or `Right` module needs a side suffix on its name.
- **Driver labels** unchanged, except a label left out, which defaults to the mirror's own socket id.
- **`mirror`** removed. Everything else is copied.

The mirror then goes through every check an entry written by hand does. A mirror's errors name the entry it came from: `rigModules[2] 'arm_l' (mirror 'arm_r').moduleArgs: ...`.

- **A missing joint fails the build.** If no joint matches a mirrored selector, the pair is asymmetric.
- **Positions are not checked by default**, because a skeleton can be asymmetric where the rig is not. Pass `checkMirrorPositions=True` to `createRigFromTemplate` to also fail a mirrored joint that is more than 0.001 from its source joint reflected across X = 0.
- **Name clashes** fail like any duplicate: a mirror named `arm_r` and an entry named `arm_r` cannot both exist.

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
