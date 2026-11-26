# ReverseFoot: foot roll, pivots and an FK toe, on top of a Limb

**Status:** Signed off by the author, 2026-11-24 (D74). Written from the author's answers. The author answered the seven open questions the same day, and section 14 records the answers. Built 2026-11-24, and the inverse (phase 5) 2026-11-25, except the perf rig (phase 6): D74 records what phases 2 to 4 settled that this page left open (the chain's composition, the plugs without controls, `controlName`, the pivot socket names). Depends on markers (`markers.md`, D73).

## 1. Animator-facing problem

A leg's IK ends at the ankle. A foot needs more: roll over the ball, peel off the toe, bank onto an edge, swivel about the heel, ball or toe, tap the toe, and push the ankle up when the ball lifts. Animators want these as **channels** on one control (and many prefer them to FK controls), and they want an FK foot that works when the leg is in FK. Today a Limb's foot is a rigid part of its end joint, and `ikTarget` (D62, `limb.md` section 8) is built and waiting for a module to feed it.

## 2. What exists, and what it settles

| Source | Settles |
|---|---|
| D62, `limb.md` section 8 | One `Limb`. The foot is a later module, `ReverseFoot`, with its own gate (this page). `Limb` does not know about feet. |
| D62 phase 4 | `ikTarget`: an optional child plug taking a world matrix, applied as a motion from rest (`handleRest * targetDelta`). Ignored at `ikFkBlend` 0. `ik`, `pole` and `settings` sockets are their own array and do not read it. The cycle check skips a path that enters by `ikTarget` and leaves by one of those sockets (`blindSockets`). |
| D62 | The end joint of `joints` may be up to 80 degrees off the shin, so an ankle's X can point at the ball. |
| D57 | Controls with no joint are derived. Foot pivots were the one exception, kept as marker joints. **Reversed here by D73.** |
| D41, D52 | The inverse is module-owned. Joint-derived values win, then the sidecar, then the rest. Scalar channels reach it through `inverseChannels`. |
| `limb.md` section 8.1, section 14 | Open and handed to this design: matching an IK pose from FK while a foot channel is raised. |

The designs never said how the foot behaves in FK, where its inputs come from, or what its inverse can solve. This page does.

## 3. Decisions the author has made (2026-11-24)

