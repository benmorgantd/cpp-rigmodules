# RigModules

A C++ rigging plugin for Autodesk Maya 2026+. You mark up a skeleton, describe the rig in a JSON template, and one command builds the rig from both.

The rig it builds is aimed at animation and game export: predictable controls, stable names, and a clean split between controls and export joints.

> **Status:** early development. Eight modules exist (Layout, FkChain, FkHierarchy, Aim, TwoBoneIk, Ribbon, Limb, and ReverseFoot, whose stage 1 is built). The build is Windows and Visual Studio only. The Python package (`python/rigmodules`) has the rig queries, display layers and selection sets, skin weight helpers, `rigmodules.pose` (key controls), and JointClip capture with FBX files and bone maps. Beyond building, the plugin has a pose and animation library (poses, keys, animation files, mirror and flip), skin weight files, and a solve that restores control values from joint data.

## How it works

1. **Mark up the joints.** Each joint gets a `rigMarkup` attribute that says what it is (`side` and `usage`, for example `Left` / `upperArm`), never which module uses it. `rigJointMarkup` writes it.
2. **Write a template.** A JSON file lists the rig's modules. Each module picks its joints by `[side, usage]` selector, so the same template builds on any skeleton with matching markup.
3. **Build.** `createRigFromTemplate` checks the whole template against the skeleton first and builds nothing if anything is wrong. It reports every error at once, each with its JSON path. Then it creates the modules and connects them.

Inside the rig:

