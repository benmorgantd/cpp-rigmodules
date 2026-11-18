# Ribbon, what is left after phase 2: handoff

For whoever picks up the Ribbon module (D56) next. Read `ribbon.md` (section 14 "As built" first), D56 and D36 in `decisions.md`, and `docs/modules.md` (Ribbon). Delete this page when its work is committed.

## Where things stand (2026-11-17, branch `ribbon-module-planning`, nothing committed)

Built and passing (full suite 1151 tests): the math, the node, the registry entry, the inverse, the 15 channels, fixtures and tests, docs, and 7 Ribbons in the perf rig.

Phase 2 did three things:

- **Performance was a bug, not a cost.** The first build's 308 ms against a 72 ms floor came from `compute()` failing on a reopened scene (a saved scene drops stored-rest array elements that equal their default). Fixed, with a reload test. Now 50 rigs 107 to 116 ms against a 66 ms floor (ratio 1.6 to 1.7, the same as before the ribbons), 20 rigs 20 to 21 fps. The math is 5.5 microseconds per ribbon, so the old suspects (struct initialisation, plug copies, three curve evaluations per bind frame, `pow`/`sin`/`cos`, the matrix inverse) are not worth touching. The remaining gap to D36's 1.5 and 30 fps is the `rigControlNode` premium, see `control-node-perf-handoff.md`.
- **Twist** from the ends runs the whole chain, carried in the interior controls' offset parent matrix. `twistFalloffPower` and `bow` are removed.
- **Scale** was checked with hand-worked cases (stretch, squash, partial preservation, profile, control scale with volume).

## Left, in order

### 1. `fkStyle` (phase 4 of ribbon.md)

Design is in ribbon.md sections 3.1 and 9 (Q-b, Q-c). Decisions already taken: a template key, default false; with it, the FK controls are a real chain built the way `FkChain` builds it, the ik control at index j floats on the FK control at index j, and **the node reads the FK controls' local matrices and composes the chain itself**, never their world matrices (it writes their `offsetParentMatrix`).

Settle with the author before building, because they change names and slots:

- Where the FK controls sit in the slot order. Slots today are ik controls first, then tweakers, and `ikControlCount` marks the split; the inverse, `RigInverse` and the golden depend on it. Slots are never renumbered once published (D34), so appending the FK slots after the tweakers is the safe order, with a second count or a generalised "group sizes" in `ModuleInverseContext`.
- Whether `fkStyle` is a `moduleArgs` key (the draft says template key) and how `readModuleArgs`' "every key required" rule handles an optional one.
- The base curve in `fkStyle` passes through the FK controls' positions, so `makeBase` becomes a spline through j knots, and `Setup::ikOffset` comes from the FK frame. `buildSetup` is the one place that changes shape. The ends' twist ramp (D56 revision) must still ride the interior controls.
- The inverse: the FK controls come from the joints as `FkChain` does (`ModuleInverse::solveChain`), then the ik controls as offsets from them.

It needs its own contract, hand-worked pose cases, a golden for the extra controls, docs (`docs/modules.md` has a "Not built yet" line to remove), and a decision entry.

### 2. Smaller items

- **No DG cycle check is in the suite.** Add one if a reliable Maya query exists, otherwise say so in the docs.
- **Viewport.** The ik controls and the hidden tweakers have not been looked at in a GUI Maya (`tests\ViewportGallery.cmd`).
- **Joint orientation.** The ribbon needs each control joint's X along the chain. Either direction works since D57 phase 0 (`chainSign`, 2026-11-18). A template whose control joints point across the chain still builds and gives a looped base curve. A read-phase warning in `planFromJson` would catch it, and D57 phase 1 adds that check.
- **Arc length.** A bind joint's `u` is its share of the control polyline, not an arc length. If animators see uneven spacing under big stretches, the design's arc-length table is the fix. It is cheap now (the math is 5.5 microseconds).
- **Arm made of two ribbons / an arm with a ribbon on `TwoBoneIk` sockets** (ribbon.md 3.4, 6): the plugs and sockets work (`test_the_end_follows_its_own_parent`), but no test has chained a ribbon to `TwoBoneIk` sockets, and the author's `solomon` arm template (start a child of the shoulder, end point-constrained to the elbow, start orient-constrained to the shoulder's parent so it does not take the shoulder's roll) is not built in a test.
- **Perf scene.** The perf rig keys only transforms. If a branch on a ribbon channel's value is ever added, key it in `key_ribbons` or the bench will flatter it.

## What not to change

Public names: the 15 channel names and their order (`RibbonChannel`, which is also the inverse's channel order), the `controls` and `joints` keys, the child plug and socket names, `ikControlCount`, and the node type `ribbonModule` (0x002191). A change needs a decision entry and the golden updated (D34).

## Done means

The suite passes on the build that holds the change, the bench numbers are in D56, and the docs pass (CLAUDE.md "Docs change with the code") is done.
