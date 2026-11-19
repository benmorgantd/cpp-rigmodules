# Ribbon: design draft

> **Coupling warning (D62).** The `Limb` module (`limb.md`) runs this module's solve, `core/RibbonMath`, `core/SwingTwist`, its channel specs and its inverse. Changing any of them changes `Limb`. Read `limb.md` first, run the Limb contract, pose cases and `test_limb_matches_parts`, and say in the change summary what `Limb` does with it.

**Status:** D56. Built in part on 2026-11-17: phases 1, 2, 3 and 5 and the docs. `fkStyle` was built on 2026-11-18 (D59, section 3.1 as amended below). Phase 6 (the perf rig and `bench`) was built after the first build. Section 14 lists what the build did differently from this page. Branch: `ribbon-module-planning`.

Sources: the author's note, the Claude-written handoff `maya_c_ik_ribbon_node_handoff.md` (a source of math, not of design: it conflicts with this repo in several places, section 10) and the author's answers.

## 1. Gate (D12)

1. **Animator-facing problem.** Tails, spines and twist-distributed limb segments that bend, stretch and twist smoothly over many bind joints, driven by a few controls, with a start and end twist that works past 180 degrees, a bow, a sine, and squash and stretch with volume preservation. `FkChain` is one control per joint, `Aim` points one joint, `TwoBoneIk` (D52) is explicitly not a bendy limb and names this as the layer to build on it (`two-bone-ik.md` section 7.4).
2. **Existing module check.** No small extension of `FkChain`, `Aim` or `TwoBoneIk` covers it: the solve (a virtual curve with frames, nonlinear layers, a residual layer) is new.
3. **Design.** Sections 2 to 8.
4. **Maintenance surface.** Section 11.
5. **Performance rig and `bench`.** Section 12.

## 2. Concept

The ribbon is **virtual**. There is no curve node and no mesh. The node evaluates a piecewise cubic Hermite spline in a normalised parameter `u` in [0, 1], and a frame (tangent, normal, binormal) at any `u` by parallel transport. All the math runs in the **module's local space** (section 3.3), so the same ribbon works on a tail and on a biped spine that moves with its pelvis.

Inputs, from the template's skeleton:

- **n bind joints**, already distributed in the rest pose. Each gets its rest parameter `u_i` from its rest position projected onto the rest curve, so the rest spacing is kept at any stretch (no `uniformSpacing`: the joints were fed in already spaced).
- **j control joints**, at least start and end, plus up to about 6 interior ones. Each gets `u_j` from its rest position the same way. These are non-skinned (author, round two).

Every bind joint is driven through a hidden **tweaker** control (section 3.2).

## 3. Structure

### 3.1 Control layers

```
fk controls           (only when fkStyle is true)
  ik controls         (always, floating, j of them: start, end, interior)
    tweaker controls  (always, n of them, hidden, the direct parent of each bind joint)
```

