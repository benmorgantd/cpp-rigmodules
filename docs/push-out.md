# Push out

A `PushOut` keeps one control out of one capsule. It is for a collar that passes through a head, a pouch through a thigh, a strap through a shoulder: the control is moved out the same way on every frame, with no state, no simulation and nothing to set up per pose. It is not collision in the physics sense. Cloth, hair and anything with contact between bodies or memory between frames belongs in the engine's physics.

The reasoning is in [`design/push-out.md`](../design/push-out.md) (D79).

## The template

A push out is an entry of the template's `collisions` list. The list comes after `constraints`: the build makes every constraint first, then every collision, and a push out runs after the constraint on its target.

```json
"collisions": [
  { "name": "collarPush_00_L_CON", "type": "PushOut", "target": "fkchain_02_L_MOD.start", "mirror": true,
    "capsule": { "follow": ["Center", "a"], "radius": 2.0, "halfLength": 1.0,
                 "offset": { "translate": [0, 0.5, 0], "rotate": [0, 0, 0] } },
    "margin": 0.1, "maxPush": 5.0, "constraintWeight": 1.0 }
]
```

| Key | Required | Meaning |
|---|---|---|
| `name` | yes | In the [naming convention](naming.md) with the `CON` kind (`collarPush_00_L_CON`). Unique among constraints and collisions |
| `type` | yes | `"PushOut"`, the only type. The value is checked exactly |
| `target` | yes | `"module.childPlug"`, as a constraint's. The plug's control is the one that is pushed. A plug takes at most one push out, and the plug must be one a constraint can target |
| `capsule` | yes | The capsule, below |
| `margin` | no | Added to `radius`. Padding: the solve is exact for the pose it sees, but the control's own children and the next frame's motion are not, so a small margin keeps them from touching. Default 0 |
| `maxPush` | no | The most the control is moved. Default 100 |
| `constraintWeight` | no | 0 to 1, how much of the push is applied. Default 1. The same key as a constraint's |
| `mirror` | no | `true` builds a copy on the other side, see below |

The capsule:

| Key | Required | Meaning |
|---|---|---|
| `follow` | yes | `[side, usage]` of the joint the capsule follows (a joint's markup, see [`rigJointMarkup`](commands.md#rigjointmarkup)). The joint's position and orientation move it. The joint's scale is ignored |
| `radius` | yes | Above 0 |
| `halfLength` | no | Half the length of the straight part, 0 or more. 0 is a sphere. The capsule runs along its own X, so its whole length is `2 * halfLength + 2 * radius` |
| `offset` | no | `translate` (`[x, y, z]`) and `rotate` (degrees, applied X then Y then Z) of the capsule relative to the joint. Both default to 0. There is no scale: the size is `radius` and `halfLength` |

Unknown keys are errors, at every level.

## What it does

Each frame the node takes the control's origin where its plug's frame (the socket or constraint feeding it) would put it, and finds the nearest point on the capsule's axis. If that point is closer than `radius + margin`, the control moves straight away from it by the missing distance, capped by `maxPush` and scaled by `constraintWeight`. The push is a translation only.

- A control exactly on the axis has no direction to leave by, so it goes along the capsule's local +Z.
- The same pose gives the same result on every frame. Nothing carries over from one frame to the next, so scrubbing and playing back agree.
- The push is not animated and not a channel. It is in no pose and no key.
- A plug that is already pushed out by a constraint's space switch is pushed out of the result: the push out comes last.

Cost is one point-to-segment distance per frame, with no allocation.

## Mirroring

An entry with `"mirror": true` and a target on a `Left` or `Right` module gets a copy for the other side, with its name and target swapped and the side of `follow` swapped (a `Center` joint stays). The mirror's capsule is not the source's offset copied: it is the source's capsule reflected across X = 0 and then taken relative to the mirror's own joint, so it does not depend on how the two joints' axes are oriented.

## Restoring controls from joints

A push out is not inverted: a push saturates, so many poses give one result. When the rig is restored from a joint clip, the sweep runs the push out forward with the capsule where the clip's joint is, so the solved control keeps the pose it was animated with and the rig does not push it a second time.

## Seeing the capsule

The build draws nothing for a push out. Three commands show it, temporarily, and report where you put it:

```python
displays = cmds.showRigPushOuts(namespace="skel")
# ["skel:collarPush_00_L_CON_display"]
```

Each display is a blue capsule, drawn as the capsule the solve uses, at the stored offset. It follows the capsule's joint live, so it moves with the rig. Move and rotate it, and change its `radius` and `halfLength` attributes in the channel box. None of this changes the rig.

```python
cmds.getRigPushOutCapsule("skel:collarPush_00_L_CON_display")
# '{"follow":["Center","head"],"radius":2.0,"halfLength":1.0,"offset":{"translate":[0.0,0.5,0.0],"rotate":[0.0,0.0,0.0]}}'
```

The text is the template's `capsule` object for the display as it is now: paste it into the entry and rebuild. A display with a scale is refused, because a capsule has no scale: change `radius` and `halfLength`. Writing it into the template file for you belongs to a later rig template authoring tool.

```python
cmds.hideRigPushOuts(namespace="skel")
```

deletes the displays. All three commands are undoable except `getRigPushOutCapsule`, which only reads. See [commands.md](commands.md).

## Limits

One capsule and one point per entry. No capsule against capsule, no sphere, box or plane shapes, no memory between frames, no rotation. A hinge limit would be its own `type` with its own design. Real collision belongs in the engine.
