# IK limb: design plan and build steps

**Status:** draft for sign-off, revised 2026-11-15 with the author's requirements and two answers (below). Nothing here is built, and the module has not passed the gate (D12) until the author signs off section 2. Written from the code, `decisions.md` (D1 to D46), `inverse-solve.md` and `module-directions.md`. Where those disagree, section 9 lists it.

The IK limb is the next module (author, 2026-11-10). It is IK/FK only. It is **not** a bendy limb: no twist, no bend bow, no ribbon. Section 7.4 says how those can be built on top later.

## 0. Requirements and answers (author, 2026-11-15)

1. An FK-style start control, so the IK chain can be moved from its start as well as its end.
2. An IK control and a pole vector control.
3. Optional stretch, a 0 to 1 blend.
4. Pin to the elbow, a 0 to 1 blend.
5. An IK/FK switch, a 0 to 1 blend, with no pop because FK is always up to date with IK.
6. Pole vector spaces include the IK handle and the layout.

Answers to the two design questions this raised:

- **No-pop (requirement 5): independent values and a matched switch.** FK controls hold absolute joint values, and IK controls hold IK values. A command writes the inactive side from the live joints with the module's closed-form inverse, then sets the blend (section 7.2). Setting `ikFkBlend` directly with `setAttr` does no match, like setting a `space` enum directly (D27). Rejected: FK stored as offsets on the IK result (FK values wouldn't be absolute, so FK poses and animation wouldn't transfer, and FK-to-IK would still pop).
- **Pole spaces (requirement 6): one module-owned `space` enum on the pole control, with the IK handle in it.** `rigConstraint` can't take this module's own `ik` socket as a driver, because D17 counts that as a cycle and the evaluation graph would cycle (D36). Rejected: a `poleFollow` slider next to a constraint enum, and relaxing D17 to per-slot dependencies.

## 1. Gate (D12)

1. **Animator-facing problem.** Arms and legs that animators pose by placing a hand or foot (IK) and by rotating joints (FK), that switch between the two without a pop, that can stretch or pin the elbow, and whose pole follows the world or the hand. `FkChain` gives FK only and `Aim` points one joint. Nothing solves a two-bone chain.
2. **Existing module check.** No small extension of FkChain or Aim covers it. A two-bone solve, a blend between two joint solutions and channels on controls are new, and all transform math lives in module nodes (D1).
3. **Design.** Sections 3 to 7.
4. **Maintenance surface.** Section 10.
5. **Performance rig and `bench`.** Section 8, step 14.

## 2. Decisions that need the author's sign-off

Each has a recommendation. Changing one after the module ships changes a template key, a control markup or a channel name (D33, D34).

| # | Question | Recommendation | Why it matters |
|---|---|---|---|
| 1 | Where do the IK handle, pole and settings controls get their markup and rest? | **Three marker joints in the skeleton**, selected in `moduleArgs` like Aim's `target` and `up`, marked `export: false`, `skin: false`. Rejected: derived usages with a computed pole position. | Follows Aim (D15: markup says what a joint is, the template picks joints), puts the pole where the rigger put it, invents no names. Cost: three non-export joints per limb. |
| 2 | Where do `ikFkBlend` and `stretch` live? | **A dedicated `settings` control**, channel-only, always visible. Rejected: on the IK handle. **Not yet confirmed: the requirements list no settings control.** | The IK handle is hidden in FK, so the switch would be hidden with it (priority 1, D10). The module only needs the two channels connected from some control, so the choice changes slot 5 and one marker joint and nothing else. |
| 3 | Pole spaces. | **Answered:** a module-owned `space` enum on the pole control, labels `Parent` and `Ik` (section 4.4). `Parent` is whatever the `pole` child plug's `parentStructure` socket is, `layout.end` by default. A template constraint on `pole` is a read-phase error. | Gives requirement 6 with one enum. Limit: one external space (the child plug's parent), not a list. More spaces later means more labels (append-only, D34) and extra child plugs, and `moduleArgs` keys with a `moduleVersion` step. |
| 4 | What is the blend's default? | **0 (FK), no template key.** | A default is a template key, and a new key later is a `moduleVersion` step (D33). If legs should default to IK, add `"defaultBlend"` now. |
| 5 | FK controls: which channels are keyable? | **All of FkChain's** (translate, rotate, scale), so FK can reproduce a stretched or pinned IK pose when the matched switch writes it. | Locking FK translate would leave stretch and pin unreproducible in FK, and the switch would pop. |
| 6 | Hide controls by blend? | **Yes in v1.** FK lower and end visible when blend < 1, IK handle and pole when blend > 0. The FK **start** control and the settings control are always visible (the start control moves the IK chain too). | Nothing surprising for the animator. Cost: two scalar outputs on the node (section 3.4). |
| 7 | Scope of v1. | Two-bone limb, IK/FK blend, pole, `stretch`, `pin`, pole spaces, matched switch, spaces on `start`, `ik` and `settings` through `rigConstraint`. **Not in v1:** twist joints, bend bow, ribbon, soft IK, reverse foot, spline IK. | Each is its own gate. Section 7.4 keeps them open. |
| 8 | Names. | Module type `IkLimb`, node `ikLimbModule`, MTypeId `0x00218F` (checked free 2026-11-15), channels `ikFkBlend`, `stretch`, `pin`, and `space` on the pole. | Names are public API. They follow D34: camelCase, no side or module in them, 0 at the first-named end. |
| 9 | New commands. | **Two:** `setRigIkFkBlend` and `setRigPoleSpace` (section 7.2). | The matched switch needs scene access and undo, so it is a C++ command like `setRigConstraintSpace`. |

