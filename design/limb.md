# Limb module (direction, not designed)

**Status:** a direction only, recorded 2026-11-18. Nothing is designed, nothing is built, and it has not been through the D12 gate. This page exists so the reasoning from the conversation that produced the `Twist` constraint is not lost. `ribbon.md` section 6 already names a `limb` module as future work.

## 1. The problem

A limb today is a `TwoBoneIk` module and one or two `Ribbon` modules wired together by `parentStructure` and by constraints. That works, but it leaves two animator-facing problems the wiring cannot fix:

- **No rounded elbow or knee.** Each ribbon is a separate spline that ends at the joint, so the skin has a crease at the bend.
- **Four stacked controls at the elbow or knee.** The IK/FK control, the upper ribbon's end, the forearm ribbon's start and the tweakers all sit on one spot.

## 2. How the Twist constraint came about (2026-11-18)

The author asked for a `Twist` constraint for the ribbon ends at the wrist and ankle, so that only the hand's or foot's twist is distributed up the forearm or shin. With the end hung from the wrist socket (the `parentStructure` default), the hand's swing is carried with it. That tilts the end frame, bends the spline and moves the middle control.

What was considered:

1. **`Twist` as a constraint type, chained after a `Point` on the same plug.** Rejected. A plug takes one constraint node today. Two on one plug works only because the second reads the first's output as its parent, and that needs changes to template validation, the space-switch lookup (D27) and the inverse solve (D41).
2. **Constraints that do several things.** `Swing` already takes the driver's translation as well as its swing, so the wrist needs only the same shape: `Twist` takes the driver's translation and its twist, on the swing of the plug's parent. One node, one driver array, one parent: `computeDelta`, the inverse solve, D27 matching and mirroring are untouched. **Built (D31 amendment, 2026-11-18).** What this does not give is separate drivers per component (orient from A, point from B). That needs a driver array per component, or chaining, and nothing needs it yet.
3. **A `Limb` module that combines the ribbons and the IK limb.** The real fix for the elbow. The swing and twist split becomes plain maths between the shoulder, elbow and wrist frames inside the module, with no constraints, no stacking and no inverse chain. `Swing` and `Twist` stay useful for the neck, spine and clavicle, but not for limbs.

The author's call was to build `Twist` now, since it is small and fixes the wrist and ankle on the current rig, and to record the Limb direction here and design it next.

## 3. What a design has to answer (not answered)

- The animator-facing problem and what an existing module or a small extension already covers (D12 gate). The first candidate to rule out is a Ribbon that can pass through a mid joint.
- One spline through the elbow with a single elbow control, and where the IK/FK controls and the tweakers live.
- Whether the module reuses `TwoBoneIk` and `Ribbon` code, or one is built on the other. `core/SwingTwist` and `core/RibbonMath` are the shared maths.
- Names and channels: the limb changes the saved control names of both modules, so poses and animation need a defined path (D34, D33).
- The perf target (D36): 30 fps for 20 rigs and 1.5 times the native floor for 50, with many instances in the perf rig.
- Export and the inverse solve (D41): the control a joint restores to must stay well defined with one spline through the elbow.
- The maintenance surface it adds, and what it retires.

## 4. Next step

Write the design page for sign-off before any code. If the Limb module lands soon, the wrist and ankle `Twist` constraints on the perf rig are the first thing it would retire.