1. **Markers, option C** (`markers.md`). The pivots are markers, in a file of their own, and they survive the round trip to the engine format because `export` is in the file from the start.
2. **Pivot order** is fixed by the module: `heel`, `toeTip`, `bankOut`, `bankIn`, `toe`, then the ankle (`FootSolve::chainOrder`). `toeTip` is the pivot at the end of the toe. `toe` is the pivot at the base of the toe, the toe joint, and it is the ball of the foot: the foot rolls about it, so the planted toe keeps its bone length exactly. **Changed 2026-11-25 (author):** there was a separate `ball` pivot last in the chain, carrying the ball channels. Because the axes come from the foot frame and not from a marker, it could only move the roll's centre off the toe joint, which slid the toe and dropped the toe tip, so it was folded into `toe` (D74). **Changed 2026-11-24 (author):** the order and the roles were template data (`reverseHierarchy`), and are now the module's. Every foot has all its pivots and none is worth leaving out, and the order is a rig-behaviour choice: the author's own order put the toe under the ball, so a roll below the break tipped the toe into the ground with nothing to warn of it.
3. **One signed `bank` channel**, no set-driven keys. Positive banks the foot toward the character's left (world +X when it faces +Z). That is "out" for a left foot and "in" for a right foot, so the mouse moves the way the foot goes when you face the character. It is `mirrorNegate`. The author says this is not a hard rule if it does not work out.
4. **Peel is computed in the node,** with conditionals, from a `footRoll` channel and a `breakAngle` channel (default 30). Pivot angles add: raising the ball adds to the ball's angle and never starts the peel.
5. **Channels only.** No FK pivot controls. The animator sees one channel control.
6. **No `Leg` module,** and no foot inside `Limb`. See the note in section 13.
7. **FK mode.** The foot has its own FK controls (the toe, section 8) and its blend is driven by the limb's `ikFkBlend` through a module argument, `ikFkDriver`.
8. **No cycle.** The foot is two stages, with a socket and plug split by attribute (section 6). Two plugs and two stages confirmed by the author.
9. **Limb owns the ankle.** The foot never drives the ankle bind joint. The ankle comes from the Limb's IK or FK end.
10. **The channels live on a special control,** a world-space aligned control derived from the `toe` marker (the `ball` marker until 2026-11-25) (author: they generally live on the IK leg control, but a control of the foot's own avoids a cross-module channel).
11. **The inverse's gauge:** every pitch into `footRoll`, every roll into `bank`, the handle takes yaw and position (section 10). A mocap clip then reads with `footRoll` carrying the pitch, which is acceptable.

## 4. Where the foot's inputs come from

Both of these are true and are not in conflict:

- **The foot's visible result hangs from the Limb's `end` socket,** the blended ankle (IK or FK or between). The toe and toe tip follow the ankle that is really there. This is the author's correction: not the `ik` socket.
- **The foot's IK half still needs the `ik` socket,** because it computes where the ankle *should* go and that is what `ikTarget` takes. It cannot read `end`: `end` depends on `ikTarget`, which depends on this half. That loop is the reason the `ik` plug exists, and it is the `blindSockets` case D62 built.

So a template wires **two child plugs** (section 6). The author's "unless ik also matches the fk end position always" would let one plug do both. It does not hold: at `ikFkBlend` 0 the IK handle stays where the animator left it, and the foot must follow the FK ankle there.

## 5. Template

```json
{
  "moduleType": "ReverseFoot",
  "name": "foot_00_L_MOD",
  "side": "Left",
  "mirror": true,
  "moduleArgs": {
    "joints": [["Left", "ankle"], ["Left", "toe"], ["Left", "toeTip"]],
    "ikFkDriver": "leg_00_L_MOD",
    "controlName": "foot"
  },
  "parentStructure": { "ik": "leg_00_L_MOD.ik", "ankle": "leg_00_L_MOD.end" }
}
```

| `moduleArgs` key | Default | Meaning |
|---|---|---|
| `joints` | required | Three joint selectors: the ankle, the toe (ball joint) and the toe tip. The foot **drives the toe and the toe tip.** The ankle is a reference only: it belongs to the Limb (a joint two modules drive is a template error, D71) |
| (no `reverseHierarchy`) | removed 2026-11-24 | The pivots are the five markers `[side, role]` on the foot's own side, `heel`, `bankOut`, `bankIn`, `toeTip` and `toe`, all required (a `ball` marker is not read since 2026-11-25), in the fixed order of item 2. A missing one is a read-phase error naming it, and the old key is an unknown key. Two feet on one side (a creature) would need a marker prefix argument; not built, since a quadruped leg is likely a module of its own (author) |
| `ikFkDriver` | required | The module id (as written in the template) of the Limb whose `ikFkBlend` drives this foot. Validated: it must exist, have a blend, be the module that `ik` and `ankle` hang from, and take the foot's `ankle` socket as its `ikTarget` (`ModuleInterface::driverPlug`, 2026-11-24: the author's solomon build left it out and the foot moved nothing, with no error). A TwoBoneIk has no `ikTarget`, so it cannot carry a foot |
| `controlName` | `foot` | Base of the control names |

Roles are a table in `core/` that says which channels act on a pivot. The order is the list's, so a rig with no bank pivots, or the pivots in another order, is a different list and no code change. A role that appears twice, or a `bankOut` without a `bankIn`, is a read-phase error. The mirrored module swaps the selectors' side and the `ikFkDriver` id (D28).

## 6. Interface