- All controls sit under the **same parent** and each interior control shapes its **own region** of the curve only. A piecewise Hermite spline already has that locality: moving one control point changes the two adjacent segments.
- **`fkStyle`** is a template key, `false` by default. The author asked for it as a template choice unless it changes the logic too much. It doesn't, if the base curve is defined this way:
  - `fkStyle` false: the **base curve** is built from the start and end only (tangent from the control's X axis blended with an aim at the other end, `bendRoundness`). Interior controls take their frame from that curve at `u_j`.
  - `fkStyle` true: the base curve passes through the **FK controls' positions** (an FK chain, like `FkChain`), and each IK control takes its frame from the FK control at the same index.
  
  - **As built (D59, 2026-11-18):** `fkStyle` and `fkControlCount` (default `controlCount`) are template keys. The fk chain is a separate control layer of its own size, so an ik control can't take the frame of "the fk control at the same index": every ik control, the start and end too, takes its frame from the fk base curve **at its own parameter** `knotU[j]`, and its rest offset (`ikOffset`) is measured against that curve, so the rest is exact at any two counts. The ends have no twist in that frame, since their local carries it. The fk chain follows the `start` plug and the ribbon has **no `end` child plug** with an fk chain (the end rides the chain). The fk controls are the last slots, with no joint and no socket, so the inverse leaves them at the sidecar or rest and the end controls and tweakers carry the joints.

  Either way the next step is the same: each IK control's **local matrix** is an offset in that frame, and the detail curve passes through `frame_j * local_j`. Only where `frame_j` comes from differs.

### 3.2 Why there is no cycle

An interior IK control's `offsetParentMatrix` is `frame_j`. The node reads its **local `matrix`** (its translate, rotate and scale channels), never its world matrix. A control's local `matrix` doesn't depend on its `offsetParentMatrix`, so the DG has no loop and D17 isn't engaged: the node never takes its own socket as an input. This is the usual "drive the ribbon from the control channels" trick, done in one node. It needs a plug no module takes today, a control's local matrix, and that goes in the module's interface (section 5).

The **tweakers** are never read by the node. Each tweaker's `offsetParentMatrix` is the final bind frame at `u_i`, and its local channels are the animator's offset on top. The bind joint follows the tweaker. That is the whole reason they are "always": they are a residual layer, and the inverse (section 7) uses them to be exact.

### 3.3 Local space

The node computes in a reference space, not world: the parent of the module's first child plug (the pelvis for a spine, the tail root for a tail), like `TwoBoneIk`'s settings control follows `parentStructure` (D52). Matrices come in as `world * parentInverse` and the outputs are written back through the controls' `offsetParentMatrix` parent-inverse step, so a moving parent doesn't change the shape. Confirmed (section 9, Q-a).

### 3.4 Start and end driven from another module

The ribbon can be all self-contained, or its start and end controls can be **driven by another module's sockets** and left visible (author, round two): an arm is two ribbons (shoulder to elbow, elbow to wrist) fed by the `TwoBoneIk` sockets. The control then follows the socket and keeps its own local offset channels, like any control with a space (D27). The consequence the author named: a control's animation interface would mix modules, and a new `limb` module that combines IK/FK and ribbons may be the later answer. Not designed here.

## 4. Evaluation

1. **Base curve** per section 3.1. Aim-versus-rest blend at the ends (`startAimBlend`, `endAimBlend`) and `bendRoundness` (Hermite handle scale: 0 is straight, about 0.65 to 1 is rounded) are kept.
2. **Frame per control** at `u_j` (or from the FK control). Parallel transport of the up vector along the base curve, no 180 degree flip.
3. **Detail curve** through `frame_j * local_j` for all j. Positions, tangents (finite difference, blended with the control's own X axis) and scale from the control's local matrix.
4. **Rest up vector.** The ribbon is a plane, so the frame needs a reference up. Take **the start joint's rest Y axis** (Maya X-down, Y-up), projected orthogonal to the rest tangent. No module argument. Parallel transport carries it, so the start's frame is twist-free, and twist is layered on after.
5. **Twist and layers**, parameterized from the first control (author, round two, revised 2026-11-17):
   1. **Twist**, start and end: the two ends' total twists ramp linearly in `u` along the whole ribbon (revised: it was `u^twistFalloffPower` between neighbours, so the start's twist stopped at the next control). Every interior control carries the ramp in its offset parent matrix, and its own roll adds on top. (Revised 2026-11-17, `ribbon-twist-follow.md`: the end's total also includes the twist of its end plug's motion relative to the start plug's, `extractTwist(endDelta)`, so a twisting end parent twists the ribbon. The start is the ribbon's frame, so it adds nothing.)
   2. ~~Bow~~, removed (2026-11-17).
   3. **Sine**, windowed by smoothstep envelopes at both ends, amplitude, frequency, phase, orientation. In v1 (author). The layer stack is sized to add more (Q-d).
6. **Bind frames** per `u_i`: position, tangent re-orthogonalised after the displacements (central difference), twist applied around the tangent, scale composed. The tweaker's `offsetParentMatrix` takes the result.

### 4.1 Twist, stateless (decided)

No keyed twist channel, no node state, no `m_acc*` members: the output is a pure function of the inputs, so scrubbing, baking, evaluation order and parallel evaluation cannot change it.

The start and end angle come from the **swing-twist split** of each end control against its rest (X axis), wrapped to [-pi, pi]. To reach past 180 degrees, the first control carries a **secondary twist** per end, an angle channel, and the total uses the **branch-picking form** (author, round two):

```
total = secondary + wrap(extracted - secondary)
```

The secondary value picks which branch of the extracted angle the result lands on. The animator keys the secondary near the intended total, and the pop happens only when it is far from it (more than half a turn). With the secondary at 0, the result is the plain wrapped angle. This adds no state, and it is the "frame-independent closest-rotation" the author asked about: nothing is compared with a previous frame, the secondary is the reference.

### 4.2 Scale and volume

The scale of each control interpolates along `u` (cubic). On top, `volumePreserve` (0 to 1, a multiplier) applies `lambda^(-0.5 * volumePreserve * w(u))` to the cross-section axes (Y and Z), with `lambda = liveLength / restLength`. `w(u)` is a **three-point profile**, `scaleStart`, `scaleMid` and `scaleEnd` weights (0 to 1, default 1) interpolated quadratically. This answers the author's arm case: the upper arm wants the elbow end smallest, so `scaleEnd` carries the weight, the forearm wants `scaleStart`, and a tail or spine wants `scaleMid`. Weights are channels (defaults equal rest, D34), with nothing to set for the common case.

## 5. Interface (proposed)

Names are public API (D34). None of this is final until sign-off.

- **Module type** `Ribbon`, node `ribbonModule`, MTypeId from the free list in `CLAUDE.md` (not `0x00219`, retired).
- **`moduleArgs`:** the bind joint selectors, the control joint selectors (markers by markup, like `Aim`'s), `fkStyle`. Counts are derived from them, not stated. **Reversed by D57 (2026-11-18, `derived-controls.md`):** `controls` is gone. `controlCount` (default 3) and `controlName` replace it, and the ik controls are placed from the bind joints.
- **Child plugs:** `start` and `end`, which the template can connect to another module's sockets. With no connection the controls stay self-contained.
- **Sockets:** `start`, `end`, and one per interior control.
- **Control inputs:** the node takes each IK control's **local matrix**, a plug type new to the module layer.
- **Channels, all on the first control** (author, round two), defaults equal rest (D34): `startTwist` and `endTwist` (the secondary angles, `mirrorNegate`), `sineAmplitude`, `sineFrequency`, `sinePhase`, `sineOrient` (`mirrorNegate`), `sineStartFalloff`, `sineEndFalloff`, `startAimBlend`, `endAimBlend`, `bendRoundness`, `volumePreserve`, `scaleStart`, `scaleMid`, `scaleEnd`. Names in `RigAttr`. The first control is the start IK control in both modes (section 9, Q-b).
- **Limits** for allocation-free `compute()`: j up to 8, n up to 64, fixed scratch buffers, a fixed sample count for the arc-length table.
- **Read-phase validation (D17):** joint existence and claimed once, j >= 2, n >= 2, every control and bind joint within the caps, every `fkStyle` control with a matching FK control, and the `start`/`end` child plugs either both connected or neither.

## 6. Relation to other modules

- `TwoBoneIk` supplies the arm's sockets, and an arm is two ribbons (section 3.4).
- A tail or spine is one ribbon, self-contained, optionally with `fkStyle`.
- Spine on a biped: the pelvis and chest sockets feed the start and end through child plugs.
- A future `limb` module that combines IK/FK and ribbons is named and not designed.

## 7. Inverse (D41)

Included in v1 (author, round two). The tweakers make it exact: the detail curve and layers are evaluated from the controls' current values, so every bind joint's residual becomes its tweaker's local. The controls themselves are solved in this order: start and end from the end joints (swing-twist, with the current secondary and the nonlinear channels as scalar inputs, as TwoBoneIk's channels are in D52), interior controls from the rest-frame-relative offsets of the bind joints nearest each `u_j`, tweakers last, as the residual. Where the nonlinear layers cannot be inverted, the author's fallback is to let the interior controls and tweakers carry the pose, with the nonlinear channels held at their current values. The solve and its tests need their own pass (phase 5, section 13).

## 8. Discrepancies between the handoff and this repo

| Handoff | This repo | Resolution |
|---|---|---|
| Interior drivers via `outputDriverOPM`, Stage A/B split by `attributeAffects`. | A module's own socket can't drive its own control (D17, D52). | Section 3.2: the node reads the control's local matrix, so no loop exists. |
| `compute()` keeps accumulated twist, `kSerial` scheduling. | Allocation-free, stateless `compute()`, parallel evaluation (D36). | Section 4.1. |
| `startRotateXHint`, `driverBindParam`, `uniformSpacing`. | The hint is replaced, `u` comes from the rest joints, spacing is the rest spacing. | Sections 2 and 4.1. |
| `std::vector` samples per compute. | No allocation in `compute()`. | Section 5 limits. |
| Free attribute names. | `RigAttr`, D34 channel rules, `controlMarkup` in `ModuleInterface::controls`. | Section 5. |
| No inverse, no tests. | Inverse (D41), `ModuleContract`, pose cases, golden snapshot. | Sections 7 and 12. |

## 9. Answers to the open questions (author, 2026-11-17, round three)

Section 3 and the rest of this page are updated to match where a line below changes them.

- **Q-a, reference space.** The parent of the module's first child plug (its `parentStructure` socket): the pelvis for a spine, the tail root for a tail. The ribbon is computed in that space and the outputs go back through the parent inverse, so a moving parent doesn't change the shape. The same rule as `TwoBoneIk`'s settings control (D52).
- **Q-b, channel host.** The first IK control, with or without `fkStyle`. The IK controls are what animators pose and what the inverse solves, and an FK-only host would be hidden whenever FK controls are.
- **Q-c, FK controls.** A real chain, built the way `FkChain` builds it. The IK control at index j floats on the FK control at index j, and the IK controls don't carry each other. The tweakers ride the detail curve. **The node reads the FK controls' local matrices and composes the chain itself**, never their world matrices: it writes their `offsetParentMatrix`, so reading world would bring the cycle back. The rule of section 3.2 applies to FK controls too.
- **Q-d, nonlinear layers.** v1 is twist, bow and **one** sine. Each layer is one function in `core/` from a parameter and frame to a displacement, so a later layer is one function and its channels. No runtime-configurable list of layers in v1: it would multiply channels, mirror rules and tests before the pattern is proven.
- **Q-e, stretch.** No clamp on the live length, since the ends are the animator's to place. The volume exponent clamps `lambda` to about 0.25 to 4 so an extreme pull can't zero or explode the scale. A curve whose chord is under about 1e-4 falls back to the rest frame, so the node never builds a singular matrix.
- **Q-f, driven start and end.** Visible by default. They need no space match, because their only space is the fixed socket from the other module. A switchable space on one of them later brings D27's match with it.

## 11. Maintenance surface added

One module node and descriptor, one more control kind (tweaker), a template key (`fkStyle`), about 15 channel names, a plug type (a control's local matrix) the module layer didn't have, an inverse, and the heaviest `compute()` in the plugin.

## 12. Verification plan

- A `ModuleContract` subclass and hand-written pose cases: straight, bent, bowed, twisted past 180 with the secondary, sine on and off, stretched and squashed with each profile, `fkStyle` on and off. A golden snapshot.
- **Determinism:** evaluating frames in a different order, and twice, gives identical matrices (guards section 4.1).
- **Locality:** moving one interior control leaves bind joints outside its region unchanged.
- **Cycle:** a built rig has no DG cycle (evaluation manager check) and passes read-phase D17.
- **Math in `core/`** (spline, parallel transport, swing-twist, branch picking, layers) with no Maya headers, tested through the suite (no GoogleTest, D20), so it can be checked against closed-form cases.
- **Perf:** many ribbons in `tools/perf/perf_scenes.py` (tails and limb segments), `bench` run by hand, fps reported against D36.

## 13. Build phases (one commit each, once signed off)

1. The math in `core/` with tests.
2. The node, descriptor, `planFromJson` and validation, controls and tweakers (no inverse).
3. The nonlinear layers, scale and volume.
4. `fkStyle` and a driven start/end.
5. The inverse and its pose cases.
6. The perf rig and `bench`.
7. Docs.

## 14. As built (2026-11-17)

Built: `core/RibbonMath` (phase 1), `maya/modules/RibbonModule` with controls, tweakers, sockets, child plugs and the 15 channels (phase 2), the twist, sine, scale and volume layers (phase 3), the inverse (phase 5), the perf rig with `bench` (phase 6) and the docs (phase 7). The suite has the contract, hand-worked pose cases, behaviour tests, the plan step's checks and a golden snapshot. Not built: `fkStyle` (phase 4).

**Revised 2026-11-17 (D56):** the ends' twist ramps along the whole chain and the interior controls carry the ramp in their offset parent matrix, `twistFalloffPower` and `bow` are removed (15 channels), and the first build's performance number was a reload bug (see D56).

Where the build differs from the sections above:

- **No new plug type** (sections 3.2, 5 and 11). The module layer already feeds each control's local `matrix` into `controlData[i].controlLocalMatrix`, so the ribbon reads exactly what every module reads. There is no cycle for the same reason: an interior ik control's OPM is a function of the two end controls' locals and the stored offsets, never of its own world.
- **Reference space** (3.3). The ribbon is solved in the rest world, where the start and end sit at their rests, and the start child plug's parent motion carries the whole result (`world = result * startDelta`). The end control takes its own child plug's motion (`rest * endDelta * startDelta^-1` in the solve), which is what lets another module's socket drive the end. This is the draft's parent-space rule with no per-control inverse.
- **Parameters** (2). `u` is each control's and bind joint's share of the rest length along the control joints' polyline (a bind joint projects onto the closest chord). There is no arc-length table, so under a very uneven stretch joints keep their parameter shares, not equal distances. The 32-sample table is the parallel-transport frame table only.
- **Rest up** (4.4). The start joint's rest Y, made perpendicular to the rest tangent, carried to the live start tangent by the shortest arc. The same carried frame table serves the base and detail curves.
- **Frames at rest are exact by construction.** The interior controls store their rest as an offset in the base curve's frame at their `u` (`ikRestOffset`), and the bind joints store theirs in the detail curve's frame at their `u` (`bindRestOffset`), both computed at build with the default channels. Any rest, curved or not, comes back exactly. The joints must still point down the chain: a rest tangent against the chord makes a looped curve.
- **Tangents** (4.1, 4.3). The end tangents come from the base curve (the end control's X axis blended toward the aim by `startAimBlend` and `endAimBlend`, scaled by chord times `bendRoundness`). An interior knot's tangent is the finite difference of its neighbours blended half and half with its control's own X axis (`kControlTangentBlend`, not a channel). A two-control ribbon is its base curve.
- **Twist** (4.1). The ends' totals (through the branch picker) ramp linearly in `u` along the whole ribbon. Each interior control's offset parent matrix is its base-curve frame turned by the ramp at its `u`, and **an interior control's own roll adds to the ramp** (the draft said start and end only). The detail twist at a control is the ramp plus its own roll, and a bind joint between two controls takes the linear blend of their totals. With no interior roll that is the straight ramp, so the start's twist reaches the far end.
- **Layers** (4.2, Q-d). The sine displaces along the twisted frame, so it turns with the twist. The tangent of a bind frame is the central difference of the displaced curve (the spline extrapolates past its ends so an end stays central).
- **Volume** (4.2). The length ratio is the sum of the control chords against the rest length, clamped to 0.25 to 4. `volumePreserve` defaults to 0.
- **Inverse** (7). It needs to know where the ik controls end and the tweakers begin, so `ModuleInverseContext` gained `ikControlCount`, filled by `RigInverse` from the module's `ikControlCount` attribute. The sweep reads marker joints when the clip has them. With a control joint missing, that control keeps its sidecar or rest value, and the tweakers carry the difference.
- **Names.** The moduleArgs keys are `controls` (the control joints) and `joints` (the bind joints). The node is `ribbonModule`, MTypeId 0x002191.

- **A chain that runs back (D57 phase 0, 2026-11-18).** The first build assumed each control's +X pointed down the chain, so a mirrored limb whose joints keep the other side's axes (-X down the chain) took its end tangents backward and twisted against its controls. `RibbonMath::Setup::chainSign` (+1 or -1, stored on the node as `chainSign`) is read from the control rests at build. Every use of a control's X as a direction goes through it, and every twist about a control's X is turned about the forward tangent with its sign flipped. The inverse needed no change, since it rebuilds the setup. Tests: `negx_*` pose cases, `RibbonNegXContract`, and a mirror-symmetry test against the +X chain.

Checked: the full suite (1151 tests, all passing on the build that holds this change), and `bench` (D56: 50 rigs 107 to 116 ms against a 66 ms floor, 20 rigs 20 to 21 fps). Not checked: viewport drawing of the controls, and `fkStyle`.
