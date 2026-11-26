# Limb module: design

**Status:** signed off by the author on 2026-11-19 (D62), after one round of questions. **Phases 0 to 2 are built (2026-11-19):** the shared solves, the anchored ribbon and the `Limb` module. The inverse (phase 3) is next. This page replaces the direction note of 2026-11-18, whose history is kept in section 2. Implementation follows the phases in section 12, one commit each.

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

The FK, IK, pole and settings controls are derived as in D57. **The ribbon ik controls are placed on the anchor curve** from parameters (the mid one at the mid joint's, the others spread between), not from `core/ControlPlacement` (section 5.1, which amends D57 for `Limb`).

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
| `ikFkBlend`, `stretch`, `soft` | `startTwist`, `endTwist`, `volumePreserve`, `scaleStart`, `scaleMid`, `scaleEnd`, `sineAmplitude`, `sineFrequency`, `sinePhase`, `sineOrient`, `sineStartFalloff`, `sineEndFalloff` | `roll` on the IK handle, `pin` and `space` on the pole, `space` on the FK start control and the IK handle (D27) |

Dropped: `startAimBlend`, `endAimBlend` and `bendRoundness`. The swing and twist rules in section 5 replace the aim blends, and the ends' handles are the chord of their one segment. **Added: `bendSoftness`** (0 to 1, rest 0, `mirrorCopy`): how round the elbow is (section 5.2). Each channel keeps its default, its rest and its `mirrorCopy` or `mirrorNegate` mode (D34). The settings control is channel-only and has no transform channels, so the channels moving onto it is the only change an animator sees.

### 4.5 Child plugs and sockets

- **Child plugs:** `start` (the FK start, the IK root and the ribbon start; the settings control and all ribbon controls follow its parent), `ik`, `pole`, and the new optional `ikTarget` (section 8). Their `parentStructure` keys and their spaces work as in `TwoBoneIk`.
- **Sockets:** `index_0`, `index_1`, `index_2` (the blended joint worlds, which are `start`, `mid` and `end`), `ik`, `pole`, `settings`. A hand, a foot or a clavicle child hangs from `end`. The bind joints are leaves, so the ribbon's ik controls have no sockets. **Added 2026-11-20: `bind_0` to `bind_<n-1>`, one per bind joint**, carrying the bind joint's solved world, so a forearm prop, a twist-joint child or a sleeve can hang from the ribbon. Rejected: the bind joint's usage as the socket name (it would break if a joint were renamed, and the index is what the template reads). A socket has no DAG transform, as `index_0` to `index_2` have none, so a child's controls do not inherit the hidden tweaker's visibility. The restore gives them the solved bind joints' worlds, so a child follows in a restore too.

## 5. Evaluation

`compute()` runs three steps and allocates nothing.

1. **IK/FK solve.** The existing two-bone solve, blended per joint (D52). It gives the three blended joint worlds.
2. **Ribbon anchors.** The ribbon's end frames are computed from those worlds:
   - **Start:** the start joint's translation and **swing**, with the twist of the start plug's parent. This is what `Swing` does on the current rig, with the same formula (`core/SwingTwist`).
   - **End:** the end joint's translation and **twist**, on the swing of the mid joint. This is what `Twist` does now.
   - **Mid:** the mid joint's world.

   The start and end rules are the ones in D31's `Swing` and `Twist`, so the elbow no longer needs a constraint and the wrist's swing never reaches the forearm.
3. **Ribbon solve.** One spline from start through mid to end, the ribbon controls riding it, then the tweakers, as `Ribbon` does. `startTwist` and `endTwist` add to the twist that arrives from the anchors. `bendSoftness` rounds the elbow (section 5.2).

### 5.1 What phase 1 found (built 2026-11-19, `core/RibbonMath`)

The curve through the mid joint is not new maths. It is the base curve D59 already builds for `fkStyle`: a Hermite spline through an fk chain's positions, with each ik control floating on it. `Limb` supplies its three blended joint frames as that chain, which is "an array of anchors" with no new curve type. The spike found two things the existing `fkStyle` did not do, and one choice for you.

1. **An anchor's roll never reached the bind joints.** In `fkStyle` an fk roll turns the ik controls and stops there, because the detail curve takes its twist from the ik controls' own channels. For a limb the anchors are real joints, so the shoulder's and wrist's twist is what the skin must follow. `Setup::fkAnchors` makes the anchors' roll about the curve part of the detail twist, easing from one anchor's to the next. In the 3-control probe a start twist of 0.8 comes out at 0.80, 0.60, 0.40, 0.20, 0.00 down the bind joints and nothing past the elbow, and an end twist the same from the elbow to the wrist.
2. **The ik controls must be placed on the anchor curve, from chosen parameters.** Laid out as a limb's controls normally are (the interior ones on the chord or on a separate spline), they rest a little off the curve and get a different parameter from the elbow anchor's. Bent, the mid control then sits 0.1 unit off the mid joint, and rolling an anchor swings an off-curve control around it: a 0.8 start twist leaked 0.05 radian past the elbow. `RibbonMath::buildAnchoredSetup(anchorRest, anchorCount, controlU, controlCount, bindRest, bindCount, ...)` takes the parameters, places the controls on the curve with the first anchor's orientation carried along it, and returns their rests. Their offsets from the curve are zero, so the mid control sits on the mid joint exactly, and the twist does not leak. **This amends D57 for `Limb` only:** its ik control rests come from the anchors, not from `core/ControlPlacement`'s spline through the bind joints. The bind joints keep their authored places, and their offsets from the detail curve are measured at build as for `Ribbon`.
3. **(Superseded by 5.2: the elbow is now a sharp corner by default.) The elbow is rounded, not creased, and only as round as the cubic allows.** The curve passes exactly through the mid joint, so a joint-sharp bend cannot become a wide arc. On a 98 degree bend with four bind joints on each side, the two steps at the elbow turn 46 and 34 degrees (48 and 34 with 5 controls), and the rest turn 1 to 5. That is the Catmull-Rom tangent at the mid knot, and `bendRoundness` only scales the two end handles. **Open, your call:** a mid-handle scale (one more channel, `bendRoundness` also applied at the mid knot, or its own) would spread the bend. Not added: nothing asked for it yet, and it is one channel and one line in `makeFkBase` if you want it after seeing the limb.

The mirrored chain (X running back) works: the controls take the half-turn between the first anchor and the curve frame, the positions equal the +X chain's, and a twist about the own X reads with the sign flipped about the forward tangent.

Existing `Ribbon` results do not change: the mode is a flag on the setup that no module sets yet, and `buildSetup` is the same code behind a wrapper.

### 5.2 The elbow is a sharp corner by default, and there is no curve degree (author, 2026-11-19, built in phase 2)

The author asked for a very sharp elbow with a softness parameter at 0 by default, and for the curve through the bind joints to round no corner either, asking whether a curve degree (1 to 3) was too complex.

- **`bendSoftness`** (0 to 1, rest 0). At 0 each side of the mid joint leaves it along its own chord, so a segment between two corners is a straight line and the curve through the anchors is the polyline: the bind joints lie on the bones, keep their spacing along them, and point along them, and the bend is all in the elbow bind joint, which points along the bisector. At 1 the two sides share one tangent (the smooth curve of 5.1). Between, the tangents blend. The elbow bind joint is on the mid joint at every softness.
- **The same corner in both curves.** The base curve through the three joints and the detail curve through the ik controls both have it: the ik control at the mid joint's parameter is a corner of the detail curve at the same softness, so nothing between the ik controls and the bind joints rounds it. The other ik controls stay smooth knots, so moving the control halfway down the upper arm bows the bone smoothly and does not kink it.
- **Why not a curve degree.** A cubic Hermite curve with the tangents along the chords is exactly a straight line, so one softness channel already runs from degree 1 to the smooth cubic, and a degree 2 does not exist in this form. A second knob (degree, and softness) would overlap the first, and a degree applied to every knot would kink the curve wherever an animator pulls an ik control, which is not wanted. Rejected for priority 2 (simplicity). If the author wants every knot to be able to kink, the same softness can be applied to all of them in `softenCorners`.
- **Frames at a corner needed work** (a finding of the phase 2 tests). Carrying the normal along the curve by projecting it onto the next tangent's plane flips it when a corner turns more than a quarter turn, so a split spline is sampled segment by segment and the normal goes through each corner by the shortest rotation. A joint at a corner has its axes on the bone the curve leaves along, so its roll is measured on each side: for the side that arrives, its Y axis is first turned back through the corner. Without that, a joint that is only bent at the elbow would read as a 180 degree twist. The twist of the detail curve is per side for the control at the corner. Non-corner splines (every `Ribbon`) take the old code, so their results did not change.

## 6. Reuse: the shared solves

`TwoBoneIkNode::evaluate` and `RibbonModuleNode::evaluate` were pure functions over plain values (`TwoBoneIkEval`, `RibbonSolve`), but private statics. Phase 0 moved them into `maya/shared/TwoBoneIkSolve` (`TwoBoneIkSolve::evaluate`, with the slots, `TwoBoneIkRest`, `TwoBoneIkEval`, `TwoBoneIkSolveResult` and the frame helpers) and `maya/shared/RibbonSolve` (`RibbonSolve::evaluate`, `Result`, `MaxSlots`), and the nodes call them. **No behaviour changes in phase 0**: the existing suite and the golden snapshots must pass untouched.

`Limb` then reuses, and never copies:

| Part | Reused from |
|---|---|
| IK/FK solve, soft, stretch, pin, roll | the shared `TwoBoneIk` solve (`TwoBoneIkSolve::computeTwoBoneIk`, `blendMatrices`) |
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

### 7.1 What phase 3 built (2026-11-20)

- **Step 3 is smaller than the sketch above.** The ribbon's ends are the anchors, which the two-bone half already matched, and a Limb's ribbon ik controls have no joint (D57). So the inverse leaves every ik control at its fallback (the sidecar's value, else the rest) and gives each tweaker `bindJoint * controlOPM^-1`, which brings every bind joint back exactly whatever the ik controls hold. The `Ribbon` inverse's end refinement is not needed, because a Limb's ends do not follow the first and last bind joint's frame. The ribbon is rebuilt from the stored rests with `RibbonMath::buildAnchoredSetup`, as the plan step made it, and `setAnchors` is the one place the anchors are marked, for `compute()` and the inverse.
- **Channels.** `LimbNode::inverseChannels()` is the two-bone list (`TwoBoneIkInverseChannel`, moved to `TwoBoneIkSolve.h`) followed by the 13 ribbon channels, all on the settings control. The sockets for slots 0 to 5 are the forward joints.
- **The match outputs are shared.** `TwoBoneIkPart` owns `fkMatchLocal`, `ikMatchLocal`, `matchWarnings` and their `computeMatch` (it takes the inverse to run), so a Limb's outputs are the TwoBoneIk's, on the first six slots. `setRigIkFkBlend` accepts either module. It does not write ribbon controls: the ribbon rides the matched joints.
- **A restore does not put the ribbon's ik controls back.** A posed ik control comes back as the sidecar's value or rest, with its pose carried by the tweakers. The contract round trip leaves them unposed for that reason.

## 8. Hand, foot and `ikTarget` (author, 2026-11-19)

**One `Limb`, no `Arm` and `Leg`.** A hand needs nothing special. A foot needs a virtual transform that pushes the ankle target up when the ball raises, and `Limb` should not know about feet. The foot is a later module, `ReverseFoot`, with its own gate.

**The mechanism.** In a joint-and-constraint rig, the foot pushes up the IK joint while the IK joint stays constrained to the IK control. Here:

- `Limb` has an optional child plug `ikTarget`. It takes a world matrix: where the end joint should reach. Unconnected, the target is the IK handle's own world, which is the arm and any footless leg.
- `Limb`'s `ik` socket is the IK handle control's world, and it does not depend on `ikTarget`.
- `ReverseFoot` hangs from `ik`, runs its pivot chain (the fixed order in `FootSolve::chainOrder`, D74), and outputs an `ankle` socket. The template wires `ankle` to `ikTarget`.
- At blend 0 (FK) `ikTarget` is ignored. What the foot does in FK belongs to the foot design.

**The cycle, and the exemption.** The module graph then reads `Limb` → `ReverseFoot` → `Limb`. At plug level it is acyclic, because `ik` depends only on the control and its parent, while the solve and the other sockets depend on `ikTarget`. D17's read-phase check works per module and D52 rejected relaxing it to per-slot dependencies for the pole's space. This case needs it, and the author has approved it:

- `ModulePlugInfo` gets a flag for a child plug whose input is not seen by the sockets it lists (`ikTarget` lists `ik`, and the pole's `Ik` space).
- The validator skips that edge when it looks for module cycles, and the module's `attributeAffects` must match what it declares.
- A suite test proves the plug graph acyclic with a `ReverseFoot`-shaped test module attached, so the exemption cannot hide a real loop.
- Rejected: a foot offset applied inside `Limb` (it would put foot behaviour in every limb), and a combined `Limb` and `ReverseFoot` (feet are optional, hands have none, and it forks into arm and leg modules).

**Update 2026-11-24:** the `ReverseFoot` design is `reverse-foot.md` (D74). It hangs from two sockets, `ik` for its IK half and `end` for the toe, so the foot follows the FK ankle too. Its blend is driven by this module's `ikFkBlend`. Its inverse (phase 5, built 2026-11-25) gives the Limb the foot's motion through a `RigInverse` pre-pass, which answers the match question below. Limb and the foot are then a coupled pair: a change to Limb's blend, `ikTarget` or inverse changes the foot.

**Coupled pair (2026-11-24, D74 phases 3 and 4):** `ReverseFoot` is built except its inverse (`reverse-foot.md`, `ReverseFootModule`). It reads this module's `ik` socket (the handle, through its `ik` plug) and `end` socket (through its `ankle` plug), follows its `ikFkBlend` (pass 2 connects this module's `ikFkBlend` attribute to the foot's, and `TwoBoneIkSolve::makeInterface`, which Limb and TwoBoneIk share, names it as the `blendSource`: this module's own code and compute are unchanged by it), and its `ankle` socket is what a template wires to `ikTarget`: the build refuses a template whose driving Limb does not take `"ikTarget": "<foot>.ankle"` (`validateBlendDrivers`, through `ModuleInterface::driverPlug`). **A change to the Limb's blend, sockets, `ikTarget` or inverse changes the foot**, and `test_reverse_foot` (a foot at rest leaves the Limb as it is alone, and the graph has no cycle in either evaluation mode) is the guard. Read `reverse-foot.md` before changing any of them.

**Closed (D74 phase 5, 2026-11-25): matching an IK pose from FK (`setRigIkFkBlend`) while a foot channel is raised.** The match used to put the handle where the end joint is, which popped when the foot's channels were not zero. Now the match takes the target's motion when `ikTarget` is connected: the joints reach it as `compute` does, and the inverse places the handle at `ikRest * F^-1 * ikRest^-1 * reach`, with `F` the target's motion over the handle's, so the ankle stays and the foot keeps its channels. A restore from joints does the same with the `F` the foot's inverse solves (`reverse-foot.md` section 10). The Limb's match outputs are affected by its ikTarget inputs. Still open, and the foot's: the IK to FK match does not write the toe FK control.

### 8.1 What phase 4 built (2026-11-20)

- **The plug is not an element of `childPlugs`.** It is two input attributes of its own, `ikTargetMatrix` and `ikTargetRestMatrix`, with no slot, control, joint or socket, so the slot numbers are unchanged. **Why:** Maya orders the graph by attribute, not by array element. A first version put it at slot 6 of `childPlugs` and read the three handle sockets without the target (`computeIndependentSocket`), and that is not enough: `outputSocketMatrix` and `childPlugParentMatrix` are one attribute each, so the `ik` socket the foot hangs from and the target the foot feeds are the same attributes, which is a cycle to the legacy DG. A cold evaluation read the foot's socket back as the identity (the hand went to the wrong place; the suite runs in DG mode). The plug graph is acyclic only with the target and the handle's sockets on attributes of their own.
- **The handle's sockets are their own array.** `handleSocketMatrix` holds the sockets of slots 3 to 5 (`ik`, `pole`, `settings`), and `getSocketMatrixPlug` finds them there (`TwoBoneIkSolve::isHandleSocket`), so everything that connects to a socket by index (child plugs, constraint drivers) is unchanged and the inverse reads the source's logical index as before. `attributeAffects` gives it the controls, the child plugs and the pole's space, and not the target. `outputSocketMatrix` still holds `index_0` to `index_2`, `start` and `end`, and the target moves those.
- **The solve takes the target as a motion.** `TwoBoneIkEval::hasTarget` and `mTargetDelta` (the plug's parent motion since rest, as every child plug gives): the end joint reaches `handleRest * targetDelta`, in place of the handle's own world, for both the position the IK solves for and the end frame. The handle's local is not applied again, because a foot that hangs from the `ik` socket already carries it. Unconnected, the delta is not read: a build-time flag, `ikTargetConnected`, says whether the template wired the plug, because an unconnected plug reads as the identity and that would pin the end to the handle's rest.
- **`ModulePlugInfo::optional`.** The plug stays unparented when the template does not list it (pass 2 skips it). No other plug has this.
- **`ModulePlugInfo::blindSockets`** (ikTarget lists `ik`, `pole` and `settings`). The cycle finder now tracks, for each module on a path, the plug it was entered through, and skips an edge that leaves by one of that plug's blind sockets. This is narrower than the first sketch (skip the `ikTarget` edge): a foot on `end` still reports the loop, because `end` does see the target. Rejected: skipping the edge outright, which would hide that loop.
- **The sockets really are independent.** `LimbNode::computeHandleSockets` solves the three handle sockets from their own controls and never reads the target. A suite test evaluates the foot-shaped rig cold in DG and in parallel mode and checks Maya reports no cycle.
- **The restore orders a Limb and its foot as parent and child.** The plug is not in `childPlugs`, so the module loop does not see it. Since D74 phase 5 `RigInverse` finds the module that feeds `ikTarget` from the connection and, for a ReverseFoot, solves it just before the Limb for the foot's motion, which the Limb's inverse undoes. A module whose inverse gives no motion (an FkChain on the target) leaves the Limb matched as if the target were at rest. A scene walk of `childPlugs` does not list `ikTarget` either, so the golden interface snapshot is unchanged.
- **The foot-shaped consumer is now the real one (2026-11-24, D74).** The tests that stood in a one-joint `FkChain` (`limb_foot`) for the foot still do; `ReverseFoot` (`reverse-foot.md`) feeds `ikTarget` for real, and its cycle test is `test_reverse_foot`. The match from FK with a raised foot is the foot's inverse, built in phase 5 (2026-11-25), and `test_limb_matches_parts_with_a_foot` checks that the Limb alone, with an identity foot and with a ReverseFoot at rest give the same joints.

## 9. Names, mirroring, versions, export, libraries

- **Names (D58, D34).** Module type `Limb`, node `limbModule`, `<base>FK`, `<base>IK`, `<base>Pole`, `<base>Settings`, `<base>RibbonIK_<nn>`, `<base>Tweak_<nn>`. Every control carries `controlMarkup` (D25), unique per rig.
- **Mirroring.** By template (D28). The mirror modes are the existing channels' (D34). A chain with its -X down the limb works, as `RibbonMath::chainSign` handles it.
- **Poses and animation.** `Limb` is a new module with new control names, so a saved pose from the old arm does not load onto it, and the library browser shows that. No legacy rig has to survive (author, 2026-11-19), so there is no migration step. The solomon template changes by hand in phase 5.
- **Export.** One skeleton: the FK/IK joints and the bind joints, as today. The controls are not exported.

## 10. Performance (D36)

The cost of a limb is its node count, not the solver (1.4 percent of node time in D52's profile). `Limb` replaces five nodes and 21 controls with one node and 14 to 16 controls. The node's own `compute()` costs more, because it runs both solves, and that is measured. Per the gate, phase 4 adds `Limb` instances to the complex rig (`chain_specs`, `limb_specs` in `tools/perf/perf_scenes.py`), keeps the 12 `TwoBoneIk` limbs, and runs `bench` once. The fps and the ratio to the floor go in the commit message, with a statement when they are below D36's target.

## 11. Tests (D20)

- A `ModuleContract` subclass for `Limb`, with a -X chain case (`RibbonNegXContract` is the model).
- Pose cases written by hand: IK, FK, a 0.5 blend, stretch, soft, pin, roll, the elbow control moved, a start twist, an end twist, the wrist's swing not reaching the forearm.
- **`test_limb_matches_parts`**, the coupling check. On one set of inputs, `Limb`'s three joint worlds equal a `TwoBoneIk`'s, and with the elbow control at rest and no twist its bind joints equal a `Ribbon` over the same joints within a tolerance. It fails when a change to either module is not carried through.
- DAG parenting: every ribbon ik control under the start plug's parent, every tweaker under the first ik control.
- Validation errors: a stacked bind joint, no bind joint at the mid, fewer than 5 bind joints, `controlCount` out of range.
- The inverse round trip on the solomon pose, mirrored builds, and `ikTarget` (an `ikTarget` socket raised moves the end joint, a foot-shaped test module, the plug graph acyclic).
- A golden snapshot, and a worked example in `docs/modules.md` with a test in `test_docs_examples.py`.
- **From the phase 1 probe, to become pose cases through the `Limb` module** (`core/` has no test harness of its own, D20; the probe was a standalone program over a 3-anchor limb with joints at (0,0,0), (10,-3,0) and (20,0,0), 3 and 5 ik controls, 4 bind joints a side, and all 34 checks passed): the rest pose reproduces every bind frame, and every ik control rests on the curve with an identity offset. A rigid motion of the whole limb moves every bind frame with it. Bent (bone turns of 0.4 and 1.3 radians), the mid ik control and the elbow bind joint sit on the mid joint within 1e-6, the first and last bind joints sit on the start and end, and no step between neighbouring bind joints turns more than 65 percent of the joints' bend. A start-anchor roll of 0.8 gives 0.8 at the first bind joint, falling to 0 at the elbow and 0 past it, and an end-anchor roll of -0.9 gives 0 up to the elbow and -0.9 at the last bind joint. A mirrored chain (frames turned half a turn about Z) has the same bind positions, and its roll reads with the sign flipped about the forward tangent. Tier: the module tier, with `Limb`'s pose cases.

## 12. Build phases

Each phase ends with the build, the full suite, the docs pass and one commit that cites D62.

| Phase | Work | Public change |
|---|---|---|
| 0 | Extract the `TwoBoneIk` and `Ribbon` solves. The coupling warning lands in the headers, `CLAUDE.md` and both design pages (this change) | none |
| 1 | **Built 2026-11-19.** `RibbonMath::buildAnchoredSetup` and `Setup::fkAnchors` (section 5.1). Checked by a throwaway probe against hand-worked cases (section 11); the old ribbon results stay identical | none |
| 2 | **Built 2026-11-19.** `Limb`: args, plan, create, `compute`, DAG parenting, registry, vcxproj, source map, `docs/modules.md`. Contract, pose cases, snapshot, `bendSoftness` (5.2). The two-bone half is the template `TwoBoneIkPart` and `TwoBoneIkSolve::planSlots`, and the ribbon's channels and stored rest are in `RibbonSolve`, so the two older modules run on the same code. The inverse is not built: the contract's three inverse tests are skipped for `Limb` | `Limb` module, `bendSoftness` |
| 3 | **Built 2026-11-20.** The inverse and the matched switch through `setRigIkFkBlend` (section 7.1). The contract's three inverse tests run on `Limb` | `setRigIkFkBlend` and `find_ik_fk_limb` accept a Limb |
| 4 | **Built 2026-11-20.** `ikTarget` and the validator exemption, with the foot-shaped test module (section 8.1) | `ikTarget` plug (a template key) |
| 5 | **Built 2026-11-20.** 8 Limbs per rig in the complex perf rig (`limb_module_specs`) and one `bench` (D62 entry for the numbers) | none |
| 6 | **Found done 2026-11-20.** The solomon template already uses `Limb` for both arms and both legs (the author's hand edit, section 13 answer 5). Checked by building it (see the D62 entry); nothing in the repo changed | solomon template |

## 13. Answers recorded (author, 2026-11-19)

1. DAG parents: ribbon ik controls are leaf children of the start plug's DAG parent, and tweakers are leaf children of the first ik control.
2. `controlCount` defaults to 5 and its minimum is 3.
3. `ikTarget` and the validator exemption are approved.
4. Names as in section 9.
5. The solomon template moves to `Limb`.

## 14. Risks

- **The ribbon through the mid joint** needed two changes beyond an anchors array and has one open choice (section 5.1). It is answered before the node exists.
- **The coupling.** The shared-solve extraction makes it deliberate, and `test_limb_matches_parts` makes it tested.
- **The `ReverseFoot` match problem** is answered in `reverse-foot.md` section 10 (the inverse pre-pass, D74 phase 5) and built 2026-11-25.
- **Size.** About 21 channels and 16 controls per limb. If `bench` is far below target, section 7's trim list applies before any structural change.
