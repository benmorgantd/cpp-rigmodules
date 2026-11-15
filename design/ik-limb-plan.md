# IK limb: design plan and build steps

**Status:** draft for sign-off, revised 2026-11-15 with the author's requirements and answers, a soft IK design, a roll channel and a full review (section 9). Nothing here is built, and the module has not passed the gate (D12) until the author signs off section 2. Written from the code, `decisions.md` (D1 to D46), `inverse-solve.md` and `module-directions.md`. Where those disagree, section 9 lists it.

The IK limb is the next module (author, 2026-11-10). **It is a two-bone IK limb: exactly three joints (root, mid, end), for arms and legs.** Three or more IK bones is a different solve (a chain solver such as CCD or FABRIK, with no closed form, no single pole plane and no analytic inverse), and spline IK is another again. Both would be their own modules through the gate (D12) and share nothing with this solver except the blend and the matched-switch pattern. The module is IK/FK only. It is **not** a bendy limb: no twist, no bend bow, no ribbon. Section 7.4 says how those can be built on top later.

## 0. Requirements and answers (author, 2026-11-15)

1. An FK-style start control, so the IK chain can be moved from its start as well as its end.
2. An IK control and a pole vector control.
3. Optional stretch, a 0 to 1 blend.
4. Pin to the elbow, a 0 to 1 blend.
5. An IK/FK switch, a 0 to 1 blend, with no pop because FK is always up to date with IK.
6. Pole vector spaces include the IK handle and the layout.
7. Soft IK designed in, even if it ships after v1.
8. A `roll` attribute that rolls the IK plane.

Answers and confirmations:

