# Limb module: design

**Status:** signed off by the author on 2026-11-19 (D62), after one round of questions. Nothing is built. This page replaces the direction note of 2026-11-18, whose history is kept in section 2. Implementation follows the phases in section 12, one commit each.

> **Coupling warning.** `Limb` is not a subclass of `TwoBoneIk` or `Ribbon`, but it runs their solves. It calls the same shared functions, reads the same channel specs and uses the same inverse routines. **A change to the IK/FK solve, the ribbon solve, `core/RibbonMath`, `core/SwingTwist`, the TwoBoneIk or Ribbon channels, their inverses or their control derivation changes `Limb` too.** Before changing any of them, read this page, run the Limb contract and pose cases, and say in the change summary what Limb does with it. A suite test (section 11, `test_limb_matches_parts`) fails when the three disagree, so the coupling is checked and not only documented. The same warning is at the top of `two-bone-ik.md` and `ribbon.md`, in `CLAUDE.md`, and in the headers of `TwoBoneIkModule.h`, `RibbonModule.h` and `RibbonMath.h`.

## 1. The problem, as the solomon rig shows it

Built from `solomon/rigs/default/template.json` (checked in mayapy, 2026-11-19). An arm is a `TwoBoneIk` module, two `Ribbon` modules, a `Swing` constraint and a `Twist` constraint. A leg is the same with three ribbons and two constraints on the left side's template. Findings:

- **Two splines meet at the elbow.** `shoulderRibbon` has 5 bind joints and `wristRibbon` has 4. `shoulderTwist_04`, `wristTwist_00` and `elbow_00` are at the same point. There is no rounding at the bend and no continuity between the upper and lower arm, and the nonlinear channels (sine, scale profile, volume) restart at the elbow.
- **Controls and nodes multiply.** One arm has 21 controls: 6 for IK/FK, 8 for the shoulder ribbon and 7 for the wrist ribbon. Five nodes drive them.
- **Channels are scattered.** The settings control carries `ikFkBlend`, `stretch` and `soft`. The same 15 ribbon channels sit on `shoulderTwistIK_00` and again on `wristTwistIK_00`.
- **Constraints do a module's job.** `shoulderRibbonStartSwing` takes `arm.start` as driver and parent, and `wristRibbonStartTwist` takes the wrist as driver and the elbow as parent. Both are the swing and twist split between joints that the limb already knows.
- The upper ribbon's end is hung from `arm.start` by the template, so nothing ties it to the elbow.

## 2. History

The `Twist` constraint came first (2026-11-18, D31 amendment). A plug takes one constraint node, and stacking a `Twist` on a `Point` needed changes to template validation, the space-switch lookup (D27) and the inverse (D41). So `Twist` took the driver's translation and twist on the swing of the plug's parent, one node and one driver array. It fixes the wrist and ankle on the current rig. The real fix for the elbow was recorded as a `Limb` module. `Swing` and `Twist` stay useful for the neck, spine and clavicle, but not for limbs.

## 3. The gate (D12)

1. **The animator problem.** A rounded elbow or knee, one continuous twist, and one place for all of a limb's settings. Today the animator hunts through three modules, two copies of the ribbon channels and four controls stacked at the bend.
2. **Existing modules.** A `Ribbon` that passes through a mid joint was ruled out: its curve is built from its own start and end, and it has no IK/FK, so the blended joints would have to arrive as constraints, which is the current rig. A bigger `TwoBoneIk` would take on the ribbon and become both modules. `Limb` composes the two solves.
3. **Maintenance surface added.**
   - One module node (`limbModule`, MTypeId `0x002192`), its args struct, its inverse, one registry row.
   - A shared-solve extraction (section 6), so the node composes code instead of copying it.
   - One optional child plug with a declared cycle exemption (`ikTarget`, section 8).
   - A DAG-parenting rule that differs from `Ribbon`'s (section 4.3).
   - No new channel names, commands or `RigAttr` strings.
4. **What it retires.** On the solomon template, the arm's and leg's `TwoBoneIk`, ribbons, `Swing` and `Twist` entries. The modules and constraint types stay.

## 4. Interface

### 4.1 `moduleArgs`

| Key | Default | Meaning |
|---|---|---|
| `joints` | required | Three IK/FK joints: start, mid, end, none of them skin joints. Same rules as `TwoBoneIk` |
| `bindJoints` | required | 5 or more skin joints, root to tip, all leaves. The first sits at the start joint, the last at the end joint, and exactly one at the mid joint. Any other overlap is a read-phase error: there are no stacked joints at the bend |
| `controlCount` | 5 | Ribbon ik controls, 3 to 8. 3 is start, mid and end. With 5, the upper and lower segments each get an interior control. The mid control is pinned at the mid joint, and extras are divided between the segments in proportion to their length |
| `controlName` | first joint's usage without `_NN` | The base of every control usage |
| `poleOffset`, `settingsOffset` | derived | As `TwoBoneIk` (D57), X negated on a mirrored copy |
| `ikOrientToWorld`, `ikAlignToPole`, `ikFkBlend` | as `TwoBoneIk` | Same names and meanings |

