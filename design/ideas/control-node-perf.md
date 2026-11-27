# Idea: making `rigControlNode` cheaper to evaluate (D36, D37)

Written 2026-11-15 after the D52 phase 8 `bench` run. Nothing here is built. The author has said they will change the node drastically if that is what it takes, so the design space below includes replacing it. Read CLAUDE.md, then D33, D36, D37 and D38 in `design/decisions.md`, then `cpp/maya/nodes/RigControlNode.h/.cpp`, then `tests/README.md` (the perf section). Propose a design and wait for sign-off before writing code (CLAUDE.md working style). A change to the node type name or its attributes is public API and needs its own decision and a format update (D33).

## 1. What we measured (all on the author's machine, 8 physical cores, headless, parallel evaluation, no viewport)

Source: `perf_scenes.py profile` on `complex_x20.mb` (10 frames, GUI Maya, viewport suspended) and a headless profiler pass over 121 frames. Complex rig now: 266 controls and 272 joints per rig, 12 `TwoBoneIk` limbs.

| Event | Events per frame (20 rigs) | Thread time per event | Note |
|---|---|---|---|
| `rigControlNode worldMatrix` | 5,320 | about 30 µs | the largest row |
| `joint worldMatrix` | 5,440 | about 6.4 µs | same graph, native node |
| `rigControlNode rotate` | 4,840 | about 4.5 µs | |
| `animCurveTA output` | 14,760 | about 2.1 µs | |
| `rigControlNode matrix` | 5,080 | about 1.3 µs | what the modules read (`controlLocalMatrix`) |
| `rigControlNode worldInverseMatrix` | 5,320 | about 1.1 µs | |
| `twoBoneIkModule` (all outputs) | 130 | tiny | 1.4% of node time |

- Share of node thread time (headless, 121 frames, 20 rigs): `rigControlNode` 53%, `joint` 15%, `fkChainModule` 12%, `animCurveTA` 11%, `rigConstraint` 5%, `twoBoneIkModule` 1.4%.
- `rigControlNode worldMatrix` is about 160 ms of thread time per frame on the 20-rig scene. Spread over 8 threads that is roughly 20 ms of a 41.5 ms frame (viewport suspended). So "about half the frame" is real for this rig, and it is the `worldMatrix` plug of the control, not its other plugs.
- The same row costs the same with the controls hidden (layer hide on playback, D38): about 1,590 ms against 1,626 ms over 10 frames. **Hiding controls does not stop their evaluation.** Only drawing stops.
- Whole-frame view: 20 rigs run at 22 to 27 fps with limbs and 41 to 43 without. 50 rigs cost 1.60 and 1.67 times the native floor (`perf_scenes.py floor`).

**A correction to keep honest.** The 53% is thread time summed over cores and it includes work a native `transform` also does. Native controls are not free. D36 measured the premium of `rigControlNode` over a native `transform` at about 18 to 21 ms at 100 rigs, paired runs 51 to 56 ms native against 71 to 78 ms custom (about 35% on that scene). So replacing the node type with a native transform and nothing else buys something like 20 to 35% of the frame, not 50%. Going further needs fewer evaluated transforms, not a cheaper type (section 4, option D).

## 2. What is already ruled out (do not redo)

- `setDependentsDirty` override: neutralizing it changed nothing (D36 probe).
- Draw override flags: `isTransparent()` false, `hasUIDrawables()` false and `isBounded()` false do not help playback, and `isBounded()` false is much worse (D37).
- The solver and the module nodes: they are about 18% of node time in total and `twoBoneIkModule` is 1.4% (D36, this run). Making them faster cannot give the headline win.
- Draw callbacks do not run during playback (`prepareForDraw`, `addUIDrawables`, `boundingBox`): VP2 moves cached drawables. The GUI cost is inside VP2 per object (D37). That is a separate cost from the evaluation cost in this document.
- Chain solves are already one compute per module per evaluation (D36).
- Reading a joint's world matrix costs the same with either control type (D36).

## 3. Why the evaluation cost is high (hypotheses, none proven)

