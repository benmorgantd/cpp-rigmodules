# Design pages

Which design page to read, and what state it is in. `decisions.md` is the log of what was decided and why (start with its Index). The pages below hold the long reasoning for one area. Read only the page for the area you are changing: most are 20 to 40 KB. Status is as of 2026-11-20, taken from each page's own status line, so check that line before relying on this table.

Design pages are history with dates, not user documentation. What a feature does now is in `README.md` and `docs/`.

## Decision log

| Page | Read when |
|---|---|
| [`decisions.md`](decisions.md) | Before any architectural change. Read its Index, then the entries it points to in [`decisions/`](decisions/) (one file per area) |

## Built: read for the reasoning

| Page | Decision | Area |
|---|---|---|
| [`fk-hierarchy.md`](fk-hierarchy.md) | D71 | `FkHierarchy`: an FK control per joint under one root, terminators, sockets named from markup. Section 16 lists where the build differs from the draft |
| [`two-bone-ik.md`](two-bone-ik.md) | D52 | The IK limb: the signed-off decisions and what it adds to maintenance. The full design is in [`archive/`](archive/) |
| [`inverse-solve.md`](inverse-solve.md) | D41 | Restoring controls from joints: the module-owned inverse. The USD part was withdrawn |
| [`control-library.md`](control-library.md) | D42 | The pose and animation library: ControlPose and ControlAnim, FBX and the bone map |
| [`asset-model.md`](asset-model.md) | D29, D54 | Asset id, scene structure, the file model. The scene side is built |
| [`rigs-and-looks.md`](rigs-and-looks.md), [`rigs-and-looks-phase4.md`](rigs-and-looks-phase4.md) | D54 | Skeletons, rigs and looks: the model and the phase 4 file model |
| [`attachments.md`](attachments.md) | D55 | A rig that follows another rig through `parent:` ids |
| [`mayadev-integration.md`](mayadev-integration.md) | D24 | The RigModules and mayadev boundary |
| [`tool-registry.md`](tool-registry.md) | D47 | mayadev's tool registry and menus (built in mayadev) |
| [`mayadev-ui.md`](mayadev-ui.md), [`active-rig-panel.md`](active-rig-panel.md), [`library-browser.md`](library-browser.md) | D49, D50, D51 | mayadev's Qt UIs, the active rig panel and the library browser (phases 1 to 3 built; the later phases are not) |
| [`asset-editor.md`](asset-editor.md) | D61 | mayadev's Asset Editor: the asset context, the Files, Markup and Shapes tabs (phases 1 to 3 built) |
| [`attachments-mayadev.md`](attachments-mayadev.md) | D55 | The mayadev side of attachments (built, merged into mayadev `main` and pushed (`01a3d55`)) |

## In progress

| Page | Decision | State |
|---|---|---|
| [`ribbon.md`](ribbon.md) | D56 | Built. `fkStyle` was added in D59 (2026-11-18). Open items are in its section 15 |
| [`derived-controls.md`](derived-controls.md) | D57 | Built (phases 0 to 5, 2026-11-18). The solomon asset's own files are the author's to change |
| [`ribbon-twist-follow.md`](ribbon-twist-follow.md) | D56 | Proposed, not signed off |
| [`limb.md`](limb.md) | D62 | Signed off 2026-11-19, phases 0 to 2 built (the module, a sharp elbow with `bendSoftness`); the inverse is next: a Limb module that runs the TwoBoneIk and Ribbon solves with one continuous ribbon. Changes to those modules affect it |
| [`control-node-perf-handoff.md`](control-node-perf-handoff.md) | D36, D37 | Handoff, nothing built |

## Handoffs

A handoff says "delete when its work is committed". Where the work is built but not committed, the page stays.

None at present. `control-node-perf-handoff.md` (above) is a design handoff with nothing built, so it stays until that work is designed.

## Proposals and speculation (nothing built)

| Page | Decision | Area |
|---|---|---|
| [`module-directions.md`](module-directions.md) | D46 | Where the modules go after the IK limb |
| [`asset-pipeline.md`](asset-pipeline.md) | D44, D45 | The art stages before the rig, face data |
| [`asset-build-fallbacks.md`](asset-build-fallbacks.md) | D70 | Builds that survive a changed skeleton, mesh or weights: the fallback ladder (phase 1 built), the transfer (open, no mesh data in the weight files), draft and baseline gates |
| [`asset-build-fallbacks-handoff.md`](asset-build-fallbacks-handoff.md) | D70 | Where the D70 design stood at the cloud handoff (phase 1 has since been built: see D70) |
| [`engine-rig.md`](engine-rig.md) | none | Running the rig in Unreal. Speculation, unverified |

## Archive

[`archive/`](archive/) holds the full text of designs whose short form replaced them. Archived pages are history: where one and the code disagree, the code wins.