There is no fk ribbon and no `fkStyle` (author, 2026-11-19: the ribbon does not support FK). Mirroring is template data (D28) and `planFromJson` negates the offsets through `isMirror()`, as `TwoBoneIk` does. `moduleVersion` starts at 1.

### 4.2 Controls and slots

| Group | Count | Markup usage | Visible |
|---|---|---|---|
| FK start, lower, end | 3 | as `TwoBoneIk` (joint usage) | by blend, as D52 |
| IK handle, pole | 2 | `<base>IK`, `<base>Pole` | by blend, as D52 |
| Settings | 1 | `<base>Settings` | always |
| Ribbon ik | `controlCount` | `<base>RibbonIK_<nn>` | always |
| Tweakers | one per bind joint | `<base>Tweak_<nn>` | hidden |

An arm with 5 ik controls and 5 bind joints has 16 controls, down from 21. With 3 ik controls it has 14. Slot indices are append-only, like every module's (D34). The FK, IK, pole and settings slots keep `TwoBoneIk`'s order.

### 4.3 DAG parenting (author, 2026-11-19)

DAG parenting never drives a result (D1), but it must be correct for the outliner, selection and the active rig panel.

- **Every ribbon ik control is a leaf child of the start plug's DAG parent**, the control that owns the socket the `start` child plug is wired to (the chest for an arm, the pelvis for a leg). It is the same reparent target `reparentChildPlugToSocket` uses for the start control.
- **Every tweaker is a leaf child of the first ribbon ik control.**
- The FK, IK, pole and settings controls keep `TwoBoneIk`'s layout. The settings control already follows the start plug's parent through the internal child plug (D52 decision 14).

Phase 2 checks how `Ribbon` parents its controls today and reuses that code for the tweaker rule. The ik-control rule is Limb's own.

### 4.4 Channels

Every module-wide channel is on the **settings control**. Names are the ones already in use, so nothing new is public.

| From `TwoBoneIk` | From `Ribbon` | Stays where it is |
|---|---|---|
| `ikFkBlend`, `stretch`, `soft` | `startTwist`, `endTwist`, `bendRoundness`, `volumePreserve`, `scaleStart`, `scaleMid`, `scaleEnd`, `sineAmplitude`, `sineFrequency`, `sinePhase`, `sineOrient`, `sineStartFalloff`, `sineEndFalloff` | `roll` on the IK handle, `pin` and `space` on the pole, `space` on the FK start control and the IK handle (D27) |

Dropped: `startAimBlend` and `endAimBlend`. The swing and twist rules in section 5 replace them. Each channel keeps its default, its rest and its `mirrorCopy` or `mirrorNegate` mode (D34). The settings control is channel-only and has no transform channels, so the channels moving onto it is the only change an animator sees.

### 4.5 Child plugs and sockets

- **Child plugs:** `start` (the FK start, the IK root and the ribbon start; the settings control and all ribbon controls follow its parent), `ik`, `pole`, and the new optional `ikTarget` (section 8). Their `parentStructure` keys and their spaces work as in `TwoBoneIk`.
- **Sockets:** `index_0`, `index_1`, `index_2` (the blended joint worlds, which are `start`, `mid` and `end`), `ik`, `pole`, `settings`. A hand, a foot or a clavicle child hangs from `end`. The bind joints are leaves, so the ribbon controls have no sockets (`Ribbon`'s `index_k` sockets exist for chaining and nothing chains to a limb's ribbon).

## 5. Evaluation

`compute()` runs three steps and allocates nothing.

1. **IK/FK solve.** The existing two-bone solve, blended per joint (D52). It gives the three blended joint worlds.
2. **Ribbon anchors.** The ribbon's end frames are computed from those worlds:
   - **Start:** the start joint's translation and **swing**, with the twist of the start plug's parent. This is what `Swing` does on the current rig, with the same formula (`core/SwingTwist`).
   - **End:** the end joint's translation and **twist**, on the swing of the mid joint. This is what `Twist` does now.
   - **Mid:** the mid joint's world.

   The start and end rules are the ones in D31's `Swing` and `Twist`, so the elbow no longer needs a constraint and the wrist's swing never reaches the forearm.
3. **Ribbon solve.** One spline from start through mid to end, the ribbon controls riding it, then the tweakers, as `Ribbon` does. `startTwist` and `endTwist` add to the twist that arrives from the anchors. `bendRoundness` rounds the elbow.

