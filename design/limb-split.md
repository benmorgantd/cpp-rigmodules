# Splitting Limb into a TwoBoneIk and an anchored Ribbon: design

**Status:** proposed 2026-11-27, waiting for the author's sign-off. Nothing here is built. When it is, this page replaces `limb.md` (which moves to `archive/`) and the decision entry that records it replaces the Limb one.

The short version: the `Limb` node goes away. An arm or a leg becomes a `TwoBoneIk` (the IK/FK solve, unchanged, plus the `ikTarget` plug the foot needs) and a `Ribbon` in a new **anchored** form, whose curve passes through three frames that arrive through child plugs from the TwoBoneIk's `index_0`, `index_1` and `index_2` sockets. The ribbon's maths is the code Limb runs today, so the elbow behaves the same. The ribbon's two end ik controls are dropped because they sit on the FK and IK controls at the shoulder and wrist, so no control is doubled. Inverses stay on both modules.

## 1. Why

The author, 2026-11-26: Limb was meant to simplify and did the opposite.

- **Redundant code.** `TwoBoneIkPart.h` is a template over two nodes (TwoBoneIk and Limb) because a Maya node cannot inherit. Limb runs the TwoBoneIk solve, its channels, its match outputs and its inverse, and the Ribbon's solve, channels, stored rest and inverse. Its own channel list (`LimbChannel`) is a filtered copy of the Ribbon's.
- **Coupling that has to be policed.** A change to either module changes Limb. That is why there is a coupling warning in four headers, both design pages and `CLAUDE.md`, a `limb-coupling-check` agent, and a test (`test_limb_matches_parts`) whose only job is to catch the three disagreeing.
- **ReverseFoot is tied to "a Limb".** Validation, `RigInverse`'s pre-pass and the docs name the Limb, when what the foot needs is any two-bone limb with an `ikTarget`.
- **Doubled controls.** At the shoulder a Limb has the FK upper control and the first ribbon ik control. At the wrist it has the FK end control, the IK handle and the last ribbon ik control.

What Limb gave, and the split must keep:

1. One continuous curve from the shoulder through the elbow to the wrist, with a corner at the elbow that `bendSoftness` rounds.
2. The twist rules at the ends: the ribbon's start takes the start joint's swing without its twist, and its end takes the end joint's twist on the mid joint's swing, so the hand's swing never reaches the forearm. No `Swing` or `Twist` constraints.
3. One place for a limb's settings: every channel on the settings control.
4. The foot: `ikTarget`, the cycle exemption, and the inverse pre-pass.
5. A restore from joints that brings every bind joint back exactly.

## 2. The elbow

### 2.1 What happened when ribbons were constrained to a TwoBoneIk

Before Limb, the solomon arm was a `TwoBoneIk`, two `Ribbon` modules, a `Swing` constraint and a `Twist` constraint (`limb.md` section 1).

- **Two splines met at the elbow.** The upper ribbon's last bind joint, the lower ribbon's first and the elbow joint sat at one point. The curve had no continuity across the bend, so the skin creased there, and the ribbon channels that run along the curve (sine, scale profile, volume) restarted at the elbow.
- **The seam popped by the shoulder's twist.** The first build ignored the end plug's roll, so the upper ribbon's end did not follow the shoulder's twist and the two ribbons disagreed by it: 60 and 170 degrees in the probe. A ribbon's end now takes its plug's twist, which closed the pop, but the two curves still did not meet smoothly.
- **Nothing tied the upper ribbon to the elbow.** The template hung its end from `arm.start`, so it followed the elbow only through a constraint.
- **Constraints did a module's job.** The `Swing` on the upper ribbon's start and the `Twist` on the lower ribbon's end were the swing and twist split the limb already knew.
- **Four controls at the bend** and 21 per arm, with the 15 ribbon channels on two controls.

The root cause was that **a Ribbon's curve was built from its own two ends**. It could not pass through a third frame that another module owned. That is what ruled out "a Ribbon through the mid joint" in Limb's gate, and the reason Limb fused the two solves.

### 2.2 A single ribbon through the elbow: the anchored Ribbon

