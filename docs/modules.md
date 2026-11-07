# Modules

A module is a node that takes joints and controls and computes the joints' world transforms. Modules attach to each other through **child plugs** (inputs) and **sockets** (world-space matrix outputs). A template's `parentStructure` connects a child plug to a socket by id, `module.socket` (see [templates.md](templates.md#parentstructure)).

Controls and joints have `inheritsTransform` off. The module node computes every result, and the DAG hierarchy under the rig is only for organization.

Control names come from the joint and the module's `side`: the namespace and `_jnt` are dropped, then `_ctrl` and the side suffix (`_c`, `_l`, `_r`) are added. `skel:arm_jnt` in a `Left` module gives `arm_ctrl_l`.

Each control also carries markup, `(side, usage)`, taken from its joint. Tools and shape files find controls by markup instead of by name (see [controls.md](controls.md)).

Any `Left` or `Right` module can be built on both sides from one template entry with `"mirror": true` (see [templates.md](templates.md#mirroring)). The mirror builds from the other side's joints exactly as the module always does.

Child plug, socket, control and attribute names are public: saved poses, animation and exports depend on them.

## Layout

Built into every rig. A template never lists it, but refers to it as `layout`.

| | |
|---|---|
| Node | `layout` (type `layoutModule`) |
| Controls | `layout_0_ctrl_c`, `layout_1_ctrl_c`, `layout_2_ctrl_c`: three stacked square controls at the origin, each one driving the next. Markup `Center`, `layout_0` to `layout_2`. |
| Child plugs | none |
| Sockets | `index_0`, `index_1`, `index_2`. The last is also `end`. |

Any child plug that a template doesn't parent attaches to `layout.end`, so moving the layout controls moves the whole rig.

## FkChain

A forward-kinematics chain: one control per joint, each control's rotation and translation carried down the rest of the chain.

```json
{
  "moduleType": "FkChain",
  "name": "spine",
  "moduleArgs": { "joints": [["Center", "spine1"], ["Center", "spine2"], ["Center", "spine3"]] }
}
```

| `moduleArgs` key | Required | Value |
|---|---|---|
| `joints` | yes | Joint selectors in chain order, root first |

| | |
|---|---|
| Node type | `fkChainModule` |
| Controls | One per joint, named from the joint, with the joint's markup |
| Child plugs | `start`: the first control's parent |
| Sockets | `index_<n>` for joint *n* (from 0). The first is also `start`, the last also `end`. |

With a single joint, `index_0`, `start` and `end` are the same socket.

## Aim

Points a joint at a target control, with an up control setting its roll. The aim joint's +X points at the target, and its +Z points toward the up control. The joint keeps its rest orientation at rest.

```json
{
  "moduleType": "Aim",
  "name": "eye_r",
  "side": "Right",
  "moduleArgs": { "aim": ["Right", "eye"], "target": ["Right", "eyeTarget"], "up": ["Right", "eyeUp"] },
  "parentStructure": { "aim": "head.end", "target": "head.end", "up": "head.end" }
}
```

| `moduleArgs` key | Required | Value |
|---|---|---|
| `aim` | yes | Selector for the joint that aims |
| `target` | yes | Selector for the joint where the target control is placed |
| `up` | yes | Selector for the joint where the up control is placed |

| | |
|---|---|
| Node type | `aimModule` |
| Controls | Target and up, named from their joints and with their markup. The aim joint has no control. |
| Child plugs | `aim` (the aim joint's parent), `target`, `up`. Each can attach to a different socket. |
| Sockets | `aim` (the solved aim joint), `target`, `up` (the controls) |

- The `aim` child plug has no control, so it cannot be a constraint target. `target` and `up` can.
- The build fails if the target or up joint sits on the aim joint, or if the up joint lies on the line from the aim joint to the target.
