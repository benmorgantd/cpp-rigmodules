# Machine learning on the animation formats

**Status:** early exploration (D47, Open). Nothing here is built, and nothing has passed the gate (D12). The question is only whether the current design path leaves room for it. The answer so far is yes, with no change to the plugin. This page records the reasoning so it is not lost.

## 1. What is wanted

Three things, in this order (author, 2026-11-15):

1. **Better mocap to control solving.** The analytic inverse (D41) turns a JointClip into control keys. "Better" means output an animator would accept.
2. **Mocap blending.**
3. **Tweening** between an animator's own keys.

## 2. Why the design path already fits

- **Stable names are the feature schema.** A channel is identified by control markup (side, usage, D25) and channel name. `getRigControlChannels` gives rest, range and mirror mode (D34). A model's input and output layout comes from the rig, and the format versions (D33) give a migration path.
- **The JointClip seam (D41).** `animateRigFromJointClip` already lands joint motion on controls through a module-owned inverse. A model that outputs joint motion plugs in there with no rig change.
- **Deterministic forward evaluation.** All transform math lives in the module nodes (`inheritsTransform` is off), so any control pose or ControlAnim can be evaluated headless to joints. That gives exact (controls, joints) pairs without mocap.
- **Mirror modes** (`mirrorRigKeys`, D42) give left/right augmentation.
- **ControlAnim with tangents (D42)** can hold a result, but a model emits dense curves, so a key-reduction step is needed (section 4).

## 3. Where the analytic solve is expected to fall short

To be confirmed on real clips before any training:

- **Key density.** The inverse produces baked, dense curves. Animators want sparse keys with sensible tangents. Key placement is a style judgment, and it is the likeliest ML target.
- **Discrete choices.** Space-switch state, and IK vs FK per segment once the IK limb exists. Space switches must be exported as per-key classes, not floats.
- **Noise and proportions.** Foot sliding, jitter, and the offset seam in `inverse-solve.md`.

FK itself is exact, and ML is not needed there.

## 4. Data

- **Paired data from existing animation.** Any ControlAnim on the rig can be baked to a JointClip by forward evaluation. That gives (joints, animator keys) pairs with the keys as the target. Mocap-style noise on the joint side simulates a capture. A few dozen hand-keyed clips is a start.
- **The real library** (mocap to animator-cleaned keys) can be built later from the tool's own output once animators correct it.
- **Blending and tweening** are joint-space problems. Public sets (LAFAN1, 100STYLE, AMASS) retargeted through `bonemap.py` and markup are enough to start, and no rig-paired library is needed.
- **Training in joint space, not control space.** Control space has space-switch discontinuities, Euler wrap and IK/FK blends. Rotations as 6D or quaternion train better, and the existing inverse lands the result on controls.

## 5. Baseline first

Before any model, a non-ML key reducer (curve simplification within a tolerance) sets the bar. A model earns its place only where it beats the baseline on held-out clips. This follows the priority order: simplicity before cleverness.

Measures, all testable without ML:

- **Round trip:** joints to controls to joints, error per joint.
- **Key count** per curve against the animator's own keys.
- **Key invariance (tweening):** animator keys must be reproduced exactly by the solve.

## 6. Placement

- **Plugin:** no new module, no `compute()` change. ML stays out of the node graph.
- **Tooling here (if it goes ahead):** bake ControlAnim to JointClip, a random pose sampler within channel ranges, a batch evaluator, the key reducer and the metrics above. These read and write existing formats.
- **Training and inference:** mayadev or a separate package that reads and writes JointClip and ControlAnim files. `rigmodules` imports no numpy (D24).

## 7. Open

- Where the current solve actually disappoints: key density, IK/FK choice, or noise and proportions.
- Where the ML code lives: mayadev or a new repo.
- Per-rig models or skeleton-conditioned ones. The template's topology and markup give the conditioning input.
- Hard constraints: how a tweening model is made to hit animator keys exactly, versus fixing it after the fact with the solve.
- Whether a key reducer belongs in `rigmodules` (it only needs ControlAnim) or beside the ML code.
