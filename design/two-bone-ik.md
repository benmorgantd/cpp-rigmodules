# Two-bone IK: design and build phases

**Status:** Decided, D52 (author, 2026-11-15). Signed off on every section 2 question in two rounds the same day. Nothing is built. Drafted on the cloud branch `ccr-a13423e5-wb8f9h` as `two-bone-ik-plan.md` from D1 to D46, then resolved against main at `6da3e27` (D1 to D51) for this page. Section 9 lists what changed in that pass. Section 11 records the third round of answers, which closed the last questions.

The IK limb is the next module (author, 2026-11-10). **It is a two-bone IK limb: exactly three joints (root, mid, end), for arms and legs.** One-bone IK is the existing `Aim` module (a joint aimed at a target with an up control), so this module doesn't repeat it. Three or more IK bones is a different solve (a chain solver such as CCD or FABRIK, with no closed form, no single pole plane and no analytic inverse), and spline IK is another again. Both would be their own modules through the gate (D12) and share nothing with this solver except the blend and the matched-switch pattern. The module is IK/FK only. It is **not** a bendy limb: no twist, no bend bow, no ribbon. Section 7.4 says how those can be built on top later.

## 0. Requirements and answers (author, 2026-11-15)

1. An FK-style start control, so the IK chain can be moved from its start as well as its end.
2. An IK control and a pole vector control.
3. Optional stretch, a 0 to 1 blend.
4. Pin to the elbow, a 0 to 1 blend.
5. An IK/FK switch, a 0 to 1 blend, with no pop because FK is always up to date with IK.
6. Pole vector spaces include the IK handle and the layout.
7. Soft IK in v1.
8. A `roll` attribute that rolls the IK plane.

Answers and confirmations:

- **No-pop (requirement 5): independent values, matched values published by the node, and a command that copies them.** FK controls hold absolute joint values, and IK controls hold IK values. The node also computes, on request, the control values each side would need to reproduce the current pose (the module's closed-form inverse, section 4.1), so FK is always matched to IK and the other way round, as data. `setRigIkFkBlend` writes them to the controls and sets the blend (section 7.2). Setting `ikFkBlend` directly with `setAttr` does no match, like setting a `space` enum directly (D27).
- **Why the blend alone doesn't match** (author asked). A blend interpolates the two chains' *poses*. It never changes the *values* stored on the inactive side's controls, and `compute()` can't write plugs. So when the blend flips from 1 to 0 the joints jump to whatever the stale FK values describe. For the controls to follow the active side inside `compute()`, their values would have to mean *offsets from the other chain's pose*. That works in one direction (FK as a layer on IK), and then FK values aren't absolute joint rotations: an arm waving in FK while the body moves would drag against a world-space IK handle, and FK poses and animation stop transferring (D42). In the other direction the two chains depend on each other, a cycle inside the node. Rejected for those reasons.
- **The settings control** exists, controls hide by blend (decision 6), and **every limb defaults to IK (`ikFkBlend` 1)**, with no template key.
- **Soft IK ships in v1** (decision 10, second round).
- **Roll** (requirement 8): a `roll` channel on the IK handle that rotates the IK plane about the root-to-handle axis (section 4.2). An angle, default 0, `mirrorNegate`.
- **Pole spaces (requirement 6): a module-owned space** on the pole control, with the IK handle in it. `rigConstraint` can't take this module's own `ik` socket as a driver, because D17 counts that as a cycle and the evaluation graph would cycle (D36). Rejected: a `poleFollow` slider next to a constraint enum, and relaxing D17 to per-slot dependencies.
- **Module-owned spaces are a general concept, not a pole special case** (second round). A module-owned space is a `space` enum on a control that the module itself reads. It differs from a `rigConstraint` space in two ways: its labels are static (the module fixes them, the template can't add drivers), and it needs its own `attributeAffects` inside the module. `getRigControlSpace` reports it, and one switch command matches it (section 4.4, section 7.2).
- **The start control's rotation does nothing to the IK chain** (second round). The IK root takes the start control's translation only.
- **The settings control follows the space of the module's first child plug's parent** (second round): the chest for an arm, the pelvis for a leg. It follows the `parentStructure` socket only, never a constraint on `start` (third round). The template can't name a parent for it (section 3.2).
- **The inverse's scalar channels are a vector like the rest of the contract** (third round, section 6). The second round said fixed-size. That was withdrawn because only the channels could be fixed-size: the matrices and the result would still allocate.
- **One commit per phase** (second round, section 8).

## 1. Gate (D12)

1. **Animator-facing problem.** Two-bone limbs (arms and legs) that animators pose by placing a hand or foot (IK) and by rotating joints (FK), that switch between the two without a pop, that can stretch, soften or pin the elbow, and whose pole follows the world or the hand. `FkChain` gives FK only and `Aim` points one joint. Nothing solves a two-bone chain.
2. **Existing module check.** No small extension of FkChain or Aim covers it. A two-bone solve, a blend between two joint solutions and channels on controls are new, and all transform math lives in module nodes (D1).
3. **Design.** Sections 3 to 7.
4. **Maintenance surface.** Section 10.
5. **Performance rig and `bench`.** Section 8, phase 8.

## 2. Decisions (all answered, author, 2026-11-15)

Changing one after the module ships changes a template key, a control markup or a channel name (D33, D34).

| # | Question | Answer | Why it matters |
|---|---|---|---|
| 1 | Where do the IK handle, pole and settings controls get their markup and rest? | **Three non-export marker joints in the skeleton** (`ik`, `pole`, `settings`), selected in `moduleArgs` like Aim's `target` and `up`, marked `export: false`, `skin: false`. Rejected: derived usages with a computed pole position. | Follows Aim (D15: markup says what a joint is, the template picks joints), puts the pole where the rigger put it, invents no names. Cost: three non-export joints per limb. |
| 2 | Where do `ikFkBlend`, `stretch` and `soft` live? | A dedicated `settings` control, channel-only, always visible. | The IK handle is hidden in FK, so the switch would be hidden with it (priority 1). |
| 3 | Pole spaces. | A module-owned space on the pole control, labels `Parent` and `Ik` (section 4.4). `Parent` is whatever the `pole` child plug's `parentStructure` socket is, `layout.end` by default. A template constraint on `pole` is a read-phase error. | Limit: one external space (the child plug's parent), not a list. More spaces later means more labels (append-only, D34), extra child plugs and `moduleArgs` keys with a `moduleVersion` step. |
| 4 | The blend's default. | **1 (IK) for every limb, no template key.** | Default equals rest (D34), so a built rig, `zeroRigControls` and the contract's rest pose are all in IK, with the FK lower and end controls hidden. FK pose cases set `ikFkBlend` to 0. |
| 5 | FK controls: which channels are keyable? | **All of FkChain's** (translate, rotate, scale). | Locking FK translate would leave stretch and pin unreproducible in FK, and the switch would pop. |
| 6 | Hide controls by blend? | **Yes.** FK lower and end visible when blend < 1, IK handle and pole when blend > 0. The FK start control and the settings control are always visible. | Cost: two scalar outputs on the node (section 3.4). |
| 7 | Scope of v1. | Two-bone limb, IK/FK blend, pole, `stretch`, `soft`, `pin`, `roll`, pole spaces, matched switch, spaces on `start` and `ik` through `rigConstraint`. **Not this module:** one-bone IK (`Aim`), IK over three or more bones, spline IK, twist joints, bend bow, ribbon, reverse foot. A limb with more than three joints is an error (section 5). | Each is its own gate. Section 7.4 keeps them open. |
| 8 | Names. | Module type `TwoBoneIk`, node `twoBoneIkModule`, MTypeId `0x00218F` (free on main, 2026-11-15), channels `ikFkBlend`, `stretch`, `soft`, `pin`, `roll`, and `space` on the pole. | Names are public API. They follow D34: camelCase, no side or module in them, 0 at the first-named end. |
| 9 | New commands. | **Two:** `setRigIkFkBlend` and `setRigControlSpace`, which takes a control and switches either kind of space (section 7.2). Not `setRigModuleSpace`: the space belongs to a control, not a module (third round). | The matched switch needs scene access and undo, so it is a C++ command like `setRigConstraintSpace`. |
| 10 | Soft IK in v1? | **Yes.** One channel, one attribute and one formula (section 4.2a). | The solver takes the soft distance from the start. |
| 11 | Matched values as node outputs. | **Yes.** `fkMatchLocal` and `ikMatchLocal`, computed only when something pulls them. | Gives "always matched" as data and lets `setRigIkFkBlend` read plugs instead of a live single-module solve. No playback cost (section 4.1). |
| 12 | Roll. | A `roll` angle channel on the IK handle, default 0, `mirrorNegate`. | It rotates the plane, so it lives with the handle. A reflection reverses the sense of a rotation about the limb's axis, so the mirrored pose negates it. |
| 13 | The inverse contract gets scalar channel values. | **Yes, as a vector by channel, like the contract's other fields** (section 6). The match outputs call the same inverse and allocate, which is accepted because only tools pull them. | D41 made a module's inverse matrices only. Soft, stretch, pin, roll and the pole space are scalars the inverse needs, and it must use the same values the forward solve does. A D41 amendment goes in `decisions.md` with phase 5. |
| 14 | Where does the settings control hang? | **It follows the `parentStructure` socket of the module's first child plug** (`start`'s): the chest for an arm, the pelvis for a leg. A constraint on `start` doesn't carry it. It has an internal child plug the template can't name, and no constraints. | `parentStructure` can't name this module's own sockets (D17), and its transform channels are locked, so a space switch couldn't match. A constraint takes over `start`'s input, so settings needs its own input to see the socket alone. |
| 15 | Module-owned spaces. | **A general concept** (section 4.4): static labels, the module's own `attributeAffects`, reported by `getRigControlSpace`, switched by one command. | The rig panel (D50) switches spaces through `getRigControlSpace`, and a pole space it can't see would be the one space an animator can't switch there. |
| 16 | The start control's rotation in IK. | **Does nothing to the IK chain.** Translation only. | The IK plane comes from the pole. |

