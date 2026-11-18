# Ribbon twist that follows the end frame (proposal, extends D56)

**Status:** proposed 2026-11-17, nothing built. The open questions are answered (section 8, the author's go-ahead on the recommended answers, 2026-11-17); the design as a whole still needs sign-off before phase 1. It started as "a Twist constraint for a shoulder ribbon". Reading the Ribbon showed the constraint would not do the job, so this page records why and proposes the smaller change that does. Everything about the Ribbon below is from reading `RibbonModule.cpp` and `RibbonMath.cpp`, not from running a scene (phase 0 does that).

## 1. Animator-facing problem

An arm is two ribbons (`ribbon.md` section 3.4). The humerus carries the shoulder's twist T about its own axis, and the forearm inherits T through the elbow. The skin wants:

- the top of the upper arm to follow the shoulder's swing but **not** its twist (twist 0 at the start),
- the twist to grow along the upper arm until it equals T at the elbow, so the upper arm's last bind joint matches the forearm's first one.

The same shape appears at the thigh, the neck and the spine's top (a chest that twists should twist the spine ribbon).

## 2. Why a Twist constraint does not do it

The proposed constraint: the start control's rotation comes only from the shoulder's extracted twist, the rest from a Point constraint.

1. **A constraint feeds the start plug's delta, and the Ribbon carries the whole ribbon with it.** `RibbonModuleNode::evaluate` solves the curve in the rest world and multiplies every control and bind OPM by `mStartDelta` (`RibbonModule.cpp:257`). A twist-only delta turns the whole ribbon rigidly, so it can't ease anything out.
2. **A delta is applied about the rest axis, not the current bone axis.** `computeDelta` (D31) builds `C * D` with `D` in the control's rest frame. Once the arm swings, a rotation about the rest X is not a twist about the bone.
3. **The Ribbon never reads a frame's roll from a plug delta.** Its frame comes from the start's rest up (`setup.restUp`), turned by the shortest arc onto the chord and parallel-transported (`RibbonMath.cpp:261`). The ends contribute their *position* and their X axis (`startAimBlend` and `endAimBlend`, lines 245 and 246). The only roll that enters is `extractTwist(local)` of the end *controls*, the animator's own rotation.

That third point is the actual gap, and it is on the end side: whatever twist the elbow plug's parent carries is ignored. With the start plug parented to the clavicle socket and `startAimBlend` 1, the start is already twist-free relative to the body (good), but the end frame's twist T never arrives, so the upper arm ends with twist 0 while the forearm starts with T. That is a twist pop at the elbow.

I wrongly said earlier that this was "one more branch in `computeDelta`". It is a Ribbon change.

## 3. Proposal: the end twist follows the end plug's frame

In `RibbonMath::evaluate`, the end's total twist gains a term, the roll of the end frame relative to where parallel transport arrives:

```
follow = angle about t1, from the transported up at u = 1 to the end frame's up,
         minus the same angle at rest (so it is exactly 0 at rest)
endTwist = pickTwistBranch( wrap( extractTwist(local_end) + follow ), params.endTwist )
```

- `t1` is the end tangent after `endAimBlend`. The end frame's up is the end control's rest Y moved by the end plug's delta, in solve space (`input.endOPM`, which already holds `rest * mEndDelta * mStartDelta^-1`).
- Measuring against the transported frame, and not by a swing-twist about the rest X axis, makes it correct on a bent ribbon and free of the rest-axis problem in section 2.
- The linear ramp stays: `twist(u) = start + (end - start) * u`. The start term is unchanged, so the arm's upper ribbon runs 0 to T and the forearm ribbon, whose start plug is the elbow socket, starts at T and runs to its own wrist roll.
- No new constraint type, child plug, socket or attribute. `ConstraintTypes` is untouched.
- **Two optional `moduleArgs` keys, `startAimBlend` and `endAimBlend`** (numbers 0 to 1, default 0). They set the same-named channels' **default**, which is their rest (D34), so zeroing a pose returns to the aimed state and a mirror copies it. They exist because a shoulder or thigh needs a start aimed at the other end on every build, and today the template can't say so (see section 8, Q3). New public JSON keys: both are a long-term commitment, and `endAimBlend` is there only because leaving it out would be an obvious hole, so say if you'd rather have `startAimBlend` alone.

### Wiring for the shoulder (no new names)

- upper arm: start child plug parent is the clavicle (or chest) socket, end child plug parent is the arm module's elbow socket, `moduleArgs` `startAimBlend` 1 (section 4).
- forearm: start child plug parent is the elbow socket, end child plug parent is the wrist socket.

The exact template and the worked example come with the docs, as a test in `tests/maya/concepts/test_docs_examples.py`.

## 4. What changes for users

- **Behaviour:** an end plug whose parent twists now twists the ribbon. Nothing else about a ribbon at rest changes (follow is 0 at rest, a test). No saved pose or animation is affected, because no control, channel or socket name changes. Rigs built before the change that have a twisting end plug will look different; no rigs need to survive.
- **Inverse (D41):** `extractTwist(local_end)` becomes `total - follow`. The inverse already has the end plug's motion and evaluates the same frame, so it computes `follow` and subtracts it. This needs its own test.
- **Export, pose and animation libraries:** unaffected.
- **Perf:** one frame-table lookup at u = 1 and one `atan2` per ribbon, no allocation. `bench` is run by hand on the 7 ribbons in the perf rig and the fps reported against D36.

## 5. Limits to state in the docs

- **Beyond 180 degrees.** `follow` is wrapped to [-pi, pi], and the Ribbon is stateless. A shoulder that twists through 180 degrees makes the upper arm spin 360 degrees along its length in one frame. The existing `endTwist` secondary channel on the first control picks the branch, but it is keyed by the animator and does not know about the plug's parent. This is the same limit D56 accepted for `local`, now reachable without touching a Ribbon control.
- **Which twist.** The swing is the shortest arc from the rest tangent to the chord, measured in the start plug's parent frame. Twist is path-dependent for a ball joint (the Codman effect), so this is a choice, and it is the usual one for twist joints.
- **Easing.** The ramp is linear. A non-linear profile (most of the twist near the elbow) is not part of this. It would be one function in `core/` and, if wanted, a channel.

## 6. Why not the alternatives

- **A Twist (or Swing) constraint type:** rejected, section 2. Changing `computeDelta` could not give twist-free-at-start-and-T-at-end, because the delta is rigid.
- **A dedicated limb node now:** not needed to get a correct shoulder. `ribbon.md` section 6 still names a `limb` module that combines IK/FK and ribbons as future work. This change is a prerequisite either way: any limb module would still need the Ribbon to follow its end frame.
- **A `followEndTwist` channel or module argument:** rejected for now. There is no case where an animator wants a ribbon's end to ignore its parent's twist, so a switch would be a concept to explain with no use. Add one if a case turns up.

## 7. Gate checklist (CLAUDE.md)

- Animator problem: section 1. Existing code that covers it: none, the end roll is ignored (section 2).
- Maintenance surface added: one term in `RibbonMath`, one more input to the inverse, two optional `moduleArgs` keys (`startAimBlend`, `endAimBlend`) that become the channels' defaults, and no new node names, channels or attributes.
- Tests: closed-form cases on `RibbonMath` (follow is 0 at rest; a pure end twist T gives `T * u` on a straight ribbon; a body-only rotation of the start parent gives 0; the seam of the upper ribbon's last frame equals the forearm's first on a bent arm; a twist past 180 with the secondary channel), the `ModuleContract`, the golden snapshot, the inverse round trip.
- Docs: `docs/` Ribbon page and the arm example; `design/ribbon.md` section 4.1 and `decisions.md` D56 (amend, or a new entry if signed off).

## 8. Answers (2026-11-17)

1. **Direction:** twist 0 at the shoulder, growing to T at the elbow. The forearm starts from the elbow frame, so the two ribbons meet. The reverse would break the seam.
2. **Always on, no switch.** Add a module argument only if a real case turns up.
3. **Channel defaults from the template: not possible today, so add the two `moduleArgs` keys (section 4).** What the code shows:
   - Ribbon's `moduleArgs` are `controls` and `joints` only (`RibbonModuleNode::planFromJson`).
   - Each channel's default, range and mirror mode come from a static `channelSpecs()` table, and `createModule` passes `spec.defaultValue` to `RigPoseScene::addDoubleChannel` (`RibbonModule.cpp:619`). The template has no way to change them.
   - The plan's `build` lambda captures its resolved inputs by value, so a parsed `startAimBlend` and `endAimBlend` can travel from `planFromJson` into `createModule` and replace the default for those two channels. D34 holds, because a channel's default is its rest. Zeroing a pose returns to the aimed state.
   - The channel's default in the table stays 0, so a ribbon with no key behaves as before.
   - Rejected: changing the table default to 1 (moves every Ribbon's rest, tails included), and telling the animator to set it (it would have to be done on every shoulder and thigh, and it would not survive a zero or a pose load).
4. **Beyond 180 degrees:** accept the keyed `endTwist` secondary for the first version and state the flip in the docs. A scalar plug from the arm module is a larger change. Revisit it if it shows up in practice.
5. **Easing:** linear for the first version.

## 9. Build phases (one commit each, after sign-off)

0. **Probe (done 2026-11-17, confirms section 2).** A straight FK arm along X (TwoBoneIk at `ikFkBlend` 0), an upper ribbon and a forearm ribbon wired as in section 3, the shoulder FK control's `rotateX` as the twist T. Roll about X of each bind joint, in degrees, on the 2026-11-17 build:

   | wiring | T | upper start / mid / end | forearm start / mid / end | seam (forearm start - upper end) |
   |---|---|---|---|---|
   | upper start parent `layout.end` (the proposed wiring) | 60 | 0.00 / 0.45 / -0.03 | 60 / 60 / 60 | 60.03 |
   | same | 170 | 0.00 / 0.20 / 0.01 | 170 / 170 / 170 | 169.99 |
   | same, `startAimBlend` 1 on both ribbons | 60 | -0.07 / 0.41 / -0.01 | 60 / 60 / 60 | 60.01 |
   | upper start parent `arm_1.start` (the shoulder socket) | 60 | 60 / 60 / 60 | 60 / 60 / 60 | 0.00 |

   Read: the upper ribbon ignores the elbow frame's twist, so the seam pops by exactly T. The forearm takes T rigidly, start to end, because both its plugs carry it. Parenting the upper start to the shoulder socket removes the pop and also removes the ease: the whole arm turns by T at once. So today's wiring gives either a pop or no ease, never 0 to T. Not measured: a bent arm (the probe's swing row measures roll about world X, which is not the bone axis once the arm swings, so those numbers say nothing), and the warnings below. The suite was not run: no source changed. The probe script is not in the repo, so the proposed tests (phase 1) cover it: a seam test on this arm for T = 60, 170 and -170, expecting 0.
1. `RibbonMath`: the follow term and its closed-form tests. **Done 2026-11-17, with one difference from section 3.** The term is `extractTwist(endDelta)`, the swing-twist split of the end plug's motion relative to the start's about the end's rest X (`Input::endDelta`, one line of wiring in `RibbonModule::evaluate`), not the holonomy angle about the end tangent. It needs no stored rest roll and no new attribute, and it matches the holonomy angle when the end's bend is perpendicular to the bone (the elbow). A bend and a twist about other axes together can differ by the usual swing-twist order choice. Tests: 5 hand-worked pose cases (`end_parent_twist_90`, `start_parent_twist_90_carries_the_end`, `start_parent_twist_90_end_parent_held`, `end_parent_twist_200_with_the_secondary`, `end_parent_twist_200_without_the_secondary`, on the new `ribbon_driven_ends` template) and `test_an_arms_ribbons_meet_at_the_elbow_with_the_shoulders_twist_easing_in` (FK arm, T = 60, 170 and -170, joints within 1 degree of `T * u` and the seam within 1 degree). The suite: 1157 passed. Not covered: the inverse (phase 2), `bench`, a bent arm with a large swing.
2. `RibbonModule`: the two `moduleArgs` keys (a plan field and a test that the channels' default and rest follow them, and that a mirror copies them), the inverse, the contract, the golden snapshot. **Done 2026-11-17.** Optional `startAimBlend` and `endAimBlend` (0 to 1, default 0, an error outside it, and a misspelled key is an error). They are the channels' default and rest; `createModule` takes a `channelDefaults` array. `RigModuleNodeBase::readModuleArgs` gained an optional-keys parameter (default none, so no other module changes), and `RibbonArgs` uses `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT`. Tests: six behaviour tests (rest, zeroing, an aimed build straightens, mirror, range, typo), a second contract class on the new `ribbon_aimed` template (so the inverse round trip runs with a twisting end parent and the aim keys) and a docs example test for the arm snippet in `docs/modules.md`. No golden snapshot added: the node's structure is unchanged. `bench` (2026-11-17, headless, parallel, one run each, same session): 50 complex rigs 113.3 ms (8.8 fps) against the 50-rig floor's 73.2 ms, a ratio of 1.55; the 20-rig scene (built before this change, run on the new plugin) 21.8 fps. Both are below D36's targets (1.5 times, 30 fps), as they were before this change: `ribbon.md` records 107 to 116 ms against 66 ms and 20 to 21 fps for the Ribbon's first build, and the control-node handoff 1.60 to 1.67 times, so this change shows no regression beyond the session-to-session noise of about 30%. The extra work is one `extractTwist` per ribbon, whatever the value. The perf rig's ribbons don't twist their end parents, which doesn't change the cost. Not done: a large swing on a bent arm.
3. Docs, the worked example and its test, perf run, and the D56 amendment.

## 10. Phase 3: a Swing constraint for the start (2026-11-17, signed off and built)

**Built 2026-11-17.** `Swing` is in `ConstraintTypes`, `computeDelta` and `core/SwingTwist`, with the tests in `test_constraint_types` (the constraint's cases, the shoulder end to end, a tilted arm), a round trip in `test_inverse_solve`, a docs example test, `Swing` constraints on the perf rig's three arm ribbons and the D31 amendment. The suite: 1191 passed, 4 skipped. `bench` (headless, parallel, alternating, one session): 50 complex rigs 113.4 and 112.7 ms against the floor's 74.9 and 71.3 ms (about 1.55 times), 20 rigs 21.2 fps, below D36's targets as before this change.

**A bug found and fixed on the way (2026-11-17, on the author's solomon asset).** A plug delta is a motion in world axes, and both the first `Swing` and the phase 1 end follow read its twist about the world X, which is the bone's axis only for a bone lying along X. The test arms lay along X, so the suite passed. On the solomon shoulder, whose bone points obliquely, a 60 degree twist left 12 degrees at the ribbon's start and 66 at its end, and a swing alone read as a twist. Both now conjugate the motion by the control's rest rotation and read the twist about the control's own X (`withoutTwist` in `Constraints.cpp` and `RibbonModule::evaluate`). After the fix, on the solomon skeleton with the template below, the ribbon's bind joints twist 0, 15, 30, 45 and 60 degrees for a 60 degree shoulder twist (0, 30, 60, 90, 120 for 120), the same with a 40 degree swing added, and exactly 0 for a swing alone. The suite now has a tilted arm (35 degrees about Z) for the Ribbon alone and for `Swing`. Phase 1 on `main` (`ea4b0d3`) has the world-X version until the correction is committed.

The proposal as signed off follows.

**What the author wants for a shoulder (2026-11-17):** the ribbon's end has `arm_l.start` as its parent and is point constrained to the elbow, so it carries the shoulder's full rotation, twist included, at the elbow. The ribbon's start follows the position of `arm_l.start` and every rotation of the shoulder except its twist (its X axis points up when the arm raises and forward when it swings forward), and the twist eases in from there to the elbow.

**What exists.** The end half is built: with the end plug's parent turning about X relative to the start's, the end's twist includes that turn (phase 1). The end wiring the author describes already works: a Point constraint on `armRibbon_l.end` driven by `arm_l.index_1`, with `end: arm_l.start`, takes its rotation from `arm_l.start` and its translation from the elbow. The start half has no way to be written: a start plug on `arm_l.start` carries the shoulder's whole motion, twist included, rigidly through the ribbon (the probe's "no ease" rows, and the 19:52 template on the solomon asset), and no existing constraint removes a twist. Orient keeps all of a driver's rotation, Point none. The nearest thing today is parenting the start on `spine.end` with `startAimBlend` 1, which follows the arm's direction but measures the twist against the chest, not the shoulder's own swing.

**Proposal: a fourth constraint type, `Swing`.** Appended to `ConstraintTypes::All` after `Point` (the index is stored in scenes, D31). Like Orient and Point it takes one part from the driver and everything else from the plug's parent:

- **Translation:** the driver's, as Point does.
- **Rotation:** the driver's with its twist removed. With `Dd` the driver's motion since rest (a delta in the child's rest frame, D31) and `Tw` the rotation about the child's rest X by the twist of `Dd`'s rotation (the swing-twist split, `T = 2 atan2(x, w)` of its quaternion), the swing is `Sw = Tw^-1 * Dd`. `Cd = C * Dd` becomes `C * Sw` for the rotation. With a rotate order of xyz and the twist channel on X, `Dd = Tw * Sw` exactly: the animator's X rotation is the twist and Y and Z are the swing.
- **Scale and shear:** from the parent, as Orient and Point do.

The Ribbon then needs nothing more. In its solve space the start carries `Sw` and the end carries the whole of `D`, so the end plug's motion relative to the start's is `D * Sw^-1 = Tw`, whose twist is `T`. The ribbon runs 0 at the start to `T` at the elbow, which is the shoulder case end to end. The shoulder's swing reaches the start's frame because `Sw` is part of its delta, so no `startAimBlend` is needed for the frame, though 1 stays fine for the tangent.

**Template for the author's shoulder** (the names are the solomon asset's):

```json
"parentStructure": { "start": "arm_l.start", "end": "arm_l.start" },
"constraints": [
  { "name": "armRibbon_l_start_swing", "type": "Swing", "target": "armRibbon_l.start",
    "drivers": [ { "socket": "arm_l.start", "label": "Shoulder" } ] },
  { "name": "armRibbon_l_end_point", "type": "Point", "target": "armRibbon_l.end",
    "drivers": [ { "socket": "arm_l.index_1", "label": "Elbow" } ] }
]
```

**Inputs, outputs, names.** A new value of the existing `constraintType` enum and one new string in `ConstraintTypes`. No new attribute, child plug, socket or channel, so poses and animation are unaffected. The enum index is public: `Parent`, `Orient`, `Point`, `Swing`. `computeDelta` gains one branch (it already serves compute, the match on a space switch and the inverse solve, so the inverse and D27 matching work with the new type without a change). `createConstraint` already requires a parent for a type other than Parent.

**Where the math goes.** `extractTwist` is in `core/RibbonMath` on `Mat4`. The constraint needs the same split on an `MMatrix`. Proposal: move the split into a small `core/SwingTwist` taking the three axis rows as doubles, make `RibbonMath::extractTwist` call it, and have `computeDelta` build `Tw` from it. Rejected: a second copy in the constraint, and a constraint that calls into `RibbonMath` (a module's file, for a node that isn't the Ribbon's).

**Failure modes.**
- **A swing of 180 degrees from rest** has no defined twist (the quaternion's `x` and `w` both vanish). The result is stateless, so the split returns 0 there and the start's twist flips as the arm passes through it. A shoulder in a rest pose about 30 degrees below the horizontal reaches straight up at about 150 degrees, so the arm's normal range stays clear of it. State this in the docs.
- **A twist past 180 degrees** is wrapped by the split. The start removes the wrapped twist (the same rotation as the unwrapped one), and the end's follow reads the same wrapped value, so the two agree. The existing `endTwist` secondary picks the turn of the end, as in phase 1.
- **A driver that is not the plug's own parent** works (the swing is of the driver's motion), but then the end's follow measures the parent's motion against it, which is rarely what anyone wants. The docs say to give both plugs the same parent, as above.

**Gate for the constraint type** (CLAUDE.md): the animator problem is section 1 and this section's first paragraph. No existing type does it (above). Maintenance added: one enum entry, one `computeDelta` branch, one core file. Docs: `docs/templates.md` and `docs/modules.md` (the constraint types and the shoulder example, with a test in `test_docs_examples.py`), `design/decisions.md` (D31 amendment: the type list and the rule that new types are appended). Tests: hand-worked cases for `computeDelta` (a pure twist leaves the rotation at rest and the translation following, a pure swing equals Orient's, a twist and a swing together equal the swing alone), the shoulder end to end on the real arm (the start's twist stays 0 for a swing, the ramp reaches `T` at the elbow for a twist and for both together), space-switch matching with a `Swing` driver, and the inverse round trip. Perf: add `Swing` instances to the complex rig (`limb_specs`) and run `bench`.

**Open questions.**
1. Is `Swing` the right name? (Rejected: `Untwist`, which says what it removes and not what is left.)
2. Do you want the twist axis fixed at the child's rest X, as proposed, or a flag for another axis? Proposed: X only, because every module's bone axis is X.
3. Should the same `Swing` be offered on the forearm's end for the wrist, or is the forearm out of scope for now?
