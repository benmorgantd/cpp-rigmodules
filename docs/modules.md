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

## TwoBoneIk

A two-bone limb, an arm or a leg: an FK chain and an IK solve on the same three joints, blended joint by joint. Animators pose it by placing the hand or foot (IK) and by rotating the joints (FK), and switch between the two with `ikFkBlend`. The module does IK and FK only: there is no twist, bend bow or ribbon. One IK bone is [Aim](#aim), and an IK chain of three or more bones needs a different module.

```json
{
  "moduleType": "TwoBoneIk",
  "name": "arm_l",
  "side": "Left",
  "mirror": true,
  "moduleArgs": {
    "joints": [["Left", "upperArm"], ["Left", "lowerArm"], ["Left", "hand"]],
    "ik": ["Left", "handIk"],
    "pole": ["Left", "armPole"],
    "settings": ["Left", "armSettings"]
  },
  "parentStructure": { "start": "layout.end", "ik": "layout.end", "pole": "layout.end" }
}
```

| `moduleArgs` key | Required | Value |
|---|---|---|
| `joints` | yes | Exactly three selectors: root, mid, end |
| `ik` | yes | Selector for the joint where the IK handle is placed. It must sit at the end joint |
| `pole` | yes | Selector for the joint where the pole control is placed, out in front of the elbow or knee |
| `settings` | yes | Selector for the joint where the settings control is placed, anywhere the animator will find it |

`ik`, `pole` and `settings` are **marker joints**: joints that exist only to place a control. Mark them `export: false, skin: false`, as for Aim's target and up. Each of the six joints belongs to this module alone.

| | |
|---|---|
| Node type | `twoBoneIkModule` |
| Controls | Six, named from their joints, each with its joint's markup: the FK start, lower and end controls (`joints`), the IK handle (`ik`), the pole (`pole`) and the settings control (`settings`). Control shapes come from the module's `shapeType`. |
| Child plugs | `start` (the FK start control's parent), `ik`, `pole`. A fourth, `settings`, is internal: see below. |
| Sockets | `index_0`, `index_1`, `index_2` for the three blended joints, with `start` the same socket as `index_0` and `end` the same as `index_2`. Also `ik`, `pole` and `settings` (the three marker controls). |

**Channels.** Names are public: saved poses and animation depend on them ([controls.md](controls.md#channels)).

| Control | Keyable channels | Locked and hidden |
|---|---|---|
| FK start, lower, end | translate, rotate, scale | none |
| IK handle | translate, rotate, `roll` | scale |
| Pole | translate, `pin`, `space` | rotate, scale |
| Settings | `ikFkBlend`, `stretch`, `soft` | translate, rotate, scale |

- **`ikFkBlend`** (0 to 1, default 1): 0 is FK and 1 is IK, so **every limb is in IK when built**. Between, each joint's translation and scale lerp and its rotation slerps. At exactly 0 and 1 the result is exactly FK or IK. The FK lower and end controls show below 1, and the IK handle and pole show above 0. The start control and the settings control are always visible.
- **Switch with [`setRigIkFkBlend`](commands.md#setrigikfkblend).** The two chains hold their own control values, so the command first writes the controls of both sides to match the current pose, then sets `ikFkBlend`. Switching with `setAttr` pops: it shows whatever the other side's controls say, the same way setting a `space` directly doesn't match.
- **The FK start control** moves the whole chain. Its rotation turns the FK upper joint and **does nothing to the IK chain**: the IK root takes the start control's translation only, and the IK plane comes from the pole.
- **`stretch`** (0 to 1, default 0): 0 stops the end at full reach, on the line to the handle, like any native IK. 1 stretches both bones so the end reaches the handle.
- **`soft`** (0 to 1, default 0): eases the reach as the handle nears full length, so the elbow doesn't snap straight. It is a fraction of the limb's reach: 0 is a hard stop, and 0.1 starts easing in the last tenth of the reach. With `stretch` off, the end stops a little short of a far handle, which is the point of soft IK.
- **`pin`** (0 to 1, default 0, on the pole): 1 puts the elbow on the pole control and the wrist on the handle, so the bones lengthen or shorten to fit. Pin wins over stretch and soft, and it fades `roll` out.
- **`roll`** (an angle, default 0, on the IK handle): turns the bend plane about the root-to-handle axis, right-handed. A mirrored limb negates it.
- **The pole's `space`** (`Parent` or `Ik`, default `Parent`): in `Parent` the pole follows what its child plug is attached to (`layout.end` by default). In `Ik` it follows the IK handle's motion. This space belongs to the module, so the template can't add to it and a constraint on `pole` is an error. Setting it directly moves the pole and doesn't match.
- **The solve is stateless**: the same controls always give the same pose. A pole that crosses the line from the root to the handle flips the bend, as in a native IK. With the pole exactly on that line the bend keeps the direction it had at rest.
- A handle on the root folds the limb to its shortest reach. No input gives a NaN.
- Each IK joint keeps its rest orientation at rest, wherever the rigger put the pole. The end joint's rotation follows the IK handle's.

### Restoring a limb from joints

`poseRigFromJointPose` and `animateRigFromJointClip` put a limb's controls where its joints say ([commands.md](commands.md#how-the-joints-become-control-values)):

- **FK** controls come from the three joints, translate, rotate and scale, at any blend.
- The **IK handle** goes to the end joint, turned the way the hand joint is. The **pole** is placed so the elbow bends the way the joints bend, at its rest height from the root-to-handle line. With `pin` above 0 the pole's place along the limb is in the joints too, and it comes back where it was.
- `ikFkBlend`, `stretch`, `soft`, `pin`, `roll` and the pole's `space` come from the pose file given as the sidecar, and otherwise stay as they are in the scene. A `soft` or `stretch` the restore doesn't know about puts the handle in the wrong place, so give them in the sidecar for a pose that used them.
- **The IK side is exact at blend 0 and blend 1** for any pose the limb can make: bone lengths as built, or `stretch` or `pin` on. Between 0 and 1 the joints are a mix of both chains and IK matches them only approximately, so the restore warns. The FK controls reproduce the joints at any blend. A source with other bone lengths and `stretch` and `pin` off warns too: IK stops at the reach and FK still matches.
- A handle pulled past full reach with `stretch` off comes back at the wrist, because every handle from there on gives the same pose.
- The marker joints (`ik`, `pole`, `settings`) are not read. The settings control comes from the sidecar.

### Switching IK and FK

[`setRigIkFkBlend`](commands.md#setrigikfkblend) runs the same inverse on the limb's live joints. The module publishes its result on three outputs that only a tool pulls, so playback never pays for them: `fkMatchLocal` (the FK start, lower and end controls' local matrices), `ikMatchLocal` (the IK handle's and the pole's) and `matchWarnings` (a JSON list of what the match can't do). The command turns each matrix into channel values and writes them with the blend. The same rules apply as to a restore: exact at blend 0 and 1, approximate for the IK side in between, and the FK side is exact at any blend.
- A limb joint the source lacks leaves all the limb's controls to the sidecar, or rest.

**The settings control** follows the parent of the `start` child plug (the chest for an arm, the pelvis for a leg), whatever space the arm is in. A constraint on `start` doesn't carry it.

**Parenting:**

- `parentStructure` has three keys: `start`, `ik` and `pole`. A `settings` key is an error: the settings child plug is internal, and takes `start`'s parent.
- A constraint may target `start` and `ik`. A constraint on `pole` or `settings` is an error.
- Attach a hand's fingers to `arm_l.end` (or `index_2`): the sockets carry the blended joint, so the fingers follow the hand in IK and in FK, and their controls stay visible. A module cannot attach to its own sockets, so this module's own `pole` can't be parented to its `ik` socket: the pole's `Ik` space does that inside the module.
- Mirroring needs nothing extra: every selector's side is swapped, `roll` is negated by the pose mirror and everything else copies.

The build fails if the limb has any other count of joints, if two limb joints sit on each other, if the `ik` joint is more than 0.001 from the end joint, or if the pole joint lies on the line from the root to the end joint. A limb with a bent rest pose works wherever the pole joint is: IK at rest reproduces the rest bend.

```python
from maya import cmds

cmds.setAttr("rig:handIk_l_ctrl_l.translateY", 3)               # the hand follows the handle
cmds.setAttr("rig:armSettings_l_ctrl_l.stretch", 1)             # and reaches it even when it is far
cmds.setAttr("rig:armPole_l_ctrl_l.space", 1)                   # the pole now follows the handle
```
