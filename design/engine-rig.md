# Engine rig: running the rig in Unreal (speculative)

**Status:** Speculation (2026-11-13). Nothing here is decided, built or verified. There is no decision entry in `design/decisions.md` because nothing is decided. If the spike (section 9) goes well, the choices in section 10 become decisions.

**Unverified Unreal claims.** I have not tested any statement about Unreal's Control Rig, Modular Rig or RigVM against a real Unreal version. They come from general knowledge, and the Modular Rig in particular was still maturing as of my knowledge. Every such claim is marked *(unverified)*. Check the target Unreal version before relying on one.

## 1. The question

The industry is moving toward animating in the engine's context (props, environment, gameplay) instead of keeping the DCC in step with the engine. Could this plugin's rig also run in Unreal, so an animator keys the same controls there?

`design/control-library.md` (D42) treats Unreal as a skeleton and animation-FBX consumer. This document asks the other question: the engine hosts the controls too.

## 2. Gate for new modules, applied to this idea

This is not a module, but it adds a second runtime, so it goes through the same gate.

1. **Animator-facing problem.** Animators working in Sequencer or in-engine tools want the same controls, channel names, space switches and mirroring as in Maya, so poses and animation move between the two without a bake. Without that, the engine either gets a different rig, or animators bake to joints and lose the controls.
2. **Does something existing cover it?** Unreal's own Control Rig, authored by hand per character, covers the in-engine animation need. It doesn't cover parity with the Maya rig, the template as single source, or the shared pose and animation library. A hand-built engine rig per character is the drift problem D42 describes for hand-built studio rigs.
3. **Design.** Sections 3 to 8.
4. **Maintenance surface.** Section 7.
5. **Perf rig.** Not applicable until a module is ported. When one is, the Unreal side needs its own measurement. Section 8 says what to measure.

## 3. Three levels of "the same system"

| Level | What is shared | Cost | Verdict |
|---|---|---|---|
| 1. Data | Template JSON, control markup, channels with mirror modes, ControlAnim and ControlPose files | Low. The formats are already engine-agnostic. | Worth doing first. |
| 2. Math | Matrix and constraint math from `core/`, compiled into an Unreal module | Medium. Needs a coordinate and unit adapter, and a build dependency from the engine project on this repo. | Maybe, per module. |
| 3. Node graph | The module nodes themselves | Not possible. `MPxNode` plug evaluation and RigVM are different execution models. | No. |

So the engine rig is a second implementation of the same modules, held to the first by shared data and by parity tests. It is not one codebase running in both places.

## 4. What Unreal has *(all unverified)*

- **Control Rig**: a RigVM node graph over a rig hierarchy (bones, controls, nulls). It has a forward solve (controls to bones) and an optional backward solve (bones to controls). Sequencer, Anim Blueprints and the editor use it.
- **Modular Rig**: assembles a rig from modules that expose connectors and sockets. The concepts resemble this plugin's child plugs and sockets, which could make the `parentStructure` pass map directly. How stable it is in the target version is the first thing to check.
- **Custom RigVM units** (`FRigUnit`) in C++: a module's math can run natively.
- **Built-in units**: FK, aim, parent and other constraints, IK. Some of this plugin's modules may need no custom unit.

## 5. Proposed shape

### 5.1 The template is the contract

A generator on the Unreal side (editor C++ or Python, undecided) reads the same template JSON and builds the rig. It follows the same two passes: create every module, then apply `parentStructure`. The template already carries what the engine needs: modules, joints by markup selector, constraints, mirroring (expanded in the read phase, D28).

An open question is whether the generator reads the raw template and reimplements the read phase (mirror expansion, validation, update steps), or whether `core/` compiles for Unreal. Reimplementing means two read phases that can disagree. Compiling `core/` for Unreal means a build dependency, but one source of truth.

### 5.2 Controls are found by markup

Engine controls carry the same side, usage and channel names (D25, D34). Tools in either runtime find them through markup, never by name. This is what lets a ControlAnim cross runtimes.

### 5.3 Animation and poses are the transport