The cause is unknown. D36 says so: "the cause of the `rigControlNode` premium is unknown". Probes, cheapest first. Each is a throwaway build or scene, measured in one session with paired runs alternating, because drift between sessions is up to 30% (D36).

1. **Is it the plugin transform path itself?** Build a probe `MPxTransform` subclass with no custom attributes, no `setDependentsDirty`, no draw classification and no override, in the 5,000-flat-controls scene of D37, and time `worldMatrix` evaluation against native `transform` and against the current node. If the empty subclass costs about the same as the current node, the cost is Maya's plugin-transform path (virtual `MPxTransformationMatrix`, generic compute, no fast path) and no change inside the node will fix it. If it is much cheaper, the current node's attributes or callbacks are the cost: bisect them.
2. **Scheduling.** The evaluation graph shows `EvaluateNodesInTransparentCluster` (about 3.4 s per 10 frames of the 20-rig profile). Check whether plugin transforms land in a serial or "transparent" cluster that the native ones avoid, and whether `MPxNode::schedulingType()` or `MPxTransform` registration flags change that. D36 notes that no node overrides `schedulingType` yet.
3. **Number of plugs the graph evaluates per control.** Each control evaluates `worldMatrix`, `worldInverseMatrix`, `matrix`, `rotate` and `parentMatrix` style plugs as separate events. Find out which plugs are required only because the node is a DAG transform, and whether anything of ours (a connection, an attribute affects) forces extra ones. `rigControlNode rotate` at 48,400 events is worth a look: it is an input compound.
4. **Does hiding by visibility, not by layer, skip evaluation?** Layer hide on playback did not. Try `visibility` off and `intermediateObject`. Maya normally still evaluates hidden transforms, so expect no gain, but it is a one-line probe.

## 4. Design options (each needs the author's sign-off)

**A. Keep the node, trim it.** Only worthwhile if probe 1 shows the current node is slower than an empty subclass. Low risk, small gain. Public API unchanged.

**B. Native `transform` for the control, with a light draw node.** The control becomes a native transform (cheapest evaluation), carrying its markup and shape settings as dynamic attributes or on a non-DAG data node, with a child draw-only node (an `MPxLocatorNode` or an `MPxSubSceneOverride` host) that does not evaluate during playback. D37 rejected native child *shapes* for losing fill, alpha and xray, and rejected `MPxLocatorNode` because it needs a transform parent, so two DAG nodes per control. Two nodes per control may still win on evaluation, because the shape does no per-frame work, but it costs DAG size, selection behaviour and `RigTraversal` assumptions. Impact: the type name `rigControlNode` is public API (D33), so `getRigControls`, `RIG_CONTROL_TYPE`, shape files, `controlMarkup`, `findMirrorAxis`, the pose and key commands, `RigTraversal`, mayadev and every saved rig are affected, and a rig-versioning path is needed (D33 update tables).

