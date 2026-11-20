# Modules

A module is a node that takes joints and controls and computes the joints' world transforms. Modules attach to each other through **child plugs** (inputs) and **sockets** (world-space matrix outputs). A template's `parentStructure` connects a child plug to a socket by id, `module.socket` (see [templates.md](templates.md#parentstructure)).

Controls and joints have `inheritsTransform` off. The module node computes every result, and the DAG hierarchy under the rig is only for organization.

Every name follows the [naming convention](naming.md), `<camelCaseName>_##_<L|R|M>_<JNT|CTRL|MOD|CON>`. A control's name is its joint's name with the module's side and `CTRL`: the namespace is dropped, so `skel:arm_00_L_JNT` in a `Left` module gives `arm_00_L_CTRL`. A control with no joint is named from its usage: the Layout's `layout_0` gives `layout_00_M_CTRL`, and TwoBoneIk's `armIK` gives `armIK_00_L_CTRL`.

Each control also carries markup, `(side, usage)`, taken from its joint. Tools and shape files find controls by markup instead of by name (see [controls.md](controls.md)).

Any `Left` or `Right` module can be built on both sides from one template entry with `"mirror": true` (see [templates.md](templates.md#mirroring)). The mirror builds from the other side's joints exactly as the module always does.

Child plug, socket, control and attribute names are public: saved poses, animation and exports depend on them.

## Layout

Built into every rig. A template never lists it, but refers to it as `layout_00_M_MOD`.

| | |
|---|---|
| Node | `layout_00_M_MOD` (type `layoutModule`) |
| Controls | `layout_00_M_CTRL`, `layout_01_M_CTRL`, `layout_02_M_CTRL`: three stacked square controls at the origin, each one driving the next. Markup `Center`, `layout_0` to `layout_2`. |
| Child plugs | none |
| Sockets | `index_0`, `index_1`, `index_2`. The last is also `end`. |

Any child plug that a template doesn't parent attaches to `layout_00_M_MOD.end`, so moving the layout controls moves the whole rig.

## FkChain

A forward-kinematics chain: one control per joint, each control's rotation and translation carried down the rest of the chain.

```json
{
  "moduleType": "FkChain",
  "name": "spine_00_M_MOD",
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
  "name": "eye_00_R_MOD",
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
  "moduleVersion": 2,
  "name": "arm_00_L_MOD",
  "side": "Left",
  "mirror": true,
  "moduleArgs": {
    "joints": [["Left", "upperArm"], ["Left", "lowerArm"], ["Left", "hand"]],
    "controlName": "arm",
    "poleOffset": [0, 2, 0],
    "settingsOffset": [-1, 3, 0]
  },
  "parentStructure": { "start": "layout_00_M_MOD.end", "ik": "layout_00_M_MOD.end", "pole": "layout_00_M_MOD.end" }
}
```

| `moduleArgs` key | Required | Value |
|---|---|---|
| `joints` | yes | Exactly three selectors: root, mid, end |
| `controlName` | no | The base of the IK, pole and settings control names (below). Default: the first joint's usage without a trailing `_NN` (`upperArm` stays `upperArm`, so an arm wants `"controlName": "arm"`) |
| `poleOffset` | no | `[x, y, z]` in world space: the pole control's offset from the mid joint. Default: see below |
| `settingsOffset` | no | `[x, y, z]` in world space: the settings control's offset from the end joint. Default: see below |
| `ikFkBlend` | no | 0 to 1: the limb's blend when built, and the rest value of its `ikFkBlend` channel. Default 1 (IK). An arm usually wants 0 (FK). Out of range fails the build |
| `ikOrientToWorld` | no | `true` puts the IK handle on world axes instead of the end joint's. Default `false`. For arms and legs that want orthogonal controls. The joints keep their bind pose |
| `ikAlignToPole` | no | `true` (needs `ikOrientToWorld`) turns the world-aligned handle about world Y only, so +Z points along the horizontal part of the line from the end joint to the pole. Default `false`. For feet. Fails the build when the pole is straight above or below the end joint |

There are **no marker joints** (D57). The three limb joints belong to this module alone, and the other three controls are placed from them:

- **The IK handle** sits at the end joint, turned the way that joint is turned.
- **The pole** sits at the mid joint plus `poleOffset`. Without it, it is pushed out from the mid joint by the limb's length along the bend: the part of the root-to-mid vector that is perpendicular to the root-to-end line, so the elbow or knee points at it. A **straight limb is not an error** (mocap skeletons have them): when the mid joint is within 0.1 percent of the limb's length of the root-to-end line, the pole goes out along the **root joint's -Y** instead, which is the back of an elbow and the front of a knee on a skeleton whose joints are oriented the usual way. A straight limb whose root-to-end line runs along that same axis (a leg pointing straight down a Y axis) has no bend plane then, and the build fails with an error that asks for a `poleOffset`.
- **The settings control** sits at the end joint plus `settingsOffset`. Without it, it sits a quarter of the limb's length along world +X from the end joint (-X on the mirrored side).
- **Orientation.** The handle takes the end joint's rotation, or world axes with `ikOrientToWorld` (a yaw toward the pole as well with `ikAlignToPole`). Both keys apply to the mirrored side by the same rule: world axes stay world axes, and the yaw follows the mirrored pole, so the two feet turn equal and opposite and each faces its own pole. A world-aligned handle on both sides has the same axes, so its translate channels are not reflected the way a joint-aligned right handle's are. The pole and settings controls take **world axes**: their rotation is locked, so this only sets which way their translate channels point.
- **Mirroring.** `poleOffset` and `settingsOffset` are world vectors for a `Left` or `Center` module. On the mirrored copy of a module the offsets' X is negated, so one template entry gives both sides.

| | |
|---|---|
| Node type | `twoBoneIkModule` |
| Controls | Six. The FK start, lower and end controls are named from their joints and take their markup. The IK handle, pole and settings controls have no joint: their usages are `<base>IK`, `<base>Pole` and `<base>Settings` (`armIK`, `armPole`, `armSettings`), with the side of the first joint, and their nodes are named from the usage (`armPole_00_L_CTRL`). The names are public: poses and animation depend on them. Control shapes come from the module's `shapeType`. |
| Child plugs | `start` (the FK start control's parent), `ik`, `pole`. A fourth, `settings`, is internal: see below. |
| Sockets | `index_0`, `index_1`, `index_2` for the three blended joints, with `start` the same socket as `index_0` and `end` the same as `index_2`. Also `ik`, `pole` and `settings` (the three derived controls). |

**Channels.** Names are public: saved poses and animation depend on them ([controls.md](controls.md#channels)).

| Control | Keyable channels | Locked and hidden |
|---|---|---|
| FK start, lower, end | translate, rotate, scale | none |
| IK handle | translate, rotate, `roll` | scale |
| Pole | translate, `pin`, `space` | rotate, scale |
| Settings | `ikFkBlend`, `stretch`, `soft` | translate, rotate, scale |

- **`ikFkBlend`** (0 to 1): 0 is FK and 1 is IK. The channel's default and rest come from the template's `ikFkBlend` key, which is 1 when absent, so **a limb is in IK when built unless the template says otherwise**. Between, each joint's translation and scale lerp and its rotation slerps. At exactly 0 and 1 the result is exactly FK or IK. The FK lower and end controls show below 1, and the IK handle and pole show above 0. The start control and the settings control are always visible.
- **Switch with [`setRigIkFkBlend`](commands.md#setrigikfkblend).** The two chains hold their own control values, so the command first writes the controls of both sides to match the current pose, then sets `ikFkBlend`. Switching with `setAttr` pops: it shows whatever the other side's controls say, the same way setting a `space` directly doesn't match.
- **The FK start control** moves the whole chain. Its rotation turns the FK upper joint and **does nothing to the IK chain**: the IK root takes the start control's translation only, and the IK plane comes from the pole.
- **`stretch`** (0 to 1, default 0): 0 stops the end at full reach, on the line to the handle, like any native IK. 1 stretches both bones so the end reaches the handle.
- **`soft`** (0 to 1, default 0): eases the reach as the handle nears full length, so the elbow doesn't snap straight. It is a fraction of the limb's reach: 0 is a hard stop, and 0.1 starts easing in the last tenth of the reach. With `stretch` off, the end stops a little short of a far handle, which is the point of soft IK.
- **`pin`** (0 to 1, default 0, on the pole): 1 puts the elbow on the pole control and the wrist on the handle, so the bones lengthen or shorten to fit. Pin wins over stretch and soft, and it fades `roll` out.
- **`roll`** (an angle, default 0, on the IK handle): turns the bend plane about the root-to-handle axis, right-handed. A mirrored limb negates it.
- **The pole's `space`** (`Parent` or `Ik`, default `Parent`): in `Parent` the pole follows what its child plug is attached to (`layout_00_M_MOD.end` by default). In `Ik` it follows the IK handle's motion. This space belongs to the module, so the template can't add to it and a constraint on `pole` is an error. Switch it with [`setRigControlSpace`](commands.md#setrigcontrolspace)`(pole, label="Ik")`, which keeps the pole where it is in the world. [`getRigControlSpace`](commands.md#getrigcontrolspace) lists it with the other spaces. Setting it directly with `setAttr` moves the pole and doesn't match.
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
- The IK handle, pole and settings controls have no joints. The handle and pole are solved from the three limb joints as above, and the settings control comes from the sidecar.

### Switching IK and FK

[`setRigIkFkBlend`](commands.md#setrigikfkblend) runs the same inverse on the limb's live joints. The module publishes its result on three outputs that only a tool pulls, so playback never pays for them: `fkMatchLocal` (the FK start, lower and end controls' local matrices), `ikMatchLocal` (the IK handle's and the pole's) and `matchWarnings` (a JSON list of what the match can't do). The command turns each matrix into channel values and writes them with the blend. The same rules apply as to a restore: exact at blend 0 and 1, approximate for the IK side in between, and the FK side is exact at any blend.
- A limb joint the source lacks leaves all the limb's controls to the sidecar, or rest.

**The settings control** follows the parent of the `start` child plug (the chest for an arm, the pelvis for a leg), whatever space the arm is in. A constraint on `start` doesn't carry it.

**Parenting:**

- `parentStructure` has three keys: `start`, `ik` and `pole`. A `settings` key is an error: the settings child plug is internal, and takes `start`'s parent.
- A constraint may target `start` and `ik`. A constraint on `pole` or `settings` is an error.
- Attach a hand's fingers to `arm_00_L_MOD.end` (or `index_2`): the sockets carry the blended joint, so the fingers follow the hand in IK and in FK, and their controls stay visible. A module cannot attach to its own sockets, so this module's own `pole` can't be parented to its `ik` socket: the pole's `Ik` space does that inside the module.
- Mirroring needs nothing extra: every selector's side is swapped, `roll` is negated by the pose mirror and everything else copies.

The build fails if the limb has any other count of joints, if two limb joints sit on each other, if `poleOffset` or `settingsOffset` isn't three numbers, or if the pole lands on the line from the root to the end joint (only a straight limb along its root's Y axis does that by default). A limb with a bent rest pose works wherever the pole is: IK at rest reproduces the rest bend.

**Version 1.** A `moduleVersion` 1 template names three marker joints in `ik`, `pole` and `settings`. The update to version 2 drops those keys, so the template still builds, but it has no scene to read the old joints from: the pole and settings controls go to the derived places above, **not where the old marker joints were**, and the controls are renamed (`handIk` becomes `<base>IK`). Add `controlName`, `poleOffset` and `settingsOffset` to put them back. Saved poses and animation that name the old controls stop matching.

```python
from maya import cmds

cmds.setAttr("rig:armIK_00_L_CTRL.translateY", 3)               # the hand follows the handle
cmds.setAttr("rig:armSettings_00_L_CTRL.stretch", 1)             # and reaches it even when it is far
cmds.setAttr("rig:armPole_00_L_CTRL.space", 1)                   # the pole now follows the handle
```

## Ribbon

A bendy, stretchy run of bind joints driven by a few controls: a tail, a spine, or a twist-distributed limb segment. A few **ik controls** (a start, an end and up to six between) shape a virtual curve, and every **bind joint** rides it, so one move bends, stretches and twists many joints smoothly. There is no curve node and no mesh: the node evaluates the curve itself. [FkChain](#fkchain) is one control per joint, [Aim](#aim) points one joint and [TwoBoneIk](#twoboneik) is two bones with no bend of its own, so none of them covers this.

```json
{
  "moduleType": "Ribbon",
  "moduleVersion": 2,
  "name": "tail_01_M_MOD",
  "side": "Center",
  "moduleArgs": {
    "controlName": "tail",
    "joints": [["Center", "tail1"], ["Center", "tail2"], ["Center", "tail3"], ["Center", "tail4"], ["Center", "tail5"]]
  }
}
```

| `moduleArgs` key | Required | Value |
|---|---|---|
| `controlCount` | no | 2 to 8, default 3: how many ik controls, the start, the end and the interior ones, spread evenly by arc length along the chain |
| `controlName` | no | The base of the control names (below). Default: the first bind joint's usage without a trailing `_NN` (`spine_00` gives `spine`, `tail1` stays `tail1`). It must be camelCase: a lowercase letter, then letters and digits |
| `joints` | yes | 2 to 64 selectors for the **bind joints**, root to tip, already spaced the way the ribbon should hold them at rest |
| `startAimBlend`, `endAimBlend` | no | A number from 0 to 1, default 0: the default, and so the rest, of the channel of the same name (below). Set `startAimBlend` to 1 on a shoulder or thigh so its start aims down the limb on every build. A value outside 0 to 1 fails the build |
| `fkStyle` | no | `true` adds an **fk chain** under the ik controls (below). Default `false` |
| `fkControlCount` | no | 2 to 8, default `controlCount`: how many fk controls, placed the same way as the ik controls. Needs `fkStyle`. Setting it without `fkStyle`, or outside 2 to 8, fails the build |

There are **no control joints**. The ik controls are placed from the bind joints (D57): a smooth spline runs through the bind joints' rest positions, and control `k` of `c` sits at the share `k / (c - 1)` of its length. Its orientation is the rotation of the two bind joints either side of it, blended by how far between them it sits, so the first and last controls take the first and last joints' frames. A bind joint's X axis must run along the chain, either toward the tip (+X down the chain, +Y up, the way an FK chain's do) or back up it (-X down the chain, as on a mirrored limb that keeps the other side's axes). The build reads which from the bind joints and every control's twist keeps its sign about its own X. The build fails, naming the joint, when a joint's X axis is more than 60 degrees off the line to the next joint (the last uses the line from the one before), when joints disagree about the direction, or when two neighbouring joints sit on each other. Every joint belongs to this module alone.

`moduleVersion` 1 templates have a `controls` list of control joints. It is read as `controlCount` (the list's length) and the joints are no longer used, so an old template still builds, with the controls at the derived places and under the new names.

| | |
|---|---|
| Node type | `ribbonModule` |
| Controls | `controlCount` **ik controls**, usage `<base>IK_<nn>` (`tailIK_00`, `tailIK_01`, ...), and one hidden **tweaker** per bind joint, usage `<base>Tweak_<nn>` with `nn` the bind joint's index from 00 (`tailTweak_00`, ...). The side is the first bind joint's. An ik control's node is named from its usage (`tailIK_00_M_CTRL`) and a tweaker's from its bind joint (`tail1_00_M_CTRL`). Usages are unique in a rig, so two ribbons with the same base and side fail the build: set `controlName` on one. Control shapes come from the module's `shapeType`. |
| Child plugs | `start` (the start control's parent) and `end` (the end control's parent). With `fkStyle` only `start`: the end rides the fk chain, and a template that connects `end` fails the build |
| Sockets | `index_0` to `index_<j-1>` for the ik controls (`j` is `controlCount`), with `start` the same socket as `index_0` and `end` the last. The tweakers and the fk controls have none. |

**The fk chain (`fkStyle`).** `fkControlCount` more controls, usage `<base>FK_<nn>` (`tailFK_00`, ...), a real chain: each rides the one before, so turning one carries the rest, as in an [FkChain](#fkchain). The first follows the `start` plug. The ribbon's base curve passes through the chain's positions, and every ik control, the start and end too, floats on that curve and keeps its own local offset, so animators pose the shape with the ik controls on top of the chain they keep as a pose. Each ik control follows the fk chain 1:1: it takes the curve's direction and the chain's roll about it, interpolated between the fk controls at its place along the chain, so rolling an fk control rolls the ik control there. The chain's own controls never move the ik controls' channels. The fk controls have no joints, so a restore from joints leaves them at the sidecar or rest values and the ik end controls and tweakers carry the joints exactly, as for the interior ik controls. The ik controls' rest is placed at the same spots as without `fkStyle`, so a rest rig is the same.

**How it moves.**

- The **start and end controls** place the ends. Their own X axes set the curve's tangents there, so turning an end bends the ribbon, and `startAimBlend` and `endAimBlend` blend each tangent toward the other end.
- An **interior ik control** floats on the curve between the ends and holds an offset from it. At rest it is where it was built. Moving it bends the curve through the new position, and only the segments around it change.
- A **tweaker** sits on the finished curve at its bind joint's place and holds the animator's last offset. It is hidden, and still keyable. What a tweaker is given reaches that one joint and no other.
- The ribbon is solved at rest and carried by the start's parent, so moving the parent moves the whole ribbon without changing its shape. The `end` child plug carries the end control with its own parent: drive it from another module's socket (a chest for a spine) and the ribbon runs between the two.
- **The end follows its parent's twist.** When the `end` plug's parent turns about the ribbon's X relative to the `start` plug's parent, the end's twist includes that turn, and the ramp carries it along the ribbon: 0 at the start, the full turn at the end. The turn is the swing-twist split of the end plug's motion relative to the start's, about the end control's rest X, wrapped to a half turn on either side, and `endTwist` picks the branch of the sum as it does for the control's own roll. Parents that move together add nothing. An arm is two ribbons: the upper one's `start` on the clavicle or chest, its `end` on the elbow socket; the forearm's `start` on the elbow socket and `end` on the wrist. The shoulder's twist then eases in along the upper arm and the two ribbons meet at the elbow. A parent twisting past 180 degrees flips the sign like any other twist unless `endTwist` is keyed to the turn.
- **Twist** is stateless. Each end's roll is read from its own rotation about X, wrapped to half a turn either side. The two ends' rolls ramp evenly along the whole ribbon (by the parameter `u`, 0 at the start to 1 at the end), and every interior control carries that ramp in its own frame, so a roll on the start reaches the far end of the chain and not only the next control. An interior control's own roll about X adds to the ramp at its place, and the joints between controls follow the controls' totals evenly. To roll past 180 degrees, key `startTwist` or `endTwist` near the total you want: the result lands on the turn of the rotation closest to it. With the channel at 0 the roll is the plain wrapped angle. Nothing is remembered between frames, so scrubbing and baking agree.
- **Scale** interpolates smoothly between the controls' scales. `volumePreserve` scales the cross-section by the length ratio to the power of -0.5, with the ratio clamped between 0.25 and 4, weighted along the ribbon by `scaleStart`, `scaleMid` and `scaleEnd`.
- A bind joint's place along the ribbon is its **parameter**: its share of the rest distance along the polyline through the ik controls' rest places. It is not an arc length, so a very uneven stretch spaces joints by that share and not by distance.

**Channels** are all on the **first control**, with the control's own translate, rotate and scale. Names are public ([controls.md](controls.md#channels)); each channel's default is its rest.

| Channel | Default | Range | What it does | Mirror |
|---|---|---|---|---|
| `startTwist`, `endTwist` | 0 | angle | The secondary twist angle that picks the turn for a roll past 180 degrees | negate |
| `sineAmplitude` | 0 | -5 to 5 | A sine wave along the ribbon, as a fraction of the rest length | copy |
| `sineFrequency` | 1 | 0 to 20 | Cycles along the whole ribbon | copy |
| `sinePhase` | 0 | -20 to 20 | Cycles | copy |
| `sineOrient` | 0 | angle | The wave's direction around the ribbon: 0 is up, a quarter turn is sideways | negate |
| `sineStartFalloff`, `sineEndFalloff` | 0.2 | 0 to 1 | The share of the ribbon over which the wave fades in and out | copy |
| `startAimBlend`, `endAimBlend` | 0, or the `moduleArgs` key | 0 to 1 | 0 lets the end's own X axis set its tangent, 1 aims it at the other end | copy |
| `bendRoundness` | 1 | 0 to 2 | The size of the end tangents: 0 is straight, about 0.65 to 1 is rounded | copy |
| `volumePreserve` | 0 | 0 to 1 | How much a stretch squashes the cross-section (and a squash fattens it) | copy |
| `scaleStart`, `scaleMid`, `scaleEnd` | 1 | 0 to 1 | The weight of `volumePreserve` at the start, the middle and the end | copy |

The sine goes on after the twist.

**Parenting.** `parentStructure` may have `start` and `end`. A `start` you leave out parents to `layout_00_M_MOD.end`, as everywhere. An `end` you leave out takes **the same parent as `start`**, so a self-contained ribbon (a tail) moves as one with its parent. List `end` to drive the end from another socket. A constraint may target either. Mirroring needs nothing extra: selector sides swap, and `startTwist`, `endTwist` and `sineOrient` are negated by the pose mirror.

**An arm** is two ribbons on a [TwoBoneIk](#twoboneik) `arm_01_L_MOD`. The upper arm hangs from the body (here the layout's end; use the clavicle's socket in a rig that has one) and ends on the elbow socket, so it starts with no twist and gathers the shoulder's twist along its length. The forearm starts on the elbow socket and ends on the wrist, so it begins with the twist the upper arm ends with. The aim keys make each end aim down its bone.

```json
[
  {
    "moduleType": "Ribbon", "moduleVersion": 2, "name": "upper_01_M_MOD", "side": "Center",
    "moduleArgs": {
      "controlName": "up", "controlCount": 2,
      "joints": [["Center", "up1"], ["Center", "up2"], ["Center", "up3"], ["Center", "up4"], ["Center", "up5"]],
      "startAimBlend": 1, "endAimBlend": 1
    },
    "parentStructure": { "start": "layout_00_M_MOD.end", "end": "arm_01_L_MOD.index_1" }
  },
  {
    "moduleType": "Ribbon", "moduleVersion": 2, "name": "fore_01_M_MOD", "side": "Center",
    "moduleArgs": {
      "controlName": "fo", "controlCount": 2,
      "joints": [["Center", "fo1"], ["Center", "fo2"], ["Center", "fo3"], ["Center", "fo4"], ["Center", "fo5"]],
      "startAimBlend": 1, "endAimBlend": 1
    },
    "parentStructure": { "start": "arm_01_L_MOD.index_1", "end": "arm_01_L_MOD.end" }
  }
]
```

**A shoulder that sheds its twist.** The arm above gives the upper ribbon a start with no twist by hanging it from the body. To hang it from the shoulder instead, so its start follows the shoulder's position and swing (its X points up when the arm raises and forward when it swings forward) with the shoulder's twist removed, use a [`Swing`](templates.md#constraint-types) constraint on the start, driven by the shoulder's own socket, and a `Point` constraint on the end driven by the elbow. Both plugs take the shoulder as their parent: the end then keeps the shoulder's whole rotation, twist included, and the elbow's position. The twist eases in along the ribbon from nothing at the shoulder to the whole twist at the elbow, and a forearm ribbon that starts on the elbow socket takes it up from there.

```json
{
  "rigModules": [
    {
      "moduleType": "Ribbon", "moduleVersion": 2, "name": "upper_01_M_MOD", "side": "Center",
      "moduleArgs": {
        "controlName": "up", "controlCount": 2,
        "joints": [["Center", "up1"], ["Center", "up2"], ["Center", "up3"], ["Center", "up4"], ["Center", "up5"]]
      },
      "parentStructure": { "start": "arm_01_L_MOD.start", "end": "arm_01_L_MOD.start" }
    }
  ],
  "constraints": [
    { "name": "upperStartSwing_00_M_CON", "type": "Swing", "target": "upper_01_M_MOD.start",
      "drivers": [{ "socket": "arm_01_L_MOD.start", "label": "Shoulder", "weight": 1.0 }] },
    { "name": "upperEndPoint_00_M_CON", "type": "Point", "target": "upper_01_M_MOD.end",
      "drivers": [{ "socket": "arm_01_L_MOD.index_1", "label": "Elbow", "weight": 1.0 }] }
  ]
}
```

Without the `Swing` constraint a start parented on the shoulder carries its twist along the whole ribbon at once, with no ease. A shoulder that twists past a half turn flips the twist's sign like any other (see the end follows its parent's twist, above), and a swing of exactly a half turn from rest has no twist to remove, so the start's twist flips there. Neither is a case for the arm's normal range.

**A wrist that sends up only its twist.** A forearm ribbon whose end hangs from the wrist socket carries the hand's swing as well as its twist. The swing tilts the end frame and bends the ribbon, so the middle control moves when the hand flops. To send up the hand's twist alone, hang the end from the elbow socket like the start and use a [`Twist`](templates.md#constraint-types) constraint on the end, driven by the wrist. The end sits at the wrist and turns with the forearm's swing, and the hand's twist about the bone eases in along the ribbon from the elbow's to the wrist's. The same goes for an ankle on a leg ribbon.

```json
{
  "rigModules": [
    {
      "moduleType": "Ribbon", "moduleVersion": 2, "name": "fore_01_M_MOD", "side": "Center",
      "moduleArgs": {
        "controlName": "fo", "controlCount": 2,
        "joints": [["Center", "fo1"], ["Center", "fo2"], ["Center", "fo3"], ["Center", "fo4"], ["Center", "fo5"]]
      },
      "parentStructure": { "start": "arm_01_L_MOD.index_1", "end": "arm_01_L_MOD.index_1" }
    }
  ],
  "constraints": [
    { "name": "foreEndTwist_00_M_CON", "type": "Twist", "target": "fore_01_M_MOD.end",
      "drivers": [{ "socket": "arm_01_L_MOD.end", "label": "Wrist", "weight": 1.0 }] }
  ]
}
```

The twist is the hand's whole twist about the bone since rest, the shoulder's and elbow's included, so a twist higher up the arm turns the whole forearm ribbon and nothing eases. Beyond a half turn the twist wraps like `Swing`'s, and the `endTwist` channel picks the turn.

**Restoring from joints** ([commands.md](commands.md#how-the-joints-become-control-values)): the start and end controls come from the first and last bind joints, whose frames they share, and each tweaker from its bind joint, so every bind joint present comes back exactly. An interior ik control has no joint, so it keeps the value from the sidecar, or rest, and the tweakers carry the difference. The channels above come from the sidecar, and otherwise stay as they are in the scene: a `sineAmplitude` the restore doesn't know about puts the interior controls in the wrong place.

The build fails if `controlCount` is not 2 to 8, if there are fewer than 2 or more than 64 bind joints, or if the bind joints' axes fail the checks above.

The build also fails if `fkControlCount` is set without `fkStyle` or is not 2 to 8.

```python
from maya import cmds

cmds.setAttr("rig:tailIK_01_M_CTRL.translateY", 2)    # the middle of the tail rises, and the joints bend around it
cmds.setAttr("rig:tailIK_02_M_CTRL.rotateX", 200)     # the end rolls 200 degrees about its own X, and the roll ramps along the ribbon, so the middle control's frame turns 100
cmds.setAttr("rig:tailIK_00_M_CTRL.endTwist", 200)    # and the ribbon follows it past 180
cmds.setAttr("rig:tailIK_00_M_CTRL.volumePreserve", 1)  # squash and stretch fatten and thin the joints
```


## Limb

An arm or a leg with **one continuous ribbon**. It is a [TwoBoneIk](#twoboneik) (the IK/FK blend, pole, stretch and soft) and a [Ribbon](#ribbon) in one node: a single curve passes through the start, mid and end joints, and a row of bind joints rides it, so the elbow or knee has no crease between an upper and a lower ribbon, and the twist and every channel run the whole length. Use it for a limb you want to deform well. Build [TwoBoneIk](#twoboneik) and [Ribbon](#ribbon) on their own when you want only one of them.

The limb's own joints (`joints`) are the IK/FK chain and are not skin joints. Each joint's X axis must run along the chain, within 60 degrees of the line to the next joint, except the last (the hand or ankle), which may be up to 80 degrees off the line from the joint before it so an ankle's X axis can point at the ball of the foot. The bind joints (`bindJoints`) are the skin joints: 5 or more, root to tip, **all leaves**, the first at the root joint, the last at the end joint and exactly one at the mid joint, none stacked. The ribbon's ik controls and the tweakers (one per bind joint, hidden) are derived from them, so the skeleton has no control joints.

```json
{
  "moduleType": "Limb", "name": "arm_01_L_MOD", "shapeType": "Box", "side": "Left",
  "moduleArgs": {
    "joints": [ ["Left", "upperArm"], ["Left", "lowerArm"], ["Left", "hand"] ],
    "bindJoints": [ ["Left", "bind1"], ["Left", "bind2"], ["Left", "bind3"], ["Left", "bind4"], ["Left", "bind5"],
                    ["Left", "bind6"], ["Left", "bind7"], ["Left", "bind8"], ["Left", "bind9"] ],
    "controlName": "arm", "controlCount": 5
  },
  "parentStructure": { "start": "layout_00_M_MOD.end", "ik": "layout_00_M_MOD.end", "pole": "layout_00_M_MOD.end" }
}
```

| `moduleArgs` key | Default | Meaning |
|---|---|---|
| `joints` | required | The three limb joints, `[side, usage]` each: root, mid, end |
| `bindJoints` | required | 5 to 64 skin joints, root to tip (above) |
| `controlCount` | 5 | The ribbon's ik controls, 3 to 8: one at the root, one at the mid joint, one at the end, and the rest spread between. 5 gives the upper and the lower arm a middle control each |
| `controlName`, `poleOffset`, `settingsOffset`, `ikFkBlend`, `ikOrientToWorld`, `ikAlignToPole` | as TwoBoneIk | See [TwoBoneIk](#twoboneik) |

There is no fk ribbon: the ribbon follows the blended joints.

**Controls.** The two-bone limb's six (the FK start, lower and end controls, the IK handle `<controlName>IK`, the pole `<controlName>Pole` and the settings control `<controlName>Settings`), the ribbon's ik controls `<controlName>RibbonIK_<nn>`, and a hidden tweaker per bind joint, named from its joint. The ribbon's ik controls hang from the start plug's DAG parent (the chest for an arm), and the tweakers from the first ik control. An arm with 5 ik controls and 9 bind joints has 6 + 5 + 9 = 20 controls (16 with 5 bind joints).

**Channels.** All of them are on the settings control, which is always visible: `ikFkBlend`, `stretch` and `soft` as on TwoBoneIk, and the ribbon's `startTwist`, `endTwist`, `sineAmplitude`, `sineFrequency`, `sinePhase`, `sineOrient`, `sineStartFalloff`, `sineEndFalloff`, `volumePreserve`, `scaleStart`, `scaleMid` and `scaleEnd` as in the [Ribbon](#ribbon) table, with `bendSoftness` (below). `roll` stays on the IK handle and `pin` and `space` on the pole. There are no aim blends or `bendRoundness`: a limb's ends come from its joints. The channels are one set with the same rests and mirror modes as on the two modules.

**The elbow.** `bendSoftness` (0 to 1, rest 0) is how round the corner at the mid joint is. At 0, the default, the curve is the straight line to each side of the mid joint: the bind joints stay on the bones and the bend happens at the elbow bind joint. At 1 it is a smooth curve through the three joints. A value between rounds it part way. The curve always passes through the mid joint.

**Twist.** The ribbon starts with the start joint's swing and no twist, so the shoulder's twist arrives at the elbow and runs along the upper arm. It ends with the end joint's twist on the swing of the mid joint, so the wrist's twist runs along the forearm and the hand's swing never reaches it. These are the [Swing and Twist constraints](templates.md#constraints) applied to the joints inside the node, so a limb needs none.

**Child plugs and sockets** are TwoBoneIk's, plus the optional `ikTarget` plug (below): child plugs `start`, `ik`, `pole`, `settings` (internal) and `ikTarget`, sockets `index_0`, `index_1`, `index_2` (also `start` and `end`), `ik`, `pole` and `settings`. A hand hangs from `end`. The bind joints have no sockets.

**Switching IK and FK, and restoring from joints.** [`setRigIkFkBlend`](commands.md#setrigikfkblend) works on a Limb exactly as on a TwoBoneIk, from any of its controls or the module: it matches the FK controls and the IK handle and pole to the joints, so the limb and its bind joints stay put. The ribbon rides the blended joints, so it needs no match. `poseRigFromJointPose` restores a Limb from its joints: the FK, IK handle and pole controls come from the three limb joints, and each tweaker takes what is left between its bind joint and the ribbon, so every bind joint comes back exactly. The ribbon's ik controls have no joint of their own, so a restore leaves them at the pose file's value, or at rest when the file has none. As for TwoBoneIk, the FK controls the blend hides do not come back in IK.

**`ikTarget`: where the end joint reaches.** An optional child plug. Wire a module's socket to it in `parentStructure` (`"ikTarget": "foot_01_L_MOD.index_0"`) and the end joint reaches that socket's motion since rest instead of the IK handle's own. Leave it out and nothing changes: an arm reaches its handle. It is meant for a foot that hangs from the handle (`"start": "leg_01_L_MOD.ik"`) and pushes the ankle target up when the ball rises, so the handle stays the foot's root. The target moves the end joint and what follows it (the sockets `index_0` to `index_2`, `start`, `end`, the ribbon and its bind joints). The `ik`, `pole` and `settings` sockets do not read it, which is what makes a foot on `ik` feeding `ikTarget` acyclic; a foot on `end` would be a real loop and is a read-phase error. At `ikFkBlend` 0 the target is ignored, and `setRigIkFkBlend` and a restore from joints match the limb as if the target were at rest (they do not read it). It takes no constraint and has no control. A `ReverseFoot` module is not built yet; `limb_foot` in the tests is a one-joint FkChain standing in for it. See `design/limb.md` section 8.

A change to the IK/FK solve, the ribbon solve or their channels changes the Limb too.