| | |
|---|---|
| Node type | `reverseFootModule`, MTypeId from `CLAUDE.md`'s free list at build time |
| Child plugs | `ik` (the IK half, internal motion from the Limb's `ik` socket), `ankle` (the ankle as it is, from the Limb's `end` socket). Each can attach to a different socket. A constraint on either works as for other plugs |
| Sockets | `ankle` (the IK ankle target, the world the Limb's `ikTarget` takes), `toe`, `toeTip` (the solved bind joints), and one socket per pivot named from its role (`heel`, `bankOut`, ... the posed pivot worlds, for tools and for a hand or prop hung from the foot) |
| Controls | `<base>Foot`, the channel control (D34, section 8), and `<toe usage>FK`, an FK rotate control for the toe (FK goes before a trailing number: `ball_00` gives `ballFK_00`). `<base>Foot` is **derived from the `ball` marker**: it sits at the marker's position, **aligned to the world** (its rotation is locked, so its orientation only sets how its translate reads, as the pole's does, D57). It has translate locked and hidden, so it carries channels only |
| Joints driven | The toe and the toe tip |
| Internal inputs | `ikFkBlend`, non-keyable and locked, connected to the driver (section 9) |

**Two stages, two attribute groups** (D62 phase 4 lesson: Maya orders the graph by attribute). Stage 1 reads `ik` and the foot control and writes the `ankle` socket. Stage 2 reads `end`, the foot control, the toe FK control and the blend, and writes `toe`, `toeTip` and the pivot sockets. Stage 1's output attribute and stage 2's input attribute are different attributes, so Maya sees no loop. The cycle check gets the same per-socket rule as `ikTarget`: `ankle` (the `end` plug) is blind to the `ankle` socket. A suite test builds the whole leg (Limb, foot, a hand-shaped dummy hung from `end`) and evaluates cold in DG and parallel mode.

## 7. The solve

All in `core/FootSolve` (Maya-free, tested in the suite), the node is a thin reader and writer.

