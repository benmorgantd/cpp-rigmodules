# RigModules

A C++ rigging plugin for Autodesk Maya 2026+. You mark up a skeleton, describe the rig in a JSON template, and one command builds the rig from both.

The rig it builds is aimed at animation and game export: predictable controls, stable names, and a clean split between controls and export joints.

> **Status:** early development. Three modules exist (Layout, FkChain, Aim). The build is Windows and Visual Studio only. The Python package (`python/rigmodules`) has only the rig queries so far.

## How it works

1. **Mark up the joints.** Each joint gets a `rigMarkup` attribute that says what it is (`side` and `usage`, for example `Left` / `upperArm`), never which module uses it. `rigJointMarkup` writes it.
2. **Write a template.** A JSON file lists the rig's modules. Each module picks its joints by `[side, usage]` selector, so the same template builds on any skeleton with matching markup.
3. **Build.** `createRigFromTemplate` checks the whole template against the skeleton first and builds nothing if anything is wrong. It reports every error at once, each with its JSON path. Then it creates the modules and connects them.

Inside the rig:

- **Module nodes do all the transform math.** Controls and joints have `inheritsTransform` off, so the DAG hierarchy is only for organization and never drives a result.
- **Modules connect through named matrices.** A module publishes **sockets** (world-space matrix outputs such as `start` and `end`) and takes **child plugs** (inputs that attach to a parent's socket). A template's `parentStructure` maps each child plug to a socket by id, for example `"start": "fkchain_1.end"`.
- **Every rig has a `layout` module** with three stacked root controls. Child plugs that a template doesn't parent attach to `layout.end`.
- **Space switching** uses constraints whose drivers are sockets: Parent follows the driver, Orient takes only its rotation and Point only its translation. A control with more than one driver gets a `space` enum attribute to pick the active one.

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
  "rigAuthor": "you",
  "rigModules": [
    {
      "moduleType": "FkChain",
      "name": "spine",
      "side": "Center",
      "moduleArgs": { "joints": [["Center", "spine1"], ["Center", "spine2"], ["Center", "spine3"]] }
    },
    {
      "moduleType": "FkChain",
      "name": "arm",
      "side": "Left",
      "shapeType": "Capsule",
      "moduleArgs": { "joints": [["Left", "arm"]] },
      "parentStructure": { "start": "spine.end" }
    }
  ]
}
```

Load the plugin, mark up the skeleton (joints `skel:spine1_jnt`, `skel:spine2_jnt`, `skel:spine3_jnt`, `skel:arm_jnt`), and build:

```python
from maya import cmds

cmds.loadPlugin(r"<repo>\cpp\x64\Release\RigModules.mll")

# Joints are always passed by name. The selection is never used.
for usage in ("spine1", "spine2", "spine3"):
    cmds.rigJointMarkup("skel:%s_jnt" % usage, usage=usage, side="Center", export=True, skin=True)
cmds.rigJointMarkup("skel:arm_jnt", usage="arm", side="Left", export=True, skin=True)