## 3. Interface

### 3.1 JSON

```json
{
    "moduleType": "TwoBoneIk",
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
        "pole": "layout.end"
    }
}
```

- `moduleArgs`: `joints` is exactly three selectors, root first. `ik`, `pole` and `settings` are one selector each. All four are required, like Aim (`readModuleArgs` takes them as `knownKeys`). `moduleVersion` is 1 and `argUpdates` is empty.
- `parentStructure` has three keys. The settings control follows the socket `start` names (decision 14), and a `settings` key in `parentStructure` is a read-phase error.
- `ik` sits at the end joint (position and orientation, normally equal to the hand joint's). `pole` sits out in front of the elbow or knee. `settings` sits anywhere the animator will find it.
- Constraints may target `start` and `ik` (D31, `rigConstraint` gives each its `space` enum, with D27 matching). **A constraint on `pole` is an error**: the pole owns its space (section 4.4). A constraint on `settings` is an error too (decision 14).
- **No child plug may be parented to this module's own sockets** (D17 cycle rule). Fingers parent to `arm_l.end`, and `arm_l.ik` is not a valid `pole` parent. The pole's IK handle space is inside the module for that reason.
- Mirroring needs nothing: `expandMirrors` swaps every `[side, usage]` selector in `moduleArgs`, as it already does for Aim. `planFromJson` has no mirror branch (D28).

### 3.2 Slots, controls, joints, plugs, sockets

One slot index is shared by `controlInputData`, `controlOPM`, `jointOPM`, `outputSocketMatrix` and `socketNames`, like `AimPart` in `modules/AimModule.h`. A `namespace TwoBoneIkSlot` in the header names them. Child plugs are slots 0, 3, 4 and 5.

| Slot | Name | Control | Joint driven | Child plug | Sockets |
|---|---|---|---|---|---|
| 0 | `upper` | FK **start control**, markup of `joints[0]` | `joints[0]` | `start` | `index_0`, `start` |
| 1 | `lower` | FK, markup of `joints[1]` | `joints[1]` | none | `index_1` |
| 2 | `end` | FK, markup of `joints[2]` | `joints[2]` | none | `index_2`, `end` |
| 3 | `ik` | IK handle, markup of the `ik` joint | the `ik` joint | `ik` | `ik` |
| 4 | `pole` | pole, markup of the `pole` joint | the `pole` joint | `pole` | `pole` |
| 5 | `settings` | settings, markup of the `settings` joint | the `settings` joint | `settings` (internal: wired to `start`'s socket) | `settings` |

- `ModuleInterface::childPlugs` is `start`, `ik`, `pole`, `settings`, each with a control (`ModulePlugInfo::hasControl`). `sockets` is the names above. `controls` is the six markups in slot order. `PlugAndSocketNames` gets `Ik`, `Pole`, `Settings`.
- **The `settings` child plug is internal.** The template can't name a parent for it: the build connects it to the socket `start`'s `parentStructure` entry names, in the same parenting pass. A constraint on `start` takes over `start`'s input (`createConstraint` in `Constraints.cpp`), not this one, so the settings control stays with the chest or pelvis whatever space the arm is in. How the plan hands the parenting pass that entry (a plan-filled parent, or a `ModulePlugInfo` field naming the plug it copies) is settled in phase 4. Prefer the smaller change to the parenting pass.
- **`ModulePlugInfo` gets one new field**, `allowsConstraint` (default true), false for `pole` and `settings`. `RigTemplateValidation` reports a constraint on a plug that doesn't allow one, in the read phase. `hasControl` can't carry it: `RigModule.cpp` checks that a plan's `hasControl` equals whether the built plug has a control. Without the field the build would fail late, after the scene has changed: `createConstraint` refuses a control that already has `space` (`Constraints.cpp`).
- **Sockets `index_0` to `index_2` are the blended joint worlds, not the FK controls' worlds.** A hand's fingers must follow the joint whichever mode is active. In FkChain the control world and joint world are the same, so this is new.
- **Those three sockets have no DAG transform** (`socketInfo.oTransform` null, as Aim does for its joint). `reparentChildPlugToSocket` DAG-parents a child module's control under the socket's control, and visibility inherits through the DAG even though transforms don't (D1). A finger control under a hidden FK hand control would be hidden in IK. The `ik`, `pole` and `settings` sockets keep their control.
- **DAG organization:** FK lower under FK upper, FK end under FK lower. `ik`, `pole` and `settings` are not under the FK controls, for the same visibility reason. The settings control is DAG-parented under the socket's control, like any child plug.
- Rotate orders: FK controls XYZ (D40), IK handle XYZ. Pole and settings pass XYZ too (their rotation isn't a channel). Every control passes its order to `createRigControl`.

### 3.3 Channels (D34)

| Control | Keyable channels | Locked and hidden |
|---|---|---|
| FK upper (start), lower, end | translate, rotate, scale, as FkChain | none |
| `ik` | translate, rotate, `roll`, `space` when constrained | scale |
| `pole` | translate, `pin`, `space` (module-owned: `Parent`, `Ik`) | rotate, scale |
| `settings` | `ikFkBlend`, `stretch`, `soft` | translate, rotate, scale |

- `ikFkBlend`: double 0 to 1, **default 1**, 0 is FK and 1 is IK. `stretch`: double 0 to 1, default 0 (0 clamps at full reach, 1 stretches). `soft`: double 0 to 1, default 0 (0 is a hard stop at full reach, section 4.2a). `pin`: double 0 to 1, default 0 (0 free, 1 the elbow sits on the pole). `roll`: an angle (degrees in the UI), default 0, unbounded, on the `ik` control. The pole's `space`: enum `Parent` (0), `Ik` (1), default 0, append-only labels.
- Every channel is `mirrorCopy` except `roll`, which is `mirrorNegate`. Each is declared through `RigPoseScene::setMirrorMode` before `addAttribute`. Defaults equal rests (D34). `ModuleContract` runs `getRigControlChannels` on every control, so a channel with no mirror category fails. The data mirror of `loadRigPose(mirror=True)` (`mirrorPoseData`, D51) and `mirrorRigKeys` read the same modes, so `roll` negates there too.
- The new names (`ikFkBlend`, `stretch`, `soft`, `pin`, `roll`) go in `docs/controls.md` the first time (D34 naming rule). `space` on the pole is the same concept and the same name as the constraint's.
- Channel names are constants in `RigAttr` (`IkFkBlend`, `Stretch`, `Soft`, `Pin`, `Roll`), used for the control attribute and the module input attribute (D7).

### 3.4 Module attributes

New on the node. The channel inputs are single plugs, not arrays: a limb has one of each.

- Inputs: `ikFkBlend`, `stretch`, `soft`, `pin` (doubles), `roll` (an angle) and `poleSpace` (short), each connected from the owning control's attribute in `createModule`, in the same modifier.
- Stored at build: `ikRestOffset` (a matrix array of 3, section 4.3), `restBendNormal` (a vector), `restPlaneAngle` (a double), `limbLengths` (the two rest bone lengths). Same pattern as `aimRestOffset`.
- Outputs: the shared `controlOPM`, `jointOPM`, `outputSocketMatrix`, plus `fkVisible` (drives the FK lower and end controls) and `ikVisible` (drives the IK handle and pole), doubles, not writable, not storable, connected to the controls' `visibility` in `createModule`.
- Match outputs: `fkMatchLocal` (matrix array, slots 0 to 2) and `ikMatchLocal` (matrix array, slots 3 and 4). Not writable, not storable, connected to nothing. Maya computes an output only when someone pulls it, so they cost nothing during playback. `compute()` has a separate branch for them (section 4.1).
- `declareStandardAffects()` covers the shared inputs. Every new attribute gets `attributeAffects` to every output it changes: `ikFkBlend` to every output, `stretch`, `soft`, `pin`, `roll`, `ikRestOffset`, `restBendNormal`, `restPlaneAngle` and `limbLengths` to `jointOPM` and `outputSocketMatrix`, every input to `fkMatchLocal` and `ikMatchLocal`, `poleSpace` to `controlOPM`, `jointOPM`, `outputSocketMatrix` and both match outputs, and `ikFkBlend` to `fkVisible` and `ikVisible`. Missing `attributeAffects` is the common silent bug (CLAUDE.md).
- `compute()` reads plugs through the module's own `MObject` attributes, never by name (D36).

## 4. Evaluation

One `compute()` solves everything once and writes every output array (D36, `commitOutput`). Inputs are world-space matrices and deltas (D3).

### 4.1 Per frame

```
D_start        = motion of the `start` child plug's input since rest (a constraint on `start` when there is one)
FK chain       : the shared chain walk over controls 0..2
                   fkWorld[i] = fkLocal[i] * fkRestWorld[i] * parentDelta_i
IK handle      : ikWorld    = ikLocal   * ikRest   * D_ik         (child plug `ik`)
Pole space     : D_pole     = poleSpace == Parent ? D_parentPlug : motion of ikWorld since rest   (4.4)
Pole           : poleWorld  = poleLocal * poleRest * D_pole
Settings       : settingsWorld = settingsLocal * settingsRest * D_settings   (its internal plug: start's socket, no constraint)   (decision 14)
IK root        : rootPos    = translation of fkWorld[0]          (the start control's rotation is ignored)
IK solve       : positions and plane from (rootPos, ikWorld, poleWorld, lengths, stretch, soft, pin, roll)   (4.2)
IK joint worlds: ikJointWorld[i] = ikRestOffset[i] * frame_i                                       (4.3)
Blend          : jointWorld[i] = blend(fkWorld[i], ikJointWorld[i], ikFkBlend)                     (4.5)
Outputs        : jointOPM[i] = socket[i] = jointWorld[i] for i in 0..2
                 jointOPM[3..5] = socket[3..5] = the marker controls' own worlds
                 controlOPM[i] = control i's rest carried by its own parent delta (excludes the local)
                 fkVisible, ikVisible = see decision 6
Match outputs  (a separate compute branch, pulled only by tools, never during playback):
                 fkMatchLocal[0..2], ikMatchLocal[3..4] = the module's inverse (section 6) run on the
                 blended jointWorld[0..2] above with this evaluation's parent deltas. They are the control
                 local matrices that would reproduce the current pose on each side.
```

The IK root takes the start control's **translation** only (decision 16). Its rotation turns the FK upper joint and does nothing to the IK chain, because the IK plane comes from the pole. Say this in `docs/modules.md`.

Blend at exactly 0 returns the FK worlds and at exactly 1 the IK worlds, with no float residue and without running the unused solve. The FK upper control is always evaluated, because the IK root follows it. FK lower and end are skipped at blend 1, and the IK solve at blend 0. This matters for animators who keyed 0 or 1 and for the inverse round trip.

### 4.2 Two-bone solve with stretch, soft, pin and roll

`MathUtils::computeTwoBoneIk` in `maya/shared/Utilities.h`, next to `computeAimMatrix`. It solves exactly two bones in closed form and is not a general chain solver. It is shareable (a ribbon module or another two-bone user can call it), so it goes in the shared layer when written (CLAUDE.md, "Look before you write", step 6). It takes `rootPos`, `handlePos`, `polePos`, `L1`, `L2`, `stretch`, `soft`, `pin`, `roll`, `restPlaneAngle`, the rest bend normal and the rest axis, and returns the three positions and the plane normal.

1. Reach `d = |handle - root|`, axis `a = normalize(handle - root)`. Below a small epsilon the axis is the rest axis (the rest handle's position minus the rest root's, both stored as control rests), so a handle on the root gives no NaN.
2. Soft reach `de = softReach(d)` (section 4.2a). With `soft = 0` it is `min(d, L1 + L2)`.
3. Stretch scale `s = lerp(1, d / de, stretch)` (never below 1, since `de <= d`). Bone lengths `L1s = L1*s` and `L2s = L2*s`. Base distance `dc = de * s`, clamped to `[|L1s - L2s| + eps, L1s + L2s]`. With `soft = 0`: a handle past full reach with `stretch = 0` leaves the end at full reach on the line to the handle, like any native IK, and with `stretch = 1` the end reaches the handle.
4. Plane normal. `n0 = normalize(pole - root - a * dot(pole - root, a))`. If its length is below 1e-4 (pole on the line), use `restBendNormal` projected perpendicular to `a`, then any perpendicular. Then `n = rotate(n0, about a, restPlaneAngle + roll * (1 - pin))`. Positive roll is right-handed about `a`. `restPlaneAngle` is the build's correction so IK at rest reproduces the rest bend (4.3). The solver is **stateless** (D36), so a pole crossing the line flips the limb the way a native IK does. Say so in `docs/modules.md`.
5. Law of cosines: `x = (dc^2 + L1s^2 - L2s^2) / (2 dc)`, `h = sqrt(max(0, L1s^2 - x^2))`. `midFree = root + a*x + n*h`, `endFree = root + a*dc`.
6. **Pin:** `mid = lerp(midFree, polePos, pin)` and `end = lerp(endFree, handlePos, pin)`. At `pin = 1` the elbow sits on the pole control and the wrist on the handle, so both bones lengthen or shorten to fit. Pin wins over stretch and soft. The pinned mid sits on the pole, which is not in the rolled plane, so roll fades out with pin (step 4).

Hand-worked checks for pose cases: `L1 = 3`, `L2 = 4`, root `(0,0,0)`.
- Handle `(5,0,0)`, pole toward `+Y`: `x = (25+9-16)/10 = 1.8`, `h = sqrt(9-3.24) = 2.4`, `mid = (1.8, 2.4, 0)`.
- Handle `(0,5,0)`, pole toward `+X`: `mid = (2.4, 1.8, 0)`.
- Handle `(10,0,0)`: stretch 0 gives end `(7,0,0)` and mid `(3,0,0)`. Stretch 1 gives end `(10,0,0)` and mid `(30/7,0,0)`.
- Handle `(5,0,0)`, pole at `(2,3,0)`: pin 1 gives mid `(2,3,0)` and end `(5,0,0)`. Pin 0.5 gives mid `(1.9, 2.7, 0)`.
- Handle `(5,0,0)`, pole toward `+Y`, `roll` 90 degrees: the plane normal turns from `+Y` to `+Z`, so `mid = (1.8, 0, 2.4)`. Roll 180 gives `(1.8, -2.4, 0)`. The mirrored limb at roll -90 gives the reflected pose.
- Rest plane angle: a rest limb bent toward `+Z` (`mid = (1.8, 0, 2.4)`) with the pole joint toward `+Y` stores `restPlaneAngle = +90` degrees, and IK at rest reproduces the rest mid.

### 4.2a Soft IK (a channel on the settings control, v1)

Soft IK eases the reach as the handle nears full length, so the elbow doesn't snap straight. With `L = L1 + L2`, a softness distance `ds` and `da = L - ds`:

```
d <= da :  de = d
d >  da :  de = da + ds * (1 - exp(-(d - da) / ds))
```

- **Why it works.** At `d = da` the value and the slope both match the linear part, so there is no kink. As `d` grows, `de` approaches `L` and never reaches it.
- **Units.** `soft` is 0 to 1 and `ds = soft * L`, so it scales with the rig. `ds` under a small epsilon means a hard stop (`de = min(d, L)`), which also avoids dividing by zero.
- **With stretch.** `s = lerp(1, d / de, stretch)` scales the bones so the end still reaches the handle with the same softened bend. With `stretch = 0` the end stops short of the handle by `d - de`, which is the point of soft IK. The end joint's rotation still comes from the handle.
- **With pin.** Pin blends the positions at the end (step 6), so pin 1 ignores soft.
- **Hand-worked check:** `L1 = 3`, `L2 = 4`, `soft = 1/7` so `ds = 1`, `da = 6`. `d = 6` gives `de = 6`. `d = 7` gives `de = 6 + (1 - e^-1) = 6.632121`. `d = 8` gives `de = 6 + (1 - e^-2) = 6.864665`. With `stretch = 1` at `d = 7`: `s = 7 / 6.632121 = 1.055469`, bones `3.166408` and `4.221877`, end at `7` (the first draft had `1.055470`, `3.166410` and `4.221880`, rounded wrong in the sixth digit; corrected in phase 2). At `d = 8` with `stretch = 1`: `s = 1.165388`, bones `3.496165` and `4.661553`.
- **Cost.** The forward solve stays closed form: `softReach` only remaps `d`, and `exp` is one call. What gets harder is the inverse (section 6, item 10), and the end joint no longer sits on the handle when the handle is pulled far.
- **Shape.** The exponential is the standard because it is C1 at `da` and has a closed-form inverse.
- **Not in the contract:** a minimum-reach (folded) soft zone. Add it only if animators hit it.

### 4.3 Joint frames and rest offsets

Joint rest orientations are arbitrary, so no axis assumption is made (D40's +X down the bone is a default for FK controls only). Each IK joint is a *frame* built from the solved positions, times a stored rest offset.

- Root frame: X toward `mid`, Z along `n` (the bend direction from 4.2 step 4, made perpendicular to X: `Z = normalize(n - X * dot(n, X))`, `Y = Z x X`). The phase 2 pose cases read it this way, and a pole on the other side of the axis flips the frame's handedness. Mid frame: X toward `end`, same `n`. End joint: translation from the solved end, **rotation from the IK handle's world** (rotating the handle turns the hand), scale ignored.
- One frame builder, used by `createModule` (to compute the rest offsets), `compute()` and the inverse, so they can't drift (D41, P3). It is `computeAimJointWorld` and `computeAimRestOffset` again for three joints: `ikRestOffset[i] = jointRest[i] * frameAtRest[i]^-1`.
- At rest the IK joints equal the joint rests, wherever the rigger put the pole. `createModule` stores `restPlaneAngle`: the signed angle about the root-to-handle axis from the pole's direction to the rest mid joint's direction. A straight rest limb stores 0 and the pole only sets the plane. `restBendNormal` is the rest mid's perpendicular direction when the limb is bent, and the pole's otherwise.

### 4.4 Module-owned spaces, and the pole's

A **module-owned space** is a `space` enum on a control that its module reads as an input (D27's enum, without a `rigConstraint`). Compared to a constraint space:

- **Static labels.** The module fixes them in `createModule`. A template can't add one, and labels are append-only (D34).
- **Its own `attributeAffects`.** The enum is a module input, so it is declared in `initialize()` with every output it changes.
- **Reported and switched like a constraint space.** `getRigControlSpace` reports it and the space switch command matches it (section 7.2), so the rig panel (D50) shows it with the rest.
- **The module computes the deltas.** Matching needs each label's delta (D27's `D_old` and `D_new`). A new `ModuleDescriptor` function, `spaceDelta`, returns the delta of a control's space label from the module's current plugs. Only rig modules with a module-owned space set it.

The pole's space is the first. `poleSpace` is read by the module (`Parent` = 0, `Ik` = 1):

- `Parent`: the pole's delta is the `pole` child plug's parent motion, as for any control. It is `layout.end` unless `parentStructure` says otherwise.
- `Ik`: the pole's delta is the motion of the IK handle's world since rest, `computeDeltaFromRest(ikRest, ikWorld)`.
- This reads one slot's solved world as another slot's input, inside one node. It is not a graph cycle, because the IK handle depends only on the `ik` plug and its own control. It fixes the slot order in `compute()` and in the inverse's forward pass: solve slot 3 before slot 4.
- Setting the enum directly moves the pole, as with a `rigConstraint` enum (D27).

### 4.5 Blend math

Translation lerps, rotation slerps along the shorter arc, scale lerps. Decompose with `MTransformationMatrix` and rebuild. `compute()` stays allocation-free (D36, CLAUDE.md), so check the decomposition when building. `MathUtils::blendMatrices` goes in the shared layer for the same reason as the solver.

### 4.6 Failure modes

| Case | Result |
|---|---|
| Handle on the root | `dc` clamps to `\|L1s - L2s\| + eps` and the axis falls back to the rest axis, so the limb folds, no NaN. |
| Pole on the line | Rest bend normal, then any perpendicular (4.2). |
| Zero-length rest bone | Plan error (section 5). |
| Blend, stretch, soft, pin outside 0 to 1 | The attributes are clamped (min 0, max 1). |
| Pole `space` set directly | The pole moves, no match. Use the space switch command. |

## 5. Read-phase validation (`planFromJson`)

All errors go to `ctx.report` with the JSON path, and every selector is claimed even after a failure so the report lists all of them (the FkChain and Aim pattern).

1. `joints` has exactly three selectors. Any other count is an error that says this is a two-bone IK limb, that one bone is the Aim module, and that longer chains need a different module. `ik`, `pole`, `settings` each resolve to one joint, and none of the six is claimed twice (`claimJoint` covers it).
2. Rest bone lengths `L1` and `L2` are above a minimum (as `kMinAimDistance`).
3. The pole joint is not on the root-end line (as `kMinAimUpSine`). Its side and plane don't matter: `restPlaneAngle` absorbs them.
4. The `ik` joint is within a tolerance of the end joint's position. An offset handle is a separate feature: error for now.
5. Shared control markups are caught by `validateControlMarkup`. `checkMirrorPositions` works through `claimJoint` for all six.
6. In `core/`, `RigTemplateValidation`: a constraint whose target plug has `allowsConstraint == false` is an error (section 3.2).

## 6. Inverse (D41, `inverse-solve.md` section 8)

**The contract today** (`maya/shared/ModuleInverse.h`) is matrices only, in `std::vector`s by slot, and its result carries a `std::string` reason and a vector of warnings. That is fine for the restore, which runs outside evaluation. It allocates, so `compute()`'s match branch can't call it as it stands.

**The change (decision 13):**

- `ModuleInverseContext::channelNames`: the scalar channels the module wants, in a fixed order (`soft`, `stretch`, `pin`, `roll`, `poleSpace`), read once per solve. Empty for FkChain, Aim and Layout, so they don't change.
- `ModuleInverseInput::channels`: their values, a `std::vector<double>` in `channelNames` order, like the contract's other fields. The driver fills it from the sidecar when the channel is there, otherwise from the rig's current channel value, so the inverse uses the value the forward solve will. Assuming `soft = 0` while the rig has `soft > 0` would pull the wrist inward and miss the pose.
- **One solve, two callers.** `TwoBoneIkNode::inverse` serves the restore and `compute()`'s match branch, so the match outputs and the restore can't drift (D41, P3).
- **The match branch allocates**, against the allocation-free rule for `compute()` (D36, CLAUDE.md). Accepted: Maya computes those outputs only when a tool pulls them, never during playback, so D36's targets are untouched. The branch says so in a comment, and `bench` (phase 8) confirms playback doesn't pull them. Rejected: a fixed-size channel array (it would leave the matrices and the result allocating anyway), a fully fixed-size contract (it would cap FkChain's length), and a separate fixed-size core (a second entry point for one module's math).

What the inverse does:

1. **FK controls** from `jointWorld[0..2]`, the `ModuleInverse::solveChain` walk on three slots with `useJoints = true`. Translate, rotate and scale are all derived, so a stretched pose is reproduced in FK.
2. **IK handle** (slot 3): the end joint's world, then `ModuleInverse::localFromWorld` with the `ik` parent delta.
3. **Pole** (slot 4): `a` is the unit root-to-end axis. `nObs` is the unit perpendicular from the root-end line to the mid joint. Undo the plane rotation: `n0 = rotate(nObs, about a, -(restPlaneAngle + roll * (1 - pin)))`. The pole's world position is the point on the line nearest the mid joint, plus `n0` times the rest pole distance. Rotation stays at rest. Then `localFromWorld` with the delta of the current `poleSpace`. **In `Ik` space that delta comes from the matched handle** (the end joint's world, `computeDeltaFromRest(ikRest, endWorld)`), not the handle's current world, because the match writes the handle and the pole together. With `pin > 0` the pole goes to the mid joint's position and the handle to the end joint's, exact at `pin = 1` and an approximation between (item 11).
4. **Settings** (slot 5): no joints. Transform from the fallback (the sidecar, or rest), as Layout does.
5. **Marker joints** (slots 3 to 5) are `export: false`, so the source skeleton normally lacks them. The inverse derives its controls from slots 0 to 2 and ignores `jointWorld[3..5]`.
6. `socketWorld` returns the forward result for every slot, so the sweep feeds modules below.
7. **`solved = false`** when any limb joint is missing, with a reason. The driver then takes the controls from the sidecar, then rest.
8. **Channels from joints:** transforms yes. `ikFkBlend`, `pin`, `space`, `soft`, `roll` and `stretch` come from the sidecar, never derived. `stretch` is not derivable from the length ratio as `inverse-solve.md` requirement 3 says: below full reach the joints can't tell stretch 0 from 1, and with `pin` the bone lengths mean something else.
9. **A source whose bone lengths differ from the rest** is reproduced exactly by FK. IK reproduces it only with `stretch` or `pin` on, so the restore warns when IK can't match the joints.
10. **Soft IK in the inverse.** Given the observed end distance `de` and `soft`, the handle's distance is `d = da - ds * ln(1 - (de - da) / ds)` for `de > da`, and `d = de` otherwise. It goes to infinity as `de` approaches `L`, so clamp `de` to `L * (1 - 1e-4)` first: the handle lands a few `ds` past the wrist and the wrist error is 1e-4 of the limb. With stretch on and `pin = 0`, the bones' scale is observable: `s = |mid - root| / L1`, then `de = |end - root| / s`, still closed form. Near full extension the handle's error is the joint error times about `ds / (L - de)`, so noisy mocap makes the handle curve jitter while the wrist stays right. Accepted. The round-trip tests include a soft case and an extended one.
11. **Exactness.** FK is exact at any blend. IK is exact at blend 0 and 1 for a pose it can make: bone lengths equal to the rest, or `stretch` or `pin` on. At `0 < blend < 1` a bone can be shorter than its rest (the chord), and IK reproduces it only approximately. With `0 < pin < 1` the inverse isn't closed form. Both cases warn, and the matched switch is meant for blend 0 and 1.

## 7. Library, commands and Python

### 7.1 What works with no change

Every item keys on control markup and channel name, so a limb is in them from the first build. Each needs a test and a docs line.

- `getRigControls(moduleType=["TwoBoneIk"])`, `getRigControlInfo`, `getRigControlChannels`, `getRigMirrorControl`.
- `zeroRigControls` (IK, blend 1, stretch 0, soft 0, pin 0, roll 0, pole space `Parent`), `mirrorRigPose`, `flipRigPose`.
- `saveRigPose` and `loadRigPose` (with `mirror`, D51), `saveRigAnimation` and `loadRigAnimation` (D34, D42), with `offset`. The new floats key on default tangents, `space` keys stepped. `getRigDataFileInfo` and the library browser (D51) see the controls by markup.
- Control shape save, load and mirror for the six controls (D26).
- `createRigConstraint` and `setRigConstraintSpace` on `start` and `ik`, and template `constraints` entries.
- `rigmodules.pose.key_controls` and `key_modules`, which read `get_channels`.
- Selection sets (D39), display layers (D38) and `rig_all` (D43).
- `animateRigFromJointClip` and `poseRigFromJointPose`, through the inverse.

Shape choices: the shape type comes from the module's `shapeType`, one for all six controls, like every module (the shape file lets a rigger edit them per asset, D26).

### 7.2 Commands (D14 shape)

**`setRigIkFkBlend(control | module, blend=1.0)`** (`commands/TwoBoneIkCmds.h/.cpp`; the settings control or the module node; objects by name, never the selection). It matches both sides to the limb's live joints, then sets `ikFkBlend`:

1. Pull the module's match outputs, `fkMatchLocal` (slots 0 to 2) and `ikMatchLocal` (slots 3 and 4).
2. Convert each local to channel values with the driver's matrix-to-channels conversion (euler nearest the current values, the control's own rotate order), and write them through `RigPoseScene`. Locked or driven channels keep their values and warn, as in D27 and D41.
3. Set `ikFkBlend` to `blend`.

The switch doesn't pop in either direction **at blend 0 and 1**. A pose IK can't reproduce (section 6, items 9 and 11) warns. The command sets no keys. A keyed switch is a mayadev tool on top of this command, like the keyed space switch the rig panel already has (D50). Undoable as one step.

**`setRigControlSpace(control, space=n | label=l)`** (`commands/RigControlCmds.h/.cpp`, beside `getRigControlSpace`) switches a control's space, either kind, and keeps the control's world matrix by D27's formula, `L' = L * OPM * D_new^-1 * D_old * OPM^-1`. For a constraint space it goes through `matchSwitchSpace`, as `setRigConstraintSpace` does. For a module-owned space `D_old` and `D_new` come from the module's `ModuleDescriptor::spaceDelta` (section 4.4). Named for the control, not the module, because the space belongs to the control (author), and it pairs with `getRigControlSpace` (get/set). `setRigConstraintSpace` stays. Undoable. A direct `setAttr` on the enum doesn't match.

**`getRigControlSpace`** (D50) reports a module-owned space too. Today it returns `{"constraint", "labels", "current"}` from `RigConstraintNode::getControlSpace`. It gains a `module` key (the module node, empty for a constraint space), and `constraint` is empty for a module-owned space. Keys are only added, so the JSON stays readable by older callers, but **mayadev's rig panel breaks on a pole**: `RigPanelSession.set_space` calls `setRigConstraintSpace(space.constraint, ...)` (`bmaya/maya/rigpanel.py`), which would get `""`. The mayadev edit, proposed with phase 7, is one line: call `setRigControlSpace(space.control, label=label)` for every space. `rigmodules.query.ControlSpace` gains `module`.

**Shared machinery, not new logic per command:**
- The driver's matrix-to-channels conversion (private to `RigInverse.cpp` today) moves to `RigPoseScene` or a shared helper. `RigInverse::solve` keeps calling it.
- D27's formula is in `RigConstraintNode::matchSwitchSpace` (called by `setRigConstraintSpace` in `ConstraintCmds.cpp`). Factor the formula (new local from old local, old and new delta, the control's OPM) into one shared function and call it from both.
- No live single-module solve is needed: the node computes the match matrices itself (decision 11). A partial JointClip would be wrong here, because the sweep takes parent sockets from rest and gets the parent deltas wrong.

### 7.3 Python

`rigmodules.query` already reads the new controls and channels. `ControlSpace` gains `module` (phase 7). No wrapper for the commands: they are `cmds`. mayadev's rig panel gets the module-owned space switch and an IK/FK switch button later (D24: mayadev depends on RigModules, and mayadev edits need the author's go-ahead).

### 7.4 Later: bendy limb, ribbon, twist, bend bow

Not built, and nothing in v1 closes the door.

- **Recommended shape: a separate module** that reads this module's sockets (`index_0` to `index_2`, the blended joint worlds), and `settings` if it needs a channel, and drives a ribbon chain with distributed twist and bend bow. The IK solver and blend are in `MathUtils`, so it can reuse them.
- **If it becomes one mega module**, the limb gains optional `moduleArgs` keys (a `moduleVersion` step with a default, D33), and new channels (`twist`, `bendBow`) are appended with their mirror mode. Existing names don't change.
- The three socket families, the six slots and the channel names above are stable, and slot numbers are never reused.

## 8. Build phases (one commit each)

Each phase ends with a local build, the full suite and its own docs pass (CLAUDE.md, "Working style"), so docs aren't a last phase. Cloud sessions can do phases 1 and 2. Every code phase needs local build and Maya verification.

Constants, shared helpers and the validation flag go in with the first phase that calls them, so no phase adds code nothing calls.

1. **Design records.** This page, D52 in `decisions.md` (and "the next module is the IK limb" in its Open items marked as designed), `inverse-solve.md`'s status line, `module-directions.md`'s status line, the design tables in `README.md` and `CLAUDE.md`. No code.
2. **Fixtures. Built (2026-11-15).** Hand-worked pose cases (`tests/maya/fixtures/poses/twoboneik/*.json`, 31 cases) from sections 4.2 and 4.2a, skeletons (`fixtures/skeletons/twoboneik_basic.json`, `twoboneik_offplane.json` for a rest bend off the pole's plane, `twoboneik_mirror.json`: a bent limb, the pole on the bend side, three non-export marker joints) and templates (`fixtures/templates/twoboneik_basic.json`, `twoboneik_mirror.json`). Nothing loads them until phase 4, which is accepted so the numbers are fixed before any code exists. **Validation cases are not in this phase; they move to phase 4.** `rigtest/cases.py` loads every `fixtures/validation/*.json` automatically, so a `TwoBoneIk` case would fail the suite until the module is registered. Pose cases load only when a test calls `add_pose_cases`, so they can wait. The pose cases assert joint rotations only where the rotation is a hand-checkable rigid turn (the frame definition in 4.3 decides the rest), and positions elsewhere.
3. **Shared custom-channel helper.** One helper adds a keyable double or enum channel to a control with a default, a range and a mirror mode, and `DagUtils::queueLockAndHideTransform` gets an optional list of channels to keep. The constraint's `space` enum in `Constraints.cpp` moves onto the helper, so it has a caller and the current suite is its test. No behaviour change. **Built 2026-11-15**: `RigPoseScene::addEnumChannel` (enum only: nothing calls a double or angle channel yet, so phase 4 adds that sibling with its first caller), `queueLockAndHideTransform(..., keepChannels)`, and the `space` enum rebuilt on the helper. The suite (820) and the golden files are unchanged.
4. **The module, forward.** `RigAttr` and `PlugAndSocketNames` constants. `ModulePlugInfo::allowsConstraint` and its check, and the internal `settings` plug wired to `start`'s socket (decision 14). `MathUtils::computeTwoBoneIk` (with soft) and `MathUtils::blendMatrices`. `maya/modules/TwoBoneIkModule.h/.cpp` (`TwoBoneIkSlot`, `TwoBoneIkArgs` with `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE`, `initialize` with create, add and `attributeAffects` together, `compute`, `createModule`, `planFromJson`), the registry row, the project files, `test_plugin.py`, the MTypeId in `CLAUDE.md`. Tests: `TwoBoneIkContract` with `JOINT_OWNERS`, `MIRROR_TEMPLATE`, `MIRROR_SKELETON`; the pose cases; behaviour tests (blend 0 and 1 exact, 0.5 the lerp and slerp, hand follows the handle, visibility by blend, `index_*` sockets follow the blended joints and a child module's controls stay visible at blend 1, the start control's rotation does nothing in IK, the settings control follows `start`'s socket and stays put when a constraint switches `start` to another space, the pole on the line, a constraint on `pole` or `settings` and a `settings` key in `parentStructure` fail validation, `start` and `ik` take `rigConstraint` spaces); validation cases; the golden snapshot. The pole's `space` enum exists and evaluates, with no matched switch yet. The descriptor's `inverse` returns `solved = false` with "phase 5" as the reason (D41 requires one), and the contract's round trip is skipped for this module until phase 5 (section 11, answer 3).
5. **Inverse.** `ModuleInverseContext::channelNames` and `ModuleInverseInput::channels`, filled by `RigInverse`. `TwoBoneIkNode::inverse`. The D41 amendment in `decisions.md` and `inverse-solve.md` (section 4's contract, section 8's requirements). Tests: the round trip at blends 0 and 1 (and a warning at 0.5), a missing joint, a sidecar carrying `ikFkBlend`, a stretched source, soft with and without stretch, a roll, a rig whose current `soft` differs from the sidecar's. The contract's round trip skip is removed.
6. **Matched IK/FK switch.** The match outputs and their `compute()` branch (calling `inverse`, allocation accepted, section 6). The matrix-to-channels conversion moved out of `RigInverse.cpp`. `commands/TwoBoneIkCmds.h/.cpp` with `setRigIkFkBlend`, a row in `CommandRegistry.cpp`, the long flag names in `test_plugin.py`, each `addFlag` in `CHECK_MSTATUS`. Tests (`commands/test_two_bone_ik_cmds.py`, tier 5): FK to IK and back leaves every joint where it was, at rest and posed in each mode, stretched, pinned, soft, with a space switch on `ik`; a warning when IK can't match; locked channels warn; undo and redo; every error. Setting the controls from the match outputs leaves the joints still at blends 0, 0.5 and 1.
7. **Module-owned spaces.** `ModuleDescriptor::spaceDelta`, D27's formula factored out of `matchSwitchSpace`, `setRigControlSpace` (with a constraint space too), the `module` key in `getRigControlSpace` and `ControlSpace` in `core/ControlChannels`, `rigmodules.query.ControlSpace.module`. Tests: `setRigControlSpace` on a constrained control matches as `setRigConstraintSpace` does, and the switch keeps the pole's world matrix in both directions, with the handle moved, and undoes; `getRigControlSpace` on the pole, on a constrained `ik` and on an FK control. The matched IK/FK switch in `Ik` pole space leaves the pole and the joints where they were. Proposed to the author in the same change: the mayadev rig panel edit (section 7.2).
8. **Performance rig and `bench` (D36).** `limb_specs()` in `tools/perf/perf_scenes.py` beside `chain_specs()` and `aim_specs()` (about 12 limbs: arms and legs, three marker joints each), the skeleton, template and instance builders and `check_perf_scene.py`, then `mayapy tools\perf\perf_scenes.py check` and `bench` on the 20-rig scene (30 fps target) and the 50-rig scene (within 1.5 times the native floor). Report the fps, and say so when below target. Fix the docs' and D36's "200 joint" and "51 constraints" counts.

The docs each phase owns (from `docs/README.md`): `docs/modules.md` (a `TwoBoneIk` section: JSON, args, node table, controls, child plugs, sockets, channels, the start control, stretch, soft, pin, roll, pole spaces, the pole-on-line behaviour, the own-socket rule), `docs/templates.md` (args and a mirrored limb), `docs/controls.md` (the new channel names, marker joints, module-owned spaces), `docs/commands.md` (the two commands, `getRigControlSpace`'s `module` key, the `TwoBoneIk` filter value), `docs/library.md` (a raw `ikFkBlend` change doesn't match), `docs/python.md` (`ControlSpace.module`), `docs/architecture.md` (module and slot tables, the shared helpers, the inverse's scalar channels, the recipe notes for custom channels and module-owned spaces), `docs/mayadev.md` (the rig panel call site), `README.md`, `CLAUDE.md`, `tests/README.md`, and a `test_docs_examples.py` test for each new worked example. Search every page for `IK limb` and stale mentions in the phase that makes them stale.

## 9. Discrepancies and risks

**Found resolving the draft against main (2026-11-15):**

- **The decision number.** The draft called this D47. D47 to D51 are taken (tool registry, mayalaunch, mayadev UI, rig panel, library browser). It is D52.
- **The inverse contract allocates.** The draft called `ModuleInverseInput::channels` allocation-free so `compute()` could call the inverse, but every other field is a `std::vector` and the result holds strings. Resolved by accepting the allocation in the pulled-only match branch (section 6).
- **`getRigControlSpace` (D50) knows only constraint spaces.** Built after the draft. Its result and the rig panel's switch assume a `rigConstraint`. Resolved by module-owned spaces (decision 15, section 4.4) and `setRigControlSpace`, with a one-line mayadev edit.
- **`setRigPoleSpace` is gone.** `setRigControlSpace` replaces it (section 7.2).
- **The settings child plug is internal.** The template can't name it: it is wired to `start`'s socket, and a constraint on `start` doesn't move it (decision 14).
- **D51's data mirror** (`loadRigPose(mirror=True)`, `mirrorPoseData`) postdates the draft. It reads mirror modes, so `roll` negates there as in `mirrorRigKeys`. Phase 4 adds a pose test for it.
- **The draft's build order ended in one commit.** Replaced by eight phases, one commit each (section 8).

**From the draft, still true:**

- **`inverse-solve.md` requirement 3** says `stretch` is derivable from the length ratio. It isn't reliably (section 6, item 8). Phase 5 updates that page.
- **D31 and D17 against requirement 6.** D31 says the IK module exposes the pole as a child plug that gets spaces through `rigConstraint`. That doesn't work for the IK handle, which D17 forbids. Resolved by the module-owned space, at the cost of one external space and the `allowsConstraint` flag.
- **Raw `ikFkBlend` writes pop.** Only `setRigIkFkBlend` matches, as for `space` (D27). Animators who key the blend by hand lose the match, so the keyed switch tool matters (mayadev).
- **`docs/architecture.md`'s recipe** says nothing about custom channels, hidden controls, marker joints or module-owned spaces. Updated with phases 3, 4 and 7.
- **Visibility inheritance.** D1 turns transforms off in the DAG and leaves visibility inherited. Section 3.2 avoids hiding children with null socket transforms. A test checks it.
- **The match outputs must equal what the command writes.** Same inverse as the restore (section 6), and a phase 6 test.
- **The match branch allocates in `compute()`.** The one exception to the allocation-free rule, accepted because playback never pulls it (section 6). A future tool that pulls the match outputs every frame would pay for it.
- **Soft IK near full extension.** The inverse is ill-conditioned there (section 6, item 10), so the matched switch from an extended FK arm puts the handle a few `ds` past the wrist.
- **The settings control can't follow the wrist.** Decision 14. It follows the chest or pelvis, through the socket only.
- **Compute cost.** Three blended joints plus an IK solve cost more than a chain solve. D36's `rigControlNode` premium dominates, but run `bench` before assuming anything.
- Review fixes kept from the draft: the pole's placement at rest (`restPlaneAngle`), a handle on the root (the rest axis), the pole in `Ik` space when matching (the matched handle's delta), matched switch exactness (blend 0 and 1 only), the inverse's scalar channels, roll mirrored by negation.

## 10. Maintenance surface added

- One module node, its args struct, its inverse, one registry row.
- Six channel names (`ikFkBlend`, `stretch`, `soft`, `pin`, `roll`, and `space` on the pole with two labels), four child plug names (one internal), two socket families, three marker-joint markups per limb.
- Two commands (`setRigIkFkBlend` and `setRigControlSpace`), and a `module` key on `getRigControlSpace` and `ControlSpace`.
- Constants in `RigAttr` and `PlugAndSocketNames`, `ModulePlugInfo::allowsConstraint`, `ModuleDescriptor::spaceDelta`, and scalar channels in the inverse contract.
- `MathUtils::computeTwoBoneIk` and `blendMatrices`, the shared custom-channel helper, the shared match formula, the shared matrix-to-channels conversion and two match output arrays on the node.
- One golden snapshot, a pose case folder, two test files and fixtures, and a block in the performance rig.

Complexity stated (CLAUDE.md, "Complexity checks"): the first custom channels, the first scalar module inputs, the first module-owned space (a concept with a descriptor hook), a visibility output, three marker joints per limb, a new plug flag in `core/`, a key on a command's result, and two commands. Rejected as simpler: an IK-only solve with no blend (the problem is the switch), a `poleFollow` slider (two controls for one idea), and a pole-only `setRigPoleSpace` (the rig panel couldn't find it).

## 11. Third round of answers (author, 2026-11-15)

1. **The switch command is `setRigControlSpace`**, not `setRigModuleSpace`, because the space belongs to a control, not a module. It switches either kind of space (section 7.2).
2. **The inverse's scalar channels are a vector, not fixed-size.** The match branch in `compute()` allocates, accepted because only tools pull it (section 6).
3. **Phase 4 ships without an inverse**: `inverse` returns `solved = false` and the contract's round trip is skipped until phase 5.
4. **The settings control follows the `parentStructure` socket only**, never a constraint on `start` (decision 14, section 3.2).

No question is open. One detail is left to phase 4: how the plan hands the internal `settings` plug its parent (section 3.2).
