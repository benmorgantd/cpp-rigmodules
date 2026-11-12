# Inverse solve: restoring controls from joints

**Amended by D42 (2026-11-12, `control-library.md`):** control data (poses and keyed animation on the rig's own controls) is the primary library, and this solve is for a different rig or skeleton, mocap and the engine. The stored sidecar (sections 6.2 and 7) and the `poses/` and `anims/` pairing by basename are withdrawn: USD goes to `exports/` and `sources/`, and the control supplement is a ControlAnim sampled by Python into the in-memory sidecar. The solve, the JointClip file and USD IO are unchanged. The joint file is FBX, not USD (D42, `control-library.md`, section 12): USD IO is retired (`control-library.md`, section 13) and sections 6.1 and 7's USD parts are withdrawn, and a bone map replaces the `rigmodules:` attributes as the carrier of markup for foreign files.

**Status:** Decided, D41 (2026-11-11). Amended 2026-11-11 (author): every channel is baked every frame with no tangent data, and the calibration offset and its composition order are dropped for now. Built 2026-11-11 (section 13, steps 1 and 2): the types, the JointClip reader and capture, the animation sidecar reader, the solve driver with the inverses of FkChain, Aim and Layout, the shared constraint delta, `poseRigFromJointPose`, `animateRigFromJointClip` and the keyframe command (D34). The file has `startFrame`, and translations are in internal units (centimeters). USD IO (`write_usd`, `read_usd`) and mayadev's `anims` file handler are built too. Not built: the sidecar writer, a menu for clips in the asset, IK and retargeting. Where the code differs from sections 4 and 5, the code wins and D41 lists why: a module's inverse takes and returns matrices only, and the driver converts them to channels.

The forward rig turns control values into joint matrices. This design adds the other direction: given joint matrices, find the control values that reproduce them. Modules own that behaviour. It comes before the IK module, so IK is designed with an inverse from the start.

## 1. Goal and scope

**Animator-facing problem.** Pose and animation libraries that store control values only restore 1:1 on the rig they came from. They fail when the proportions differ, and they can't come from outside the rig at all (mocap, another rig, a game export). Joints are the rig-independent carrier, so joints become the main pose and animation storage, and the controls are solved from them. The plain control-value library stays as the simplest case.

**In scope**
- A solve from a joint pose or joint animation (side and usage markup, D15) to control values, one module at a time.
- A sidecar file, in the pose file's format (D34), for what joints can't hold.
- USD as the joint file format.
- A seam for retargeting to different proportions: kept open, not designed (section 9).

**Not in scope:** retargeting itself, an IK module, mocap cleanup, Maya files as a storage format, FBX.

**What it replaces.** Nothing. `saveRigPose` / `loadRigPose` stay as the control-value path (the "basic anim library").

## 2. What the code already gives us

These facts shape every choice below. All checked in the code on 2026-11-11.

1. **FkChain joint world equals control world.** `solveFkChain` writes the same matrix to `jointOPM` and the socket, and the joint's own local is identity (`wireJoint` bakes the rest into the OPM). So the inverse needs no joint-versus-control offset.
2. **The inverse of an FK control is simple.** With row-major matrices, `W_i = L_i * rest_i * D_{i-1}` and `D_{i-1} = rest_{i-1}^-1 * W_{i-1}`. So `L_i = W_i * D_{i-1}^-1 * rest_i^-1`. Inside a chain this reduces to `L_i = l_i * rl_i^-1`, where `l_i` is the joint's local matrix (relative to its parent joint) and `rl_i = rest_i * rest_{i-1}^-1` is the rest local. No parent state is needed inside a chain. Only the chain start depends on its parent socket's delta `D_socket`: `L_0 = W_0 * D_socket^-1 * rest_0^-1`.
3. **Module boundaries and spaces need the delta.** `D_socket` comes from the parent module's socket, through a `rigConstraint` (D27, D31) when the child plug has one. With a space other than the default, `L_0` means something different, so the solve needs the same constraint math the rig uses.
4. **Aim has three joints and two controls.** `parts[i].oJoint` is wired for aim, target and up, but only target and up have controls. The aim joint is solved from them (`computeAimMatrix`), so it adds nothing to invert.
5. **Layout has controls and no joints** (D39). Nothing in a skeleton can restore them.
6. **All rests are stored on the rig:** `controlRestWorld`, `childPlugParentRest`, `jointRestWorld`, `aimRestOffset`, each constraint's rests (D27, D35). An inverse never reads a posed scene for rest.
7. **The export skeleton is the joints with `export: true`** (D15). A source skeleton from a game pipeline lacks the rig-internal joints, so some modules will have joints missing.
8. **Control rotate orders are locked per module** (D40), so converting a matrix to euler values is well defined.
9. **No keyframe command exists yet** (D34 TODO), and `PoseFile` holds one frame.

## 3. Proposed decisions

- **P1. Modules own the inverse.** A module's descriptor gets an `inverse` function beside `plan`. The shared layer holds only the driver (the sweep in section 5), never per-module branches (D13, "Prefer composition and data").
- **P2. A module only ever matches world matrices.** Its inverse takes target world matrices for its joints and returns control channel values. It knows nothing about source skeletons, proportions or files. Retargeting, if it ever exists, manufactures target world matrices for the rig's joints and calls the same functions (section 9). This is what keeps proportions out of the modules.
- **P3. The inverse reuses the forward math.** Each module's inverse calls the same helpers its `compute()` uses (`solveControlStep`, `computeDeltaFromRest`, `computeAimMatrix`, the constraint delta), so the two can't drift. A round-trip contract test (section 11) is the guard.
- **P4. Joints win for what they can express, the sidecar fills the rest.** Per control channel: the joint-derived value if the module derives that channel from joints that are present, else the sidecar's value, else the rest value.
- **P5. The solver runs in memory, not through the scene.** It reads stored rests once, sweeps the rig's modules in dependency order with plain matrices, and returns channel values per frame. Writing values and keying go through `RigPoseScene` and the keyframe command. Setting attributes and pulling plugs once per frame would be slow and would need undo bookkeeping per frame.
- **P6. USD is the joint file format, read and written in Python.** The C++ plugin does not link USD (section 7).
- **P7. The sidecar is the existing pose format.** A pose's sidecar is a pose file. A clip's sidecar is the same per-control data keyed by frame (section 6).

## 4. The module contract

```cpp
// What one module's inverse is given, for one frame. No Maya scene access.
struct ModuleInverseInput
{
	std::vector<std::optional<MMatrix>> jointWorld;   // by joint index (the jointOPM index). empty = joint missing from the source
	std::vector<MMatrix> parentDelta;                 // by child plug index: motion of the parent socket since rest, from the sweep
	const SidecarChannels& sidecar;                   // this module's controls' sidecar values for this frame
};

struct ModuleInverseResult
{
	std::vector<ControlValues> controls;   // by control index: the channels it derived (translate/rotate/scale, or custom)
	std::vector<MMatrix> socketWorld;      // forward result, so the sweep can feed the next module
	bool solved = false;                   // false: a needed joint was missing, controls left to the sidecar
};
```

- **`ModuleDescriptor::inverse`** (built as a plain function pointer, and the types below are the sketch the code replaced, see D41): `std::function<ModuleInverseResult(const ModuleInverseContext&, const ModuleInverseInput&)>`, where the context holds the module's stored rests and its derived-channel list, read once per restore. `nullptr` is not allowed: a module that can't be inverted says so with an `inverse` that returns `solved = false` and a reason, and `ModuleContract` requires one or the other. A new module without a decision can't ship.
- **Derived channels** are declared, not discovered: FkChain derives every transform channel of every control, Aim derives its target and up transforms, Layout derives nothing. A custom channel a module derives (a stretch from bone lengths) is declared by that module. Anything not declared derived comes from the sidecar. The list is also what the sidecar writer uses to decide what to save (section 6).
- **Euler output.** The solver converts each control's local matrix to rotate values in the control's locked order, choosing the euler closest to the previous frame (the same rule as D27), so curves don't flip. The first frame uses the closest to the control's current value.
- **Shear and scale** a control's channels can't hold give one warning per control, as in D27. Locked or driven channels keep their values and give a warning naming them.

## 5. The driver (the sweep)

`maya/shared/RigInverse` reads the rig once with `RigTraversal`: modules, their stored rests, which socket feeds which child plug, each constraint's drivers and `space` enum, and each module joint's markup (the joint's `rigMarkup`, found through `jointOPM`). It orders the modules parents first. The connections define a DAG, since D17 already forbids cycles.

Per frame:

1. Read the sidecar's channels for the frame, `space` first, because it decides which driver feeds each constrained child plug.
2. For each module in order:
   1. Build `parentDelta` for each child plug: the parent socket's world from earlier in the sweep, run through the constraint's own delta function for the sidecar's `space` (a static function factored out of `rigConstraint::compute`, shared by both).
   2. Map the module's joints to the source's world matrices by markup `(side, usage)`.
   3. Call `inverse`. Keep its `socketWorld` for modules below it.
3. Merge by P4 into one `PoseMap`-shaped result per frame.

A module whose parent didn't solve (a missing joint, no sidecar) gets its parent socket from the rest pose, with a warning naming it. Controls that couldn't be solved stay at rest. Nothing aborts the restore, because a partial skeleton is normal (point 7 above).

**Why sweep instead of reading joints directly.** In a chain the joints are enough (point 2). Across a module boundary the child's `L_0` depends on the parent socket's delta, and with a space switch that delta isn't the parent joint's delta. The sweep is small (a few matrix products per control per frame) and reuses forward code, so it is cheaper than special cases.

**Source check.** Before a restore the driver compares the source's rest world positions with the rig's `jointRestWorld`, by markup, and reports the largest difference. If bone lengths differ by more than a tolerance, the restore still runs in `match` mode and warns that the result will not reproduce the source's pose and that a retarget is needed (section 9). This is also the check that tells a tool which mode to offer.

## 6. Data

### 6.1 The joint file: USD

**Withdrawn (D42, 2026-11-12).** USD IO was removed and FBX is the joint file (`control-library.md`, section 12). The layout below is kept as history, and the tag `usd-joint-io` has the code.

A joint pose or clip is one USD file (`.usd`, `.usda` or `.usdc`) holding a `UsdSkelRoot` with a `Skeleton` and, for a clip, a `SkelAnimation`.

| USD | Content |
|---|---|
| `Skeleton.joints` | Joint paths. Hierarchy is the path. Names are free (they must be valid USD identifiers). |
| `Skeleton.restTransforms` | Local rest matrices. The source's rest, used for the source check and, later, retargeting. |
| `SkelAnimation` | `translations`, `rotations` (quaternions), `scales`, time sampled, local to the parent joint. This is UsdSkel's own convention, so other tools read it. A pose is one sample (or the default value). |
| `rigmodules:jointMarkup` | A `string[]` parallel to `joints`: each entry is the joint's `rigMarkup` JSON verbatim (D15, D16). Parsed by the same `core/JointMarkup` code as the scene's markup. A joint with no markup is `""`. |
| `rigmodules:formatVersion` | An integer, through the D33 runner. |
| `rigmodules:assetId`, `rigmodules:skeletonId` | The asset the clip came from and its shared skeleton (D29), so tools can tell which rigs share it. Informational. **Not written yet:** `write_usd` takes paths only, and nothing reads them. Adding them is a new optional argument and no format change. |
| Stage metadata | `timeCodesPerSecond`, `startTimeCode`, `endTimeCode`, `metersPerUnit`, `upAxis`. |

- **Unmarked joints stay in the file.** They are needed to compose world matrices through the source hierarchy (a twist helper between two marked joints), and are never mapped to the rig.
- **Conversions live in the reader:** meters per unit to the scene's linear unit, stage up axis to Maya's, the file's time codes to the scene's frame rate (a mismatch is an error unless the caller asks for a resample).
- **Joint matrices, not channels.** Maya joint orient and rotate order don't exist in the file. The writer bakes each joint's full local matrix to translate, quaternion and scale. A restore never depends on how the source built its joint rotations.
- **Why not FBX:** the author prefers USD (more current, a defined skeleton and animation schema, open parsing libraries). Maya 2026 ships OpenUSD 0.24.11 and numpy, and `from pxr import Usd, UsdSkel` works in mayapy after `cmds.loadPlugin("mayaUsdPlugin")` (checked 2026-11-11).

### 6.2 The sidecar

**A pose's sidecar is a pose file, unchanged** (`PoseFile`, format version 1): control markup `(side, usage)` to channel values, in UI units, enums by index. The writer saves only the channels the joints can't derive: custom channels, `space`, Layout controls, and the transforms of any control whose joints aren't in the file. Nothing about this file says it is a sidecar. A full pose file also works, and then it is the control-value library.

**A clip's sidecar** needs time. It reuses the pose file's per-control data, one entry per frame:

```json
{
    "formatVersion": 1,
    "frameRate": 30,
    "frames": {
        "1": { "Left": { "arm": { "ikFkBlend": 0.0, "space": 0 } } },
        "2": { "Left": { "arm": { "ikFkBlend": 0.0, "space": 0 } } },
        "3": { "Left": { "arm": { "ikFkBlend": 0.1, "space": 0 } } }
    }
}
```

- Each frame's value is the pose file's `controls` map, parsed by the same code. **Withdrawn (D42, 2026-11-12):** the animation sidecar is a ControlAnim file now, and `core/AnimationSidecar` is deleted.
- **Every channel is baked every frame (author, 2026-11-11).** Joints are samples, and the solve is nonlinear (matrix to euler), so the derived channels can't be anything else. The sidecar channels follow the same rule: one entry per frame of the clip, no sparse keys. This keeps one rule for every channel, one clip frame range, and no curve data in any file. The file is larger than sparse keys would be, and it is only the channels the joints can't derive.
- **No tangent data (author, 2026-11-11).** The restored curves have a key on every frame with Maya's default tangents. Simplifying them (key reduction, tangent fitting) is a separate tool on the animation, not part of the restore, and any format that wants tangents later adds them as a format-version step (D33). Rejected: a `tangents` block beside `frames`, sparse sidecar keys, and baking only the derived channels.- **Pairing:** `<name>.usd` and `<name>.rigdata.json` share a basename, in the asset's `poses/` or `anims/` folder (D29). The USD file doesn't point at the sidecar, so the two can be moved or versioned independently and a USD tool that drops custom data costs nothing.
- **Version control:** the writer is hand-written like the shape and pose writers, so a one-key change is a one-line diff.

## 7. Python and C++

- **C++ never links USD.** The devkit has no USD headers, and a plugin that loads against Maya's bundled USD build ties the build to it. `core/` stays dependency-free (D13).
- **The bridge is a working file, not a second interchange format (author, Q1).** Python reads the USD with pxr and numpy and writes a *JointClip* file: JSON, versioned, `core/JointClipFile`, holding the markup per joint, the rest locals, the frame rate and a list of *JointPoses*. A JointPose is one frame: every joint's local translate, rotate and scale. A JointClip is the rest, the markup and its JointPoses. A pose from a single-frame USD is a JointClip file with one JointPose. A command reads the file. JSON follows D16 and D35's precedent (an 11 MB weight file parses in 0.25 s). A 200 joint, 300 frame JointClip is about 600,000 numbers. It is a temporary and is never stored in an asset.
- **The payoff:** the suite can hand-write a JointClip, so the solver tests need neither pxr nor USD files, and the USD conversion is tested alone in a Python test.
- **Where the Python lives:** `python/rigmodules/jointclip.py`. Placement rule (CLAUDE.md): it must change when a RigModules name, attribute or template key changes. It encodes the RigModules-owned schema (`rigmodules:` attributes, markup JSON, clip file), so it belongs here. It takes paths, never resolves asset ids, and imports no Qt or `bmaya` (D24). mayadev owns where files live and the menu.
- **Capture (rig to USD)** is the other direction: sample the rig's export joints over a frame range into a JointClip, write the USD, and write the sidecar from the controls' non-derived channels. v1 is Python with `cmds.getAttr(..., time=frame)`, which evaluates without moving the time slider. A C++ sampler is only worth it if that is too slow.
- **A source skeleton in the scene** (mocap imported some other way) is a second producer of a JointClip, in Python, with no change to the solver.

**Commands** (author, Q6). They set values on a rig that already exists, so neither name says "create" or "build". Verbs follow CLAUDE.md. Sketch:

```python
import maya.cmds as cmds
import rigmodules.jointclip

joint_clip = rigmodules.jointclip.read_usd(r"C:\proj\hero\anims\walk.usd")      # Python, pxr. Returns a JointClip.
cmds.animateRigFromJointClip(rig_root, jointClip=joint_clip.path, sidecar=r"C:\proj\hero\anims\walk.rigdata.json", mode="match")
cmds.poseRigFromJointPose(rig_root, jointPose=joint_clip.pose(1).path, sidecar=r"C:\proj\hero\poses\idle.rigdata.json", mode="match")
```

- `animateRigFromJointClip(rigRoot | *controls, jointClip=path, sidecar=path, mode="match", startFrame=n, endFrame=n)`: sets a key on every frame of the range for every channel it writes, through the keyframe command. Undoable.
- `poseRigFromJointPose(rigRoot | *controls, jointPose=path, sidecar=path, mode="match")`: one JointPose, written through `RigPoseScene::setRigPose`, undoable, no keys, like `loadRigPose`. The file is a JointClip file with exactly one JointPose.
- It never reads the selection (D14). A control scope restores only those controls, the same rule as the pose commands.
- The read phase validates everything first (markup in the file, the sidecar, the frame range, the mode) and writes nothing on any error, like D17 and D26.

## 8. Per-module behaviour

| Module | Derives from joints | From the sidecar | Notes |
|---|---|---|---|
| FkChain | Every control's translate, rotate and scale: `L_i = l_i * rl_i^-1`, and the chain start through the parent delta. | Nothing, unless a joint is missing. | Mirrored modules are ordinary modules (D28), no change. A chain with a missing joint solves the controls above the gap and leaves the rest to the sidecar. |
| Aim | Target and up control transforms, as control steps on their own joints. | Nothing. | The aim joint is not inverted. If the source's aim joint orientation disagrees with the solve by more than a tolerance, the restore warns, since the source wasn't driven by this aim. |
| Layout | Nothing. | Layout control transforms. | Without a sidecar entry they stay at rest. |
| `rigConstraint` | Not a module. Its `space` comes from the sidecar and is needed first. | `space` | If `space` is missing, the constraint's current value (the rest default for a build) is used and the restore warns when the result doesn't match the joints. |

**Requirements this puts on the IK module (design before building it):**
1. A closed-form `inverse`: the IK handle's world from the end joint, the pole control from the mid joint's side of the root-end line, and any twist from the end joint's twist.
2. Both FK and IK controls are solved from the joints whatever `ikFkBlend` is. Then the blend is a sidecar value, and switching it after a restore doesn't pop. This is also the IK/FK match tool, and it falls out for free. Blend values between 0 and 1 are fine as long as the FK and IK sides reproduce the same joints.
3. It declares which custom channels it derives: `stretch` from the chain length ratio is derivable, `ikFkBlend` and `space` are not.
4. The `ModuleContract` round trip covers it (section 11).

## 9. Proportions (kept open, not designed)

A rig with different proportions from the source can't use `match` mode: it reproduces the source's world matrices, which is wrong when bone lengths differ (the source check in section 5 warns). Retargeting is its own design. This design only keeps it possible:

1. **Modules match worlds only (P2).** A retarget layer would sit above the module functions and manufacture target world matrices for the rig's own joints: the target's own rest locals (so bone lengths and joint frames come from the target) with the source's animated rotation applied, and root translation scaled by a proportion ratio. Every module then solves with the same `inverse`. Nothing in a module knows the source's proportions.
2. **`mode`** is a command argument that only accepts `match` for now.
3. **The source's `restTransforms`** are stored in the USD and in the JointClip, since any retarget needs the source's rest.

**Dropped for now (author, 2026-11-11):** the calibration pose, the offset delta applied to restored values, its `offset` argument and its rotation composition order. They aren't reserved in any file or command. Adding them later is a new design and, if it needs a stored field, a format-version step (D33). Not designed: frame changes between source and target rests, per-limb length ratios, foot locking, root motion scaling.

## 10. Writing keys

Built (D34, 2026-11-11): `RigKeyScene::setRigKeys` creates the animCurves through an `MDGModifier` and adds each channel's keys in one `MFnAnimCurve::addKeys` call recorded in an `MAnimCurveChange`, and it is undoable as one step. The design before building read: animating from a JointClip needs the keyframe command that D34 listed as a TODO: animCurve creation and key insertion in one `MDGModifier`, undoable. With a key on every frame it is also the speed-critical part of a restore: a 200 joint, 300 frame clip is tens of thousands of keys. It is the first thing in this design that depends on unbuilt work, and key all, pose restore and bake need it as well. Until it exists, a range uses `cmds.setKeyframe` and a single pose uses `setRigPose`. Neither touches the solver. Keys go only on channels the command wrote.
## 11. Tests

Per CLAUDE.md the checks live in the suite. Proposed, by tier:

- **`ModuleContract` round trip** (every module, including mirrored ones). For each existing pose case, pose the controls, read the joints' world matrices, zero the controls, restore from those matrices, and compare the derived channels with the posed values (euler equivalence, tolerance 1e-5) and the joints with the posed joints. The invariant is that forward and inverse agree. Expected values come from the hand-written pose cases, never recorded from the rig.
- **Missing-joint case** for each module: remove a joint, check the module reports `solved = false` or solves what it can, and that the sidecar fills the rest.
- **Space case:** every `space` of a constrained child plug, with a sidecar `space`. The restored controls reproduce the joints in each space.
- **Sidecar precedence:** joint-derived beats sidecar, sidecar beats rest, a sidecar-only restore equals `loadRigPose`.
- **Clip:** euler continuity. A quaternion can't carry a turn of more than 180 degrees per frame, so the case is a sweep of 170 degrees per frame, and the check is that no rotate curve flips. A frame rate mismatch is an error.
- **JointClip file and the sidecar:** parse, every error path, each version step (D33), a one-line diff for a one-key change.
- **USD IO (Python tier):** write then read a clip through pxr, unit and up-axis conversion, markup round trip, an unmarked joint kept, a missing `rigmodules:` attribute.
- **Docs examples:** each worked example in `docs/` gets a test in `test_docs_examples.py`.

## 12. Cost and maintenance surface

- **Evaluation:** none. No `compute()` changes, no new node, no new attribute on a module. Inverse functions run only in a restore, so D36's targets are untouched. No `bench` run is needed for the inverse itself, but the IK module's design gate applies to it later.
- **Restore speed:** a few matrix products per control per frame, so a 200 joint, 300 frame clip is milliseconds of math. The parse and the key writes dominate.
- **New permanent surface:** the `inverse` function per module (a new module must write one), the JointClip file and sidecar formats, the `rigmodules:` USD attributes, the `animateRigFromJointClip` and `poseRigFromJointPose` commands, `core/JointClipFile`, `core/AnimationSidecar`, `maya/shared/RigInverse`, `python/rigmodules/jointclip.py`. The constraint delta function becomes shared between `compute()` and the sweep.
- **Name impact:** none on existing controls, channels, sockets or child plugs. Markup keys become load-bearing in a second file type, so they are even more fixed (D15).
- **Complexity added, stated:** one new descriptor field, one new shared driver, one new file format on top of USD, and the rule that every module needs an inverse. The alternative, per-module restore code in Python tools, would put module math in a second language and a second place.

## 13. Build order

1. **Core and driver.** `JointClipFile`, the sidecar reader, `RigInverse` (rest read, order, sweep), the constraint delta factored out, FkChain and Aim and Layout inverses, `poseRigFromJointPose`. Contract round trip for all three modules. This is the first useful tool: a pose from joints.
2. **Clip.** Keyframe command (D34), euler continuity, the clip sidecar, `animateRigFromJointClip`.
3. **USD IO.** `rigmodules.jointclip` read and write, capture from a rig, the Python tests. Built 2026-11-11 (D41).
4. **mayadev.** Menu items, the asset `poses/` and `anims/` folders, the pairing by basename. The `anims` file handler is built (2026-11-11): `AnimClipHandler` in `bmaya.maya.assethandlers`.
5. **IK** (its own design, using section 8's requirements).
6. **Retargeting** (its own design, using section 9's seams).

## 14. Answers to the open questions (author, 2026-11-11)

- **Q1. The bridge:** a transient JointClip (JSON). USD is not linked into the plugin. See section 7.
- **Q2. Keys and tangents:** bake every frame, for every channel (derived and sidecar), and store no tangent data. Curve simplification is a separate tool. Amended 2026-11-11, replacing the first answer (sidecar channels keep sparse keys and tangent data). See section 6.2.
- **Q3. Precedence:** P4. The sidecar fills only what the joints can't give.
- **Q4. Pairing:** two separate files, USD and sidecar, same basename.
- **Q5. Missing joints:** a warning. The module's controls come from the sidecar, then rest.
- **Q6. Names:** `animateRigFromJointClip` (keys) and `poseRigFromJointPose` (values only), with *JointClip* and *JointPose* as the terms everywhere. They set values on an existing rig, so neither says restore or build.
- **Q7. Hierarchy: yes, and it holds because the module inverses take world matrices.** The source's joint paths are used only to compose the source's world matrices. The rig's sockets give each module its parent delta. At a chain start the control absorbs any difference (`L_0 = W_0 * D_socket^-1 * rest_0^-1`), so a source whose clavicle hangs off the spine and a rig that hangs it off a chest socket both reproduce the same world matrices. Two caveats. A chain start's control value is relative to the rig's socket, which is correct but not the source's local value. And world matching is only the goal of `match` mode: for `retarget`, rotation deltas have to be taken in a frame that doesn't depend on which parent the source had (world-aligned, or source-parent-local applied under the rig's parent), because a source world translation is wrong on a different proportion. That best fit is part of the retarget design, and it changes no module.
- **Q8. Dropped for now (author, 2026-11-11).** The offset delta and its composition order are out of the design, together with the calibration pose and the `offset` argument. See section 9.