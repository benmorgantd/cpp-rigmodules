# mayadev integration plan

**Status:** decided 2026-11-06 (D24 in `decisions.md`). The author signed off on the boundary, the placement rule and the migration steps. On 2026-11-08 the author decided to keep mayadev as the pipeline repo rather than start a new one, and to delete its legacy code there (D30). Built so far: the RigModules menu and plugin load at startup, and the asset file model on mayadev branch `asset-model` (D29).

`mayadev` is the author's Python pipeline repo (https://github.com/benmorgantd/mayadev, local checkout `C:\Users\bmorgan\Documents\dev\maya\mayadev`). Its package is `bmaya`, under `py/scripts/bmaya`. It has its own rig builder, which works completely differently from RigModules. The author will use mayadev for skinning, animation and other GUI tools, so this plan sets the boundary before either side grows further.

This is personal portfolio work run as if it were a studio pipeline. No rigs or animation built with mayadev's builder need to survive.

## What mayadev contains

About 17k lines of Python, Python 3, Maya API 2.0 for the most part. Verdicts: **Keep** (use as is), **Adapt** (keep the idea, rework it to sit on RigModules), **Retire** (replaced by RigModules), **Reference** (read it to find requirements, don't port the code).

| Area | Path under `py/scripts/bmaya` | What it is | Verdict |
|---|---|---|---|
| Custom startup | `py/scripts/userSetup.py` | Deferred startup: log streaming to a file, logger, progress window, tool registry, tool menus. Loads `RigModules` from `RIGMODULES_PLUGIN` (a path, or the plugin name once `RigModules.mod` exists) before building the menus | Keep. The author wants it |
| Core utilities | `core/log.py`, `progressmanager.py`, `decorators.py`, `util/fileUtils.py` | Logger with a Maya handler, singleton progress manager, timing decorator, file versioning | Keep |
| Tool registry | `core/tool_registry.py`, `maya_ui/tool_views/tool_menu.py`, `maya_ui/tool_registry_editor.py`, `toolsets/*.json` | Tools stored as GUID + Python snippet in JSON, a menu layout JSON, editors for both. `rigmodules_menu.json` is a second menu, built only when RigModules loads, whose snippets call the RigModules commands on the selection | Keep. When the query API exists, its snippets switch to it (rig root from any selected node) |
| Batch framework | `core/batch/`, `maya/batch/`, `tests/`, `batch/docgen/` | mayapy batch runner with a manifest, logs and a Markdown report. Used by one smoke test and the mkdocs generator | Keep |
| Events | `maya/eventsregistry.py` | Singleton OpenMaya callback registry with pause and auto-cleanup | Keep |
| Qt base | `maya_ui/qtbase.py` | Window, dockable widget, tree and list widgets, progress window | Keep |
| Asset layer | `maya/asset/asset.py`, `asset/components/*` | Asset id `project\|type\|name`, folders under `RIGGING_PROJECT_DIR`, component files, asset versioning | Retire. Replaced by the Maya-free `bmaya.asset` package (D29, branch `asset-model`) |
| Skinning | `maya/rigging/weights/` | `weightio` (deformerWeights JSON by vertex index), `weightutils`, skin tools and MPx commands (prune, extrude, average, smooth, copy, cvShapeInverter) | Adapt |
| Joint authoring | `maya/rigging/joint.py` | Joint at selection center, orient to world or mesh, mirror face joints, `label_joints` from names | Keep the authoring tools. `label_joints` is replaced by `cmds.rigJointMarkup` |
| Animator tools | `maya/rigging/rigTools.py`, `activerig.py` | Reset control, module or character to defaults, select module, key all, IK/FK match and bake, the active-rig tracker | Adapt onto the RigModules query API |
| Rig builder | `maya/rigging/base/` (`Rig`, `Module`, `Layout`), `buildio.py`, `templates/*.json` | Rig build from a nested JSON tree, using cmds constraints, offset-group DAG hierarchies and network nodes | Retire |
| Legacy modules | `maya/rigging/modules/` | Arm, Leg, `_Foot`, IkChain, IkFkChain, FkChain, FkTwist, Spline, NeckHead, SingleJoint, Face, `__Ribbon`, `_ThreeJointIkFkTwist` | Reference |
| Control shapes | `maya/rigging/shapes/` | `ControlCurve` (nurbsCurve with offset and space groups), `shapeio` saves, loads and mirrors world-space CVs for every `*_ctrl` | Retire the code. Keep the workflow: rebuilt as shape IO in RigModules (see "Shape IO") |
| Rig manager UI | `maya_ui/rigManager/` | Dockable editor for the legacy `build.json` tree, plus the AssetCreator. Its attribute fields come from Python `__init__` signatures | Retire. Replaced by New Asset and New Variant tools and a template authoring UI that validates through RigModules `python/` (D30) |
| Generic utils | `maya/util/` | dg, dag, math, name, selection, bounding box, `BMETADATA` JSON tagging, a Python matrix parent constraint | Keep the generic ones. `matrixConstraint.py` is replaced by `rigConstraint` inside rigs |
| Python plug-ins | `py/plug-ins/`, `py/scripts/plug-ins/` | Python API nodes: aim constraint, quatExtract, softIK, coneReader, rayIntersect, wheel, trig, simple collision, spring | Reference. quatExtract (twist), softIK and coneReader are candidates for C++ when a module needs one |

## Where the two systems conflict

RigModules wins each of these, because each is already a decision.

| Concern | mayadev | RigModules | Resolution |
|---|---|---|---|
| Transform evaluation | `cmds.parentConstraint` plus offset-group hierarchies | Module nodes compute everything, `inheritsTransform` off (D1) | RigModules |
| Finding joints | Names (`arm_00_L_JNT`), mirroring by replacing `_L_` with `_R_`, twist joints found by `"Twist"` in the name | `rigMarkup` JSON and `[side, usage]` selectors (D15) | RigModules. mayadev naming stays a naming convention only |
| Template | Nested `children` tree. Keys are Python `__init__` arguments, found by introspection. `modType` | Flat `rigModules` list, `parentStructure`, `moduleType`, `moduleArgs`, read-phase validation (D17) | RigModules. Old `build.json` files are not converted (no rigs need to survive) |
| Rig metadata | Every Python instance attribute stamped as a locked string attribute on network nodes. A general `BMETADATA` JSON attribute | Typed attributes on rig nodes, JSON in string attributes through `JsonUtils` (D16) | RigModules on rig nodes. `BMETADATA` may stay for tool tagging, never on rig nodes or joints |
| Sides | `L`, `R`, `M` | `Left`, `Right`, `Center` (`SideNames`) | RigModules. Python reads side names from one place and does not copy them |
| Control shapes | nurbsCurves, shape edits saved as world-space CVs in `cvs.json` | `rigControlNode` drawn by VP2 from `shapeType`, `shapeTransform`, `wireframeColor`, `wireframeAlpha`, `filled` | RigModules node. Shape IO saves `shapeType`, `shapeTransform` and `filled` per rig (built, D26) |
| Mirroring | Build a second module instance with mirrored arguments | `"mirror": true` on a template module or constraint, side swap in the read phase (D28) | RigModules |
| Validation | None | C++ read phase | RigModules |
| Names | `Layout`, `FkChain`, `LayoutRig.json`, "build" | The same words mean different things | Deleting the legacy builder removes the clash. Until then, "legacy" means mayadev's builder |

## The boundary

**Two repos, one dependency direction: mayadev depends on RigModules, never the reverse.** Nothing moves wholesale from mayadev into this repo. RigModules' `python/` package is mostly new code that wraps the C++ nodes. Everything else stays in mayadev.

### The test for where a piece of Python goes

Ask: **would this code have to change if a RigModules node, attribute, command flag or template key changed?**

- **Yes:** it goes in RigModules `python/`, where the suite tests it against the real plugin in the same change as the C++.
- **No:** it stays in mayadev.

Two hard limits on `python/`, whatever the answer: it imports no Qt and no `bmaya`, and it reads no environment variables or asset paths. It has to work in a bare mayapy with only the plugin loaded, so the suite, an export job or a farm can use it.

### Where each piece lands

| Piece | Home | Why |
|---|---|---|
| Build wrapper around `cmds.createRigFromTemplate` | RigModules `python/` | Mirrors the command's flags |
| Rig query API: rig root from any node, modules, controls, sockets, child plugs, rest values | RigModules `python/` | Knows the node graph. This is the only way tools reach rig internals |
| Template load and validate (calls the C++ core) | RigModules `python/` | Template schema |
| Module interface query (`moduleArgs`, child plugs, sockets per `moduleType`) | RigModules: a C++ command, wrapped in `python/` | Comes from the `ModuleRegistry`. The template editor reads it so it never copies the schema |
| Shape IO: read and write control shape data | RigModules C++ commands `saveRigControlShapes` / `loadRigControlShapes` (D26) | The data is `rigControlNode` attributes |
| Pose data read and write (control name → values) | RigModules `python/` | Keyed on control names and attributes, which are rig public API |
| Asset id → folder paths, component files, versioning | mayadev | Pipeline layout, uses `RIGGING_PROJECT_DIR` |
| Build orchestration (import skeleton and mesh, build, apply shapes, load weights, post-ops, save) | mayadev | Calls the RigModules wrapper and shape IO with paths from the asset layer |
| Skinning tools and weight IO | mayadev | Deformers, not rig nodes |
| Animator tools (reset, key all, IK/FK match, pose library UI) | mayadev | Built on the query API and pose data functions |
| Qt, tool registry, menus, events registry, batch framework, custom startup | mayadev | General tooling |

Pose and shape IO split the same way. RigModules owns the data format and the functions that read and write a rig. mayadev decides where the file lives and when it gets called.

### How the move happens

1. **Tag, then delete the legacy code in mayadev** (D30). No rigs need to survive, so a freeze period isn't needed. Commit or stash the uncommitted legacy work, tag main `legacy-rig-builder`, and merge branch `asset-model`. Then, on a cleanup branch, delete `rigging/base/`, `rigging/modules/`, `buildio.py`, `rigging/shapes/`, `rigging/templates/`, `maya_ui/rigManager/`, the legacy `bmaya.maya.asset` and the constants that read `RIG_TEMPLATES_DIR`, and fix startup and the menus so nothing imports them. The legacy code stays readable at the tag as requirements reference.
2. **Start `python/rigmodules/` in this repo.** Design first: the package layout, the query API surface and its tests. Its tests join a tier in `tests/run_tests.py`, so the suite covers the C++ and its wrapper together.
3. **Point mayadev at it.** mayadev imports `rigmodules` and nothing else from this repo. Its startup loads `RigModules.mll`.
4. **Rebuild mayadev's rig pieces on top.** The asset build, the rigManager rewrite, shape and weight load, then the animator tools.

### Packaging: one `.mod` per repo

Each repo ships its own module file. `MAYA_MODULE_PATH` points at both, and either repo can move or version on its own.

- **RigModules.mod:** `MAYA_PLUG_IN_PATH` gets `cpp/x64/Release`, `PYTHONPATH` gets `python`. No other environment variables.
- **mayadev.mod:** `PYTHONPATH` and `MAYA_SCRIPT_PATH` get `py/scripts`, so `bmaya` and `userSetup.py` are found. `MAYADEV_ROOT` and `RIG_TEMPLATES_DIR` can be module-relative (`VAR:=path`), which removes them from `Maya.env`. `RIGGING_PROJECT_DIR` points at user content outside the repo, so it stays in `Maya.env`.
- **To verify locally:** that Maya runs `userSetup.py` from a module's script path, and how a module `.mod` path behaves with a plugin that is locked while Maya has it loaded (the test runner copies the `.mll` today).

### Custom startup and the test suite

mayadev's `userSetup.py` is wanted in interactive Maya. The suite keeps skipping it (`MAYA_SKIP_USERSETUP_PY=1` in `tests/RigTests.cmd`), so tests run against the plugin alone. That is also how RigModules `python/` stays independent of mayadev: if it works there, it doesn't depend on the startup having run.

## Asset folders (TODO, mayadev side)

Superseded by `asset-model.md` (D29, 2026-11-08), which proposes the asset id, file model, versions and registry. The notes below record the legacy layout.

mayadev's `Asset` already resolves an asset id to a folder. Keep that model. Find the asset root node by its `assetId`, then find its rig data next to it on disk.

Legacy layout, under `RIGGING_PROJECT_DIR/<project>/<type>/<name>/`:

```
rig/components/mesh.ma         mesh
rig/components/skeleton.ma     skeleton
rig/components/build.json      legacy build tree
rig/components/cvs.json        control CVs
rig/components/weights/        one deformerWeights JSON per mesh
rig/components/animRig.ma      built rig
rig/components/postops.py      post-build script
rig/components/facePoses.json  face poses
<name>.Versions/               whole-asset version folders
```

TODO for the RigModules era:
- `build.json` becomes a RigModules template. Its `assetId` matches the folder.
- `cvs.json` becomes the shape IO file (`shapes.json`).
- Add a `poses/` folder for pose library data, in the format RigModules `python/` defines.
- Keep `weights/`, `postops.py`, `animRig.ma` and versioning as they are.
- A tool can go from a node in the scene to the asset folder: query API → rig root → asset root `assetId` → mayadev `Asset`.

## Shape IO (built, RigModules side)

Riggers edit control shapes per asset. Artists and animators don't. A rig rebuilt from its template has to come back with the rigger's shapes. Built 2026-11-07 (D25, D26):

- **Format:** one JSON file per rig, keyed by control markup `(side, usage)`, not by control name. Each entry holds `shapeType`, `shapeTransform` and `filled`. Color is not saved: it follows the module's side. Format reference in `docs/controls.md`.
- **Commands:** `cmds.saveRigControlShapes(rigRoot, file=path)` and the undoable `cmds.loadRigControlShapes(rigRoot, file=path)`. Load reports controls missing from the file and entries with no control, and applies the rest.
- **Apply:** mayadev's asset build calls `loadRigControlShapes` after `createRigFromTemplate`, with the path from the asset layer.
- **Mirroring:** `cmds.mirrorRigControlShapes(rigRoot, leftToRight=True)` replaces legacy `shapeio.mirror`, keyed on markup `usage` across sides (D28).
- **Still TODO:** documenting the asset's `shapes.json` in mayadev's docs.

## How a rig build maps across

mayadev's `Rig.build_rig` steps, and who owns each one afterwards:

| Legacy step | New owner |
|---|---|
| New scene, load plugins | mayadev (startup and the build both load `RigModules.mll`) |
| Make the character network node | RigModules: `createRig` makes `assetRootModule` and `rigRootModule`. The `assetId` is the same `\|`-separated format, with variable depth (D29). One asset root per namespace, shared by the rig variants |
| Read `build.json` | The asset's template file |
| `Layout` imports the skeleton and mesh files | mayadev, importing the skeleton into the rig namespace |
| Build each module, then mirror | `cmds.createRigFromTemplate(*joints, template=path, asset=assetId)` through the `python/` wrapper (D29) |
| `CTRL_set`, `BIND_set` | Open: built from the query API, or by RigModules. Animators and export both need a control set |
| Skin and load weights | mayadev skinning, on the export joints |
| Load control shapes | RigModules shape IO, reading the asset's `shapes.json` |
| Color controls by side | RigModules (`Colors.h`) |
| Mesh reference toggle | mayadev |
| Lock constraints, hide IK handles | Gone. No cmds constraints left |
| Post-ops script | mayadev |
| Save `animRig.ma` and version it | mayadev |

mayadev's guide workflow (guides, then bind joints) is skeleton authoring. Under D15 joints and markup are the rig input, so guides stay a mayadev tool whose last step is `cmds.rigJointMarkup`. RigModules modules never read guides.

## Using the legacy modules as reference

The legacy modules are a list of what the author's rigs have needed. Each new RigModules module still goes through the gate (D12). These files feed the gate's first two steps (what is the animator-facing problem, and does an existing module cover it). They are not code to port.

Animator-facing features found there, which RigModules does not have yet:
- `followWorld` on the first FK control (`setup_align_world`)
- moveable pivot controls (`add_moveable_pivot`)
- per-control `defaultValues`, used for reset. A pose library needs the same data
- IK/FK match and bake, key all, select module (`rigTools.py`)
- twist distribution (FkTwist, quatExtract), soft IK, spline spine, foot roll

## mayadev issues found while reading (not fixed)

- `weightutils.py` imports `maya.api.OpenMaya` but calls API 1 patterns (`MScriptUtil`, out-parameter `getPathAtIndex`). Those functions fail as written.
- `py/plug-ins/` and `py/scripts/plug-ins/` hold different copies of the same plug-ins.
- `toolsets/master_tool_registry.json` "Orient to World" imports `bmaya.maya.rigging.joints`. The module is `joint`.
- `docs/guides/Mayadev Env Setup.md` points `RIG_TEMPLATES_DIR` at a `bmaya3` path that does not exist.
- `joint.add_twist_joints` hard-codes `skel:shoulder_00_L_JNT` and `skel:elbow_00_L_JNT`.
- The only test is a one-joint build smoke test.
- At the time of reading, `rig.py` and `Spline.py` had uncommitted changes.

## Phases

0. **Done (docs only):** this page, D24, CLAUDE.md, memory.
1. **mayadev cleanup:** done 2026-11-08 (D30). `legacy-rig-builder` tagged, `asset-model` merged, legacy code deleted on branch `asset-tools` (not merged), with a copy in mayadev's ignored `legacy_reference/`. `mayadev.mod` exists, and startup loads the plugin.
2. **RigModules `python/` package:** started (D30). Built: `rigmodules.query` (node → rig root, asset id, variant, namespace, stamps) on the `getRigInfo` command, `rigmodules.template.get_rig_name`, and `RigModules.mod` (`PYTHONPATH` only). Still to do: the build wrapper, the rest of the query API, and the module interface query.
3. **Shape IO:** built, mirroring included (see "Shape IO", D28).
4. **mayadev asset tools** (D30): built in `bmaya.maya.assettools` on branch `asset-tools`: New Project, New Asset, New Variant (writes the variant's `template.json`), Save Skeleton, Build Rig from Asset, Save Rig, and asset-aware shape save and load. Still to do: the template authoring UI. Rigs still build anywhere before an asset exists, and each tool falls back to a file dialog when the rig's asset folder doesn't exist. The asset file model they use is on branch `asset-model` (D29, `asset-model.md`).
5. **Skinning and animation tools** on the query API: pose data, reset, key all, IK/FK match once IK modules exist.

## Answers so far (2026-11-06)

1. **Boundary:** two repos. `python/` here holds only the wrappers tied to the rig modules. Skinning, Qt, animation tools, the events registry and the rest stay in mayadev. The author asked how the move should work. "The boundary" above is that answer, signed off the same day.
2. **Legacy rigs:** none need to survive. This is personal portfolio work, run as if it were a studio pipeline.
3. **Control shapes:** riggers edit shapes per asset, artists don't. That is what `shapeTransform` is for. Shape IO is built (D26). The asset folder layout is a TODO above.
4. **Distribution:** `.mod` packaging is fine. This plan proposes one `.mod` per repo.

Still open: control sets (`CTRL_set` and a bind or export set). Shape IO is C++ commands (D26), and mirroring is a template key plus shape and control commands (D28).
