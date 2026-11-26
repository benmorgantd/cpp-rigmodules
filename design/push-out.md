# Push out: keep one control out of one capsule

**Status:** proposed 2026-11-26, not signed off, nothing built. Decision D79. It replaces the D77 collision design, which is parked on the `collision` branch (tag `collision-archive`) and will not be merged.

## 1. Why this exists, and where it stops

An animator moves a collar, a pouch or a strap and it passes through a body part. The rig should push it out, the same way every frame, with nothing to set up per pose.

The D77 design grew past that: a collider file, probe shapes, a solved swing and push, and three authoring tools. It became a small physics system with a long setup. This design keeps the one thing the animator asked for.

Real collision (cloth, hair, anything with contact between bodies or memory between frames) belongs in the engine's physics (Havok, Unreal). This constraint does not try to become that. The limits are in section 8.

## 2. The constraint

A `PushOut` is one entry in a new top-level `collisions` list of the template, a flat list like `constraints`, with a `type` on each entry. It is a separate list because collisions come after constraints: the build makes every constraint first, then every collision, and a collision runs after the constraint on its target. `PushOut` is the only type for now, and a later type (a hinge limit, say) would be another value of `type` in the same list.

```json
"collisions": [
  { "name": "collarPush_00_L_CON", "type": "PushOut", "target": "collar_00_L_MOD.start", "mirror": true,
    "capsule": { "follow": ["Center", "head"], "radius": 6.0, "halfLength": 10.0,
                 "offset": { "translate": [0, 4, 3], "rotate": [0, 0, 0] } },
    "margin": 0.2, "maxPush": 5.0, "constraintWeight": 1.0 }
]
```

| Key | Meaning |
|---|---|
| `name`, `target`, `mirror`, `constraintWeight` | As every constraint (D28, D63). `target` is a module child plug |
| `type` | `"PushOut"`. Strict enum values are capitalised like the constraint types (the author wrote `pushOut`; this follows the existing values). Names follow the convention with the `CON` kind, so no new naming kind is added |
| `capsule.follow` | `[side, usage]` of the joint the capsule follows (a joint selector, as `moduleArgs` use) |
| `capsule.radius`, `capsule.halfLength` | The capsule runs along its local X. `halfLength` is the half length of its straight part, so the whole capsule is `2 * halfLength + 2 * radius` long. Both above 0 for `radius`, 0 or more for `halfLength` |
| `capsule.offset` | `translate` and `rotate` (degrees) of the capsule relative to the joint. No scale: size is `radius` and `halfLength` |
| `margin` | Added to `radius`. The padding that absorbs the solve's small error. Default 0 |
| `maxPush` | The most the control may be moved. Default 100 |

A `PushOut` has no `drivers`, and an entry that has some is an error. A target takes at most one `PushOut`, and it runs after the target's space constraint if there is one, because the capsule limits where the control ends up. The archived collision branch also used a `collisions` key, with a different shape. That branch is not merged, so the key is free.

## 3. The solve

Stateless and closed form.

1. The point is the origin of the target's control, where the plug's current frame (the parent's delta applied to the control's rest) would put it.
2. The capsule's world matrix is its offset times the followed joint's world matrix. The point goes into the capsule's local frame, and the nearest point on its axis segment gives a distance `d`.
3. If `d` is under `radius + margin`, the control moves out along the line from that axis point to the control, by `radius + margin - d`. The push is capped by `maxPush` and scaled by `constraintWeight`.
4. If the point is exactly on the axis there is no direction. The push goes along the capsule's local +Z, so the result is still deterministic.

Output is a translation added to the plug's delta. There is no rotation. There is no swing, no cone and no contact list. A push that lands the control inside another body is not handled: that is the engine's job.

Cost: one point-to-segment distance per frame, a few dozen flops, no allocation, no string lookup. The graph edges are the capsule joint's matrix, the plug's feed and the target's rest, which are the same edges a constraint has now.

## 4. Inverse and mirror

- **Inverse:** the push is not invertible, so `RigInverse` runs the node forward after the target's constraint delta, as the D77 node did. To be confirmed against `RigInverse` when built.
- **Mirror:** an entry with `"mirror": true` is copied to the other side in the read phase (D28). `follow` and the module names swap sides, and `offset` is reflected across X. The mirror goes through the same checks as a hand-written entry.

## 5. Seeing the capsule (temporary) and saving it

The build makes no shape for the constraint, so there is a display that is not part of the rig.

- `cmds.showRigPushOuts(namespace="skel")` makes one temporary capsule display node for each `PushOut` in the rig. It is drawn from the control shape library's capsule, follows the capsule's joint live, sits at the stored offset and has the stored size.
- You move and rotate the display node, and change its `radius` and `halfLength` attributes. This does not change the rig.
- `cmds.getRigPushOutCapsule(displayNode)` returns the `capsule` object for the template (`follow`, `radius`, `halfLength`, `offset` from the node's current placement), as a JSON string to paste into the file. That is "save".
- `cmds.hideRigPushOuts(namespace="skel")` deletes the display nodes. They are tagged so a rig export or a build check ignores them.

The display nodes are the only place a size is an attribute, and only for editing. The rig's constraint takes its numbers from the template.

## 6. What carries over from the collision branch

Read from `collision-archive`, copied by hand and not merged:

- the point-to-capsule distance and normal (`core/CollisionShapes`, capsule only)
- the library capsule's draw map (`colliderCapsulePoint`)
- the offset reflection used by the collider mirror
- how the node was wired onto a plug after the constraint, and how `RigInverse` ran it

Not carried: the collider file, probes, the sphere, box and plane shapes, the `rigColliderMarkup` and `rigCollision` names, the `COL` and `PRB` naming kinds, the `collisions` template key, the three authoring commands and all the mayadev work. The new node is `rigPushOut`, MTypeId `0x002196`, so it never clashes with the archived `rigCollision` (`0x002195`).

## 7. Maintenance added

One template list (`collisions`) with one `type`, one node, one core file of math and three display commands. No new file format and no mayadev change. Because it is a new kind of constraint node, it gets instances in the complex perf rig and one `bench` run when built.

## 8. Limits (do not grow past these)

- One capsule, one point, per constraint.
- No capsule against capsule, no sphere, box or plane.
- No memory between frames.
- No rotation: a hinge limit, if wanted, is a separate `type` with its own design.
- No collider file and no tool that creates shapes in the scene.

## 9. Tests

- `core`: point-capsule solve (outside, inside, on the axis, at the end caps, the cap and the weight).
- `tests/maya`: the node, template validation (drivers refused, unknown keys, bad selector, two on one target), mirror, the inverse forward run, show, get and hide, a docs example.
- Hand-worked expectations, as in the collision tests.

## 10. Open

- Decided (author, 2026-11-26): `getRigPushOutCapsule` only returns the text. Writing it into the template file belongs to a larger rig template authoring tool, later.
- A rounded end (capsule) is the only shape. A flat-ended cylinder would be a second shape, so it is not here.