**C. One draw host per rig (D37's open item).** An `MPxSubSceneOverride` per rig with instanced render items per shape type, and the controls become native transforms with no draw classification. This is the only route to the VP2 floor (3.2 µs per control against 8.5 now) and it also removes the evaluation premium. Cost: the animator-facing behaviours listed in D37 (selection, xray, isolate, visibility, layers) are reimplemented. The author deferred it on 2026-11-14 because a cheaper route might exist. If the author is willing to change the node drastically, this is the option that fixes both costs.

**D. Fewer evaluated transforms.** The control's `worldMatrix` is not consumed by anything in the rig: the modules read the control's `matrix` (`controlLocalMatrix`) and write the control's `offsetParentMatrix`, and joints are driven by the module's `jointOPM`. The `worldMatrix` exists only for the viewport and manipulators. If controls could stop being DAG transforms during playback (a display-time proxy, or a lighter stand-in for passive controls such as the 12 limbs' pole and settings controls, which have locked rotation), the evaluation count would fall, not just the per-node cost. Needs its own design: manipulating a control needs a real transform, and "not evaluated when not selected or not visible" is not something Maya offers today (probe 4).

**E. Reduce controls per rig.** Out of scope for this node, but note it: 266 controls per rig is the benchmark rig's choice. The author accepted the limbs' cost on 2026-11-15 (D36).

## 5. How to measure

All of this is in `tools/perf/perf_scenes.py` and `tests/README.md` (the perf section). Manual runs, never in the suite.

```
mayapy tools\perf\perf_scenes.py complex --count 20
mayapy tools\perf\perf_scenes.py complex --count 50
mayapy tools\perf\perf_scenes.py floor   --count 50      # native transforms and joints, same hierarchy and keys
mayapy tools\perf\perf_scenes.py bench   tools\perf\out\complex_x50.mb
mayapy tools\perf\perf_scenes.py bench   tools\perf\out\floor_x50.mb
mayapy tools\perf\perf_scenes.py profile tools\perf\out\complex_x20.mb --frames 10   # GUI Maya, through mayalaunch
```

- Run the two `bench` lines in the same session and twice, alternating. Reported numbers from 2026-11-15: 20 rigs 27.2 and 22.3 fps, 50 rigs 92.6 and 94.8 ms against a floor of 57.8 and 56.9 ms.
- `floor` is the natural place to add a third scene for a candidate design (the probe node in place of the native transform). It builds the same keys through `key_rom`, using custom attributes for the limbs' channels.
- The 2026-11-15 profile used a throwaway headless script (`MProfiler` events grouped by node type and description, 121 frames). If it is useful again, make it a `perf_scenes.py` command rather than a scratchpad file.
- The goal figures (D36): 30 fps for 20 rigs, within 1.5 times the floor on 50. The floor depends on what counts as native, so state which floor a result is against.

## 6. Constraints any change must respect

- Names are public API: `rigControlNode`, `controlMarkup`, the shape attributes, `RigAttr` names, control names and channels, the profiler event names are developer-facing only (D37).
- `inheritsTransform` is off for every control and joint, and all transform math is in the module nodes (D1). A native transform with `offsetParentMatrix` fits that.
- `compute()` has no allocation, no string lookups and no `MGlobal` calls, and must be safe on any thread (D36).
- Shape attributes are set, not animated: parallel evaluation does not call `setDependentsDirty` (D37).
- The suite (`RigTests.cmd`) must pass on every build, the `ModuleContract` tests check every control's channels and markup, and the viewport gallery (`ViewportGallery.cmd`, GUI Maya) is the only check for drawing.
- Docs change with the code, and a new decision goes in `design/decisions.md` with the alternative that was rejected.

## 7. Suggested first session

1. Run probe 1 (empty `MPxTransform` against the current node against native) in a throwaway build, paired runs, and report the three numbers per control. Do not change the shipped node.
2. Run probes 2 and 4 the same way if probe 1 points at the plugin path.
3. Write the design for B, C or D with the API impact listed (section 4), the migration path for saved rigs, and the expected gain from the numbers above, then ask the author which to build.

## 8. Findings, session of 2026-11-15 (probes run, nothing built or changed in the repo)

All numbers: `complex_x20.mb` (20 rigs, 5,380 controls), headless, parallel evaluation, chain tips pulled, ms per frame, paired runs alternating over 3 rounds. Session noise is about 3 ms, so a difference under that is not a result. The joint world-matrix checksum was identical in every swapped scene, so each swap kept the rig's behaviour.

**What was already known before this session.** D36: native transform vs `rigControlNode` on a flat scene at 100 rigs, 51 to 56 ms against 71 to 78 ms. D37: native transform, native curve child and the current node in the viewport on 5,000 flat controls (1.7, 2.8 and 8.5 µs per control, 3.2 with no draw classification). Not done before: a full-rig swap, an empty `MPxTransform`, the scheduling and evaluator checks.

**Node type does not matter for headless evaluation.** Every control swapped in place, connections kept:

| Control type | mean | rounds |
|---|---|---|
| Current `rigControlNode` | 36.1 | 35.3, 34.3, 38.8 |
| Native `transform`, no shape | 34.6 | 32.6, 33.7, 37.5 |
| Native `transform` + `nurbsCurve` | 37.9 | 35.3, 36.7, 41.8 |
| Probe, empty `MPxTransform` | 37.3 | 37.6, 36.4, 38.0 |
| Probe, the real node's attributes, no overrides | 39.1 | 40.1, 39.0, 38.2 |
| Probe, empty plus a `setDependentsDirty` override | 38.2 | 37.3, 38.5, 38.8 |
| Probe, empty plus explicit `MPxTransformationMatrix` | 36.7 | 33.9, 39.2, 37.2 |
| Probe, empty plus `schedulingType()` = `kParallel` | 36.0 | 34.5, 36.6, 37.0 |

- Native transform is about 4% faster, inside the noise. The curve variant is no faster headless because nothing draws, so it says nothing about the viewport.
- The empty subclass costs the same as the current node. Nothing inside the node (attributes, `setDependentsDirty`, matrix class, scheduling) is the cost, and a custom NURBS-style node would gain nothing in evaluation.
- The profiler row `rigControlNode worldMatrix` (about 30 µs per event, 53% of node thread time) does not turn into wall-clock savings. Likely explanation, **not verified**: the event includes upstream work the pull triggers.

**Scheduling and evaluators (probe 2) are ruled out.**
- Forcing `rigControlNode` to parallel (`cmds.evaluationManager("rigControlNode", nodeTypeParallel=True)`): 33.7 against 34.0. Forcing serial: 33.9 to 37.2. Forcing globally serial: 165 to 167 (so the flags work). The default is not serial.
- `transformFlattening`, `pruneRoots` and `invisibility` off: 1 to 3 ms, inside the noise.

**The control's `offsetParentMatrix` drive (the module writes it) is a cost on a native transform only.**

| Scene | mean |
|---|---|
| Current node | 36.7 |
| Current node, control `offsetParentMatrix` disconnected | 36.0 |
| Native controls | 37.8 |
| Native controls, control `offsetParentMatrix` disconnected | 28.8 |

- On native transforms the drive costs about 9 ms (24%). Joint outputs are unchanged without it, so nothing consumes the control's `worldMatrix`, and Maya computes it anyway.
- On `rigControlNode` disconnecting it saves nothing. The two types tie when driven, and the custom node is about 7 ms slower undriven. Cause unknown.
- A first run also disconnected the joints (`joint` derives from `transform`), which removed almost all rig work and was discarded.
- The drive can't be dropped: `inheritsTransform` is off and the parent-space math lives in the modules (D1).

**Hiding by `visibility` (probe 4) is worse, not better.** Controls hidden through the `visibility` attribute: 100.5 (current node) and 92.2 (native), against 35 for both visible. Not investigated. Layer hiding already did not help (section 1).

**Consequences for the options in section 4.**
- A: nothing to trim. The empty subclass costs the same.
- B and C: no gain for headless evaluation. Their case is the viewport cost (D37: 8.5 against 3.2 µs per control).
- D: untested, and capped at about 9 ms of 38 for all 5,380 controls (the offsetParentMatrix drive, native transforms). A stand-in for passive controls gets a fraction of that.
- The 20-rig frame is 34 to 36 ms against about 23 ms for the native floor. The control node type is not where that gap lives.

**Not done.** The full-rig playback A/B in a Maya window (current node, native transform, native curve), which is where the viewport cost shows. The per-event profile that would confirm the nested-time explanation. Why the custom node does not benefit from losing the drive. Why `visibility` off is slower.

**How it was run.** Throwaway files, not in the repo (scratchpad of the session, which a cleanup can clear): `probe/ctrlprobe.cpp` (five `MPxTransform` probe types built with `cl` against the devkit, registered like the real node), `swap_scene.py` (swaps every `rigControlNode` in a built scene for another type by UUID, keeping connections, children and dynamic attributes), `bench_scenes.py` (paired alternating bench over several scenes). If this work continues, make the swap and the probe node a `perf_scenes.py` command (section 5), as `floor` was.
