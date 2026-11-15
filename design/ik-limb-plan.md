# IK limb: design plan and build steps

**Status:** draft for sign-off. Nothing here is built, and the module has not passed the gate (D12) until the author signs off section 2. Written 2026-11-15 from the code, `decisions.md` (D1 to D46), `inverse-solve.md` and `module-directions.md`. Where those disagree, section 9 lists it.

The IK limb is the next module (author, 2026-11-10). This page is the plan to follow: the gate, the open decisions, the interface, the math, the build order, the tests and the docs pass. Build order is in section 8.

## 1. Gate (D12)

1. **Animator-facing problem.** Arms and legs that animators can pose by placing a hand or foot (IK) and by rotating joints (FK), switch between the two without a pop, and stretch when a pose reaches past the limb's length. Today `FkChain` gives FK only, and `Aim` points one joint at a target. Nothing solves a two-bone chain.
2. **Existing module check.** No small extension of FkChain or Aim covers it. FkChain has no solver, no blend and no custom channel. Aim solves one joint's orientation. A two-bone solve, a blend between two joint solutions and channels on controls are new concepts, and the blend needs its own node because all transform math lives in module nodes (D1).
3. **Design.** Sections 3 to 7.
4. **Maintenance surface.** Section 10.
5. **Performance rig and `bench`.** Section 8, step 12.

## 2. Decisions that need the author's sign-off

Each has a recommendation. Changing one after the module ships changes a template key, a control markup or a channel name, so all of them are format-level (D33, D34).