Limb's phase 1 found that the curve through three moving frames needs no new maths. `RibbonMath::buildAnchoredSetup`, `Setup::fkAnchors`, `markAnchorCorners` and the corner softness already do it, and `RibbonSolve::evaluate` takes the frames as `pAnchorWorld`. What the ribbon needs from the two-bone solve is **three world matrices at evaluation and their rests at build**. Nothing else. Sockets carry exactly that.

So the Ribbon gets an anchored form:

- **At build**, `anchorJoints` names the three limb joints. The ribbon reads their rest worlds without claiming them (as the ReverseFoot reads its ankle, `findJointRest`) and builds the anchored setup from them: the knots, the ik controls placed on the curve, the bind joints' offsets. This is Limb's plan step for its ribbon, moved.
- **At evaluation**, three child plugs, `start`, `mid` and `end`, carry the TwoBoneIk's `index_0`, `index_1` and `index_2` sockets: the blended joint worlds. The ribbon applies the same two end rules Limb applies (`startAnchor` and `endAnchor` move with it, out of `LimbModule.cpp`) and calls `RibbonSolve::evaluate` with the three frames, as Limb does.

At the elbow, nothing changes from today's Limb:

- The mid anchor is a corner. At `bendSoftness` 0 (the rest) each bone's half of the curve leaves the elbow along its own bone, so the bind joints lie on the bones and the bend is all in the elbow bind joint, which points along the bisector. At 1 the two sides share a tangent.
- The elbow bind joint sits on the elbow joint at every softness, and so does the elbow ribbon ik control at rest.
- The frames go through the corner by the shortest rotation, segment by segment, so a bend past a quarter turn does not flip a normal.
- The shoulder's twist eases down the upper arm and stops at the elbow, and the wrist's twist eases down the forearm from the elbow.

**Why this answers the old rejection.** The anchored ribbon does not build its curve from its own ends: it is handed three frames. The blended joints arrive through sockets, not constraints, and the twist rules are inside the ribbon, so the `Swing` and `Twist` constraints stay retired for limbs.