**Frames.** The pivot positions come from the markers (positions only; a marker's orientation is not read by this module, so a rigger cannot get the axes wrong). The foot frame is derived: **forward** is heel to toe tip projected onto the ground (world up at rest), **up** is world +Y, **lateral** is up crossed with forward. Roll, raise and swivel axes are these. This is what makes the order and the marker orientation not matter, and what the inverse relies on. **Kept after the Axis locators (author, 2026-11-24):** the editing locators now show each marker's axes, so a rigger may expect a rotated toe marker to set the roll axis. The author chose to keep the derived frame over reading the marker axes (which would give a slanted ball line, at the cost of non-parallel pitches in the inverse), and the build warns once per foot, naming every pivot marker that is not world aligned.

**Chain.** For each pivot in list order, `pivot_i = pivot_{i-1} * rotation_i * restOffset_i`. The channels in the table give each pivot's rotation. The end of the chain is the ankle: `ankleTarget = chain end`, delivered to the Limb as a motion from rest, like every child plug. Because the chain carries the ankle, **raising the ball pushes the ankle up** with no special rule.

**As built in phase 2 (`core/FootSolve`, author's answer 2026-11-24).** The chain is the literal hierarchy with the heel as root: a pivot is carried by the pivots before it. That equals turning the ankle about REST pivots and rest axes, the last pivot first. **In the fixed order (2026-11-24, the ball folded into the toe 2026-11-25)** that is `ankle' = Rot_heel(Rot_toeTip(Rot_bank(Rot_toe(ankle))))`: the toe pivot turns the ankle first and the toe bone is carried only by the outermost of its turns, `ballSwivel` (then `toeTap`), so below the break the toe stays planted and a ball swivel turns the whole foot, and past the break the toe tip carries the whole foot and stays on the ground. (With the earlier template order, ball before toe tip and toe, the ball's roll tipped the toe and carried the toe tip below the ground.) Signs and the order of the turns within one pivot are in D74. The rule "pivot angles about parallel axes add" holds only while no roll sits between them. The toe pivot now turns before the bank, so a ball roll with a bank is a pitch then a roll, and `decomposeRotation` (roll first) does not read back `footRoll` and `bank` exactly: the handle absorbs the rest, and the joints still come back exactly. A heel roll with a bank still reads back exactly (the bank turns first). Which gauge the phase 5 inverse uses is decided there.

**Peel** (`footRoll`, degrees, positive rolls forward, the heel lifts; negative lifts the toes about the heel):

```
heel   = max(-footRoll, 0)            toes up about the heel
ball   = clamp(footRoll, 0, breakAngle)
toeTip = max(footRoll - breakAngle, 0)  heel and ball up about the toe tip
```

The three sum to `footRoll`. The raise channels (`heelRaise`, `ballRaise`, `toeRaise`) are added to their pivot's angle **after** that split. So `ballRaise` moves the foot but never changes which regime the peel is in.

**Bank.** `bank` greater than 0 turns about the bank pivot on the side of the foot that is toward world +X in the foot frame, `bank` less than 0 about the other. The pivots are picked from the markers' lateral positions, so a left foot's positive bank uses `bankOut` and a right foot's uses `bankIn`, with no side branch in the node.

**Toe.** `toeTap` rotates the toe bone about the `toe` pivot's lateral axis. It does not move the ankle (it is not in the chain that reaches the ankle).

**Stage 2 (toe and toe tip).** With IK the toe is the chain's `toe` pivot turned by `toeTap`, then **pulled toward the actual ankle**: its position keeps the ankle-to-toe bone length, aimed at the pivot, so a leg that does not reach (stretch off, `pin`) keeps the bone length and still points at the ground. When the leg reaches, the end joint equals the ankle target and the pull changes nothing. The toe tip rides the toe rigidly. With FK the toe is the actual ankle's frame times the rest offset times the toe FK control. The result is blended by `ikFkBlend` with the same blend function TwoBoneIk uses. The ankle bind joint is never touched: the Limb owns it (author, 2026-11-24).

## 8. Channels

On `<base>Foot`. camelCase, no side, no module in the name (D34). Every channel's default is its rest, and a `mirrorCopy` or `mirrorNegate` is declared through `RigPoseScene::setMirrorMode`. Degrees.

| Channel | Acts on | Rest | Mirror | Notes |
|---|---|---|---|---|
| `footRoll` | peel | 0 | `mirrorCopy` | Heel, then ball, then toe tip |
| `breakAngle` | peel | 30 | `mirrorCopy` | Where the ball hands over to the toe tip. Keyable, so the author's "from another attr" |
| `heelRaise` | heel pivot, pitch | 0 | `mirrorCopy` | Toes up about the heel |
| `heelSwivel` | heel pivot, about up | 0 | `mirrorNegate` | |
| `ballRaise` | toe pivot (the ball of the foot), pitch | 0 | `mirrorCopy` | Heel up about the ball |
| `ballSwivel` | toe pivot, about up | 0 | `mirrorNegate` | The whole foot, toe tip included, turns about the ball ("squash a bug", author 2026-11-25): the one toe pivot turn that carries the toe bone |
| `ballRoll` | toe pivot, about forward | 0 | `mirrorNegate` | The author's "ankle break": the ankle goes left and right over the ball |
| `bank` | bank pivots | 0 | `mirrorNegate` | Signed, section 3 |
| `toeRaise` | toe tip pivot, pitch | 0 | `mirrorCopy` | Heel and ball up about the toe tip |
| `toeSwivel` | toe tip pivot, about up | 0 | `mirrorNegate` | |
| `toeTap` | the toe bone about the `toe` pivot | 0 | `mirrorCopy` | Only the toe moves |

`footRoll` and `breakAngle` are the only channels the author named for the peel, so they are the only ones there. Every foot has all five pivots, so the control always has all 11 channels. The toe FK control is rotate only (translate and scale are locked and hidden).

**An animator sees these names forever.** They are public API (CLAUDE.md, name stability): a rename later breaks saved poses and animation. The author confirmed the table and the signs on 2026-11-24.

## 9. FK and IK

The Limb is the switch. `ikFkDriver` names the Limb (or TwoBoneIk) module. Build pass 2 connects that module's settings control `ikFkBlend` channel to the foot's `ikFkBlend` input attribute. The attribute is non-keyable and locked, so an animator can only reach the blend through the limb, and `setRigIkFkBlend` moves both with one write. This is the first module input driven by another module's channel, so it is new surface (section 12):

- It is a connection from a control channel to a module input attribute. The foot already depends on the Limb's sockets, so the module graph gains no new direction. The cycle check sees nothing new.
- The module interface gains the means to say "my blend source plug" (`ModuleInterface`), implemented by TwoBoneIk and Limb, and the validator checks `ikFkDriver` against it.
- Rejected: the foot owning an `ikFkBlend` channel of its own. Two switches for one limb is the surprise animators hit (D59 step 3, "two drivers of one end").

At blend 1 the channels drive and the toe FK control is hidden. At blend 0 the toe FK control drives and the channels that move the ankle have no effect, because the Limb ignores `ikTarget`. A visibility output hides what does not apply (TwoBoneIk has one), so the animator is not left turning a knob that does nothing. Between 0 and 1 both halves run and blend. This means **the Limb's blend is "IK with channels and pivots" against "pure FK"**, a difference from the first design, which left the FK foot open.

**The ankle's FK control** is the Limb's: it already has an FK control on each of its three joints, and the ankle comes from the Limb's IK or FK end (author, 2026-11-24). This module adds only the toe's.

## 10. The inverse

The foot's inverse is where the work is, and the question was which channels can be solved from joints. The answer depends on one fact.

**The handle absorbs the chain.** The ankle is `handle * footOffset(channels)`, and `footOffset` is a rigid transform. So for *any* channel values there is a handle that puts the ankle exactly where the joints have it: `handle = ankle * footOffset^-1`. The inverse is therefore exact for the ankle and the toe whatever the channels are. The only question is which channels it picks, and the answer changes where the handle ends up and what the animator sees in the channel box.

**What cannot be solved, and why.** Heel, ball and toe tip raises, the three swivels and `ballRoll` all move the ankle, and the ankle's six degrees of freedom are shared with the handle's six. Raising the ball by 20 degrees and rotating the handle to compensate gives the same joints as a handle at rest and a different channel. No joint-based rule can tell them apart.

**The smallest useful subset, solved from joints:**

| Solved | From | How |
|---|---|---|
| `footRoll` | the ankle's total pitch about the lateral axis | Pivot angles about parallel axes add, so the total pitch is the peel's own sum. Every pitch is put in `footRoll` (the raises stay at zero), and the handle takes the rest |
| `bank` | the ankle's total roll about the forward axis | Put into `bank` (`ballRoll` stays at zero) |
| `toeTap` | the toe joint relative to the ankle | The toe bone's lateral rotation. Exact |

**What the handle takes:** the foot's heading (yaw), its position, and the part of the toe's rotation that is not `toeTap` (its own roll). The handle ends up where a foot with that pitch and bank would be planted, not necessarily where the animator originally had it. Heel, ball and toe-tip swivel, the raises and `ballRoll` are left at the sidecar's value, or at their rest. With no sidecar (a mocap clip) the ankle and toe come back exactly, `footRoll`, `bank` and `toeTap` are filled in, and the rest are zero.

**The author's three channels** (toe raise, ball raise, toe swivel): the first two are folded into `footRoll`, because that is what they are, a pitch about a pivot, and a pose does not say which pivot the animator used. The third is not solvable, for the same reason. If a different gauge is wanted (all pitch into `ballRaise`, say) it is one argument, but the choice has to be made once and written down.

**FK is lossless.** At blend 0 the ankle FK control and the toe FK control come from the joints exactly, and a pose from FK needs nothing from the sidecar.

**How the Limb gets the foot's offset** (the pop the designs left open):

- `ModuleInverseContext` gains `ikTargetOffset`, a matrix. A foot is not an ordinary parent: its offset depends on the *joints* (through `footRoll` and `bank`), and the Limb's handle depends on its offset. `RigInverse` runs a **pre-pass** over modules that feed an `ikTarget`: it asks the foot for its channel solve first (pure joints and sidecar, no handle needed), and hands the resulting offset to the Limb's inverse, which sets `handle = end * offset^-1`. The pre-pass is one function and one loop, and it is the only reason `RigInverse` changes.
- `setRigIkFkBlend` runs the same inverse, so **a match from FK puts no pop** in the ankle or the toe, and a match from IK to FK solves the FK controls from the joints. The open item in `limb.md` section 8 is closed by this.
- **`inverseChannels`** for the foot lists `footRoll`, `bank` and `toeTap` as derived. The rest are read as scalars from the sidecar or the rig (D52), so they come back as the animator left them when the sidecar has them and as the current rig values otherwise.

**As built (phase 5, 2026-11-25, D74).** Where this differs from the plan above, the build wins:

- **The gauge is "subtract the sidecar" (author).** `footRoll` and `bank` take the pitch and roll that the given channels (the sidecar's, else the rig's) miss, on top of their given values (`FootSolve::solveAnkleChannels`), so a sidecar's raises are not counted twice and a sidecar that already makes the turn comes back unchanged. With no sidecar the raises and swivels are the rig's current values, zero after `zeroRigControls`, and the table above holds as written.
- **The pitch and roll are read in the space the Limb's handle hangs in (author).** A turned, tilted or scaled Layout stays in the Layout. The handle's own pitch and roll still go into `footRoll` and `bank`: a turned handle comes back level, with the turn in the channels.
- **The toe is exact only when the channels can make the pose.** The ankle always comes back exactly. The toe comes back exactly when the leftover is a pitch `toeTap` can hold. It is not when the channels put the turn on another pivot than the animator did: a raise folded into `footRoll` past the break turns the toe tip pivot instead of the ball, and a handle turned in pitch and roll puts those into `footRoll` and `bank`, which don't commute with the toe pivot. The toe is then off by about the leftover (6e-4 for a quarter degree on each axis of the handle).
- **The offset is `ModuleInverseInput::ikTargetMotion`**, the foot's motion `F`, not a context field, and the Limb's handle is `ikRest * F^-1 * ikRest^-1 * reach`. The pre-pass runs for each Limb just before it in the sweep, once the Limb's parents are solved. The full API is in D74's phase 5 entry.
- **The match (`setRigIkFkBlend`)** from FK to IK puts no pop in the ankle and keeps the foot's channels. The foot has match outputs of its own (2026-11-25, author), pulled only by the command, as the Limb's are: `fkMatchLocal` at the toe FK slot (the inverse's `localFromWorld` on the toe as it is), `ikMatchToeTap` (`solveToeTap` with the channels kept and the handle the Limb's match will place, `F^-1` times the ankle's motion) and `matchWarnings` (the IK side's: a toe turned off toeTap's pitch). The command finds the feet through `RigTraversal::getBlendFollowers` (the modules whose `ikFkBlend` the build connected from the Limb's) and writes both sides with the Limb's, in one undo step, so the toe holds both ways. Rejected: leaving the foot to a second command, which is two switches for one limb (section 9).

**Cost of the gauge.** Because the roll and bank go into `footRoll` and `bank`, a clip that was authored with `ballRaise` comes back from the joints with `footRoll` set instead. The animation plays the same, and a curve editor shows different channels. That is the price of having no sidecar to read from, and a sidecar-carrying clip keeps what it carried.

## 11. Mirroring, names, export, libraries

- **Mirror (D28).** The template's `"mirror": true` swaps the selectors' side and the `ikFkDriver` id. The file holds both sides' markers (`mirrorRigMarkers`). Mirror modes are as in section 8.
- **Names (D58).** Type `ReverseFoot`, node `reverseFootModule`, controls `<base>Foot` and `<toe usage>FK`. Markers `<role>_00_<side>_MRK`. Control markup (D25) is unique per rig: `Foot` usage and the toe's.
- **Poses and animation.** Two new controls and eleven new channel names, so nothing existing changes. A saved pose from a rig with no foot still loads on the rest. The channels are control data (D42), so they save, mirror and blend (D72: `blend` past 1 adds offset from rest, and the channels are rest-0). **The pivots matter to playback**, which is why `markersHash` is stamped on animations (`markers.md` section 5).
- **Export.** Only the toe and toe tip bind joints are exported, as today. Markers are rig-only unless `export: true`. The foot control and the pivots are not exported.
- **Rig versioning.** New module, so no `moduleVersion` step. A Limb template with no foot is unchanged.

## 12. What it adds to the maintenance surface

- A module and node (`ReverseFoot`, `reverseFootModule`), `core/FootSolve` (peel, chain, decomposition), 11 channels and 2 controls, 2 child plugs and about 9 sockets.
- **A cross-module blend source** (`ikFkDriver`): a new kind of connection, a `ModuleInterface` method and a validator rule.
- **An inverse pre-pass** in `RigInverse` (per Limb, just before it in the sweep) and, as built (D74 phase 5), `ModuleInverseContext::moduleRest`, `ModuleDescriptor::readInverseRest`, `ModuleInverseInput::ikTargetJointWorld`, `ikTargetMotion` and `ikHandleParentDelta`, and `ModuleInverseResult::channels` and `ikTargetMotion`.
- A second split-by-attribute pair on the cycle check, one more `blindSockets` entry.
- Everything in `markers.md` section 8, which this depends on.

Complexity stated and justified: the foot is the most common thing animators ask a game rig for, and the alternatives (a foot inside Limb, FK pivot controls and set-driven keys) each cost more in surprise or in code. The two-stage plug split and the pre-pass are the price of keeping Limb unaware of feet.

## 13. Limb and ReverseFoot are now one animator-facing unit

D62 rejected a combined `Limb` and `ReverseFoot` and an `Arm` and `Leg`. This design keeps them separate modules, and the foot reads the Limb's blend, its `ik` and `end` sockets, and (through the inverse pre-pass) its handle. In practice they are now a pair: **a change to Limb's blend, sockets, `ikTarget` or its inverse changes the foot.** That may come to be a reason to split Leg and Arm logic properly (a `Leg` that owns its foot, an `Arm` that owns its hand), but **not now**: the author's decision is no `Leg` module. The coupling warning goes in `limb.md`, the new headers and `CLAUDE.md`, and `test_limb_matches_parts` gets a foot-shaped case (section 15) so the pair stays checked.

## 14. Questions answered (author, 2026-11-24)

1. **Ankle entry.** None. The chain ends at the ankle joint, and nkle is not a usage in everseHierarchy.
2. **The ankle joint.** The Limb owns it. The foot drives the toe and toe tip only, and the length-preserving pull in section 7 stays.
3. **Channel names and signs.** Confirmed as the table in section 8, allRoll included.
4. **FK controls.** Only the toe's does not exist. The ankle comes from the Limb's IK or FK end, so this module adds one FK control.
5. **The inverse's gauge.** Confirmed: pitch into ootRoll, roll into ank, the handle takes yaw and position.
6. **The channel control.** Animators generally want these channels on the IK leg control, but the foot gets its own: a world-aligned control derived from the all marker (section 6). Putting the channels on the Limb's handle would need a cross-module channel, which nothing else does.
7. **A clip with no sidecar** comes back with ootRoll carrying the whole pitch. Acceptable.

**Still open:** nothing blocks sign-off. Two things to watch at build time: the sign of each pitch channel against the hand-worked pose cases (section 15), and whether the world-aligned all control is easy to select next to the foot, which only the viewport gallery shows.

## 15. Tests (D20)

- A `ModuleContract` subclass for `ReverseFoot` (controls' channels, naming, interface, golden snapshot), with a mirrored build and a -X chain case.
- **Pose cases written by hand** (never recorded from the rig): rest, each peel regime (heel, ball, toe tip), a roll exactly at the break, `ballRaise` with a roll inside the ball regime (it must not start the toe tip), bank positive and negative on both sides (the pivot swap), each swivel, `ballRoll`, `toeTap`, the ankle pushed up when the ball rises, a limb that cannot reach (the length-preserving pull), blend 0, 0.5 and 1.
- **Validation cases:** a missing pivot marker (all five are required since `reverseHierarchy` was removed, D74), `reverseHierarchy` as an unknown arg, a marker the file lacks, a marker whose joint is not in the build, `ikFkDriver` naming a missing module or a module of the wrong type, the ankle joint driven by two modules, a plug wired to a module that is not the driver.
- **Cycle:** the whole leg, cold evaluation in DG and parallel mode (extends `limb_foot`), and a foot wired to `end` for stage 1 still reports the loop.
- **Inverse:** the round trip on a hand-built rig pose (the ankle and toe come back to 1e-4), the FK-to-IK match with a raised foot and no pop, the pre-pass order, the missing-joint and sidecar-precedence cases from `inverse-solve.md`.
- `test_limb_matches_parts` gets a foot-shaped case: Limb alone, Limb with an identity foot, and Limb with a foot at rest give the same joints.
- A worked example in `docs/modules.md` with a test in `test_docs_examples.py`.

## 16. Build phases

Each ends with the build, the full suite, the docs pass and one commit that cites the decision.

| Phase | Work | Public change |
|---|---|---|
| 0 | `core/MarkerFile`, hash, tests (`markers.md`) | none |
| 1 | Markers in Maya: the five commands, `MRK` suffix, `createRigFromTemplate markers=`, resolution, the stamps and `getRigInfo`, `docs/markers.md` | commands, flag, stamps |
| 2 | `core/FootSolve`: frames, peel, chain, the decomposition. Hand-worked cases through a standalone harness first, then in the suite | none |
| 3 | `ReverseFoot` stage 1: the module, channels, the `ankle` socket into `ikTarget`, the cycle test | the module, the template key |
| 4 | Stage 2, the toe FK control, `ikFkDriver` and the blend source, the visibility output | `ikFkDriver` |
| 5 | The inverse, the pre-pass, the match through `setRigIkFkBlend` | none |
| 6 | The complex perf rig and one `bench` (D36): many feet in `foot_specs`, the fps and ratio in the commit message, and a statement when they are below target | none |
| 7 (separate gate) | Aim to markers (`markers.md` section 7), and export markers | Aim `moduleVersion` |

## 17. Docs

New: `docs/markers.md`, the `ReverseFoot` section in `docs/modules.md`. Updated: `docs/commands.md`, `docs/templates.md` (marker selectors, `ikFkDriver`), `docs/naming.md`, `docs/controls.md` (the channels), `docs/skeletons.md`, `docs/architecture.md` (the pre-pass and the blend source), `docs/source-map.md`, `docs/mayadev.md`, `design/limb.md` (the open item answered, the coupling warning), `design/README.md`, `CLAUDE.md` (the module list, node types and the coupling note), `README.md`.

**mayadev.** Additive again. A foot adds no command and changes no flag. Proposed for the author: the Asset Editor's Markers tab and the `markers` file kind (see `markers.md` section 12), and the active rig panel showing the foot's channels.