- **No-pop (requirement 5): independent values, matched values published by the node, and a command that copies them.** FK controls hold absolute joint values, and IK controls hold IK values. Every evaluation the node can also compute the control values each side would need to reproduce the current pose (the module's closed-form inverse, section 4.1), so FK is always matched to IK and the other way round, as data. `setRigIkFkBlend` writes them to the controls and sets the blend (section 7.2). Setting `ikFkBlend` directly with `setAttr` does no match, like setting a `space` enum directly (D27).
- **Why the blend alone doesn't match** (author asked, 2026-11-15). A blend interpolates the two chains' *poses*. It never changes the *values* stored on the inactive side's controls, and `compute()` can't write plugs. So when the blend flips from 1 to 0 the joints jump to whatever the stale FK values describe, by exactly the difference between the two poses. For the controls to follow the active side inside `compute()`, their values would have to mean *offsets from the other chain's pose*. That works for one direction (FK as a layer on IK), and then FK values aren't absolute joint rotations: an arm waving in FK while the body moves would drag against a world-space IK handle, and FK poses and animation stop transferring (D42). In the other direction it makes the two chains depend on each other, a cycle inside the node. Rejected for those reasons.
- **Confirmed (author, 2026-11-15):** the settings control exists, controls hide by blend as in decision 6, and **every limb defaults to IK (`ikFkBlend` 1)**, with no template key.
- **Soft IK** is designed in section 4.2a and can ship in v1 or later without changing any name, slot or template key.
- **Roll** (requirement 8): a `roll` channel on the IK handle that rotates the IK plane about the root-to-handle axis (section 4.2). An angle, default 0, `mirrorNegate`.
- **Pole spaces (requirement 6): one module-owned `space` enum on the pole control, with the IK handle in it.** `rigConstraint` can't take this module's own `ik` socket as a driver, because D17 counts that as a cycle and the evaluation graph would cycle (D36). Rejected: a `poleFollow` slider next to a constraint enum, and relaxing D17 to per-slot dependencies.

## 1. Gate (D12)

1. **Animator-facing problem.** Two-bone limbs (arms and legs) that animators pose by placing a hand or foot (IK) and by rotating joints (FK), that switch between the two without a pop, that can stretch or pin the elbow, and whose pole follows the world or the hand. `FkChain` gives FK only and `Aim` points one joint. Nothing solves a two-bone chain.
2. **Existing module check.** No small extension of FkChain or Aim covers it. A two-bone solve, a blend between two joint solutions and channels on controls are new, and all transform math lives in module nodes (D1).
3. **Design.** Sections 3 to 7.
4. **Maintenance surface.** Section 10.
5. **Performance rig and `bench`.** Section 8, step 14.

## 2. Decisions that need the author's sign-off

Each has a recommendation. Changing one after the module ships changes a template key, a control markup or a channel name (D33, D34).

| # | Question | Recommendation | Why it matters |
|---|---|---|---|
| 1 | Where do the IK handle, pole and settings controls get their markup and rest? | **Three marker joints in the skeleton**, selected in `moduleArgs` like Aim's `target` and `up`, marked `export: false`, `skin: false`. Rejected: derived usages with a computed pole position. | Follows Aim (D15: markup says what a joint is, the template picks joints), puts the pole where the rigger put it, invents no names. Cost: three non-export joints per limb. |
| 2 | Where do `ikFkBlend`, `stretch` and `soft` live? | **Answered:** a dedicated `settings` control, channel-only, always visible. | The IK handle is hidden in FK, so the switch would be hidden with it (priority 1, D10). |
| 3 | Pole spaces. | **Answered:** a module-owned `space` enum on the pole control, labels `Parent` and `Ik` (section 4.4). `Parent` is whatever the `pole` child plug's `parentStructure` socket is, `layout.end` by default. A template constraint on `pole` is a read-phase error. | Gives requirement 6 with one enum. Limit: one external space (the child plug's parent), not a list. More spaces later means more labels (append-only, D34) and extra child plugs, and `moduleArgs` keys with a `moduleVersion` step. |
| 4 | What is the blend's default? | **Answered: 1 (IK) for every limb, no template key.** | Default equals rest (D34), so a built rig, `zeroRigControls` and the contract's rest pose are all in IK, with the FK lower and end controls hidden. FK pose cases set `ikFkBlend` to 0. |
| 5 | FK controls: which channels are keyable? | **All of FkChain's** (translate, rotate, scale), so FK can reproduce a stretched or pinned IK pose when the matched switch writes it. | Locking FK translate would leave stretch and pin unreproducible in FK, and the switch would pop. |
| 6 | Hide controls by blend? | **Yes in v1, confirmed.** FK lower and end visible when blend < 1, IK handle and pole when blend > 0. The FK **start** control and the settings control are always visible (the start control moves the IK chain too). | Nothing surprising for the animator. Cost: two scalar outputs on the node (section 3.4). |
| 7 | Scope of v1. | Two-bone limb, IK/FK blend, pole, `stretch`, `pin`, `roll`, pole spaces, matched switch, spaces on `start` and `ik` through `rigConstraint`. **Not in v1, and not this module:** IK over three or more bones, spline IK, twist joints, bend bow, ribbon, reverse foot (soft IK is decision 10). A limb with more than three joints is an error, not a longer chain (section 5). | Each is its own gate. Section 7.4 keeps them open. |
| 8 | Names. | Module type `IkLimb`, node `ikLimbModule`, MTypeId `0x00218F` (checked free 2026-11-15), channels `ikFkBlend`, `stretch`, `soft`, `pin`, `roll`, and `space` on the pole. | Names are public API. They follow D34: camelCase, no side or module in them, 0 at the first-named end. |
| 9 | New commands. | **Two:** `setRigIkFkBlend` and `setRigPoleSpace` (section 7.2). | The matched switch needs scene access and undo, so it is a C++ command like `setRigConstraintSpace`. |
| 10 | Soft IK in v1? | **Yes, if you agree.** One channel, one attribute and one formula (section 4.2a). The author said it can wait. | Adding it later is a new channel on the settings control of every existing rig, which means a rebuild (D33) and a new golden snapshot, but no template key and no rename. The solver is written for it either way (`computeTwoBoneIk` takes the soft distance). |
| 11 | Matched values as node outputs. | **Yes.** Two output arrays, `fkMatchLocal` and `ikMatchLocal`, computed only when something pulls them. | Gives "always matched" as data (answer to requirement 5) and lets `setRigIkFkBlend` read plugs instead of needing a live single-module solve. Cost: two more output attributes with `attributeAffects`, no playback cost (section 4.1). |
| 12 | Roll. | **Answered:** a `roll` angle channel on the IK handle, default 0, `mirrorNegate`. | It rotates the plane, so it lives with the handle. A reflection reverses the sense of a rotation about the limb's axis, and that axis points the other way on the mirrored side, so the mirrored pose negates it (D34 needs this declared). |
| 13 | The inverse contract gets scalar channel values. | **Yes.** `ModuleInverseContext::channelNames` and `ModuleInverseInput::channels`, filled by the driver from the sidecar, else the rig's current channel value. | D41 made a module's inverse matrices only. Soft, stretch, pin, roll and the pole space are scalars the inverse needs, and it must use the same values the forward solve will (a mismatch misses the pose). Existing modules declare none. A D41 amendment goes in `decisions.md`. |
| 14 | The settings control can't follow this limb's own wrist, and takes no constraints. | **Accept.** Parent it to a sibling socket (the clavicle, the layout). | `parentStructure` can't name this module's own sockets (D17). All its transform channels are locked, so a space switch couldn't match. A module-owned follow like the pole's can be added later with a new label or channel. |

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
- Constraints may target `start` and `ik` (D31, `rigConstraint` gives each its `space` enum, with D27 matching). **A constraint on `pole` or `settings` is an error.** The pole owns its `space` enum (section 4.4), and the settings control has every transform channel locked, so a switch couldn't match (decision 14).
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
- **`ModulePlugInfo` gets one new field**, `allowsConstraint` (default true), false for `pole` and `settings`. `RigTemplateValidation` reports a constraint on a plug that doesn't allow one, in the read phase. Without it the build would fail late: `createConstraint` refuses a control that already has `space` (`Constraints.cpp`), after the scene has changed (D17).
- **Sockets `index_0` to `index_2` are the blended joint worlds, not the FK controls' worlds.** A hand's fingers must follow the joint whichever mode is active. In FkChain the control world and joint world are the same, so this is new.
- **Those three sockets have no DAG transform** (`socketInfo.oTransform` null, as Aim does for its joint). `reparentChildPlugToSocket` DAG-parents a child module's control under the socket's control, and visibility inherits through the DAG even though transforms don't (D1). A finger control under a hidden FK hand control would be hidden in IK. The `ik`, `pole` and `settings` sockets keep their control.
- **DAG organization:** FK lower under FK upper, FK end under FK lower. `ik`, `pole` and `settings` are not under the FK controls, for the same visibility reason.
- Rotate orders: FK controls XYZ (D40), IK handle XYZ. Pole and settings pass XYZ too (their rotation isn't a channel). Every control passes its order to `createRigControl`.

### 3.3 Channels (D34)

| Control | Keyable channels | Locked and hidden |
|---|---|---|
| FK upper (start), lower, end | translate, rotate, scale, as FkChain | none |
| `ik` | translate, rotate, `roll`, `space` when constrained | scale |
| `pole` | translate, `pin`, `space` (the module's own: `Parent`, `Ik`) | rotate, scale |
| `settings` | `ikFkBlend`, `stretch`, `soft` | translate, rotate, scale |

- `ikFkBlend`: double 0 to 1, **default 1**, 0 is FK and 1 is IK. `stretch`: double 0 to 1, default 0 (0 clamps at full reach, 1 stretches). `soft`: double 0 to 1, default 0 (0 is a hard stop at full reach, section 4.2a). `pin`: double 0 to 1, default 0 (0 free, 1 the elbow sits on the pole). `roll`: an angle (degrees in the UI), default 0, unbounded, on the `ik` control. It rolls the IK plane about the root-to-handle axis (section 4.2). The pole's `space`: enum `Parent` (0), `Ik` (1), default 0, append-only labels.
- Every channel is `mirrorCopy` except `roll`, which is `mirrorNegate` (a reflection reverses the sense of a rotation about the limb's axis, and that axis points the other way on the mirrored side). Each is declared through `RigPoseScene::setMirrorMode` before `addAttribute`. Defaults equal rests (D34). `ModuleContract` runs `getRigControlChannels` on every control, so a channel with no mirror category fails.
- The new names (`ikFkBlend`, `stretch`, `soft`, `pin`, `roll`) go in `docs/controls.md` the first time (D34 naming rule). `space` on the pole is the same concept and the same name as the constraint's.
- Channel names are constants in `RigAttr` (`IkFkBlend`, `Stretch`, `Soft`, `Pin`, `Roll`), used for the control attribute and the module input attribute (D7).

### 3.4 Module attributes

New on the node. The channel inputs are single plugs, not arrays: a limb has one of each.

- Inputs: `ikFkBlend`, `stretch`, `soft`, `pin` (doubles), `roll` (an angle) and `poleSpace` (short), each connected from the owning control's attribute in `createModule`, in the same modifier.
- Stored at build: `ikRestOffset` (a matrix array of 3, section 4.3), `restBendNormal` (a vector, the plane fallback in 4.2), `restPlaneAngle` (a double, section 4.3), `limbLengths` (the two rest bone lengths). Same pattern as `aimRestOffset`.
- Outputs: the shared `controlOPM`, `jointOPM`, `outputSocketMatrix`, plus `fkVisible` (drives the FK lower and end controls) and `ikVisible` (drives the IK handle and pole), doubles, not writable, not storable, connected to the controls' `visibility` in `createModule`.
- Match outputs: `fkMatchLocal` (matrix array, slots 0 to 2) and `ikMatchLocal` (matrix array, slots 3 and 4). Not writable, not storable, connected to nothing. Maya computes an output only when someone pulls it, so they cost nothing during playback. `compute()` has a separate branch for them (section 4.1).
- `declareStandardAffects()` covers the shared inputs. Every new attribute gets `attributeAffects` to every output it changes: `ikFkBlend` to every output, `stretch`, `soft`, `pin`, `roll`, `ikRestOffset`, `restBendNormal`, `restPlaneAngle` and `limbLengths` to `jointOPM` and `outputSocketMatrix`, every input to `fkMatchLocal` and `ikMatchLocal`, `poleSpace` to the pole's `controlOPM` and everything the pole feeds, and `ikFkBlend` to `fkVisible` and `ikVisible`. Missing `attributeAffects` is the common silent bug (CLAUDE.md).
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
IK solve       : positions and plane from (rootPos, ikWorld, poleWorld, lengths, stretch, soft, pin, roll)   (4.2)
IK joint worlds: ikJointWorld[i] = ikRestOffset[i] * frame_i                                       (4.3)
Blend          : jointWorld[i] = blend(fkWorld[i], ikJointWorld[i], ikFkBlend)                     (4.5)
Outputs        : jointOPM[i] = socket[i] = jointWorld[i] for i in 0..2
                 jointOPM[3..5] = socket[3..5] = the marker controls' own worlds
                 controlOPM[i] = control i's rest carried by its own parent delta (excludes the local)
                 fkVisible, ikVisible = see decision 6
Match outputs  (a separate compute branch, pulled only by tools, never during playback):
                 fkMatchLocal[0..2], ikMatchLocal[3..4] = IkLimbNode::inverse run on the blended
                 jointWorld[0..2] above with this evaluation's parent deltas. They are the control
                 local matrices that would reproduce the current pose on each side.
```

The IK root takes the start control's **translation** only. Its rotation turns the FK upper joint and does nothing to the IK chain, because the IK plane comes from the pole. Say this in `docs/modules.md`.

Blend at exactly 0 returns the FK worlds and at exactly 1 the IK worlds, with no float residue and without running the unused solve. The FK upper control is always evaluated, because the IK root follows it. FK lower and end are skipped at blend 1, and the IK solve at blend 0. This matters for animators who keyed 0 or 1 and for the inverse round trip.

### 4.2 Two-bone solve with stretch, pin and roll

`MathUtils::computeTwoBoneIk` in `maya/shared/Utilities.h`, next to `computeAimMatrix`. It solves exactly two bones in closed form and is not a general chain solver (a multi-bone solver would be a separate function). It is shareable (a ribbon module or another two-bone user can call it), so it goes in the shared layer when written (CLAUDE.md, "Look before you write", step 6). It takes `rootPos`, `handlePos`, `polePos`, `L1`, `L2`, `stretch`, `soft`, `pin`, `roll`, `restPlaneAngle`, the rest bend normal and the rest axis, and returns the three positions and the plane normal.

1. Reach `d = |handle - root|`, axis `a = normalize(handle - root)`. Below a small epsilon the axis is the rest axis (the rest handle's position minus the rest root's, both stored as control rests), so a handle on the root gives no NaN.
2. Soft reach `de = softReach(d)` (section 4.2a). With `soft = 0` it is `min(d, L1 + L2)`.
3. Stretch scale `s = lerp(1, d / de, stretch)` (never below 1, since `de <= d`). Bone lengths `L1s = L1*s` and `L2s = L2*s`. Base distance `dc = de * s`, which is `lerp(de, d, stretch)`, clamped to `[|L1s - L2s| + eps, L1s + L2s]`. With `soft = 0` this is the plain rule: a handle past full reach with `stretch = 0` leaves the end at full reach on the line to the handle, like any native IK, and with `stretch = 1` the end reaches the handle.
4. Plane normal. `n0 = normalize(pole - root - a * dot(pole - root, a))`. If its length is below 1e-4 (pole on the line), use `restBendNormal` projected perpendicular to `a`, then any perpendicular. Then `n = rotate(n0, about a, restPlaneAngle + roll * (1 - pin))`. `roll` turns the plane, and so the bend direction, about the root-to-handle axis: positive is right-handed about `a`. `restPlaneAngle` is the build's correction so IK at rest reproduces the rest bend (4.3). The solver is **stateless** (D36), so a pole crossing the line flips the limb the way a native IK does. Say so in `docs/modules.md`.
5. Law of cosines: `x = (dc^2 + L1s^2 - L2s^2) / (2 dc)`, `h = sqrt(max(0, L1s^2 - x^2))`. `midFree = root + a*x + n*h`, `endFree = root + a*dc`.
6. **Pin:** `mid = lerp(midFree, polePos, pin)` and `end = lerp(endFree, handlePos, pin)`. At `pin = 1` the elbow sits on the pole control and the wrist on the handle, so both bones lengthen or shorten to fit. Pin wins over stretch. The pinned mid sits on the pole, which is not in the rolled plane, so roll fades out with pin: the roll used in step 4 is `roll * (1 - pin)`.

Hand-worked checks for pose cases: `L1 = 3`, `L2 = 4`, root `(0,0,0)`.
- Handle `(5,0,0)`, pole toward `+Y`: `x = (25+9-16)/10 = 1.8`, `h = sqrt(9-3.24) = 2.4`, `mid = (1.8, 2.4, 0)`.
- Handle `(0,5,0)`, pole toward `+X`: `mid = (2.4, 1.8, 0)`.
- Handle `(10,0,0)`: stretch 0 gives end `(7,0,0)` and mid `(3,0,0)`. Stretch 1 gives end `(10,0,0)` and mid `(30/7,0,0)`.
- Handle `(5,0,0)`, pole at `(2,3,0)`: pin 1 gives mid `(2,3,0)` and end `(5,0,0)`. Pin 0.5 gives mid `(1.9, 2.7, 0)`.
- Handle `(5,0,0)`, pole toward `+Y`, `roll` 90 degrees: the plane normal turns from `+Y` to `+Z` (right-handed about `+X`), so `mid = (1.8, 0, 2.4)`. Roll 180 gives `(1.8, -2.4, 0)`. The mirrored limb at roll -90 gives the reflected pose.
- Rest plane angle: a rest limb bent toward `+Z` (`mid = (1.8, 0, 2.4)`) with the pole joint toward `+Y` stores `restPlaneAngle = +90` degrees, and IK at rest reproduces the rest mid.

### 4.2a Soft IK (reserved in v1's solver, a channel on the settings control)

Soft IK eases the reach as the handle nears full length, so the elbow doesn't snap straight. The usual function is an exponential. With `L = L1 + L2`, a softness distance `ds` and `da = L - ds`:

```
d <= da :  de = d
d >  da :  de = da + ds * (1 - exp(-(d - da) / ds))
```

- **Why it works.** At `d = da` the value and the slope (`exp(0) = 1`) both match the linear part, so there is no kink. As `d` grows, `de` approaches `L` and never reaches it. The end of the chain slows smoothly instead of locking.
- **Units.** The channel `soft` is 0 to 1 and `ds = soft * L`, so it scales with the rig. `ds` under a small epsilon means a hard stop (`de = min(d, L)`), which also avoids dividing by zero.
- **With stretch.** `s = lerp(1, d / de, stretch)` (section 4.2) scales the bones so the end still reaches the handle with the same softened bend. With `stretch = 0` the end stops short of the handle by `d - de`, which is the point of soft IK. The end joint's rotation still comes from the handle.
- **With pin.** Pin blends the positions at the end (step 6), so pin 1 ignores soft. That is the intended override.
- **Hand-worked check** for pose cases: `L1 = 3`, `L2 = 4`, `soft = 1/7` so `ds = 1`, `da = 6`. `d = 6` gives `de = 6`. `d = 7` gives `de = 6 + (1 - e^-1) = 6.632121`. `d = 8` gives `de = 6 + (1 - e^-2) = 6.864665`. With `stretch = 1` at `d = 7`: `s = 7 / 6.632121 = 1.055470`, bones `3.166410` and `4.221880`, end at `7`.
- **Why it makes the solve harder, and what doesn't change.** The forward solve is still closed form: `softReach` only remaps `d` before the law of cosines, and `exp` is one call in `compute()`. The plane, the frames, the blend and the pin are untouched. What gets harder is the **inverse** (section 6, item 10), because the handle's distance has to be recovered from the end joint's, and it blows up near full extension. A second cost is that the end joint no longer sits on the handle, which animators will notice when the handle is pulled far.
- **Other shapes.** A polynomial or smoothstep ease is possible. The exponential is the standard because it is C1 at `da` and has a closed-form inverse.
- **Not in the solver's contract:** a minimum-reach (folded) soft zone. Add it only if animators hit it.

### 4.3 Joint frames and rest offsets

Joint rest orientations are arbitrary, so no axis assumption is made (D40's +X down the bone is a default for FK controls only). Each IK joint is a *frame* built from the solved positions, times a stored rest offset.

- Root frame: X toward `mid`, Z along the plane normal `n` of 4.2 (made perpendicular to X). Mid frame: X toward `end`, same `n`. End joint: translation from the solved end, **rotation from the IK handle's world** (rotating the handle turns the hand), scale ignored.
- One frame builder, used by `createModule` (to compute the rest offsets), `compute()` and `inverse()`, so they can't drift (D41, P3). It is `computeAimJointWorld` and `computeAimRestOffset` again for three joints: `ikRestOffset[i] = jointRest[i] * frameAtRest[i]^-1`.
- At rest the IK joints equal the joint rests, wherever the rigger put the pole. `createModule` stores `restPlaneAngle`: the signed angle about the root-to-handle axis from the pole's direction to the rest mid joint's direction. IK at rest then reproduces the rest bend even with the pole off the limb's plane or on the other side. A straight rest limb stores 0 and the pole only sets the plane. `restBendNormal` is the rest mid's perpendicular direction when the limb is bent, and the pole's otherwise. (The first draft required the pole on the mid joint's side and in its plane, and would have broken the rest pose for any other placement. Section 9.)

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
| Handle on the root | `dc` clamps to `\|L1s - L2s\| + eps` and the axis falls back to the rest axis, so the limb folds, no NaN. |
| Pole on the line | Rest bend normal, then any perpendicular (4.2). |
| Zero-length rest bone | Plan error (section 5). |
| Blend, stretch, soft, pin outside 0 to 1 | The attributes are clamped (min 0, max 1). |
| Pole `space` set directly | The pole moves, no match. Use `setRigPoleSpace`. |

## 5. Read-phase validation (`planFromJson`)

All errors go to `ctx.report` with the JSON path, and every selector is claimed even after a failure so the report lists all of them (the FkChain and Aim pattern).

1. `joints` has exactly three selectors. Any other count is an error that says this is a two-bone IK limb and that longer chains need a different module. `ik`, `pole`, `settings` each resolve to one joint, and none of the six is claimed twice (`claimJoint` covers it).
2. Rest bone lengths `L1` and `L2` are above a minimum (as `kMinAimDistance`).
3. The pole joint is not on the root-end line (as `kMinAimUpSine`). Its side and plane don't matter: `restPlaneAngle` absorbs them (section 4.3).
4. The `ik` joint is within a tolerance of the end joint's position. An offset handle is a separate feature: error for now.
5. Shared control markups are caught by `validateControlMarkup`. `checkMirrorPositions` works through `claimJoint` for all six.
6. In `core/`, `RigTemplateValidation`: a constraint whose target plug has `allowsConstraint == false` is an error (section 3.2).

## 6. Inverse (D41, `inverse-solve.md` section 8)

`IkLimbNode::inverse` takes world matrices by slot and returns control local matrices (`ModuleInverseInput`, `ModuleInverseResult`). **It also needs scalar channel values**: `soft`, `stretch`, `pin`, `roll` and the pole's `space`. D41 made the contract matrices only, so decision 13 adds `ModuleInverseContext::channelNames` (the names the module wants, in a fixed order) and `ModuleInverseInput::channels` (a fixed-size array, no allocation, so `compute()`'s match branch can call the same function). The driver fills it from the sidecar if the channel is there, otherwise from the rig's current channel value, so the inverse always uses the value the forward solve will use. Assuming `soft = 0` while the rig has `soft > 0` would make the forward solve pull the wrist inward and miss the pose. Modules with no scalars declare none, so FkChain, Aim and Layout don't change.

1. **FK controls** from `jointWorld[0..2]` with `ModuleInverse::solveChain` on three slots with `useJoints = true`. Translate, rotate and scale are all derived, so a stretched pose is reproduced in FK.
2. **IK handle** (slot 3): the end joint's world, then `ModuleInverse::localFromWorld` with the `ik` parent delta.
3. **Pole** (slot 4): `a` is the unit root-to-end axis. `nObs` is the unit perpendicular from the root-end line to the mid joint. Undo the plane rotation: `n0 = rotate(nObs, about a, -(restPlaneAngle + roll * (1 - pin)))`. The pole's world position is the point on the line nearest the mid joint, plus `n0` times the rest pole distance. Rotation stays at rest. Then `localFromWorld` with the delta of the current `poleSpace`. **In `Ik` space that delta must come from the matched handle** (the end joint's world, `computeDeltaFromRest(ikRest, endWorld)`), not the handle's current world, because the match writes the handle and the pole together and the pole's space moves with the handle. With `pin > 0` the pole goes to the mid joint's position and the handle to the end joint's, which is exact at `pin = 1` and an approximation between (item 11).
4. **Settings** (slot 5): no joints. Transform from the fallback (the sidecar, or rest), as Layout does.
5. **Marker joints** (slots 3 to 5) are `export: false`, so the source skeleton normally lacks them. The IK inverse derives its controls from slots 0 to 2 and ignores `jointWorld[3..5]` unless the source has them.
6. `socketWorld` returns the forward result for every slot, so the sweep feeds modules below.
7. **`solved = false`** when any limb joint is missing, with a reason. The driver then takes the controls from the sidecar, then rest.
8. **Channels from joints:** transforms yes. `ikFkBlend`, `pin`, `space`, `soft`, `roll` and `stretch` come from the sidecar, never derived (`roll` is redundant with the pole's position, so the pole is derived with the channel's value compensated, item 3). (`inverse-solve.md` requirement 3 said `stretch` is derivable from the length ratio. It isn't reliably, because below full reach the joints can't tell stretch 0 from 1, and with `pin` the bone lengths mean something else. See section 9.)
9. **A source whose bone lengths differ from the rest** is reproduced exactly by FK. IK reproduces it only with `stretch` or `pin` on, so the restore warns when IK can't match the joints.
10. **Soft IK in the inverse.** Given the observed end distance `de` and the `soft` value (from the sidecar, or live in the matched switch), the handle's distance is `d = da - ds * ln(1 - (de - da) / ds)` for `de > da`, and `d = de` otherwise. That is closed form, but it goes to infinity as `de` approaches `L`: a fully extended FK arm has no finite handle. Clamp `de` to `L * (1 - 1e-4)` first, so the handle lands a few `ds` past the wrist (about `7 * ds` for `L = 7`, `ds = 1`) and the wrist error is 1e-4 of the limb. With stretch on, the bones' scale is observable when `pin = 0`: `s = |mid - root| / L1`. Then `de = |end - root| / s` and the formula above gives `d`, still closed form. (Solving from the end distance alone would mix a linear and an exponential term and need Newton or Lambert W.) With `soft = 0` this reduces to the hard rule. Near full extension the handle's error is the joint error times about `ds / (L - de)`, so a noisy mocap source makes the handle curve jitter while the wrist stays right. That is accepted. The round-trip tests include a soft case and an extended one.

11. **Exactness.** FK is reproduced exactly at any blend, because its controls carry translate, rotate and scale. IK is exact at blend 0 and 1 for a pose it can make: bone lengths equal to the rest, or `stretch` or `pin` on. At `0 < blend < 1` the pose is a lerp of two valid poses, so a bone can be shorter than its rest (the chord), and IK reproduces it only approximately. With `0 < pin < 1` the inverse isn't closed form, so the handle goes on the wrist and the pole on the elbow. Both cases warn, and the matched switch is meant for blend 0 and 1.

## 7. Library, commands and Python

### 7.1 What works with no change

Every item keys on control markup and channel name, so a limb is in them from the first build. Each needs a test (section 8, step 11) and a docs line.

- `getRigControls(moduleType=["IkLimb"])`, `getRigControlInfo`, `getRigControlChannels` (the new channels with rests, ranges and mirror modes), `getRigMirrorControl`.
- `zeroRigControls` (every channel to its rest: IK, blend 1, stretch 0, soft 0, pin 0, roll 0, pole space `Parent`), `mirrorRigPose`, `flipRigPose` (copy for all new channels, D34).
- `saveRigPose` and `loadRigPose`, `saveRigAnimation` and `loadRigAnimation` (D34, D42), with `mirror` and `offset`. `ikFkBlend`, `stretch`, `soft`, `pin` and `roll` are floats on default tangents. `space` keys stepped.
- Control shape save, load and mirror for the six controls (D26).
- `createRigConstraint` and `setRigConstraintSpace` on `start` and `ik`, and template `constraints` entries.
- `rigmodules.pose.key_controls` and `key_modules`, which read `get_channels`.
- Selection sets (D39), display layers (D38) and `rig_all` (D43): the build makes them for any module.
- `animateRigFromJointClip` and `poseRigFromJointPose`, through the inverse.

Shape choices: the shape type comes from the module's `shapeType`, one for all six controls, like every module (the shape file lets a rigger edit them per asset, D26).

### 7.2 The matched switch: two new commands (D14 shape, `commands/IkLimbCmds.h/.cpp`)

**`setRigIkFkBlend(control | module, blend=1.0)`** (the settings control or the module node; objects by name, never the selection). It matches both sides to the limb's live joints, then sets `ikFkBlend`:

1. Pull the module's match outputs, `fkMatchLocal` (slots 0 to 2) and `ikMatchLocal` (slots 3 and 4). They hold the control local matrices that reproduce the current pose on each side, computed by `IkLimbNode::inverse` inside the node from the live joints and parent deltas (section 4.1). No scene reads of joints or deltas are needed.
2. Convert each local to channel values with the driver's matrix-to-channels conversion (euler nearest the current values, the control's own rotate order), and write them through `RigPoseScene`. Locked or driven channels keep their values and warn, as in D27 and D41.
3. Set `ikFkBlend` to `blend`.

Both sides reproduce the current pose, so the result is that pose at the new blend, and the switch doesn't pop in either direction **at blend 0 and 1**. A pose IK can't reproduce (section 6, items 9 and 11: lengths off the rest without stretch or pin, a blend between 0 and 1, a partial pin) warns. The command sets no keys. A keyed switch (key the frame before, switch, key the frame) is a tool in mayadev's pose menu on top of this command, like the keyed space switch D34 lists. Undoable as one step.

**`setRigPoleSpace(pole, space=n | label=l)`** switches the pole's `space` and keeps its world matrix, by D27's formula: `L' = L * OPM * D_new^-1 * D_old * OPM^-1`. `D_old` and `D_new` are the two deltas of section 4.4, read from the scene. Undoable. Only the pole takes it, and a direct `setAttr` on the enum doesn't match.

**Shared machinery, not new logic per command:**
- The driver's matrix-to-channels conversion (in `RigInverse.cpp` today, private to the driver) moves to `RigPoseScene` or a shared helper, so a command can use it. `RigInverse::solve` keeps calling it.
- D27's match formula is already in `RigConstraintNode::matchSwitchSpace`. Factor the formula (new local from old local, old and new delta, the control's OPM) into one shared function and call it from both. Check `matchSwitchSpace`'s other callers first.
- No live single-module solve is needed: the node computes the match matrices itself (decision 11). A partial JointClip would be wrong here, because the sweep takes parent sockets from rest and gets the parent deltas wrong.

### 7.3 Python

No Python is required. `rigmodules.query` already reads the new controls and channels. A wrapper for the two commands is not needed, they are `cmds`. mayadev's pose menu gets the keyed switch later (D24: mayadev depends on RigModules).

### 7.4 Later: bendy limb, ribbon, twist, bend bow

Not built, and nothing in v1 closes the door.

- **Recommended shape: a separate module** that reads this module's sockets (`index_0` to `index_2`, the blended joint worlds), and `settings` if it needs a channel, and drives a ribbon chain with distributed twist and bend bow. The blended joints are already the single source of the limb's pose in both modes, so a ribbon module doesn't care how they were made. The IK solver and blend are in `MathUtils`, so it can reuse them.
- **If it becomes one mega module** (the author has seen both), the limb gains optional `moduleArgs` keys. A new optional key is a `moduleVersion` step with a default (D33), and new channels (`twist`, `bendBow`) are appended with their mirror mode. Existing names don't change.
- To keep that cheap: the three socket families, the six slots and the channel names above are stable, and slot numbers are never reused.

## 8. Build order

Each step ends with the full suite on a local build. Cloud sessions can do the Maya-free parts and docs (CLAUDE.md, "Environments"): steps 1, 2, 3, 11 (fixtures) and 15.

1. **Sign off** section 2 (decisions 1, 5, 7, 10, 13 and 14 are still open). Then add `D47. IK limb` to `design/decisions.md` (the rejected alternatives from section 0 and 2, the consequence), replace "The next module is the IK limb" in "Open items", update `inverse-solve.md`'s status line, and promote this page to `design/ik-limb.md`.
2. **Hand-worked pose cases first** (`tests/maya/fixtures/poses/ikLimb/*.json`) and validation cases (`fixtures/validation/`), before any code, from the numbers in 4.2. Add the skeletons (`fixtures/skeletons/ik_limb.json`, `ik_limb_mirror.json`: a bent limb, the pole on the bend side, three non-export marker joints) and templates.
3. **Constants.** `core/Constants.h`: `RigAttr::IkFkBlend`, `Stretch`, `Soft`, `Pin`, `PoleSpace`, `FkMatchLocal`, `IkMatchLocal`, `IkRestOffset`, `RestBendNormal`, `LimbLengths`, `Roll`, `RestPlaneAngle`, `FkVisible`, `IkVisible`, and `PlugAndSocketNames::Ik`, `Pole`, `Settings`.
4. **`core/` validation.** `ModulePlugInfo::allowsConstraint`, the constraint check in `RigTemplateValidation`, and its validation case.
5. **Shared math.** `MathUtils::computeTwoBoneIk` and `MathUtils::blendMatrices` in `maya/shared/Utilities.h/.cpp`. Callers: none yet. Then the inverse contract change (decision 13): `ModuleInverseContext::channelNames` and `ModuleInverseInput::channels` in `maya/shared/ModuleInverse.h`, filled by `RigInverse` from the sidecar, else the scene's current channel value. Check every caller of `ModuleInverseInput`: FkChain, Aim and Layout declare no channels, so their inverses don't change.
6. **Shared custom channels.** The first module with them. One shared helper that adds a keyable double (or enum) channel to a control with a default, a range and a mirror mode, connects it to a module attribute, and queues the locks and hides for the other channels. Look at the `space` enum in `Constraints.cpp` and at `DagUtils::queueLockAndHideTransform` first and extend them: give `queueLockAndHideTransform` an optional list of channels to keep instead of writing a second version. State in the summary what was reused and what is new.
7. **Module files.** `maya/modules/IkLimbModule.h/.cpp` from FkChain and Aim: `IkLimbSlot`, `IkLimbArgs` with `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(IkLimbArgs, joints, ik, pole, settings)`, `creator`, `initialize` (create, add and `attributeAffects` together for each attribute), `compute` (one solve, `commitOutput` for each array), `createModule`, `planFromJson` (section 5), `inverse` (section 6) and the file-local frame builder.
8. **Register.** One row in `ModuleRegistry.cpp`: `"ikLimbModule", "IkLimb", IkLimbNode::id, creator, initialize, {}, planFromJson, inverse, true`. Add every new file to `cpp/RigModules.vcxproj` and `.filters`. Add the type to `tests/maya/concepts/test_plugin.py` and the MTypeId to `CLAUDE.md`.
9. **Commands.** The shared machinery of 7.2, then `commands/IkLimbCmds.h/.cpp` (`setRigIkFkBlend`, `setRigPoleSpace`), a row in `CommandRegistry.cpp`, the project files, and the long flag names in `concepts/test_plugin.py`. Each `addFlag` in `CHECK_MSTATUS`.
10. **Rotate order and locks.** The build locks rotate order on every control (D40). Locks and hides per section 3.3.
11. **Tests (tier 4, `modules/test_ik_limb.py`; commands in tier 5).**
    - `IkLimbContract(ModuleContract, RigTestCase)` with `JOINT_OWNERS` for all six joints, `MIRROR_TEMPLATE` and `MIRROR_SKELETON`. It gives the build, rest, drive, no-inherit, undo/redo, one-solve, mirrored channels and **inverse round trip** tests.
    - `add_pose_cases(IkLimbPoses, "ikLimb")`: rest; handle moved; pole moved to either side; the start control translated in IK (the chain follows); unreachable with and without stretch; pin 0.5 and 1; roll 90 and 180 and the mirrored limb's negated roll; a pole joint off the rest plane (`restPlaneAngle`); blend 0, 0.5 and 1 (the default is 1, so FK cases set `ikFkBlend` to 0 and a rest case at the default is IK); soft IK at the four distances in 4.2a, with and without stretch; pole `Ik` space with a handle move; a rotated parent; a mirrored limb.
    - Behaviour tests: blend 0 is exactly FK and 1 exactly IK. Blend 0.5 is the lerp and slerp of the two. The hand follows the handle's rotation. Visibility by blend, with the start control always visible. Sockets `index_*` follow the blended joints, and a child module's controls stay visible at blend 1. A pole on the line takes the fallback plane. A constraint on `pole` or `settings` fails validation. `start` and `ik` take `rigConstraint` spaces.
    - Command tests (`commands/test_ik_limb_cmds.py`): `setRigIkFkBlend` from FK to IK and back leaves every joint where it was, at rest, posed in each mode, with stretch and with pin, with a space switch on `ik`; a warning when IK can't match; locked channels warn; undo and redo; scopes and every error. `setRigPoleSpace` keeps the pole's world matrix in both directions and undoes.
    - Inverse tests: the round trip at blends 0 and 1 (and a warning at 0.5), a missing joint, a sidecar carrying `ikFkBlend`, a stretched source, soft IK with and without stretch, a roll, and a rig whose current `soft` differs from the sidecar's (the inverse must use the same value the forward solve does). The matched switch in `Ik` pole space leaves the pole's world and the joints where they were.
    - Validation cases: pole on the line, pole on the wrong side, `ik` far from the end joint, a joint claimed twice, a zero-length bone, a wrong joint count, a `parentStructure` value that names the limb's own socket (cycle), a constraint on `pole` or on `settings`.
    - Golden snapshot `golden/ik_limb.json` from `--update-golden`, reviewed. Assign every new test file a tier in `TIERS` in `tests/run_tests.py`. Every check a probe made becomes a suite test or a listed proposed test (CLAUDE.md).
12. **Docs example test** in `tests/maya/concepts/test_docs_examples.py` for any new worked example.
13. **Build and run.** MSBuild, then `cmd /c tests\RigTests.cmd`. Report the TOTAL line and any failed, crashed or blocked tier. Needs local build and Maya verification, so the cloud session can't do this step.
14. **Performance rig and `bench` (D36).** Add `limb_specs()` to `tools/perf/perf_scenes.py` beside `chain_specs()` and `aim_specs()` (about 12 limbs: arms and legs of the existing skeleton, three marker joints each), extend the skeleton, template and instance builders and `check_perf_scene.py`, run `mayapy tools\perf\perf_scenes.py check`, then `bench` on the 20-rig scene (30 fps target) and the 50-rig scene (within 1.5 times the native floor). Report the fps, and say so when below target. The rig grows, so search the docs and D36 for "200 joint" and "51 constraints" and fix them. The native floor has no tool, so that comparison is manual.
15. **Docs pass** (CLAUDE.md, "Working style"). Update:
    - `docs/modules.md`: an `IkLimb` section (JSON, args, node table, controls, child plugs, sockets, channels, start control, stretch, soft, pin, roll, pole spaces, the pole-on-line behaviour, the own-socket rule).
    - `docs/templates.md`: the `IkLimb` args and a mirrored limb example.
    - `docs/controls.md`: `ikFkBlend`, `stretch`, `soft`, `pin`, `roll`, and the markup pattern of marker joints.
    - `docs/commands.md`: `setRigIkFkBlend`, `setRigPoleSpace`, and the `IkLimb` `moduleType` filter value.
    - `docs/library.md`: what a limb's pose and animation hold, and that a raw `ikFkBlend` change doesn't match.
    - `docs/architecture.md`: the module table row, the slot table, the shared helpers, the commands table, the recipe notes for custom channels.
    - `README.md`: the modules table and command list.
    - `CLAUDE.md`: repo map rows (`modules/IkLimbModule`, `commands/IkLimbCmds`, the new shared functions), the Maya commands list, the node types and MTypeIds list (`ikLimbModule` `0x00218F`), and the "Existing modules" line.
    - `design/decisions.md` (D47, a D41 amendment for the scalar channel values, Open items), `design/inverse-solve.md` (status, section 4's contract sketch and section 8), `design/module-directions.md` (the IK limb is built), `design/control-library.md` (its build order), `design/mayadev-integration.md` (IK/FK match rows).
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
- **The match outputs must equal what the command writes.** `fkMatchLocal` and `ikMatchLocal` come from the same `inverse` the restore uses (D41, P3), so a test sets the controls from them and checks the joints don't move, at blends 0, 0.5 and 1, posed, stretched, pinned and soft.
- **Soft IK near full extension.** The forward solve is smooth. The inverse is ill-conditioned there (section 6, item 10), so the matched switch from an extended FK arm puts the handle a few `ds` past the wrist.
- **Review fix: the pole's placement at rest.** The first draft required the pole joint on the mid joint's side of the root-end line, and ignored whether it lay in the limb's plane. A pole off that plane makes IK solve a different plane at rest, so the contract's rest-pose test would fail. `restPlaneAngle` (section 4.3) absorbs any placement.
- **Review fix: a handle on the root.** `normalize(0)` gave a NaN axis. The rest axis is the fallback (section 4.2, step 1).
- **Review fix: the pole in `Ik` space when matching.** The match writes the handle and pole together, so the pole's local must be computed against the matched handle, not the current one (section 6, item 3). A test checks the pole's world doesn't move.
- **Review fix: constraints on `settings`.** All its transform channels are locked, so D27's match couldn't run and every switch would warn. It takes no constraints (decision 14).
- **Review fix: matched switch exactness.** The draft claimed both sides reproduce any pose at any blend. IK doesn't at blends between 0 and 1 or with a partial pin (section 6, item 11).
- **Review fix: the inverse needs scalar channels.** Section 6 and decision 13. Without them the inverse and forward solves can disagree on `soft`, `stretch`, `roll` or the pole space.
- **Roll mirrors by negation.** D34 says transform channels copy and the joint orientation decides behavior or orientation mirror. `roll` is not a transform channel: it is a world-axis rotation about the limb's own axis, so a mirrored pose negates it. A mirrored pose case checks it.
- **The settings control can't follow the wrist.** Section 3.1 and decision 14. Animators often want it there, and D17 forbids it.
- **Compute cost.** Three blended joints plus an IK solve cost more than a chain solve. D36's `rigControlNode` premium dominates, but run `bench` before assuming anything.

## 10. Maintenance surface added

- One module node, its args struct and its inverse, one registry row.
- Six channel names (`ikFkBlend`, `stretch`, `soft`, `pin`, `roll`, and `space` on the pole with two labels), four child plug names, two socket families, three marker-joint markups per limb.
- Two commands (`setRigIkFkBlend`, `setRigPoleSpace`).
- Constants in `RigAttr` and `PlugAndSocketNames`, `ModulePlugInfo::allowsConstraint`, and scalar channel values in the inverse contract (`ModuleInverseContext::channelNames`, `ModuleInverseInput::channels`).
- `MathUtils::computeTwoBoneIk` and `blendMatrices`, the shared custom-channel helper, the shared match formula, the shared matrix-to-channels conversion and two match output arrays on the node.
- One golden snapshot, a pose case folder, two test files and fixtures, and a block in the performance rig.

Complexity stated (CLAUDE.md, "Complexity checks"): a new attribute set on a module (the first custom channels, the first scalar inputs, the first module-owned enum), a visibility output, three marker joints per limb, a new plug flag in `core/`, and two commands. Rejected as simpler: an IK-only two-bone solve with no blend (the problem is the switch), and a `poleFollow` slider (two controls for one idea, which the author declined).