## 3. Interface

### 3.1 JSON

```json
{
    "moduleType": "IkLimb",
    "name": "arm_l",
    "side": "Left",
    "mirror": true,
    "moduleArgs": {
        "joints": [["Left", "upperArm"], ["Left", "lowerArm"], ["Left", "hand"]],
        "ik": ["Left", "handIk"],
        "pole": ["Left", "armPole"],
        "settings": ["Left", "armSettings"]
    },
    "parentStructure": {
        "start": "clavicle_l.end",
        "ik": "layout.end",
        "pole": "layout.end",
        "settings": "clavicle_l.end"
    }
}
```

- `moduleArgs`: `joints` is exactly three selectors, root first. `ik`, `pole` and `settings` are one selector each. All four are required, like Aim (`readModuleArgs` takes them as `knownKeys`). `moduleVersion` is 1 and `argUpdates` is empty.
- `ik` sits at the end joint (position and orientation, normally equal to the hand joint's). `pole` sits out in front of the elbow or knee. `settings` sits anywhere the animator will find it.
- Constraints may target `start`, `ik` and `settings` (D31, `rigConstraint` gives each its `space` enum, with D27 matching). **A constraint on `pole` is an error**, because the pole owns its `space` enum (section 4.4).
- **No child plug may be parented to this module's own sockets** (D17 cycle rule). Fingers parent to `arm_l.end`, and `arm_l.ik` is not a valid `pole` parent. The pole's IK handle space is inside the module for that reason.
- Mirroring needs nothing: `expandMirrors` swaps every `[side, usage]` selector in `moduleArgs`, as it already does for Aim. `planFromJson` has no mirror branch (D28).

### 3.2 Slots, controls, joints, plugs, sockets

One slot index is shared by `controlInputData`, `controlOPM`, `jointOPM`, `outputSocketMatrix`, `childPlugs` and `socketNames`, like `AimPart` in `modules/AimModule.h`. A `namespace IkLimbSlot` in the header names them.

| Slot | Name | Control | Joint driven | Child plug | Sockets |
|---|---|---|---|---|---|
| 0 | `upper` | FK **start control**, markup of `joints[0]` | `joints[0]` | `start` | `index_0`, `start` |
| 1 | `lower` | FK, markup of `joints[1]` | `joints[1]` | none | `index_1` |
| 2 | `end` | FK, markup of `joints[2]` | `joints[2]` | none | `index_2`, `end` |
| 3 | `ik` | IK handle, markup of the `ik` joint | the `ik` joint | `ik` | `ik` |
| 4 | `pole` | pole, markup of the `pole` joint | the `pole` joint | `pole` | `pole` |
| 5 | `settings` | settings, markup of the `settings` joint | the `settings` joint | `settings` | `settings` |

- `ModuleInterface::childPlugs` is `start`, `ik`, `pole`, `settings`, each with a control (`ModulePlugInfo::hasControl`). `sockets` is the names above. `controls` is the six markups in slot order. `PlugAndSocketNames` gets `Ik`, `Pole`, `Settings`.
- **`ModulePlugInfo` gets one new field**, `allowsConstraint` (default true), false for `pole`. `RigTemplateValidation` reports a constraint on a plug that doesn't allow one, in the read phase. Without it the build would fail late: `createConstraint` refuses a control that already has `space` (`Constraints.cpp`), after the scene has changed (D17).
- **Sockets `index_0` to `index_2` are the blended joint worlds, not the FK controls' worlds.** A hand's fingers must follow the joint whichever mode is active. In FkChain the control world and joint world are the same, so this is new.
- **Those three sockets have no DAG transform** (`socketInfo.oTransform` null, as Aim does for its joint). `reparentChildPlugToSocket` DAG-parents a child module's control under the socket's control, and visibility inherits through the DAG even though transforms don't (D1). A finger control under a hidden FK hand control would be hidden in IK. The `ik`, `pole` and `settings` sockets keep their control.
- **DAG organization:** FK lower under FK upper, FK end under FK lower. `ik`, `pole` and `settings` are not under the FK controls, for the same visibility reason.
- Rotate orders: FK controls XYZ (D40), IK handle XYZ. Pole and settings pass XYZ too (their rotation isn't a channel). Every control passes its order to `createRigControl`.

### 3.3 Channels (D34)

| Control | Keyable channels | Locked and hidden |
|---|---|---|
| FK upper (start), lower, end | translate, rotate, scale, as FkChain | none |
| `ik` | translate, rotate, `space` when constrained | scale |
| `pole` | translate, `pin`, `space` (the module's own: `Parent`, `Ik`) | rotate, scale |
| `settings` | `ikFkBlend`, `stretch`, `space` when constrained | translate, rotate, scale |

- `ikFkBlend`: double 0 to 1, default 0, 0 is FK. `stretch`: double 0 to 1, default 0 (0 clamps at full reach, 1 stretches). `pin`: double 0 to 1, default 0 (0 free, 1 the elbow sits on the pole). The pole's `space`: enum `Parent` (0), `Ik` (1), default 0, append-only labels.
- All four are `mirrorCopy`, declared through `RigPoseScene::setMirrorMode` before `addAttribute`. Defaults equal rests (D34). `ModuleContract` runs `getRigControlChannels` on every control, so a channel with no mirror category fails.
- The three new names go in `docs/controls.md` the first time (D34 naming rule). `space` on the pole is the same concept and the same name as the constraint's.
- Channel names are constants in `RigAttr` (`IkFkBlend`, `Stretch`, `Pin`), used for the control attribute and the module input attribute (D7).

### 3.4 Module attributes

New on the node. None are arrays: a limb has one of each.

- Inputs: `ikFkBlend`, `stretch`, `pin` (doubles) and `poleSpace` (short), each connected from the owning control's attribute in `createModule`, in the same modifier.
- Stored at build: `ikRestOffset` (a matrix array of 3, section 4.3), `restBendNormal` (a vector, the plane fallback in 4.2), `limbLengths` (the two rest bone lengths). Same pattern as `aimRestOffset`.
- Outputs: the shared `controlOPM`, `jointOPM`, `outputSocketMatrix`, plus `fkVisible` and `ikVisible` (doubles, not writable, not storable), connected to the controls' `visibility` in `createModule`.
- `declareStandardAffects()` covers the shared inputs. Every new attribute gets `attributeAffects` to every output it changes: `ikFkBlend` to every output, `stretch`, `pin`, `ikRestOffset`, `restBendNormal` and `limbLengths` to `jointOPM` and `outputSocketMatrix`, `poleSpace` to the pole's `controlOPM` and everything the pole feeds, and `ikFkBlend` to `fkVisible` and `ikVisible`. Missing `attributeAffects` is the common silent bug (CLAUDE.md).
- `compute()` reads plugs through the module's own `MObject` attributes, never by name (D36).

## 4. Evaluation

One `compute()` solves everything once and writes every output array (D36, `commitOutput`). Inputs are world-space matrices and deltas (D3).

### 4.1 Per frame

```
D_start        = motion of the `start` child plug's parent since rest
FK chain       : the shared chain walk over controls 0..2
                   fkWorld[i] = fkLocal[i] * fkRestWorld[i] * parentDelta_i
IK handle      : ikWorld    = ikLocal   * ikRest   * D_ik         (child plug `ik`)
Pole space     : D_pole     = poleSpace == Parent ? D_parentPlug : motion of ikWorld since rest   (4.4)
Pole           : poleWorld  = poleLocal * poleRest * D_pole
IK root        : rootPos    = translation of fkWorld[0]          (the start control moves the IK chain)
IK solve       : positions and plane from (rootPos, ikWorld, poleWorld, lengths, stretch, pin)    (4.2)
IK joint worlds: ikJointWorld[i] = ikRestOffset[i] * frame_i                                       (4.3)
Blend          : jointWorld[i] = blend(fkWorld[i], ikJointWorld[i], ikFkBlend)                     (4.5)
Outputs        : jointOPM[i] = socket[i] = jointWorld[i] for i in 0..2
                 jointOPM[3..5] = socket[3..5] = the marker controls' own worlds
                 controlOPM[i] = control i's rest carried by its own parent delta (excludes the local)
                 fkVisible, ikVisible = see decision 6
```

The IK root takes the start control's **translation** only. Its rotation turns the FK upper joint and does nothing to the IK chain, because the IK plane comes from the pole. Say this in `docs/modules.md`.

Blend at exactly 0 returns the FK worlds and at exactly 1 the IK worlds, with no float residue and without running the unused solve. This matters for animators who keyed 0 or 1 and for the inverse round trip.

### 4.2 Two-bone solve with stretch and pin

`MathUtils::computeTwoBoneIk` in `maya/shared/Utilities.h`, next to `computeAimMatrix`. It is shareable (a leg, a ribbon module or a future module can call it), so it goes in the shared layer when written (CLAUDE.md, "Look before you write", step 6). It takes `rootPos`, `handlePos`, `polePos`, `L1`, `L2`, `stretch`, `pin` and the rest bend normal, and returns the three positions and the plane normal.

1. Reach `d = |handle - root|`, axis `a = normalize(handle - root)`.
2. Stretch scale `s = lerp(1, max(1, d / (L1 + L2)), stretch)`. Bone lengths `L1s = L1*s` and `L2s = L2*s`.
3. Clamp `d` to `[|L1s - L2s| + eps, L1s + L2s]`, call it `dc`. A handle past full reach with `stretch = 0` leaves the end at full reach on the line to the handle, like any native IK.
4. Plane normal `n = normalize(pole - root - a * dot(pole - root, a))`. If its length is below 1e-4 (pole on the line), use `restBendNormal` projected perpendicular to `a`, then any perpendicular. The solver is **stateless** (D36), so a pole crossing the line flips the limb the way a native IK does. Say so in `docs/modules.md`.
5. Law of cosines: `x = (dc^2 + L1s^2 - L2s^2) / (2 dc)`, `h = sqrt(max(0, L1s^2 - x^2))`. `midFree = root + a*x + n*h`, `endFree = root + a*dc`.
6. **Pin:** `mid = lerp(midFree, polePos, pin)` and `end = lerp(endFree, handlePos, pin)`. At `pin = 1` the elbow sits on the pole control and the wrist on the handle, so both bones lengthen or shorten to fit. Pin wins over stretch. The pole lies in the plane of root, handle and pole, so the plane is unchanged.

Hand-worked checks for pose cases: `L1 = 3`, `L2 = 4`, root `(0,0,0)`.
- Handle `(5,0,0)`, pole toward `+Y`: `x = (25+9-16)/10 = 1.8`, `h = sqrt(9-3.24) = 2.4`, `mid = (1.8, 2.4, 0)`.
- Handle `(0,5,0)`, pole toward `+X`: `mid = (2.4, 1.8, 0)`.
- Handle `(10,0,0)`: stretch 0 gives end `(7,0,0)` and mid `(3,0,0)`. Stretch 1 gives end `(10,0,0)` and mid `(30/7,0,0)`.
- Handle `(5,0,0)`, pole at `(2,3,0)`: pin 1 gives mid `(2,3,0)` and end `(5,0,0)`. Pin 0.5 gives mid `(1.9, 2.7, 0)`.

### 4.3 Joint frames and rest offsets

Joint rest orientations are arbitrary, so no axis assumption is made (D40's +X down the bone is a default for FK controls only). Each IK joint is a *frame* built from the solved positions, times a stored rest offset.

- Root frame: X toward `mid`, with the plane containing X and the pole side. Mid frame: X toward `end`, same plane. End joint: translation from the solved end, **rotation from the IK handle's world** (rotating the handle turns the hand), scale ignored.
- One frame builder, used by `createModule` (to compute the rest offsets), `compute()` and `inverse()`, so they can't drift (D41, P3). It is `computeAimJointWorld` and `computeAimRestOffset` again for three joints: `ikRestOffset[i] = jointRest[i] * frameAtRest[i]^-1`.
- At rest the IK joints equal the joint rests. The rest bend must point to the pole's side of the root-end line, or IK would flip the bend at rest, so `planFromJson` fails if it doesn't (section 5). A straight rest limb is allowed: the pole then only sets the plane.

### 4.4 Pole space

`poleSpace` is the pole control's own `space` enum, read by the module (`Parent` = 0, `Ik` = 1).

- `Parent`: the pole's delta is the `pole` child plug's parent motion, as for any control. It is `layout.end` unless `parentStructure` says otherwise.
- `Ik`: the pole's delta is the motion of the IK handle's world since rest, `computeDeltaFromRest(ikRest, ikWorld)`.
- This reads one slot's solved world as another slot's input, inside one node. It is not a graph cycle, because the IK handle depends only on the `ik` plug and its own control. It fixes the slot order in `compute()` and in the inverse's forward pass: solve slot 3 before slot 4.
- Switching the enum directly moves the pole, as with a `rigConstraint` enum (D27). `setRigPoleSpace` matches (section 7.2).

### 4.5 Blend math

Translation lerps, rotation slerps along the shorter arc, scale lerps. Decompose with `MTransformationMatrix` and rebuild. No heap allocation is expected, but check when building, because `compute()` stays allocation-free (D36, CLAUDE.md). `MathUtils::blendMatrices` goes in the shared layer for the same reason as the solver.

### 4.6 Failure modes

| Case | Result |
|---|---|
| Handle on the root | `dc` clamps to `\|L1s - L2s\| + eps`, so the limb folds, no NaN. |
| Pole on the line | Rest bend normal, then any perpendicular (4.2). |
| Zero-length rest bone | Plan error (section 5). |
| Blend, stretch, pin outside 0 to 1 | The attributes are clamped (min 0, max 1). |
| Pole `space` set directly | The pole moves, no match. Use `setRigPoleSpace`. |

## 5. Read-phase validation (`planFromJson`)

All errors go to `ctx.report` with the JSON path, and every selector is claimed even after a failure so the report lists all of them (the FkChain and Aim pattern).

1. `joints` has exactly three selectors. `ik`, `pole`, `settings` each resolve to one joint, and none of the six is claimed twice (`claimJoint` covers it).
2. Rest bone lengths `L1` and `L2` are above a minimum (as `kMinAimDistance`).
3. The pole joint is not on the root-end line (as `kMinAimUpSine`), and it is on the same side of that line as the mid joint, or the rest limb is straight within tolerance.
4. The `ik` joint is within a tolerance of the end joint's position. An offset handle is a separate feature: error for now.
5. Shared control markups are caught by `validateControlMarkup`. `checkMirrorPositions` works through `claimJoint` for all six.
6. In `core/`, `RigTemplateValidation`: a constraint whose target plug has `allowsConstraint == false` is an error (section 3.2).

## 6. Inverse (D41, `inverse-solve.md` section 8)

`IkLimbNode::inverse` takes world matrices only, by slot, and returns control local matrices (`ModuleInverseInput`, `ModuleInverseResult`).

1. **FK controls** from `jointWorld[0..2]` with `ModuleInverse::solveChain` on three slots with `useJoints = true`. Translate, rotate and scale are all derived, so a stretched pose is reproduced in FK.
2. **IK handle** (slot 3): the end joint's world, then `ModuleInverse::localFromWorld` with the `ik` parent delta.
3. **Pole** (slot 4): the point on the root-end line nearest the mid joint, plus the unit perpendicular from that point to the mid joint, times the rest pole distance, then `localFromWorld` with the delta of the current `poleSpace`. Rotation stays at rest. With `pin = 1` the pole should sit at the mid joint: the live match (section 7.2) puts it there when `pin` is above 0.
4. **Settings** (slot 5): no joints. Transform from the fallback (the sidecar, or rest), as Layout does.
5. **Marker joints** (slots 3 to 5) are `export: false`, so the source skeleton normally lacks them. The IK inverse derives its controls from slots 0 to 2 and ignores `jointWorld[3..5]` unless the source has them.
6. `socketWorld` returns the forward result for every slot, so the sweep feeds modules below.
7. **`solved = false`** when any limb joint is missing, with a reason. The driver then takes the controls from the sidecar, then rest.
8. **Channels from joints:** transforms yes. `ikFkBlend`, `pin`, `space` and `stretch` come from the sidecar, never derived. (`inverse-solve.md` requirement 3 said `stretch` is derivable from the length ratio. It isn't reliably, because below full reach the joints can't tell stretch 0 from 1, and with `pin` the bone lengths mean something else. See section 9.)
9. **A source whose bone lengths differ from the rest** is reproduced exactly by FK. IK reproduces it only with `stretch` or `pin` on, so the restore warns when IK can't match the joints.

Both sides are solved from joints at any blend, which is what the matched switch uses.

## 7. Library, commands and Python

### 7.1 What works with no change

Every item keys on control markup and channel name, so a limb is in them from the first build. Each needs a test (section 8, step 11) and a docs line.

- `getRigControls(moduleType=["IkLimb"])`, `getRigControlInfo`, `getRigControlChannels` (the new channels with rests, ranges and mirror modes), `getRigMirrorControl`.
- `zeroRigControls` (every channel to its rest: FK, blend 0, stretch 0, pin 0, pole space `Parent`), `mirrorRigPose`, `flipRigPose` (copy for all new channels, D34).
- `saveRigPose` and `loadRigPose`, `saveRigAnimation` and `loadRigAnimation` (D34, D42), with `mirror` and `offset`. `ikFkBlend`, `stretch` and `pin` are floats on default tangents. `space` keys stepped.
- Control shape save, load and mirror for the six controls (D26).
- `createRigConstraint` and `setRigConstraintSpace` on `start`, `ik` and `settings`, and template `constraints` entries.
- `rigmodules.pose.key_controls` and `key_modules`, which read `get_channels`.
- Selection sets (D39), display layers (D38) and `rig_all` (D43): the build makes them for any module.
- `animateRigFromJointClip` and `poseRigFromJointPose`, through the inverse.

Shape choices: the shape type comes from the module's `shapeType`, one for all six controls, like every module (the shape file lets a rigger edit them per asset, D26).

### 7.2 The matched switch: two new commands (D14 shape, `commands/IkLimbCmds.h/.cpp`)

**`setRigIkFkBlend(control | module, blend=1.0)`** (the settings control or the module node; objects by name, never the selection). It matches both sides to the limb's live joints, then sets `ikFkBlend`:

1. Read the limb's three joint worlds as they are now, and each child plug's live parent delta from the scene.
2. Run `IkLimbNode::inverse` on them. It returns FK locals, the IK handle's local and the pole's local (section 6).
3. Convert each local to channel values with the driver's matrix-to-channels conversion (euler nearest the current values, the control's own rotate order), and write them through `RigPoseScene`. Locked or driven channels keep their values and warn, as in D27 and D41.
4. Set `ikFkBlend` to `blend`.

Both sides reproduce the same pose, so the result is the same pose at any new blend, and the switch can't pop in either direction. A pose that IK can't reproduce (see section 6, item 9) warns. The command sets no keys. A keyed switch (key the frame before, switch, key the frame) is a tool in mayadev's pose menu on top of this command, like the keyed space switch D34 lists. Undoable as one step.

**`setRigPoleSpace(pole, space=n | label=l)`** switches the pole's `space` and keeps its world matrix, by D27's formula: `L' = L * OPM * D_new^-1 * D_old * OPM^-1`. `D_old` and `D_new` are the two deltas of section 4.4, read from the scene. Undoable. Only the pole takes it, and a direct `setAttr` on the enum doesn't match.

**Shared machinery, not new logic per command:**
- The driver's matrix-to-channels conversion (in `RigInverse.cpp` today, private to the driver) moves to `RigPoseScene` or a shared helper, so a command can use it. `RigInverse::solve` keeps calling it.
- D27's match formula is already in `RigConstraintNode::matchSwitchSpace`. Factor the formula (new local from old local, old and new delta, the control's OPM) into one shared function and call it from both. Check `matchSwitchSpace`'s other callers first.
- A live single-module solve: `RigInverse` gets a function that takes a module node and reads its joint worlds and parent deltas from the scene. It needs no whole-rig sweep, which is why it can't be done by writing a partial JointClip: the sweep would take parent sockets from rest, and the parent deltas would be wrong.

### 7.3 Python

No Python is required. `rigmodules.query` already reads the new controls and channels. A wrapper for the two commands is not needed, they are `cmds`. mayadev's pose menu gets the keyed switch later (D24: mayadev depends on RigModules).

### 7.4 Later: bendy limb, ribbon, twist, bend bow

Not built, and nothing in v1 closes the door.

- **Recommended shape: a separate module** that reads this module's sockets (`index_0` to `index_2`, the blended joint worlds), and `settings` if it needs a channel, and drives a ribbon chain with distributed twist and bend bow. The blended joints are already the single source of the limb's pose in both modes, so a ribbon module doesn't care how they were made. The IK solver and blend are in `MathUtils`, so it can reuse them.
- **If it becomes one mega module** (the author has seen both), the limb gains optional `moduleArgs` keys. A new optional key is a `moduleVersion` step with a default (D33), and new channels (`twist`, `bendBow`) are appended with their mirror mode. Existing names don't change.
- To keep that cheap: the three socket families, the six slots and the channel names above are stable, and slot numbers are never reused.

## 8. Build order

Each step ends with the full suite on a local build. Cloud sessions can do the Maya-free parts and docs (CLAUDE.md, "Environments"): steps 1, 2, 3, 11 (fixtures) and 15.

1. **Sign off** section 2 (decisions 1, 2, 4, 5, 6 and 7 are still open). Then add `D47. IK limb` to `design/decisions.md` (the rejected alternatives from section 0 and 2, the consequence), replace "The next module is the IK limb" in "Open items", update `inverse-solve.md`'s status line, and promote this page to `design/ik-limb.md`.
2. **Hand-worked pose cases first** (`tests/maya/fixtures/poses/ikLimb/*.json`) and validation cases (`fixtures/validation/`), before any code, from the numbers in 4.2. Add the skeletons (`fixtures/skeletons/ik_limb.json`, `ik_limb_mirror.json`: a bent limb, the pole on the bend side, three non-export marker joints) and templates.
3. **Constants.** `core/Constants.h`: `RigAttr::IkFkBlend`, `Stretch`, `Pin`, `PoleSpace`, `IkRestOffset`, `RestBendNormal`, `LimbLengths`, `FkVisible`, `IkVisible`, and `PlugAndSocketNames::Ik`, `Pole`, `Settings`.
4. **`core/` validation.** `ModulePlugInfo::allowsConstraint`, the constraint check in `RigTemplateValidation`, and its validation case.
5. **Shared math.** `MathUtils::computeTwoBoneIk` and `MathUtils::blendMatrices` in `maya/shared/Utilities.h/.cpp`. Callers: none yet.
6. **Shared custom channels.** The first module with them. One shared helper that adds a keyable double (or enum) channel to a control with a default, a range and a mirror mode, connects it to a module attribute, and queues the locks and hides for the other channels. Look at the `space` enum in `Constraints.cpp` and at `DagUtils::queueLockAndHideTransform` first and extend them: give `queueLockAndHideTransform` an optional list of channels to keep instead of writing a second version. State in the summary what was reused and what is new.
7. **Module files.** `maya/modules/IkLimbModule.h/.cpp` from FkChain and Aim: `IkLimbSlot`, `IkLimbArgs` with `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(IkLimbArgs, joints, ik, pole, settings)`, `creator`, `initialize` (create, add and `attributeAffects` together for each attribute), `compute` (one solve, `commitOutput` for each array), `createModule`, `planFromJson` (section 5), `inverse` (section 6) and the file-local frame builder.
8. **Register.** One row in `ModuleRegistry.cpp`: `"ikLimbModule", "IkLimb", IkLimbNode::id, creator, initialize, {}, planFromJson, inverse, true`. Add every new file to `cpp/RigModules.vcxproj` and `.filters`. Add the type to `tests/maya/concepts/test_plugin.py` and the MTypeId to `CLAUDE.md`.
9. **Commands.** The shared machinery of 7.2, then `commands/IkLimbCmds.h/.cpp` (`setRigIkFkBlend`, `setRigPoleSpace`), a row in `CommandRegistry.cpp`, the project files, and the long flag names in `concepts/test_plugin.py`. Each `addFlag` in `CHECK_MSTATUS`.
10. **Rotate order and locks.** The build locks rotate order on every control (D40). Locks and hides per section 3.3.
11. **Tests (tier 4, `modules/test_ik_limb.py`; commands in tier 5).**
    - `IkLimbContract(ModuleContract, RigTestCase)` with `JOINT_OWNERS` for all six joints, `MIRROR_TEMPLATE` and `MIRROR_SKELETON`. It gives the build, rest, drive, no-inherit, undo/redo, one-solve, mirrored channels and **inverse round trip** tests.
    - `add_pose_cases(IkLimbPoses, "ikLimb")`: rest; handle moved; pole moved to either side; the start control translated in IK (the chain follows); unreachable with and without stretch; pin 0.5 and 1; blend 0, 0.5 and 1; pole `Ik` space with a handle move; a rotated parent; a mirrored limb.
    - Behaviour tests: blend 0 is exactly FK and 1 exactly IK. Blend 0.5 is the lerp and slerp of the two. The hand follows the handle's rotation. Visibility by blend, with the start control always visible. Sockets `index_*` follow the blended joints, and a child module's controls stay visible at blend 1. A pole on the line takes the fallback plane. A constraint on `pole` fails validation. `start`, `ik` and `settings` take `rigConstraint` spaces.
    - Command tests (`commands/test_ik_limb_cmds.py`): `setRigIkFkBlend` from FK to IK and back leaves every joint where it was, at rest, posed in each mode, with stretch and with pin, with a space switch on `ik`; a warning when IK can't match; locked channels warn; undo and redo; scopes and every error. `setRigPoleSpace` keeps the pole's world matrix in both directions and undoes.
    - Inverse tests: the round trip at blends 0, 0.5 and 1, a missing joint, a sidecar carrying `ikFkBlend`, a stretched source.
    - Validation cases: pole on the line, pole on the wrong side, `ik` far from the end joint, a joint claimed twice, a zero-length bone, a wrong joint count, a `parentStructure` value that names the limb's own socket (cycle), a constraint on `pole`.
    - Golden snapshot `golden/ik_limb.json` from `--update-golden`, reviewed. Assign every new test file a tier in `TIERS` in `tests/run_tests.py`. Every check a probe made becomes a suite test or a listed proposed test (CLAUDE.md).
12. **Docs example test** in `tests/maya/concepts/test_docs_examples.py` for any new worked example.
13. **Build and run.** MSBuild, then `cmd /c tests\RigTests.cmd`. Report the TOTAL line and any failed, crashed or blocked tier. Needs local build and Maya verification, so the cloud session can't do this step.
14. **Performance rig and `bench` (D36).** Add `limb_specs()` to `tools/perf/perf_scenes.py` beside `chain_specs()` and `aim_specs()` (about 12 limbs: arms and legs of the existing skeleton, three marker joints each), extend the skeleton, template and instance builders and `check_perf_scene.py`, run `mayapy tools\perf\perf_scenes.py check`, then `bench` on the 20-rig scene (30 fps target) and the 50-rig scene (within 1.5 times the native floor). Report the fps, and say so when below target. The rig grows, so search the docs and D36 for "200 joint" and "51 constraints" and fix them. The native floor has no tool, so that comparison is manual.
15. **Docs pass** (CLAUDE.md, "Working style"). Update:
    - `docs/modules.md`: an `IkLimb` section (JSON, args, node table, controls, child plugs, sockets, channels, start control, stretch, pin, pole spaces, the pole-on-line behaviour, the own-socket rule).
    - `docs/templates.md`: the `IkLimb` args and a mirrored limb example.
    - `docs/controls.md`: `ikFkBlend`, `stretch`, `pin`, and the markup pattern of marker joints.
    - `docs/commands.md`: `setRigIkFkBlend`, `setRigPoleSpace`, and the `IkLimb` `moduleType` filter value.
    - `docs/library.md`: what a limb's pose and animation hold, and that a raw `ikFkBlend` change doesn't match.
    - `docs/architecture.md`: the module table row, the slot table, the shared helpers, the commands table, the recipe notes for custom channels.
    - `README.md`: the modules table and command list.
    - `CLAUDE.md`: repo map rows (`modules/IkLimbModule`, `commands/IkLimbCmds`, the new shared functions), the Maya commands list, the node types and MTypeIds list (`ikLimbModule` `0x00218F`), and the "Existing modules" line.
    - `design/decisions.md` (D47, Open items), `design/inverse-solve.md` (status and section 8), `design/module-directions.md` (the IK limb is built), `design/control-library.md` (its build order), `design/mayadev-integration.md` (IK/FK match rows).
    - `tests/README.md`: the new test files in the tier table.
    - Search every page for `IK`, `ik limb` and the perf rig's joint and constraint counts, and fix stale mentions.
16. **Commit** in the repo's long form: imperative summary, bullets by area (core, plugin, tests, docs, tools), the rejected alternatives and `D47`, the public API (module type, node, channels, markups, child plugs, sockets, commands), the impact on saved poses and animation (none, the module is new), and what was verified and what was not.

## 9. Discrepancies and risks found while reading

- **`inverse-solve.md` requirement 3** says `stretch` is derivable from the length ratio. It isn't reliably (section 6, item 8). Update that page when this is built.
- **D31 and D17 against requirement 6.** D31 says the IK module exposes the pole as a child plug that gets spaces through `rigConstraint`. That works for other modules' sockets and not for the IK handle, which D17 forbids. Resolved by the module-owned enum, at the cost of one external space and a new `allowsConstraint` flag.
- **Raw `ikFkBlend` writes pop.** Only `setRigIkFkBlend` matches. The same is true of `space` today (D27). Animators who key the blend by hand lose the match, so the keyed switch tool matters (mayadev).
- **`docs/architecture.md` recipe** lists four pieces for a module and says nothing about custom channels, hidden controls, marker joints or module-owned enums. Update it with this module.
- **Visibility inheritance.** D1 turns transforms off in the DAG and leaves visibility inherited. Hiding FK controls by blend would hide anything DAG-parented under them. Section 3.2 avoids it with null socket transforms. The contract test must check this.
- **`hasControl` is also a build check.** `RigModule.cpp` requires a plan's `hasControl` to equal whether the built plug has a control, so `allowsConstraint` is a separate field and `hasControl` stays true for `pole`.
- **Compute cost.** Three blended joints plus an IK solve cost more than a chain solve. D36's `rigControlNode` premium dominates, but run `bench` before assuming anything.

## 10. Maintenance surface added

- One module node, its args struct and its inverse, one registry row.
- Four channel names (`ikFkBlend`, `stretch`, `pin`, and `space` on the pole with two labels), four child plug names, two socket families, three marker-joint markups per limb.
- Two commands (`setRigIkFkBlend`, `setRigPoleSpace`).
- Constants in `RigAttr` and `PlugAndSocketNames`, and `ModulePlugInfo::allowsConstraint`.
- `MathUtils::computeTwoBoneIk` and `blendMatrices`, the shared custom-channel helper, the shared match formula, the shared matrix-to-channels conversion and a live single-module inverse.
- One golden snapshot, a pose case folder, two test files and fixtures, and a block in the performance rig.

Complexity stated (CLAUDE.md, "Complexity checks"): a new attribute set on a module (the first custom channels, the first scalar inputs, the first module-owned enum), a visibility output, three marker joints per limb, a new plug flag in `core/`, and two commands. Rejected as simpler: an IK-only two-bone solve with no blend (the problem is the switch), and a `poleFollow` slider (two controls for one idea, which the author declined).