# Every joint passed in must carry markup.
joints = ["skel:spine1_jnt", "skel:spine2_jnt", "skel:spine3_jnt", "skel:arm_jnt"]
cmds.createRigFromTemplate(*joints, template=r"C:\path\to\spine_arm.json", asset="shooter|characters|hero")
```

`asset` is the asset's folder path below the projects root. The template names no asset, so one template can build many assets. The rig and its input joints end up in a namespace named after the asset, `hero`. The `namespace` flag changes that, and `:` means the root. Joint names are kept. Controls are named from their joint and their module's side, so `skel:arm_jnt` in a `Left` module gives `hero:arm_ctrl_l`.

Give the arm a space switch between the world and the chest. The first driver starts active, so the arm now stays in world space when the spine moves. Switch it back to following the chest:

```python
cmds.createRigConstraint("hero:arm_ctrl_l", driver=["layout.end", "spine.end"], label=["World", "Chest"])
cmds.setRigConstraintSpace("hero:arm_ctrl_l_space", label="Chest")
```

Constraints can also be declared in the template. [`RigTemplates/LayoutRig.json`](RigTemplates/LayoutRig.json) is a larger example that does that and uses all three modules.

## Modules

| Module | `moduleType` | Child plugs | Sockets |
|---|---|---|---|
| Layout | built in, named `layout` | none | `index_0` … `index_2`, `end` |
| FkChain | `FkChain` | `start` | `index_<n>` per joint, plus `start` (first) and `end` (last) |
| Aim | `Aim` | `aim`, `target`, `up` | `aim`, `target`, `up` |

Details, `moduleArgs` and behaviour are in [docs/modules.md](docs/modules.md).

## Documentation

| Page | Covers |
|---|---|
| [docs/templates.md](docs/templates.md) | Template file format, every key, mirroring, validation rules |
| [docs/commands.md](docs/commands.md) | Every Maya command and flag |
| [docs/modules.md](docs/modules.md) | Each module's joints, controls, child plugs and sockets |
| [docs/controls.md](docs/controls.md) | Control markup, shapes, the shape file, and mirroring shapes |
| [docs/skinning.md](docs/skinning.md) | Skin weight files, the mesh id, and loading weights onto a rebuilt rig |
| [docs/python.md](docs/python.md) | The `rigmodules` Python package and `RigModules.mod` |
| [tests/README.md](tests/README.md) | Running and writing tests |
| [design/decisions.md](design/decisions.md) | Why the system is built the way it is (decision log) |
| [design/mayadev-integration.md](design/mayadev-integration.md) | Plan for how RigModules and the mayadev Python pipeline fit together |

[docs/README.md](docs/README.md) explains how the documentation is organized and kept current.

## Tests

The test suite runs in mayapy against the built plugin. Build Release, then run:

```powershell
cmd /c tests\RigTests.cmd
```

It runs in tiers (plugin registration, read-only checks, build smoke tests, modules, constraints, known bugs) and stops at the first tier that fails, so the first failure shown is the root cause. See [tests/README.md](tests/README.md).

## Repository layout

All C++ is under `cpp/`, with the Visual Studio project. Inside it, source is split by whether it uses the Maya API: code in `cpp/core/` has no Maya headers, and code in `cpp/maya/` uses the Maya API and builds on `core/`.

| Path | Contents |
|---|---|
| `cpp/` | `RigModules.vcxproj`, and the build output in `cpp/x64/` |
| `cpp/core/` | Maya-free: template structs and loading (`RigJsonStructs`), template validation, joint and control markup schemas, the control shape library and shape file format, the skin weight file format, JSON string helpers, shared name constants |
| `cpp/maya/` | Plugin entry (`PluginMain.cpp`) and the registries every node and command is added to |
| `cpp/maya/modules/` | Module nodes: Layout, FkChain, Aim |
| `cpp/maya/commands/` | Maya commands: `createRigFromTemplate`, `createRigConstraint`, `setRigConstraintSpace`, `rigJointMarkup`, `getRigControls`, `getRigControlInfo`, `getRigControlChannels`, `zeroRigControls`, `mirrorRigPose`, `flipRigPose`, `saveRigControlShapes`, `loadRigControlShapes`, `getRigMirrorControl`, `mirrorRigControlShapes`, `getRigInfo`, `setRigMeshId`, `saveRigSkinWeights`, `loadRigSkinWeights`, `pruneToMaxInfluences` |
| `cpp/maya/nodes/` | Other nodes: rig and asset roots (the root also runs the build), the constraint node, the control node |
| `cpp/maya/shared/` | Code used across those folders: the module base, utilities, joint markup in the scene, rig traversal, control shapes and skin weights in the scene, enums |
| `cpp/include/` | Vendored third-party headers (nlohmann json) |
| `python/rigmodules/` | The Python package that wraps the commands for pipeline tools |
| `RigModules.mod` | Maya module file that puts `python/` on `PYTHONPATH` |
| `RigTemplates/` | Example templates |
| `docs/` | User documentation |
| `design/` | Decision log and original planning document |
| `tests/` | mayapy test suite |
| `tools/` | Developer scripts: `generate_control_shapes.py` writes the control shape library |