| # | Question | Recommendation | Why it matters |
|---|---|---|---|
| 1 | Where do the IK handle, pole and settings controls get their markup and rest? | **A: three marker joints in the skeleton**, selected in `moduleArgs` like Aim's `target` and `up`. Marked `export: false`, `skin: false`. B (rejected): derive usages (`handIk`, `armPole`) and compute the pole position from the limb. | A follows the Aim precedent (D15: markup says what a joint is, the template picks joints), puts the pole where the rigger put it, and needs no invented names. B hides a computed rest the rigger can't edit. Cost of A: three non-export joints per limb in the skeleton. |
| 2 | Where do `ikFkBlend` and `stretch` live? | **A dedicated `settings` control** (channel-only, always visible). Rejected: on the IK handle. | The IK handle is hidden in FK, so an animator would hunt for the switch (priority 1, D10). Cost: one more control and marker joint. |
| 3 | How does the pole follow the IK handle? | **A built-in channel `poleFollow` (0 to 1, default 0, `mirrorCopy`)** that blends the pole's parent motion toward the IK handle's motion. | The pole's usual space is the hand or foot. `rigConstraint` can't give it: the driver would be this module's own `ik` socket, and D17 counts a self-reference as a cycle (`parent_self_reference`, every module input affects every output). Alternatives: relax D17 to a per-slot graph (a change to the whole validator and to `attributeAffects` granularity), or split the IK handle into its own module. Neither is small. |
| 4 | What is the blend's default? | **0 (FK), no template key.** | A default is a template key, and a new key later is a `moduleVersion` step (D33). Legs are often IK by default, so the author should confirm before the key set is fixed. If legs need IK by default, add `"defaultBlend"` now. |
| 5 | FK controls: which channels are keyable? | **Rotate only.** Translate and scale locked and hidden (`DagUtils::queueLockAndHideTransform`, then unlock rotate). | Bone lengths come from the rest, so FK and IK can match at any blend. The inverse of a stretched IK pose can't be reproduced on locked FK translate, so it warns (section 6). |
| 6 | Hide controls by blend? | **Yes in v1.** FK controls visible when blend < 1, IK handle and pole visible when blend > 0, settings always. | Nothing surprising for the animator (priority 1). Cost: two scalar outputs on the node (section 3.4). Can wait for v1.1 without changing any name. |
| 7 | Scope of v1. | Two-bone limb, IK/FK blend, pole, `stretch`, `poleFollow`, spaces on `start`, `ik`, `pole`, `settings` through `rigConstraint`. **Not in v1:** twist joints, soft IK, pole lock to the knee, reverse foot, spline IK. | Each is its own gate. Twist distribution already has a legacy requirement in `mayadev-integration.md`. |
| 8 | Names. | Module type `IkLimb`, node `ikLimbModule`, MTypeId `0x00218F` (checked free 2026-11-15), channels `ikFkBlend`, `stretch`, `poleFollow`. | Names are public API. Channel names follow D34: camelCase, no side or module in them, 0 at the first-named end. |
| 9 | IK/FK match. | **No new command in v1.** Use `poseRigFromJointPose` scoped to the limb's controls on the current joints (section 7.3). | Section 7.3 shows it works with the inverse the module already needs. A dedicated command is a later, separate sign-off. |

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
- `ik` is placed at the end joint (its rest position and orientation, normally equal to the hand joint's). `pole` is placed out in front of the elbow or knee. `settings` is placed anywhere the animator will find it.
- Spaces: constraints target `start`, `ik`, `pole` or `settings`, each of which has a control (D31). `"drivers": ["layout.end", "clavicle_l.end"]` works as on FkChain.
- **`settings` can't take `arm_l.<anything>` as its parent.** D17 cycle rule. The same applies to `ik` and `pole`. This is why section 2, decision 3 exists.
- Mirroring needs nothing: `expandMirrors` swaps every `[side, usage]` selector, so `ik`, `pole` and `settings` swap with `joints` (the Aim keys already rely on this). `planFromJson` has no mirror branch (D28).

### 3.2 Slots, controls, joints, plugs, sockets

One slot index is shared by `controlInputData`, `controlOPM`, `jointOPM`, `outputSocketMatrix`, `childPlugs` and `socketNames`, like `AimPart` (`modules/AimModule.h`). A `namespace IkLimbSlot` in the header names them.

| Slot | Name | Control | Joint driven | Child plug | Sockets |
|---|---|---|---|---|---|
| 0 | `upper` | FK, markup of `joints[0]` | `joints[0]` | `start` | `index_0`, `start` |
| 1 | `lower` | FK, markup of `joints[1]` | `joints[1]` | none | `index_1` |
| 2 | `end` | FK, markup of `joints[2]` | `joints[2]` | none | `index_2`, `end` |
| 3 | `ik` | IK handle, markup of the `ik` joint | the `ik` joint | `ik` | `ik` |
| 4 | `pole` | pole, markup of the `pole` joint | the `pole` joint | `pole` | `pole` |
| 5 | `settings` | settings, markup of the `settings` joint | the `settings` joint | `settings` | `settings` |

- `ModuleInterface::childPlugs` is `start`, `ik`, `pole`, `settings`, each with a control. `sockets` is the names above. `controls` is the six markups, in slot order. `PlugAndSocketNames` gets `Ik`, `Pole`, `Settings` (`Start`, `End` and `Index` exist).
- **Sockets `index_0` to `index_2` are the blended joint worlds, not the FK controls' worlds.** A hand's fingers must follow the joint whichever mode is active. In FkChain the control world and the joint world are the same, so this is new.
- **Those three sockets have no DAG transform** (`socketInfo.oTransform` null, as Aim does for its joint). Reason: `reparentChildPlugToSocket` DAG-parents a child module's control under the socket's control, and visibility inherits through the DAG even though transforms don't (D1). A finger control under a hidden FK hand control would be hidden in IK. The `ik`, `pole` and `settings` sockets keep their control.
- **DAG organization:** FK lower under FK upper, FK end under FK lower. `ik`, `pole` and `settings` are not under the FK controls, for the same visibility reason.
- Rotate order: FK controls XYZ (D40), the `ik` control XYZ, pole and settings XYZ (their rotation isn't a channel). Controls are created through `RigControlNode::createRigControl`, which takes the order, so every control states it.

### 3.3 Channels (D34)

| Control | Keyable channels | Locked and hidden |
|---|---|---|
| FK upper, lower, end | `rotateX/Y/Z` | translate, scale |
| `ik` | `translateX/Y/Z`, `rotateX/Y/Z`, `space` (when constrained) | scale |
| `pole` | `translateX/Y/Z`, `poleFollow`, `space` (when constrained) | rotate, scale |
| `settings` | `ikFkBlend`, `stretch`, `space` (when constrained) | translate, rotate, scale |
| `start` (FK upper) | gets `space` when constrained, as on FkChain | |

- `ikFkBlend`: double 0 to 1, default 0, 0 is FK. `stretch`: double 0 to 1, default 0 (0 clamps at full reach, 1 stretches). `poleFollow`: double 0 to 1, default 0. All three are `mirrorCopy`, declared through `RigPoseScene::setMirrorMode` before `addAttribute`. Default equals rest (D34). Names go in `docs/controls.md` the first time (D34 naming rule).
- `ModuleContract` runs `getRigControlChannels` on every control, so a channel with no mirror category fails.
- Channel names are constants in `RigAttr` (`IkFkBlend`, `Stretch`, `PoleFollow`), used for the control attribute and the module input attribute. No string literals (D7).

### 3.4 Module attributes

New on the node, all inputs unless stated. None are arrays: a limb has one of each.

- `ikFkBlend`, `stretch`, `poleFollow`: doubles, connected from the settings control's and pole control's attributes (a connection made in `createModule`, in the same modifier).
- `ikRestOffset`: a matrix array of 3 (the three IK joints' rest offsets, section 4.3), storable, set at build. Same pattern as `aimRestOffset`.
- `restBendNormal`: a vector, storable. The fallback plane when the pole lies on the root-to-handle line (section 4.4).
- `limbLengths`: two doubles, storable (the rest bone lengths). Alternative: derive from `jointRestWorld` in compute. Stored is faster and has no lookup, so store.
- Outputs: the shared `controlOPM`, `jointOPM`, `outputSocketMatrix`, plus `fkVisible` and `ikVisible` (doubles, not writable, not storable) if decision 6 is yes. They connect to the controls' `visibility` in `createModule`.
- `declareStandardAffects()` covers the shared inputs. Add `attributeAffects` for every new attribute to every output it changes: `ikFkBlend` to all outputs, `stretch` and `ikRestOffset` and `restBendNormal` and `limbLengths` to `jointOPM` and `outputSocketMatrix`, `poleFollow` to `controlOPM` (pole's slot) and the sockets, and `ikFkBlend` to `fkVisible` and `ikVisible`. Missing `attributeAffects` is the common silent bug (CLAUDE.md), so the review checklist line applies to each.
- Plan-time storage and compute-time reads are by `MObject` attribute, never by name (D36 rules).

## 4. Evaluation

One `compute()` solves everything once and writes every output array (D36, `commitOutput`). Inputs are world-space matrices and deltas, as everywhere (D3).

### 4.1 Per frame

```
D_start        = motion of the `start` child plug's parent since rest
FK chain       : the shared chain walk, controls 0..2 (solveChain / solveFkChain)
                   fkWorld[i] = fkLocal[i] * fkRestWorld[i] * parentDelta_i
IK handle      : ikWorld    = ikLocal   * ikRest   * D_ik        (child plug `ik`)
Pole           : poleWorld  = poleLocal * poleRest * D_pole       (child plug `pole`, blended by poleFollow, 4.5)
IK root        : rootPos    = translation of  upperRestWorld * D_start     (NOT the FK upper control's world)
IK solve       : ikPos[0..2] and plane from (rootPos, ikWorld, poleWorld, lengths, stretch)   (4.2)
IK joint worlds: ikJointWorld[i] = ikRestOffset[i] * frame_i                                    (4.3)
Blend          : jointWorld[i] = blend(fkWorld[i], ikJointWorld[i], ikFkBlend)                  (4.6)
Outputs        : jointOPM[i] = socket[i] = jointWorld[i] for i in 0..2
                 jointOPM[3..5] = socket[3..5] = the marker controls' own worlds
                 controlOPM[i] = control i's rest carried by its own parent delta (excludes the local)
                 fkVisible = blend < 1, ikVisible = blend > 0
```

Blend at exactly 0 returns the FK worlds and at exactly 1 the IK worlds, with no float residue and without running the unused solve. This matters for animators who keyed 0 or 1 and for the inverse round trip.

### 4.2 Two-bone solve

`MathUtils::computeTwoBoneIk` in `maya/shared/Utilities.h`, next to `computeAimMatrix`. It is shareable (a leg, a spline segment or a future module can call it), so it goes in the shared layer when written (CLAUDE.md, "Look before you write", step 6). It takes `rootPos`, `handlePos`, `polePos`, `L1`, `L2`, `stretch` and the rest bend normal, and returns the three positions and the plane normal.

- Reach `d = |handle - root|`, clamped. `s` is the stretch scale: `s = lerp(1, max(1, d / (L1 + L2)), stretch)`. Bone lengths are `L1*s` and `L2*s`.
- Clamp `d` to `[|L1s - L2s| + eps, L1s + L2s]`. A handle past full reach with `stretch = 0` leaves the end at full reach on the line to the handle, like any native IK.
- Law of cosines along the axis `a = normalize(handle - root)`: `x = (d^2 + L1s^2 - L2s^2) / (2d)`, `h = sqrt(max(0, L1s^2 - x^2))`. `mid = root + a*x + n*h`, with `n` the unit vector perpendicular to `a` in the plane of `a` and the pole, on the pole's side. `end = root + a*d` (clamped `d`).
- **Plane.** `n = normalize(pole - root - a * dot(pole - root, a))`. If its length is below 1e-4 (pole on the line), use `restBendNormal` projected perpendicular to `a`, and if that also fails, any perpendicular. The solver is **stateless** (D36), so a pole crossing the line flips the limb the way a native IK does. State this to animators in `docs/modules.md`.
- Worked check, hand-written, for a pose case: `L1 = 3`, `L2 = 4`, root `(0,0,0)`, handle `(5,0,0)`, pole toward `+Y`. `x = (25+9-16)/10 = 1.8`, `h = sqrt(9 - 3.24) = 2.4`, `mid = (1.8, 2.4, 0)`. Handle moved to `(0,5,0)` with the pole toward `+X`: `mid = (2.4, 1.8, 0)`. Handle at `(10,0,0)`: stretch 0 gives end `(7,0,0)` and mid `(3,0,0)`. Stretch 1 gives end `(10,0,0)` and mid `(30/7,0,0)`.

### 4.3 Joint frames and rest offsets

Joint rest orientations are arbitrary, so no axis assumption is made (D40's +X down the bone is a default for FK controls only). Each IK joint is a *frame* built from the solved positions, times a stored rest offset:

- Root frame: X toward `mid`, Z along the bend side so the plane contains X and the pole. Mid frame: X toward `end`, same plane. End joint: translation from the solved end, **rotation from the IK handle's world** (so rotating the handle turns the hand), scale ignored.
- The frame builder is one function, used by `createModule` (to compute the rest offsets), `compute()` and `inverse()`, so the three can't drift (D41, P3). This is `computeAimJointWorld` and `computeAimRestOffset` again, for three joints: `ikRestOffset[i] = jointRest[i] * frameAtRest[i]^-1`.
- At rest the IK joints equal the joint rests. The rest bend must point to the pole's side of the root-end line, or IK would flip the bend at rest. `planFromJson` fails if it doesn't (section 5). A straight rest limb is allowed: the pole then only sets the plane.
- Cost: three stored matrices and one frame builder. Same shape as Aim, so no new pattern.

### 4.4 Pole follow

`poleFollow = 0` uses the `pole` child plug's delta. `poleFollow = 1` uses the IK handle's own motion since rest. Between, the two deltas are interpolated (translation lerp, rotation slerp), as in 4.6. This is the one place the module reads one of its own control worlds as an input for another slot. It is not a graph cycle, because `ik` depends only on the `ik` plug and the `ik` control's local. It does mean the pole's OPM depends on slot 3, so slot order matters in `compute()` and in the inverse's forward pass: solve slot 3, then slot 4.

### 4.5 Blend math

Translation lerps, rotation slerps along the shorter arc, scale lerps. Decompose with `MTransformationMatrix` and rebuild. No heap allocation is expected in these, but verify when building, because `compute()` stays allocation-free (D36, CLAUDE.md). `MathUtils::blendMatrices` goes in the shared layer for the same reason as the solver (pole follow reuses it).

### 4.6 Failure modes

| Case | Result |
|---|---|
| Handle on the root | `d` clamps to `|L1s - L2s| + eps`, so the limb folds, no NaN. |
| Pole on the line | Rest bend normal, then any perpendicular (4.2). |
| Zero-length rest bone | Plan error (section 5). |
| `stretch` with `L1 + L2 = 0` | Not reachable: plan rejects it. |
| Blend outside 0 to 1 | The attribute is clamped (min 0, max 1). |

## 5. Read-phase validation (`planFromJson`)

All errors go to `ctx.report` with the JSON path, and every selector is claimed even after a failure so the report lists all of them (the FkChain and Aim pattern). No new `core/` validation function: these checks need rests, which come from joints.

1. `joints` has exactly three selectors. `ik`, `pole`, `settings` each resolve to one joint, and none of the six is claimed twice (`claimJoint` covers it).
2. Rest bone lengths `L1` and `L2` are above a minimum (as `kMinAimDistance`).
3. The pole joint is not on the root-end line (as `kMinAimUpSine`), and it is on the same side of that line as the mid joint, or the rest limb is straight within tolerance.
4. The `ik` joint is within a tolerance of the end joint's position. A handle offset from the hand is a separate feature: error now.
5. `ik` and `pole` markups differ from every joint's markup (covered by `validateControlMarkup`, which fails on a shared pair).
6. `checkMirrorPositions` works through `claimJoint` for all six.

## 6. Inverse (D41, `inverse-solve.md` section 8)

`IkLimbNode::inverse` takes world matrices only, by slot, and returns control local matrices, as the contract requires (`ModuleInverseInput`, `ModuleInverseResult`).

1. **FK controls** from `jointWorld[0..2]` with `ModuleInverse::solveChain` on three slots and `useJoints = true`. This gives each FK control's local from the joints whatever `ikFkBlend` is.
2. **IK handle** (slot 3): the end joint's world, as the handle's world, then `ModuleInverse::localFromWorld` with the `ik` parent delta. The handle's translation is the end joint's translation, rotation the end joint's.
3. **Pole** (slot 4): the point on the root-end line nearest the mid joint, plus the unit perpendicular from that point to the mid joint, times the rest pole distance. Then `localFromWorld`. Its rotation stays at rest.
4. **Settings** (slot 5): no joints. Transform from the fallback (the sidecar, or rest), as Layout does.
5. **Marker joints** (slots 3 to 5) are `export: false`, so the source skeleton normally lacks them. The IK inverse derives its controls from slots 0 to 2 and ignores `jointWorld[3..5]` unless the source has them.
6. `socketWorld` returns the forward result, including the blended joints at blend 0 and 1 and every slot, so the sweep feeds modules below.
7. **`solved = false`** when any of the three limb joints is missing, with a reason. The driver then takes the controls from the sidecar, then rest.
8. **Derived channels.** Transforms yes. `ikFkBlend`, `poleFollow` and `space`: sidecar only, never derived. `stretch`: derivable only when the source's root-end length exceeds `L1 + L2` (then it is 1). Below that the joints can't tell `stretch = 0` from `stretch = 1`, so it comes from the sidecar. This narrows `inverse-solve.md` requirement 3, which said stretch from the length ratio is derivable (section 9).
9. **Locked FK translate.** A stretched source can't be reproduced by FK controls with translate locked. The driver already warns on locked channels (D41), so this shows as one warning per control. It does not fail the restore.

Both sides are solved from joints at any blend, so after `poseRigFromJointPose` switching `ikFkBlend` doesn't pop, which is also the IK/FK match tool (section 7.3).

## 7. Library, commands and Python

### 7.1 What works with no change

Every item below keys on control markup and channel name, so a limb is in them from the first build. Each needs a test (section 8, step 9) and a docs line.

- `getRigControls(moduleType=["IkLimb"])`, `getRigControlInfo`, `getRigControlChannels` (the three new channels with their rests, ranges and mirror modes), `getRigMirrorControl` (pairs the `ik`, `pole` and `settings` controls by markup).
- `zeroRigControls` (every channel to its rest, so blend 0, `poleFollow` 0), `mirrorRigPose`, `flipRigPose` (mirror copy for all new channels, transform copy for the rest, D34).
- `saveRigPose` and `loadRigPose`, `saveRigAnimation` and `loadRigAnimation` (ControlPose and ControlAnim, D34, D42), with `mirror` and `offset`. `ikFkBlend` and `stretch` are floats on default tangents. `space` is an enum, so it keys stepped.
- `saveRigControlShapes`, `loadRigControlShapes`, `mirrorRigControlShapes` for the six controls' shapes.
- `createRigConstraint` and `setRigConstraintSpace` on `start`, `ik`, `pole` and `settings`, and template `constraints` entries.
- `rigmodules.pose.key_controls` and `key_modules`, which read `get_channels`.
- Selection sets (D39), display layers (D38), `rig_all` (D43): the build makes them for any module.
- `animateRigFromJointClip` and `poseRigFromJointPose`, through the inverse.

### 7.2 Shape choices

Control shapes: FK controls rings (`Circle`), IK handle a `Box`, pole a `Diamond`, settings `Pivot` or a small arrow cluster. The shape type comes from the module's `shapeType` field, like every module, so one shape for all six controls unless the author adds per-control shapes later (not in v1: the shape file already lets a rigger edit them per asset, D26).

### 7.3 IK/FK match

No new command. After posing in IK, `settings.ikFkBlend` is set to 1. To match FK to it, Python calls `rigmodules.jointclip.capture_joint_clip` on the current frame and `cmds.poseRigFromJointPose(controls, jointPose=..., mode="match")` scoped to the limb's FK controls. The joints are the same at any blend, so FK reproduces them. For IK to FK, the reverse scope. Neither writes `ikFkBlend`, because nothing derives it (D41, "what the solve writes"). Wrapping this in `rigmodules` or in mayadev's pose menu is a later step, and a dedicated command needs its own sign-off (it adds public API).

Check before relying on it: that `capture_joint_clip` samples the limb's joints and not only export joints. It samples the export joints (`_get_export_joints`), and the three limb joints are export joints in a normal asset, so it does.

## 8. Build order

Each step ends with `RigTests.cmd` and the full suite, on a local build. Cloud sessions can do the Maya-free parts and docs (CLAUDE.md, "Environments"): steps 1, 2, 3 (partly), 9 (fixtures) and 13.

1. **Sign off** section 2. Then add `D47. IK limb` to `design/decisions.md` (Status: Decided or Leaning, the rejected alternatives from section 2, the consequence). Replace "The next module is the IK limb" in "Open items", and update the status line of `inverse-solve.md` (IK designed, build step 5). Promote this page to `design/ik-limb.md`.
2. **Write the hand-worked pose cases first** (`tests/maya/fixtures/poses/ikLimb/*.json`) and the validation cases (`fixtures/validation/`), before any code. Values are worked by hand (section 4.2 has four), never recorded from the rig. Add the skeleton (`fixtures/skeletons/ik_limb.json`, `ik_limb_mirror.json`: a bent limb with the pole on the bend side, three non-export marker joints) and templates (`ik_limb.json`, `ik_limb_mirror.json`).
3. **Constants.** `core/Constants.h`: `RigAttr::IkFkBlend`, `Stretch`, `PoleFollow`, `IkRestOffset`, `RestBendNormal`, `LimbLengths`, `FkVisible`, `IkVisible`; `PlugAndSocketNames::Ik`, `Pole`, `Settings`. Add the moduleType name only to the registry row.
4. **Shared math.** `MathUtils::computeTwoBoneIk` and `MathUtils::blendMatrices` in `maya/shared/Utilities.h/.cpp`. Pure `MMatrix` and `MVector`, no scene access, so they are unit-testable through the module's pose cases. Check callers: new functions, so none are affected.
5. **Shared custom channel wiring.** This is the first module with custom channels. Add one shared helper in `RigModule.h` (or `RigPoseScene` for the attribute half): *add a keyable double channel to a control with a default, a range and a mirror mode, and connect it to a module attribute*, all through the same `MDagModifier`. `space` in `Constraints.cpp` already adds an enum the same way (`setMirrorMode`, `addAttribute`), so look there first and extend a shared function instead of copying it. Say in the summary what was reused and what is new.
6. **Module files.** `maya/modules/IkLimbModule.h/.cpp`, from FkChain and Aim. `IkLimbSlot` namespace, `IkLimbArgs` with `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(IkLimbArgs, joints, ik, pole, settings)`, `creator`, `initialize` (create, add and `attributeAffects` for each attribute together), `compute` (one solve, `commitOutput` for each array), `createModule` (the six slots, joints via `wireJoint`, controls via `createRigControl`, controls wired with `wireControl`, `setParentSocketNames` and `setChildPlugName`, channel wiring, rest offsets, `connectModuleToRigRoot`, `dagParentCallback`), `planFromJson` (section 5), `inverse` (section 6), and the file-local frame builder. Keep the pattern of Aim's `computeAimJointWorld` and `computeAimRestOffset`.
7. **Register.** One row in `ModuleRegistry.cpp`: `"ikLimbModule", "IkLimb", IkLimbNode::id, creator, initialize, {}, planFromJson, inverse, true`. Add both files to `cpp/RigModules.vcxproj` and `.filters`. Add the type to `tests/maya/concepts/test_plugin.py` and the MTypeId to `CLAUDE.md`.
8. **Lock and rotate order.** After the first `doIt`, `createRig` locks rotate order on every control (D40). Lock and hide the channels in section 3.3 with `DagUtils::queueLockAndHideTransform` (it locks all nine, so unlock the keyable ones, or add an optional parameter that takes the channels to keep: prefer the parameter, since every later module with channel-only controls needs it).
9. **Tests (tier 4, `modules/test_ik_limb.py`).**
   - `IkLimbContract(ModuleContract, RigTestCase)` with `JOINT_OWNERS` for all six joints, `MIRROR_TEMPLATE` and `MIRROR_SKELETON`. It gives the build, rest, drive, no-inherit, undo/redo, one-solve, mirrored channels and **inverse round trip** tests.
   - `add_pose_cases(IkLimbPoses, "ikLimb")`: rest; handle moved (the 3-4-5 case); pole moved to either side; unreachable with and without stretch; blend 0, 0.5 and 1; `poleFollow` 1 with a handle move; a rotated parent (`start` delta); a mirrored limb.
   - Behaviour tests: blend 0 is bit-identical to FK and 1 to IK. Blend 0.5 equals the lerp and slerp of the two. The hand follows the handle's rotation. Controls hide by blend. Sockets `index_*` follow the blended joints and a child module's controls stay visible at blend 1. A pole on the line takes the fallback plane. `ik`, `pole` and `settings` accept `rigConstraint` spaces and match on a switch (D27). Undo and redo of a build.
   - Inverse tests: the round trip at blends 0, 0.5 and 1. IK/FK match through `poseRigFromJointPose` scoped to the limb. A missing joint. A sidecar carrying `ikFkBlend`. A stretched source warns on FK translate and derives `stretch = 1`.
   - Validation cases: pole on the line, pole on the wrong side, `ik` far from the end joint, a selector claimed twice, a zero-length bone, a limb joint count other than three, a `parentStructure` value that names the limb's own socket (cycle).
   - Golden snapshot `golden/ik_limb.json`, generated with `--update-golden` and reviewed. Add `ik_limb` to `test_smoke`'s inputs by adding the golden file.
   - Add the new tests' file to `TIERS` in `tests/run_tests.py`. Every check a probe made becomes a suite test or a listed proposed test (CLAUDE.md).
10. **Docs example test.** Any new worked example in `docs/modules.md` gets a test in `tests/maya/concepts/test_docs_examples.py`.
11. **Build and run.** MSBuild, then `cmd /c tests\RigTests.cmd`. Report the TOTAL line and any failed, crashed or blocked tier. Needs local build and Maya verification; the cloud session can't do this step.
12. **Performance rig and `bench` (D36).** Add `limb_specs()` to `tools/perf/perf_scenes.py` beside `chain_specs()` and `aim_specs()` (a block of about 12 limbs: arms and legs of the existing skeleton, with three marker joints each), extend the skeleton, template and instance builders and `check_perf_scene.py`, then run `mayapy tools\perf\perf_scenes.py check`, then `bench` on the 20-rig scene (30 fps target) and the 50-rig scene (within 1.5 times the native floor). Report the fps, and say so when below target. The rig grows (the docs say "200 joint, 51 constraints"), so search the docs and D36 for those counts and fix them. The native floor has no tool, so that comparison is manual (D36).
13. **Docs pass (CLAUDE.md, "Working style").** List and update:
    - `docs/modules.md`: an `IkLimb` section (JSON, args table, node table, controls, child plugs, sockets, channels, the pole-on-line behaviour, the own-socket cycle rule, `poleFollow`).
    - `docs/templates.md`: the `IkLimb` args in the module reference, and a mirrored limb example.
    - `docs/controls.md`: `ikFkBlend`, `stretch`, `poleFollow` in the channel names list, and the new markup pattern (marker joints).
    - `docs/library.md` and `docs/commands.md`: the `IkLimb` `moduleType` filter value, what a limb's pose and animation hold, and the match recipe.
    - `docs/architecture.md`: the module table row, the slot table, `MathUtils` additions, the custom channel helper, the recipe notes.
    - `README.md`: the modules table and the documentation list if a page is added.
    - `CLAUDE.md`: repo map rows (`modules/IkLimbModule`, the `core/Constants.h` entries, `MathUtils`), the node types and MTypeIds list (`ikLimbModule` `0x00218F`), the Python and commands lists if they change, and the "Existing modules" line.
    - `design/decisions.md` (D47, Open items), `design/inverse-solve.md` (status and section 8), `design/module-directions.md` (the IK limb is built), `design/control-library.md` (step 8 of its build order), `design/mayadev-integration.md` (IK/FK match rows).
    - `tests/README.md`: the new test file in the tier table and the new fixture folders.
    - Search every page for `IK`, `ik limb`, and for the perf rig's joint and constraint counts, and fix stale mentions.
14. **Commit** with the repo's long form: imperative summary, bullets grouped by area (core, plugin, tests, docs, tools), the rejected alternatives and `D47`, the public API (module type, node, channels, markups, child plugs, sockets), the impact on saved poses and animation (none, since the module is new), and what was verified and what was not.

## 9. Discrepancies and risks found while reading

- **`inverse-solve.md` requirement 3** says `stretch` is derivable from the length ratio. It is derivable only above full reach (section 6, item 8). Update that page when this is built.
- **D17 and pole spaces.** D31 says the IK module exposes the pole as a child plug that gets spaces through `rigConstraint`. That works for spaces on other modules' sockets, and not for "follow the IK handle", which is the common one. This is decision 3.
- **`docs/architecture.md` recipe** lists four pieces for a module, and says nothing about custom channels, hidden controls or marker joints. Update it with the first module that needs them.
- **`CLAUDE.md` says `Aim` and `FkChain` "build entirely from joint positions and orientations"** (D28 mirror text). Still true for the limb: no `isMirror()` branch is needed.
- **Visibility inheritance.** D1 turns transforms off in the DAG and leaves visibility inherited. Hiding the FK controls by blend would hide anything DAG-parented under them. Section 3.2 avoids this with null socket transforms. Check every parent-child pair in the contract test.
- **Compute cost.** Three joints blended plus an IK solve is more than a chain solve. D36's `rigControlNode` premium dominates the node cost, but run `bench` before anything else is assumed.

## 10. Maintenance surface added

- One module node, its args struct and its inverse, and one registry row.
- Three public channel names, three child plug names and two socket families, three marker-joint markups per limb.
- Three `RigAttr` strings, three `PlugAndSocketNames` entries.
- `MathUtils::computeTwoBoneIk` and `blendMatrices`, and one shared custom-channel helper.
- One golden snapshot, one pose case folder, one test file and a handful of fixtures.
- A block in the performance rig.

Complexity stated (CLAUDE.md, "Complexity checks"): a new attribute set on a module (the first custom channels and the first scalar inputs), a visibility output, and three marker joints per limb. The simpler alternative, a plain two-bone solve with no blend (IK only), was rejected: the animator-facing problem is the switch, and the inverse requirement (FK and IK both solved from joints) was written for it.