**What the socket boundary costs.** One check: the three sockets' rests must be the three anchor joints' rests, or the curve the ribbon built is not the one it is handed. The build checks it in pass 2 (the plug's rest against the stored anchor rest, within the bind tolerance Limb uses) and fails with a message that names the plug and the joint. A ribbon could be anchored to any three sockets that meet that check, an `FkChain` of three for a bendy FK limb for example. That is a side effect, not a goal, and gets one test.

## 3. The modules after the split

### 3.1 TwoBoneIk

Unchanged in its arguments, controls, channels and sockets. It gains Limb's `ikTarget`, moved as it is:

- The optional child plug `ikTarget`, its own pair of input attributes (`ikTargetMatrix`, `ikTargetRestMatrix`) and the build flag `ikTargetConnected`, not an element of `childPlugs`.
- The handle's sockets in their own output array (`handleSocketMatrix`), solved by `computeHandleSockets` from the controls alone, and the plug's `blindSockets` (`ik`, `pole`, `settings`) for the cycle check.
- The match outputs taking the target's motion.

`TwoBoneIkNode::inverse` already handles `ikTargetMotion` (Limb calls it), so the inverse does not change. `TwoBoneIkPart<Node>` folds back into `TwoBoneIkNode`: with one node there is nothing to template over. `moduleVersion` stays at 2. The new plug is optional and changes no argument's shape.

### 3.2 Ribbon, anchored form

**`moduleArgs`**

| Key | Plain Ribbon (today) | Anchored Ribbon |
|---|---|---|
| `joints` | the bind joints, 2 to 64 | the bind joints, 5 to 64, root to tip, all leaves: the first at the start anchor, the last at the end anchor, exactly one at the mid anchor (Limb's rules) |
| `anchorJoints` | not allowed | **new, required for this form**: `[[side, usage] x 3]`, start, mid and end. Read, not claimed |
| `controlCount` | ik controls, 2 to 8, default 3 | **the interior ik controls, 0 to 6, default 3.** The first is always at the mid anchor, the rest divided between the bones by length (Limb's `limbControlParameters`). 0 means tweakers only |
| `controlName` | default: first bind joint's usage without `_NN` | the same |
| `twistRamp` | needs `fkStyle` | allowed. Positions are along the anchors, as Limb's |
| `fkStyle`, `fkControlCount`, `startAimBlend`, `endAimBlend` | as today | an error: the anchors replace the fk chain and the aim blends |

`moduleVersion` stays at 2: `anchorJoints` is a new optional key and every existing Ribbon reads as before.

**Child plugs**

| Plug | Takes | Control | Notes |
|---|---|---|---|
| `start`, `mid`, `end` | the TwoBoneIk's `index_0`, `index_1`, `index_2` | none | the anchors. Each is a matrix input of its own, not an element of `childPlugs`, as the foot's `ankle` plug is. A plug with no control has no DAG role, so its `parentStructure` entry is its matrix parent only |
| `settings` | the TwoBoneIk's `settings` | none | which control holds the ribbon's channels (section 3.4). Its matrix is not read |

**Sockets:** `index_0` to `index_<k-1>`, one per interior ik control (its world and transform), and `bind_0` to `bind_<n-1>`, one per bind joint. There are no `start` and `end` sockets: the limb's ends are the TwoBoneIk's `start` and `end`.

**Controls:** the interior ik controls, `<base>IK_<nn>`, numbered by their place on the curve counting the anchors (section 3.3), and one tweaker per bind joint, `<base>Tweak_<nn>`, hidden. Their DAG parents: every ik control a leaf child of the control behind the `start` plug's socket (the FK upper control), and every tweaker a leaf child of the first ik control (or of the same FK control when `controlCount` is 0). See open question 4.

### 3.3 Controls: none doubled

The solomon left arm, `controlCount` 5 today (3 interior after):

| Place | Limb today | After the split | Why |
|---|---|---|---|
| shoulder | `shoulder` (FK), `shoulderRibbonIK_00` | `shoulder` (FK) | the ribbon's start is the anchor: its control only moved the skin off the shoulder joint, and its twist and scale are `startTwist` and `scaleStart` |
| middle of the upper arm | `shoulderRibbonIK_01` | `shoulderRibbonIK_01` | shapes the upper arm |
| elbow | `elbow` (FK), `shoulderRibbonIK_02` | `elbow` (FK), `shoulderRibbonIK_02` | different jobs: the FK control turns the forearm, the ribbon control moves the curve's corner (an elbow offset). Open question 2 |
| middle of the forearm | `shoulderRibbonIK_03` | `shoulderRibbonIK_03` | shapes the forearm |
| wrist | `hand` (FK), `shoulderIK`, `shoulderRibbonIK_04` | `hand` (FK), `shoulderIK` | as the shoulder. FK and IK are the two sides of the blend, and only one is visible outside a blend |
| pole, settings | `shoulderPole`, `shoulderSettings` | the same | |
| bind joints | 8 tweakers | 8 tweakers | hidden |

An arm goes from 19 controls to 17 and a leg from 18 to 16. The arm was 21 before Limb.

### 3.4 Channels: still in one place

The ribbon's channels go on the control behind its `settings` plug, which on a limb is the TwoBoneIk's settings control. Pass 2 adds them to that control and connects them to the ribbon node, the way it connects a limb's `ikFkBlend` to its foot. So `shoulderSettings` keeps all 16 channels it has today, under the same names:

| Channel | Today | After |
|---|---|---|
| `ikFkBlend`, `stretch`, `soft` | Limb, settings control | TwoBoneIk, settings control |
| `startTwist`, `endTwist`, `sineAmplitude`, `sineFrequency`, `sinePhase`, `sineOrient`, `sineStartFalloff`, `sineEndFalloff`, `volumePreserve`, `scaleStart`, `scaleMid`, `scaleEnd`, `bendSoftness` | Limb, settings control | anchored Ribbon, hosted on the TwoBoneIk's settings control |
| `roll` (IK handle), `pin` and `space` (pole), `space` (FK start, IK handle) | Limb | TwoBoneIk, unchanged |

The anchored form's channel set is Limb's: the plain Ribbon's less `startAimBlend`, `endAimBlend` and `bendRoundness`, plus `bendSoftness`. Defaults, rests and mirror modes are unchanged.

**What hosting costs.** It is new: no module today adds channels to another module's control. It needs:

- pass 2 to add the attributes to the host control, set their mirror modes, and connect them;
- `ModuleInverseChannel` to name a control outside the module (the host), so the restore reads the sidecar's values from the right control;
- the contract to check a hosted channel on the module that hosts it as well as on the ribbon;
- a validation error when the `settings` plug is missing on an anchored ribbon, or its socket has no control.

The alternative is to put the channels on the elbow ribbon control (`shoulderRibbonIK_02`), with no new mechanism, at the cost of splitting a limb's settings over two controls and moving 13 channels to a new control (a break for every saved pose that sets them). The author ruled "one place for a limb's settings" a goal of Limb, so hosting is recommended (open question 1).

### 3.5 Child plugs and sockets, before and after

| Module | Child plugs | Sockets |
|---|---|---|
| Limb (today) | `start`, `ik`, `pole`, `settings` (internal), `ikTarget` | `index_0`/`start`, `index_1`, `index_2`/`end`, `ik`, `pole`, `settings`, `ribbon_0` to `ribbon_<j-1>`, `bind_0` to `bind_<n-1>` |
| TwoBoneIk (after) | `start`, `ik`, `pole`, `settings` (internal), **`ikTarget`** | `index_0`/`start`, `index_1`, `index_2`/`end`, `ik`, `pole`, `settings` |
| Ribbon, anchored (after) | **`start`, `mid`, `end`, `settings`** | **`index_0` to `index_<k-1>`**, `bind_0` to `bind_<n-1>` |

The solomon arm's template after the split:

```json
{
  "moduleType": "TwoBoneIk", "name": "arm_00_L_MOD", "side": "Left", "mirror": true, "moduleVersion": 2,
  "moduleArgs": {
    "joints": [["Left", "shoulder_00"], ["Left", "elbow_00"], ["Left", "hand_00"]],
    "ikFkBlend": 0, "poleParentDefault": "parent"
  },
  "parentStructure": {"start": "clavicle_00_L_MOD.end", "ik": "clavicle_00_L_MOD.end", "pole": "clavicle_00_L_MOD.end"}
},
{
  "moduleType": "Ribbon", "name": "armRibbon_00_L_MOD", "side": "Left", "mirror": true, "moduleVersion": 2,
  "moduleArgs": {
    "anchorJoints": [["Left", "shoulder_00"], ["Left", "elbow_00"], ["Left", "hand_00"]],
    "joints": [["Left", "armRibbon_00"], ["Left", "armRibbon_01"], ["Left", "armRibbon_02"], ["Left", "armRibbon_03"],
               ["Left", "armRibbon_04"], ["Left", "armRibbon_05"], ["Left", "armRibbon_06"], ["Left", "armRibbon_07"]],
    "controlCount": 3, "controlName": "shoulderRibbon"
  },
  "parentStructure": {"start": "arm_00_L_MOD.index_0", "mid": "arm_00_L_MOD.index_1", "end": "arm_00_L_MOD.index_2",
                      "settings": "arm_00_L_MOD.settings"}
}
```

`controlName` is `shoulderRibbon` only so that the migrated ik controls keep their names (section 7.2). A new template would leave it out and get `armRibbonIK_<nn>` from the bind joints.

## 4. ReverseFoot after the split

The foot hangs from the **TwoBoneIk**: `ik` from its `ik` socket, `ankle` from its `end` socket, its `ankle` socket feeds the TwoBoneIk's `ikTarget`, and `ikFkDriver` names the TwoBoneIk. Its own code does not change, since `TwoBoneIkSolve::makeInterface` (which names the `blendSource`) is already the TwoBoneIk's.

- **Validation.** The rule that the driving module takes `"ikTarget": "<foot>.ankle"` stays. Its message changes from "it must be a Limb" to "it must be a TwoBoneIk".
- **The cycle.** TwoBoneIk → foot → TwoBoneIk is the loop Limb has today, cleared by the same blind-socket rule. The leg's ribbon reads `index_0` to `index_2`, which do see the target, and nothing flows back from the ribbon, so it adds no loop. The leg's ribbon therefore follows the ankle wherever the foot puts it, as Limb's does.
- **The inverse pre-pass.** `RigInverse` finds the module that feeds an `ikTarget` from the connection and solves it first. It is written for "a Limb's ikTarget". It becomes "any module with an `ikTarget` input", which after the split is the TwoBoneIk.
- **The coupled pair** becomes TwoBoneIk and ReverseFoot. The Ribbon is not part of it.

## 5. The inverse

Both modules keep an inverse. The work is today's Limb inverse cut where the node is cut.

**TwoBoneIk.** Unchanged: the FK, IK handle and pole controls from the three joints, with the foot's motion from the pre-pass when `ikTarget` is connected. The match outputs and `setRigIkFkBlend` are the TwoBoneIk's alone. The ribbon rides the joints and has nothing to match, which is what Limb's match did.

**Anchored Ribbon.** Limb's steps 2 and 3, moved:

1. The anchors are the three plugs' parent worlds, which the sweep has already solved, because the TwoBoneIk is the ribbon's parent and the sweep runs parents first.
2. The setup is rebuilt from the stored rests with `buildAnchoredSetup`, as at build.
3. The interior ik controls have no joint, so they keep the fallback (the sidecar's value, else the rest). Each tweaker is `bindJoint * controlOPM^-1`, so every bind joint present comes back exactly, whatever the ik controls hold.
4. A second evaluation gives the `index_<k>` and `bind_<n>` sockets.

The new work in `RigInverse`: the anchor plugs are not elements of `childPlugs`, so the sweep must hand the ribbon their parent sockets' solved worlds. Today it does this for one plug outside `childPlugs` (the foot's `ankle`, through `ikTargetJointWorld`). That becomes a general rule for a module's extra plugs rather than a second special case.

**Cost.** Per limb per frame, the maths is what Limb does now: one two-bone inverse, one setup rebuild (it allocates, which a restore accepts), two ribbon evaluations. The split adds one module to the sweep: assembling its context, and looking up 13 channels in the sidecar on the host control. That is small next to the setup rebuild. It has not been measured. Section 9 proposes a timing of a solomon restore before and after.

## 6. Evaluation cost compared with Limb

| | Limb | TwoBoneIk + anchored Ribbon |
|---|---|---|
| Module nodes per limb | 1 | 2 |
| Controls per solomon arm, leg | 19, 18 | 17, 16 |
| Solve per frame | two-bone + ribbon, one `compute()` | the same two, one `compute()` each |
| Extra connections | none | 3 anchor matrices (the 13 channels were connections in Limb too) |
| A ribbon ik control moves | the whole Limb recomputes, the two-bone solve included | only the ribbon recomputes |
| A two-bone control moves | the whole Limb | the TwoBoneIk, then the ribbon |

The ribbon waits on the TwoBoneIk's sockets, so the two run one after the other, as the two halves of Limb do now. D52's and Limb's profiles put a limb's cost in its node and control count, not its solve (1.4 percent of node time). One node more and two controls fewer per limb should be within `bench`'s run-to-run drift (up to about 30 percent). This is an estimate, not a measurement. The perf rig's `limb_module_specs` becomes TwoBoneIk plus anchored Ribbon specs in the same change. Whether to run `bench` once then is open question 7, since this is not a new module type.

## 7. Migration

### 7.1 The template: a format update step

The template format goes from version 1 to 2 with one update step in `templateUpdateTable()` (`core/RigJsonStructs.cpp`, which has none yet). The step needs no scene, which is why it can be a template step: everything it writes comes from the Limb entry. For each `"moduleType": "Limb"` entry (Limb's `moduleVersion` is 1, the only one there is) it writes:

1. **A TwoBoneIk** with the Limb's `name`, `side`, `shapeType`, `mirror`, `parentStructure` and `matrixParentStructure` (including `ikTarget`), `moduleVersion` 2, and the `moduleArgs` the two share: `joints`, `controlName`, `poleOffset`, `settingsOffset`, `ikFkBlend`, `ikOrientToWorld`, `ikAlignToPole`, `poleParentDefault`. Keeping the name means every reference to `arm_00_L_MOD.start`, `.ik`, `.end`, `.index_2` and the like (the hands' FkChains, the foot, the space constraints) still resolves.
2. **A Ribbon** right after it, named `<limb base>Ribbon` with the Limb's index and side (`arm_00_L_MOD` gives `armRibbon_00_L_MOD`, through `core/NamingConvention`), with the same `side`, `shapeType` and `mirror`, `moduleVersion` 2, and: `anchorJoints` = the Limb's `joints`, `joints` = its `bindJoints`, `controlCount` = its `controlCount` less 2 (default 5 gives 3), `controlName` = the Limb's control base (its `controlName`, else the first joint's usage without `_NN`) plus `Ribbon`, `twistRamp` copied. Its `parentStructure` is `start`, `mid`, `end` and `settings` on the TwoBoneIk's `index_0`, `index_1`, `index_2` and `settings`.
3. **References to the ribbon's sockets**, anywhere in the template (`parentStructure`, `matrixParentStructure`, constraint targets and drivers, push-out entries): `<limb>.bind_<n>` becomes `<ribbon>.bind_<n>`, and `<limb>.ribbon_<n>` becomes `<ribbon>.index_<n-1>` for an interior control. A reference to the first or last ribbon control (`ribbon_0`, `ribbon_<j-1>`) has no target any more. The step reports it as an error that suggests the limb's `start` or `end`.

A template still at version 1 with a Limb loads and builds as a TwoBoneIk and a Ribbon. A version 2 template with a `Limb` fails with an error that says Limb was split and how. The update runs in memory, so the file on disk stays at version 1 until it is rewritten.

The rejected option is a one-off conversion script plus a hand edit of solomon. It keeps no permanent code, but the format update rule exists so that old templates (test fixtures, mayadev's tests, any other character) load without anyone hunting for them. Open question 5.

### 7.2 Names that change

Control names are the markup usage (`side`, `usage`), which is what poses, animation and the library browser key on. Node names follow the convention. Listed for the solomon left side. The right side is the same with `R`.

**Modules**

| Before | After |
|---|---|
| `arm_00_L_MOD` (`limbModule`) | `arm_00_L_MOD` (`twoBoneIkModule`) and `armRibbon_00_L_MOD` (`ribbonModule`), new |
| `leg_00_L_MOD` (`limbModule`) | `leg_00_L_MOD` (`twoBoneIkModule`) and `legRibbon_00_L_MOD` (`ribbonModule`), new |

**Controls.** Unchanged: `shoulder`, `elbow`, `hand`, `shoulderIK`, `shoulderPole`, `shoulderSettings`, `hip`, `knee`, `ankle`, `hipIK`, `hipPole`, `hipSettings`, `shoulderRibbonIK_01` to `_03`, `hipRibbonIK_01` to `_03`, and every tweaker's node name (`armRibbon_00_L_CTRL` to `armRibbon_07_L_CTRL`, `legRibbon_00_L_CTRL` to `legRibbon_06_L_CTRL`, which come from the bind joints).

| Before (usage) | After | Effect |
|---|---|---|
| `shoulderRibbonIK_00`, `shoulderRibbonIK_04` | removed | a pose's values on them are dropped |
| `hipRibbonIK_00`, `hipRibbonIK_04` | removed | the same |
| `shoulderTweak_00` to `shoulderTweak_07` | `shoulderRibbonTweak_00` to `_07` | usage renamed (the node keeps its name) |
| `hipTweak_00` to `hipTweak_06` | `hipRibbonTweak_00` to `_06` | the same |

The tweaker rename follows from the Ribbon's naming (`<base>IK_<nn>` and `<base>Tweak_<nn>` share one base): keeping the ik controls' names forces the base `shoulderRibbon`. The other way round (base `shoulder`) would name the ribbon's ik controls `shoulderIK_01` beside the IK handle `shoulderIK`, which is worse. Open question 6.

**Channels.** None renamed. With hosting, none moves either: all 16 stay on `shoulderSettings` and `hipSettings`.

**Sockets**

| Before | After |
|---|---|
| `arm_00_L_MOD.index_0`/`start`, `index_1`, `index_2`/`end`, `ik`, `pole`, `settings` | unchanged, on the TwoBoneIk |
| `arm_00_L_MOD.ribbon_0`, `ribbon_4` | removed |
| `arm_00_L_MOD.ribbon_1` to `ribbon_3` | `armRibbon_00_L_MOD.index_0` to `index_2` |
| `arm_00_L_MOD.bind_0` to `bind_7` | `armRibbon_00_L_MOD.bind_0` to `bind_7` |
| the leg's the same | `legRibbon_00_L_MOD.*` |

Solomon refers to none of the moved sockets (the fingers hang from `arm_00_L_MOD.index_2` and `.end`, the foot from `leg_00_L_MOD.ik` and `.end`), so its rewrite is steps 1 and 2 only. **Child plugs:** the TwoBoneIk's are the Limb's. The Ribbon's `start`, `mid`, `end` and `settings` are new.

### 7.3 Saved poses, animation, shapes and export

- **Poses and animation keyed on controls** load onto every unchanged usage and channel, which is all of the TwoBoneIk, the hosted channels and the interior ribbon ik controls. Values on the two removed ribbon controls per limb are dropped and reported by the loader. Values on the renamed tweakers do not load.
- **The defined path for animation is through the joints**, which the rename does not touch: capture a joint clip on the old rig, rebuild, restore from joints. The TwoBoneIk inverse gives the FK, IK and pole controls, and the ribbon's tweakers bring every bind joint back exactly. Only what the sidecar held on a removed or renamed control is lost, and the bind joints still match.
- **Shapes** (`shapes.json`) are kept by node name, and no surviving control's node name changes. The two removed controls' shapes are left unused in the file.
- **Export:** none. The skeleton is the same joints, and controls are not exported.
- **mayadev:** `find_ik_fk_limb` and `query.IkFkLimb` drop the Limb case. mayadev's rig panel already handles only `TwoBoneIk` (`MAX_LIMB_SELECTION = 6`), so a split limb is one it can read. A mayadev-callsites pass runs before the change.

## 8. What is deleted

**Code**

- `maya/modules/LimbModule.h` and `.cpp` (about 1,070 lines), its registry row, its vcxproj and filters entries. MTypeId `0x002192` (`limbModule`) is retired and never reused.
- `maya/shared/TwoBoneIkPart.h` (455 lines) as a template. Its code stays, inside `TwoBoneIkNode`, so the net saving is the template plumbing and the second set of static attributes.
- `LimbSlot`, `LimbChannel`, `limbChannelSpecs`, `paramsFromLimbChannels` and `setAnchors` in their Limb form. The channel subset and the anchor helpers move into `RibbonSolve` for the anchored form.
- The Limb branches in `TwoBoneIkCmds.cpp` (`readLimbModule`, `isLimb`), `python/rigmodules/query.py` (`LIMB_TYPE`), `RigTemplateValidation.cpp` and `RigInverse.cpp`.
- The coupling warnings in `TwoBoneIkModule.h`, `RibbonModule.h`, `RibbonMath.h`, `TwoBoneIkSolve.h`, `RibbonSolve.h`, `two-bone-ik.md`, `ribbon.md` and `CLAUDE.md`, and the `limb-coupling-check` agent. The `ReverseFoot` and TwoBoneIk pair keeps a short note, and the Ribbon's anchored form documents the one contract it has with whatever feeds it (section 2.2).

**Tests**

- `tests/maya/modules/test_limb.py` (606 lines) is removed. Its hand-written pose cases move: the ribbon ones (corner, softness, twist ramp, ik control placement, bind sockets) to `test_ribbon.py` under an anchored contract, the `ikTarget` ones to `test_two_bone_ik.py`. `test_limb_matches_parts` and `test_limb_matches_parts_with_a_foot` are deleted with nothing to replace them: there is no second copy of a solve to disagree with.
- The fixtures `limb_arm`, `limb_bind_child`, `limb_foot`, `limb_mirror`, `limb_ribbon_child`, `limb_three`, `limb_twoboneik` and `validation/limb_unknown_bind_socket` become anchored-ribbon fixtures. One version 1 Limb fixture stays, as the input of the format step's test.
- The golden `limb_arm.json` becomes a two-module golden. `reverse_foot.json` and the reverse foot fixtures change `Limb` to `TwoBoneIk` and add the ribbon.

**What is added, for balance:** the anchored form in `RibbonModule` (arguments, three anchor plugs, the anchor rest check, the anchored inverse), channel hosting (pass 2, `ModuleInverseChannel`, the contract), `ikTarget` on TwoBoneIk (moved, not new), the template update step, and the general extra-plug rule in `RigInverse`. Channel hosting is the one new concept.

## 9. Tests

- **The anchored Ribbon:** a `ModuleContract` subclass with a -X chain case, Limb's pose cases ported by hand to the new names, a parity check (below), and validation errors: an anchor plug whose socket is not at its anchor joint, a stacked bind joint, no bind joint at the mid anchor, fewer than 5 bind joints, `controlCount` out of range, `anchorJoints` with `fkStyle` or an aim blend, a missing `settings` plug, a `settings` socket with no control. One case anchors on an FkChain of three.
- **Parity with Limb, once.** In the phase that adds the anchored form, while Limb still exists, a test builds a Limb arm and the split arm from one skeleton and checks that the joints and bind joints agree on Limb's pose cases. It is evidence for the change and goes with Limb in the deleting phase.
- **TwoBoneIk:** Limb's `ikTarget` cases (the target moves the end, the handle stays, FK ignores it, half blend, no cycle in DG or parallel mode, undo and redo).
- **ReverseFoot:** `test_reverse_foot` on a TwoBoneIk, including "a foot at rest leaves the limb where it is" and the restore order.
- **The format step:** a Maya-free core test of the JSON rewrite (both entries, the name, `controlCount` less 2, the socket references, the error for `ribbon_0`), and a mayapy test that a version 1 Limb template builds with the names in section 7.2.
- **Docs:** the worked example in `docs/modules.md` and its test in `test_docs_examples.py`.
- **Proposed, not suite tests:** a timing of a solomon clip restore before and after (section 5), and building the solomon template, which lives outside the repo.

## 10. Build phases

Each ends with the build, the full suite, the docs pass and one commit.

| Phase | Work | Public change |
|---|---|---|
| 1 | The anchored Ribbon form, channel hosting, its tests and the parity test against Limb | `anchorJoints`, the plugs `mid` and `settings`, `controlCount`'s meaning in that form |
| 2 | `ikTarget` on TwoBoneIk. ReverseFoot, validation and `RigInverse` accept it, and the extra-plug rule | `ikTarget` on TwoBoneIk |
| 3 | The template format step to version 2. Fixtures, goldens and the perf rig converted. Limb deleted (node, part template, tests, coupling notes, agent). `limb.md` archived | Limb removed, format 2 |
| 4 | Solomon: the author rewrites the template (or resaves it through the step), builds it, and restores a clip from joints | the solomon rig |

## 11. Open questions for the author

1. **Where the ribbon's channels live.** Recommended: hosted on the TwoBoneIk's settings control through the ribbon's `settings` plug, so nothing moves for the animator. The alternative is the elbow ribbon control, which needs no new mechanism but splits a limb's settings over two controls and moves 13 channels.
2. **The elbow ribbon control.** It sits on the elbow FK control but does a different job (an offset of the curve's corner). Recommended: keep it, with `controlCount` 0 allowed for a limb that wants none. Or should the default have no elbow control?
3. **Numbering the ribbon's ik controls** from `_01` (by their place, counting the anchors), which keeps `shoulderRibbonIK_01` to `_03`, or from `_00`, which renames all three.
4. **DAG parent of the ribbon's controls.** Recommended: the FK upper control, which the `start` plug's socket names under the strict DAG rule. Limb put them under the clavicle (the TwoBoneIk's `start` plug's parent). The ribbon cannot name that without another plug.
5. **Migration by a format update step** (recommended, section 7.1), or a one-off script and a hand edit of solomon.
6. **The tweakers' usage rename** (`shoulderTweak_NN` to `shoulderRibbonTweak_NN`). Accept it, given the joint restore path, or add a key that names the tweakers apart?
7. **`bench`.** Run it once in phase 3, when the perf rig's Limbs become TwoBoneIk plus Ribbon? It is not a new module type, so the rule does not require it.

## Related decisions

D62 (Limb, which this replaces), D52 (TwoBoneIk), D56 and D59 (Ribbon), D57 (derived controls), D74 (ReverseFoot), D75 (`twistRamp`), D78 (sockets and the strict DAG parent), D33 (format updates), D36 (performance). The decision entry for this page is D80.
