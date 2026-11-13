# Module directions from production experience

**Status:** proposals (D46, Leaning). Nothing here is built, and no module below has passed the gate (D12). The IK limb stays the next module (author, 2026-11-10). This page records where the author's production experience points the modules after it, so the reasoning is not lost. It names no people or projects.

## 1. Where the experience comes from

Years of AAA character work on a live game: gear and armour sets, cloth and secondary motion in the engine (vertex simulation and vertex shaders), a facial pipeline built on FACS with a data-driven rig description and mocap retargeting, batch tooling that runs Maya headless, an asset editing tool, and the pipeline's automated tests. The asset and face halves are in `asset-pipeline.md`. This page is the module half.

## 2. Order after the IK limb

1. A **pose-driver** module, with a **marker** spec for the engine.
2. The **face module**, with a channel map and a rules file.
3. **Overlap** only if animators ask, and only as a stateless approximation.

## 3. Pose-driver module

- **Animator-facing problem:** secondary behaviour that should just work without a control to key. A high collar that must not clip the head when the character looks up, shoulder and twist helpers, and corrective shapes driven by a pose.
- **What exists:** Aim points a joint at a target. It does not drive offsets or channels from a pose.
- **Sketch:** it reads world matrices (controls or joints) and outputs driven channels or joint offsets through a solve: a cone, a distance, or RBF. It is **stateless**, so it evaluates in parallel and scrubs correctly (D36). Its definition is JSON, like the constraint classes the author serialized before.
- **Open, and it decides the schema:** does the engine evaluate the same definition (HLSL or C#)? If so, the JSON is an export contract and RBF may be too costly. If the engine does not, the module can stay Maya-only.
- **Still to write at the gate:** inputs, outputs, child plugs, sockets, attributes, `affects`, JSON schema, export impact, library impact, tests (a `ModuleContract` subclass, pose cases, a golden snapshot), `chain_specs` coverage and `bench`.

## 4. Markers

A marker is a transform that follows a joint or control, carries no skin influence, and exports as clean data. In the engine, markers drive shader parameters (the previous studio's overlap and collider shaders). Velocity and angular velocity need frame history, so the **engine computes them**. The rig only provides the transform. This looks like a small extension of the control and joint separation for export (D41, D42), not a new module. Check that at the gate.

## 5. Overlap and secondary motion

A stateful simulation in the rig breaks scrubbing, mocap review and parallel evaluation (D36), and animators would see different results per playback. The simulation stays engine-side. The rig exposes data for it: markers (section 4) and per-vertex maps. If animators want to see overlap in Maya, build a stateless approximation, or bake a result.

## 6. Face module, channel map and rules

The expression direction is D45. The module-side pieces:

- **Channels:** channel-only controls (D34) with a FACS-style vocabulary, mirror modes declared per channel.
- **A channel map** (a source channel to a target channel, serialized) beside the bone map (D42), for sources that are not joints (facial mocap solves, 2D tracking data).
- **Connectivity rules as a data file:** a driver channel, a driven channel, a gain or curve, and a conflict policy for opposing muscles. The rules belong to the people who own the anatomy, so a rule change is never a code change.
- **Applying the rules is an animator-facing Python tool** on `getRigKeys` and `setRigKeys`, writing an additive layer the animator can bake or delete. A live node would make the driven channel unkeyable, which breaks animator priority 1 (D10). It lives in mayadev, or in `python/` only if it must change with a node or template key (D24).
- **Scale check:** about 120 channels over 5000 frames must go through the keys path (D34, D42) without trouble. Measure it.
- Getting a face into a shot with its keys lined up to mocap takes was slow. A tool in mayadev places a take on a shot range (`loadRigAnimation` has an `offset`).

## 7. Engine round trip

Reading engine animation data (a schema, partial data, search tooling) and sending poses and animated properties back belong in an **adapter in mayadev** that reads the engine format into a JointClip or ControlAnim. RigModules stays format-neutral (D24, D41, D42).

## 8. Diagnostics

A command that returns the plugin version, template and format versions and a rig's stamps (`getRigInfo` already has most of them) would make bug reports self-contained, in the same spirit as error reports that carry debug information. It adds public API, so it needs sign-off. The logging and email side is mayadev's.

## 9. Lessons the decisions already cover

| Lesson | Where |
|---|---|
| Hardcoded assumptions about what a folder or file holds | D17, read-phase validation |
| Log and string formatting cost in hot paths | D36 and the `compute()` rules |
| Diverging codebases and merges | D33, format and module update tables |
| Headless batch and parallel Maya processes | Every command runs in mayapy. Check that none assumes UI state |
| Callback stability and speed | D37 |
| Core logic reusable by tools in other DCCs | D13, a Maya-free `core/` |

## 10. Gear swapping is not a design driver

At the previous studio, player and some characters were built from parts separated by usage (arms, legs, torso, helmet, cape) for a plug and socket gear system. It dated from about 2013. It limited what designs could do and cost design continuity, and it paid off mainly for microtransactions. So nothing in the modules should assume a character is built from swappable parts. Usage markup supports it where an asset needs it (`asset-pipeline.md`, section 5.2), and rig variants (D29) and looks are the other route.

## 11. Open questions

- Does the engine evaluate pose-driver definitions, and in what language?
- Is a marker an export-skeleton extension or its own module?
- Where does the rules tool live, mayadev or `python/`?
- Is a replacement for the plug and socket gear model wanted, or only support for the old one?