- **Module nodes do all the transform math.** Controls and joints have `inheritsTransform` off, so the DAG hierarchy is only for organization and never drives a result.
- **Modules connect through named matrices.** A module publishes **sockets** (world-space matrix outputs such as `start` and `end`) and takes **child plugs** (inputs that attach to a parent's socket). A template's `parentStructure` maps each child plug to a socket by id, for example `"start": "fkchain_01_M_MOD.end"`.
- **Every rig has a layout module (`layout_00_M_MOD`)** with three stacked root controls. Child plugs that a template doesn't parent attach to `layout_00_M_MOD.end`.
- **Space switching** uses constraints whose drivers are sockets: Parent follows the driver, Orient takes only its rotation, Point only its translation, Swing its rotation without the twist about the bone (for a shoulder ribbon), and Twist only the twist (for a wrist or ankle ribbon end). A control with more than one driver gets a `space` enum attribute to pick the active one.

## Requirements

- Maya 2026 and its devkit. Set `MAYA_DEVKIT_LOCATION` to the devkit's `devkitBase` folder.
- Visual Studio with the C++ desktop workload. The project is `cpp\RigModules.vcxproj`, and builds target x64.
- [nlohmann/json](https://github.com/nlohmann/json) 3.12 is vendored in `cpp/include/`. Nothing else needs installing.

## Build

From the repo root, in PowerShell:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" cpp\RigModules.vcxproj /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
```

The plugin is written to `cpp\x64\Release\RigModules.mll`. You can also build the Release x64 configuration from Visual Studio.

A Maya session that has the plugin loaded locks the `.mll`, so the next build fails. Unload the plugin first, or load a copy of the `.mll` from somewhere else.

## Quick start

This builds a three-joint spine with a one-joint arm hanging off its last joint. Save the template as `spine_arm.json`:

```json
{
  "rigVersion": 1,
  "rigTemplateName": "SpineArm",
  "rigName": "body",
  "rigAuthor": "you",
  "rigModules": [
    {
      "moduleType": "FkChain",
      "name": "spine_00_M_MOD",
      "side": "Center",
      "moduleArgs": { "joints": [["Center", "spine1"], ["Center", "spine2"], ["Center", "spine3"]] }
    },
    {
      "moduleType": "FkChain",
      "name": "arm_00_L_MOD",
      "side": "Left",
      "shapeType": "Capsule",
      "moduleArgs": { "joints": [["Left", "arm"]] },
      "parentStructure": { "start": "spine_00_M_MOD.end" }
    }
  ]
}
```

Load the plugin, mark up the skeleton (joints `skel:spine1_00_M_JNT`, `skel:spine2_00_M_JNT`, `skel:spine3_00_M_JNT`, `skel:arm_00_L_JNT`), and build:

```python
from maya import cmds

cmds.loadPlugin(r"<repo>\cpp\x64\Release\RigModules.mll")

# Joints are always passed by name. The selection is never used.
for usage in ("spine1", "spine2", "spine3"):
    cmds.rigJointMarkup("skel:%s_00_M_JNT" % usage, usage=usage, side="Center", export=True, skin=True)
cmds.rigJointMarkup("skel:arm_00_L_JNT", usage="arm", side="Left", export=True, skin=True)

# Every joint passed in must carry markup.
joints = ["skel:spine1_00_M_JNT", "skel:spine2_00_M_JNT", "skel:spine3_00_M_JNT", "skel:arm_00_L_JNT"]
cmds.createRigFromTemplate(*joints, template=r"C:\path\to\spine_arm.json", asset="shooter\\characters\\hero")
```

`asset` is the asset's folder path below the projects root. The template names no asset, so one template can build many assets. The rig and its input joints end up in a namespace named after the asset and the template's `rigName`, `hero_body`. A namespace holds one rig. The `namespace` flag changes it, and `:` means the root. Joint names are kept, and each must be in the [naming convention](docs/naming.md), `<camelCaseName>_##_<L|R|M>_JNT`. Controls are named from their joint and their module's side, so `skel:arm_00_L_JNT` in a `Left` module gives `hero_body:arm_00_L_CTRL`.

Give the arm a space switch between the world and the chest. The first driver starts active, so the arm now stays in world space when the spine moves. Switch it back to following the chest:

```python
cmds.createRigConstraint("hero_body:arm_00_L_CTRL", driver=["layout_00_M_MOD.end", "spine_00_M_MOD.end"], label=["World", "Chest"])
cmds.setRigConstraintSpace("hero_body:arm_00_L_CON", label="Chest")
```

Constraints can also be declared in the template. [`RigTemplates/LayoutRig.json`](RigTemplates/LayoutRig.json) is a larger example that does that and uses several modules.

## Modules

| Module | `moduleType` | Child plugs | Sockets |
|---|---|---|---|
| Layout | built in, named `layout_00_M_MOD` | none | `index_0` … `index_2`, `end` |
| FkChain | `FkChain` | `start` | `index_<n>` per joint, plus `start` (first) and `end` (last) |
| FkHierarchy | `FkHierarchy` | `start` | one per joint, named from its markup (`left_upperArm`), plus `start` (the root) |
| Aim | `Aim` | `aim`, `target`, `up` | `aim`, `target`, `up` |
| TwoBoneIk | `TwoBoneIk` | `start`, `ik`, `pole` | `index_0`, `index_1`, `index_2` (with `start` and `end`), `ik`, `pole`, `settings` |
| Ribbon | `Ribbon` | `start`, `end` | `index_0` to `index_<j-1>`, one per ik control (with `start` and `end`) |
| Limb | `Limb` | `start`, `ik`, `pole`, `settings` (internal), optional `ikTarget` | TwoBoneIk's, plus `bind_<n>` per bind joint |
| ReverseFoot | `ReverseFoot` | `ik`, `ankle` | `ankle` (the IK ankle target, wired to a Limb's `ikTarget`) |

Details, `moduleArgs` and behaviour are in [docs/modules.md](docs/modules.md).

## Documentation

| Page | Covers |
|---|---|
| [docs/templates.md](docs/templates.md) | Template file format, every key, mirroring, validation rules |
| [docs/naming.md](docs/naming.md) | The naming convention for joints, controls, modules and constraints |
| [docs/commands.md](docs/commands.md) | Every Maya command and flag |
| [docs/modules.md](docs/modules.md) | Each module's joints, controls, child plugs and sockets |
| [docs/controls.md](docs/controls.md) | Control markup, shapes, the shape file, and mirroring shapes |
| [docs/skin-brush.md](docs/skin-brush.md) | The skin weight brushes: how a stroke works, the three operations, undo, limits, adding a brush |
| [docs/looks.md](docs/looks.md) | Looks: swapping a rig's meshes and weights with `setRigLook` |
| [docs/skinning.md](docs/skinning.md) | Skin weight files, the mesh id, and loading weights onto a rebuilt rig |
| [docs/attachments.md](docs/attachments.md) | Attachments: a rig that follows another rig, with `parent:` ids and `parentRig` |
| [docs/skeletons.md](docs/skeletons.md) | Skeleton files and the skeleton hash: when two skeletons are the same |
| [docs/markers.md](docs/markers.md) | Markers: placed points that follow a joint, their file, hash and commands |
| [docs/library.md](docs/library.md) | The pose and animation library: ControlPose and ControlAnim files, FBX, the folders |
| [docs/python.md](docs/python.md) | The `rigmodules` Python package and `RigModules.mod` |
| [docs/architecture.md](docs/architecture.md) | The code: layers, source map, the build, module nodes, evaluation, and how to add a module, command or format |
| [docs/source-map.md](docs/source-map.md) | What every source file holds, the Python package, the design pages and the tools |
| [docs/mayadev.md](docs/mayadev.md) | The mayadev pipeline repo as a consumer: what it calls and how to check its call sites before changing a name |
| [tests/README.md](tests/README.md) | Running and writing tests |
| [design/README.md](design/README.md) | Which design page to read, and its state |
| [design/decisions.md](design/decisions.md) | Why the system is built the way it is (decision log: the Index, with the entries in `design/decisions/`) |
| [design/asset-model.md](design/asset-model.md) | Asset id, namespaces and the file model (rigs and looks replace variants: [design/rigs-and-looks.md](design/rigs-and-looks.md)) |
| [design/inverse-solve.md](design/inverse-solve.md) | Restoring controls from joints |
| [design/control-library.md](design/control-library.md) | The pose and animation library design |
| [design/engine-rig.md](design/engine-rig.md) | Running the rig in Unreal (speculative, not decided) |
| [design/asset-pipeline.md](design/asset-pipeline.md) | Proposal for the art stages before the rig: file types with stages, a status model, intake checks |
| [design/module-directions.md](design/module-directions.md) | Proposed module directions after the IK limb |
| [design/mayadev-integration.md](design/mayadev-integration.md) | Plan for how RigModules and the mayadev Python pipeline fit together |
| [design/tool-registry.md](design/tool-registry.md) | Proposal for mayadev's tool registry: fixes, readable ids, function calls, checked and versioned files |
| [design/active-rig-panel.md](design/active-rig-panel.md) | The active rig panel (D50, built in mayadev except its out-of-date line): rig picker, control selection by module and side, pose tools and space switching, as a view over `RigPanelSession` |
| [design/library-browser.md](design/library-browser.md) | Design for the library browser (D51, decided): list, apply, save, rename and delete poses and animations, as a searchable view over `LibrarySession`. The RigModules side (file metadata, two commands, `loadRigPose` mirror) and the mayadev side (the session and the window) are built. Publish to Approved and thumbnails are not |
| [design/two-bone-ik.md](design/two-bone-ik.md) | The two-bone IK limb as built (D52): the signed-off decisions and the maintenance surface. The full design is in design/archive/ |
| [design/mayadev-ui.md](design/mayadev-ui.md) | Proposal for mayadev's user interfaces: thin Qt views over headless view-models, a startup registration pass between layers, questions through a `Prompter`, layer tests |

[docs/README.md](docs/README.md) explains how the documentation is organized and kept current.

## Tests

The test suite runs in mayapy against the built plugin. Build Release, then run:

```powershell
cmd /c tests\RigTests.cmd
```

It runs in tiers (registration, read only, build smoke, modules, constraints and the animation library, known bugs) and stops at the first tier that fails, so the first failure shown is the root cause. See [tests/README.md](tests/README.md).

## Repository layout

All C++ is under `cpp/`, with the Visual Studio project. Inside it, source is split by whether it uses the Maya API: code in `cpp/core/` has no Maya headers, and code in `cpp/maya/` uses the Maya API and builds on `core/`.

| Path | Contents |
|---|---|
| `cpp/` | `RigModules.vcxproj`, and the build output in `cpp/x64/` |
| `cpp/core/` | Maya-free: template structs, loading, mirroring and validation, format version updates, joint and control markup schemas, control channels, the control shape library, and the file formats (shapes, skin weights, poses, keys, animation, joint clips), selection set layout, JSON and string helpers, shared name constants |
| `cpp/maya/` | Plugin entry (`PluginMain.cpp`) and the registries every node and command is added to |
| `cpp/maya/modules/` | Module nodes: Layout, FkChain, FkHierarchy, Aim, TwoBoneIk, Ribbon, Limb, ReverseFoot |
| `cpp/maya/contexts/` | Interactive tools (contexts): the skin weight brush |
| `cpp/maya/commands/` | The Maya commands, one file per command or related set: building, constraints, joint markup, controls and shapes, poses, keys and animation files, restoring from joints, rig info, skin weights. The list is in [docs/commands.md](docs/commands.md) |
| `cpp/maya/nodes/` | Other nodes: the rig root (it also runs the build), the constraint node, the control node |
| `cpp/maya/shared/` | Code used across those folders: the module base, utilities, rig traversal, and the scene sides of joint markup, shapes, skin weights, display layers, selection sets, poses, keys and the inverse solve |
| `cpp/include/` | Vendored third-party headers (nlohmann json) |
| `python/rigmodules/` | The Python package that wraps the commands for pipeline tools |
| `RigModules.mod` | Maya module file that puts `python/` on `PYTHONPATH` |
| `RigTemplates/` | Example templates |
| `docs/` | User documentation |
| `design/` | Decision log and original planning document |
| `tests/` | mayapy test suite |
| `tools/` | Developer scripts: `build.bat` (a Release build), `generate_control_shapes.py` writes the control shape library, `fbx_spike.py` (the FBX experiment behind D42), and `perf/perf_scenes.py` builds the 50-rig performance scenes and times and profiles them ([tests/README.md](tests/README.md#performance-scenes)) |