- A ControlAnim (D42) maps onto Sequencer control rig channels: keys, tangents, weighted flag, infinities. The mapping from the ControlAnim tangent encoding to Unreal's curve tangent model needs checking *(unverified)*.
- A ControlPose maps to a set of channel values.
- Round trip in both directions: Maya to engine through `saveRigAnimation`, engine to Maya through an Unreal-side writer that produces the same file. The file formats stay the contract, so there is no new transport format.

### 5.4 Joints still export as before

The engine skeleton and baked animation FBX path (D42, section 6) stays for anything that doesn't run the Control Rig: runtime playback, AnimSequence assets, mocap. Control Rig in engine is an addition for authoring, not a replacement for the skeleton export.

## 6. What does not map cleanly (risks)

- **Coordinates and units.** Unreal is Z-up, left-handed, centimeters, double precision. Maya here is whatever the scene uses. Rest matrices and mirror axes (`findMirrorAxis`) depend on handedness, so mirroring needs care.
- **`inheritsTransform` off and no DAG inheritance** is the architecture's central rule. Whether it maps onto the Control Rig hierarchy, which does have parenting and space semantics of its own, needs checking *(unverified)*.
- **Space switching** (the `rigConstraint` node, D31): Unreal has its own space concept *(unverified)*. Matching the switch semantics, including `matchSwitchSpace`, is a likely source of divergence.
- **Control shapes** (D26, generated meshes with fill and wireframe alpha) are drawn by a VP2 draw override. Unreal needs its own shape representation. The shape file format may carry over, but drawing doesn't.
- **Authoring versus runtime.** Animating in Sequencer is an editor-time concern. Evaluating a Control Rig in a shipped game is a performance concern with a budget. This design assumes authoring. Runtime use is a separate question.
- **Version churn.** Control Rig and Modular Rig have been changing between engine versions *(unverified)*. A generator tied to one version's API may need rework on each upgrade.

## 7. Maintenance surface this adds

- Every module and constraint type needs an engine implementation or a documented mapping to a built-in unit.
- A second read phase, or a build of `core/` for Unreal (section 5.1).
- A per-engine-version compatibility burden for the generator.
- A parity test harness that runs Unreal (headless commandlet or editor automation), which the cloud and the Maya suite can't run.
- Docs: a user page and a developer page per the docs pass, once anything is built.

Two things it does not add: no new file formats (it reuses ControlAnim, ControlPose and the template), and no change to existing module names, child plugs, sockets or control names. Names stay public API.

## 8. Parity test plan

The Maya suite has pose cases and golden snapshots (`tests/README.md`). Parity extends them:

1. Build the same template in Maya and Unreal.
2. Apply the same control values by markup and channel, with hand-written expectations as everywhere else, never recorded from either runtime.
3. Compare joint world matrices within a stated tolerance, after the coordinate conversion.
4. Cover mirroring, a Center control flip, and a space switch.

If a module is ever ported, measure the engine's evaluation cost for many instances against a stated budget, as the perf rig does for Maya (D36). The target is undefined here and is the author's call.

## 9. Spike (proposed)

Smallest experiment that would settle the main unknowns, in order:

1. In a recent Unreal version, create a Control Rig for a small FkChain by script, from a template. Record which API did it.
2. Check whether Modular Rig connectors and sockets can express child plugs and `parentStructure`.
3. Compare joint world matrices against the Maya golden snapshot for a handful of poses.
4. Load one ControlAnim into Sequencer on that rig. Record what mapped and what didn't (tangents, infinities, the Center flip).
5. Write one ControlAnim back out from Sequencer.

The spike needs a local Unreal install. It can't run in the cloud.

## 10. Choices that stay open

- Reimplement the read phase in the engine, or compile `core/` for Unreal (5.1).
- A generator in editor C++, Python, or both.
- Modular Rig, or a plain Control Rig built by the generator (depends on the spike).
- Whether the engine rig is authoring-only or also runtime.
- Where the engine-side code lives: this repo, mayadev, or a third place. D24 says mayadev depends on RigModules and never the reverse, and the same direction would apply to an Unreal plugin.
- Which modules to port first. FkChain and Layout are the reference patterns, so they are the natural start.

## 11. Not decided here

Nothing in this document changes the existing design. D42's engine export remains the only engine path until the author decides otherwise.