The one open piece of maths is step 3. `RibbonMath` builds its base curve from a start and an end, with the interior controls floating on it. `Limb` needs the curve to pass through the mid joint and the mid ik control to follow it. The expected change is to generalize the setup from "start and end deltas" to "an array of anchors", with a ribbon as the two-anchor case. Phase 1 is a spike on exactly this, and it must leave every existing `Ribbon` result unchanged.

## 6. Reuse: the shared solves

`TwoBoneIkNode::evaluate` and `RibbonModuleNode::evaluate` were pure functions over plain values (`TwoBoneIkEval`, `RibbonSolve`), but private statics. Phase 0 moved them into `maya/shared/TwoBoneIkSolve` (`TwoBoneIkSolve::evaluate`, with the slots, `TwoBoneIkRest`, `TwoBoneIkEval`, `TwoBoneIkSolveResult` and the frame helpers) and `maya/shared/RibbonSolve` (`RibbonSolve::evaluate`, `Result`, `MaxSlots`), and the nodes call them. **No behaviour changes in phase 0**: the existing suite and the golden snapshots must pass untouched.

`Limb` then reuses, and never copies:

| Part | Reused from |
|---|---|
| IK/FK solve, soft, stretch, pin, roll | the shared `TwoBoneIk` solve (`MathUtils::computeTwoBoneIk`, `blendMatrices`) |
| Spline, twist, sine, scale, volume, tweakers | the shared `Ribbon` solve and `core/RibbonMath` |
| Swing and twist split | `core/SwingTwist`, as `rigConstraint` uses it |
| Channel specs and defaults | `ribbonChannelSpecs()` and the TwoBoneIk channel list, merged |
| Control derivation (handle, pole, settings, ik control placement) | `core/ControlPlacement` through the existing plan helpers |
| `argUpdateTable` | a Limb table of its own, starting at version 1 |

Planning calls both modules' plan helpers and merges the two plans with an offset for the slots. If a plan helper is private and a near match, the change is to expose it backward-compatibly, not to copy it.

## 7. The inverse

The inverse (D41) runs in sequence, and the forward evaluation is its source of truth:

1. The `TwoBoneIk` inverse gives the FK, IK and pole controls from the three limb joints, for the channels in `inverseChannels()` (D52).
2. The forward ends from step 1 (the blended joint worlds) give the ribbon anchors.
3. The `Ribbon` inverse gives the ik controls and the tweakers from the bind joints, using those anchors.

Each bind joint that is present comes back exactly, as for `Ribbon` (the tweakers absorb the residual). The channels the ribbon inverse needs (twist, sine, scale, roundness, volume) come from the sidecar or the rig, as for `Ribbon`. The match outputs run the same inverse and allocate, which is accepted because only tools pull them (D52 decision 13).

The inverse runs only in tools and never in playback. If it is too expensive on a limb, trim in this order, each one a `moduleVersion` step if it changes a name: the sine channels, then the scale profile, then `volumePreserve`. The author prefers to keep them all.

## 8. Hand, foot and `ikTarget` (author, 2026-11-19)

**One `Limb`, no `Arm` and `Leg`.** A hand needs nothing special. A foot needs a virtual transform that pushes the ankle target up when the ball raises, and `Limb` should not know about feet. The foot is a later module, `ReverseFoot`, with its own gate.

**The mechanism.** In a joint-and-constraint rig, the foot pushes up the IK joint while the IK joint stays constrained to the IK control. Here:

- `Limb` has an optional child plug `ikTarget`. It takes a world matrix: where the end joint should reach. Unconnected, the target is the IK handle's own world, which is the arm and any footless leg.
- `Limb`'s `ik` socket is the IK handle control's world, and it does not depend on `ikTarget`.
- `ReverseFoot` hangs from `ik`, runs its pivot chain (heel, ball, toe, bank), and outputs an `ankle` socket. The template wires `ankle` to `ikTarget`.
- At blend 0 (FK) `ikTarget` is ignored. What the foot does in FK belongs to the foot design.

**The cycle, and the exemption.** The module graph then reads `Limb` → `ReverseFoot` → `Limb`. At plug level it is acyclic, because `ik` depends only on the control and its parent, while the solve and the other sockets depend on `ikTarget`. D17's read-phase check works per module and D52 rejected relaxing it to per-slot dependencies for the pole's space. This case needs it, and the author has approved it:

