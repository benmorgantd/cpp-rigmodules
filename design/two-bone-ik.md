# Two-bone IK: as built

> **Coupling warning (D62).** The `Limb` module (`limb.md`) runs this module's IK/FK solve, channels, control derivation and inverse. Changing any of them changes `Limb`. Read `limb.md` first, run the Limb contract, pose cases and `test_limb_matches_parts`, and say in the change summary what `Limb` does with it.

**Status:** Decided, D52 (author, 2026-11-15). All eight phases are built. This page is the short form: the signed-off decisions and what the module adds to the maintenance surface. The full design (requirements, interface, the solve with stretch, soft, pin and roll, the inverse, build phases and the review) is kept in [`archive/two-bone-ik.md`](archive/two-bone-ik.md), which is history: where it and the code disagree, the code wins. [`archive/two-bone-ik-plan.md`](archive/two-bone-ik-plan.md) is the earlier cloud draft it superseded.

## Where it is documented now

- Using it: `docs/modules.md` (the `TwoBoneIk` section), `docs/commands.md` (`setRigIkFkBlend`, `setRigControlSpace`, `getRigControlSpace`), `docs/controls.md`.
- The code: `docs/source-map.md` (`TwoBoneIkModule`, `TwoBoneIkCmds`, `TwoBoneIkSolve::computeTwoBoneIk`), `docs/architecture.md`.
- The decision and its phases: D52 in `decisions.md`.
- Tests: `tests/maya/modules/test_two_bone_ik.py`, `tests/maya/commands/test_two_bone_ik_cmds.py`, `tests/maya/commands/test_two_bone_ik_inverse.py`, the `twoboneik` pose cases and the golden snapshot.
- Performance: 12 limbs in the complex rig (`tools/perf/perf_scenes.py`). `bench` is below D36's targets.
- Reversed by D57 (`derived-controls.md`, built 2026-11-18): the three marker joints per limb are gone, so decision 1 below no longer holds. The IK handle, pole and settings are derived from the three limb joints, with `controlName`, `poleOffset` and `settingsOffset` in `moduleArgs` and a `moduleVersion` 2 step that drops `ik`, `pole` and `settings`. Everything else here stands.

## Decisions (all answered, author, 2026-11-15)

Section numbers in the table refer to the archived page.

Changing one after the module ships changes a template key, a control markup or a channel name (D33, D34).

| # | Question | Answer | Why it matters |
|---|---|---|---|
| 1 | **Reversed by D57.** Where do the IK handle, pole and settings controls get their markup and rest? | **Three non-export marker joints in the skeleton** (`ik`, `pole`, `settings`), selected in `moduleArgs` like Aim's `target` and `up`, marked `export: false`, `skin: false`. Rejected: derived usages with a computed pole position. | Follows Aim (D15: markup says what a joint is, the template picks joints), puts the pole where the rigger put it, invents no names. Cost: three non-export joints per limb. |
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


## Maintenance surface added

- One module node, its args struct, its inverse, one registry row.
- Six channel names (`ikFkBlend`, `stretch`, `soft`, `pin`, `roll`, and `space` on the pole with two labels), four child plug names (one internal), two socket families, three marker-joint markups per limb.
- Two commands (`setRigIkFkBlend` and `setRigControlSpace`), and a `module` key on `getRigControlSpace` and `ControlSpace`.
- Constants in `RigAttr` and `PlugAndSocketNames`, `ModulePlugInfo::allowsConstraint`, `ModuleDescriptor::spaceDelta`, and scalar channels in the inverse contract.
- `TwoBoneIkSolve::computeTwoBoneIk` and `blendMatrices`, the shared custom-channel helper, the shared match formula, the shared matrix-to-channels conversion and two match output arrays on the node.
- One golden snapshot, a pose case folder, two test files and fixtures, and a block in the performance rig.

Complexity stated (CLAUDE.md, "Complexity checks"): the first custom channels, the first scalar module inputs, the first module-owned space (a concept with a descriptor hook), a visibility output, three marker joints per limb, a new plug flag in `core/`, a key on a command's result, and two commands. Rejected as simpler: an IK-only solve with no blend (the problem is the switch), a `poleFollow` slider (two controls for one idea), and a pole-only `setRigPoleSpace` (the rig panel couldn't find it).