- `ModulePlugInfo` gets a flag for a child plug whose input is not seen by the sockets it lists (`ikTarget` lists `ik`, and the pole's `Ik` space).
- The validator skips that edge when it looks for module cycles, and the module's `attributeAffects` must match what it declares.
- A suite test proves the plug graph acyclic with a `ReverseFoot`-shaped test module attached, so the exemption cannot hide a real loop.
- Rejected: a foot offset applied inside `Limb` (it would put foot behaviour in every limb), and a combined `Limb` and `ReverseFoot` (feet are optional, hands have none, and it forks into arm and leg modules).

**Left to the `ReverseFoot` design, and the only open thing about `ikTarget`:** matching an IK pose from FK (`setRigIkFkBlend`) while a foot channel is raised. The match puts the handle where the end joint is, which pops if the foot's channels are not zero, so the foot needs an inverse or the match needs to zero the foot's channels.

## 9. Names, mirroring, versions, export, libraries

- **Names (D58, D34).** Module type `Limb`, node `limbModule`, `<base>FK`, `<base>IK`, `<base>Pole`, `<base>Settings`, `<base>RibbonIK_<nn>`, `<base>Tweak_<nn>`. Every control carries `controlMarkup` (D25), unique per rig.
- **Mirroring.** By template (D28). The mirror modes are the existing channels' (D34). A chain with its -X down the limb works, as `RibbonMath::chainSign` handles it.
- **Poses and animation.** `Limb` is a new module with new control names, so a saved pose from the old arm does not load onto it, and the library browser shows that. No legacy rig has to survive (author, 2026-11-19), so there is no migration step. The solomon template changes by hand in phase 5.
- **Export.** One skeleton: the FK/IK joints and the leaf bind joints, as today. The controls are not exported.

## 10. Performance (D36)

The cost of a limb is its node count, not the solver (1.4 percent of node time in D52's profile). `Limb` replaces five nodes and 21 controls with one node and 14 to 16 controls. The node's own `compute()` costs more, because it runs both solves, and that is measured. Per the gate, phase 4 adds `Limb` instances to the complex rig (`chain_specs`, `limb_specs` in `tools/perf/perf_scenes.py`), keeps the 12 `TwoBoneIk` limbs, and runs `bench` once. The fps and the ratio to the floor go in the commit message, with a statement when they are below D36's target.

## 11. Tests (D20)

- A `ModuleContract` subclass for `Limb`, with a -X chain case (`RibbonNegXContract` is the model).
- Pose cases written by hand: IK, FK, a 0.5 blend, stretch, soft, pin, roll, the elbow control moved, a start twist, an end twist, the wrist's swing not reaching the forearm.
- **`test_limb_matches_parts`**, the coupling check. On one set of inputs, `Limb`'s three joint worlds equal a `TwoBoneIk`'s, and with the elbow control at rest and no twist its bind joints equal a `Ribbon` over the same joints within a tolerance. It fails when a change to either module is not carried through.
- DAG parenting: every ribbon ik control under the start plug's parent, every tweaker under the first ik control.
- Validation errors: a stacked bind joint, no bind joint at the mid, fewer than 5 bind joints, a non-leaf bind joint, `controlCount` out of range.
- The inverse round trip on the solomon pose, mirrored builds, and `ikTarget` (an `ikTarget` socket raised moves the end joint, a foot-shaped test module, the plug graph acyclic).
- A golden snapshot, and a worked example in `docs/modules.md` with a test in `test_docs_examples.py`.

## 12. Build phases

Each phase ends with the build, the full suite, the docs pass and one commit that cites D62.

| Phase | Work | Public change |
|---|---|---|
| 0 | Extract the `TwoBoneIk` and `Ribbon` solves. The coupling warning lands in the headers, `CLAUDE.md` and both design pages (this change) | none |
| 1 | Spike and build the anchors-array change in `core/RibbonMath`. Hand-worked cases for a straight, a bent and a -X chain. The old ribbon results stay identical | none |
| 2 | `Limb`: args, plan, create, `compute`, DAG parenting, registry, vcxproj, source map, `docs/modules.md`. Contract, pose cases, snapshot | `Limb` module |
| 3 | The inverse and the matched switch through `setRigIkFkBlend` | none new |
| 4 | `ikTarget` and the validator exemption, with the foot-shaped test module | `ikTarget` plug |
| 5 | Perf rig instances and one `bench` | none |
| 6 | Move the solomon template to `Limb` for the arms and legs | solomon template |

## 13. Answers recorded (author, 2026-11-19)

1. DAG parents: ribbon ik controls are leaf children of the start plug's DAG parent, and tweakers are leaf children of the first ik control.
2. `controlCount` defaults to 5 and its minimum is 3.
3. `ikTarget` and the validator exemption are approved.
4. Names as in section 9.
5. The solomon template moves to `Limb`.

## 14. Risks

- **The ribbon through the mid joint** may need more than an anchors array. Phase 1 is a spike so the answer is known before the node exists, and the design page is amended if it is not.
- **The coupling.** The shared-solve extraction makes it deliberate, and `test_limb_matches_parts` makes it tested.
- **The `ReverseFoot` match problem** is open and belongs to that design.
- **Size.** About 21 channels and 16 controls per limb. If `bench` is far below target, section 7's trim list applies before any structural change.
