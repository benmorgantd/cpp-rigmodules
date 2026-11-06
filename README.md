# RigModules

A C++ rigging plugin for Autodesk Maya 2026+. You mark up a skeleton, describe the rig in a JSON template, and one command builds the rig from both.

The rig it builds is aimed at animation and game export: predictable controls, stable names, and a clean split between controls and export joints.

> **Status:** early development. Three modules exist (Layout, FkChain, Aim). The build is Windows and Visual Studio only, and there is no Python front end yet.

## How it works

1. **Mark up the joints.** Each joint gets a `rigMarkup` attribute that says what it is (`side` and `usage`, for example `Left` / `upperArm`), never which module uses it. `rigJointMarkup` writes it.
2. **Write a template.** A JSON file lists the rig's modules. Each module picks its joints by `[side, usage]` selector, so the same template builds on any skeleton with matching markup.
3. **Build.** `createRigFromTemplate` checks the whole template against the skeleton first and builds nothing if anything is wrong. It reports every error at once, each with its JSON path. Then it creates the modules and connects them.

Inside the rig:

- **Module nodes do all the transform math.** Controls and joints have `inheritsTransform` off, so the DAG hierarchy is only for organization and never drives a result.
- **Modules connect through named matrices.** A module publishes **sockets** (world-space matrix outputs such as `start` and `end`) and takes **child plugs** (inputs that attach to a parent's socket). A template's `parentStructure` maps each child plug to a socket by id, for example `"start": "fkchain_1.end"`.
- **Every rig has a `layout` module** with three stacked root controls. Child plugs that a template doesn't parent attach to `layout.end`.
- **Space switching** uses parent constraints whose drivers are sockets. A control with more than one driver gets a `space` enum attribute to pick the active one.

## Requirements

- Maya 2026 and its devkit. Set `MAYA_DEVKIT_LOCATION` to the devkit's `devkitBase` folder.
- Visual Studio with the C++ desktop workload. The project is `RigModules.vcxproj`, and builds target x64.
- [nlohmann/json](https://github.com/nlohmann/json) 3.12 is vendored in `include/`. Nothing else needs installing.

## Build

From the repo root, in PowerShell:

```powershell
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" RigModules.vcxproj /p:Configuration=Release /p:Platform=x64 /m /nologo /v:minimal
```

The plugin is written to `x64\Release\RigModules.mll`. You can also build the Release x64 configuration from Visual Studio.

A Maya session that has the plugin loaded locks the `.mll`, so the next build fails. Unload the plugin first, or load a copy of the `.mll` from somewhere else.

## Quick start

This builds a three-joint spine with a one-joint arm hanging off its last joint. Save the template as `spine_arm.json`:

```json
{
  "assetId": "example",
  "assetType": "Rig",
  "rigType": "Character",
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

cmds.loadPlugin(r"<repo>\x64\Release\RigModules.mll")

# Joints are always passed by name. The selection is never used.
for usage in ("spine1", "spine2", "spine3"):
    cmds.rigJointMarkup("skel:%s_jnt" % usage, usage=usage, side="Center", export=True, skin=True)
cmds.rigJointMarkup("skel:arm_jnt", usage="arm", side="Left", export=True, skin=True)

# Every joint passed in must carry markup.
joints = ["skel:spine1_jnt", "skel:spine2_jnt", "skel:spine3_jnt", "skel:arm_jnt"]
cmds.createRigFromTemplate(*joints, template=r"C:\path\to\spine_arm.json")
```

The rig and its input joints end up in the `rig` namespace. The `namespace` flag changes that, and `:` means the root. Joint names are kept. Controls are named from their joint and their module's side, so `skel:arm_jnt` in a `Left` module gives `rig:arm_ctrl_l`.

Give the arm a space switch between the world and the chest. The first driver starts active, so the arm now stays in world space when the spine moves. Switch it back to following the chest:

```python
cmds.createRigParentConstraint("rig:arm_ctrl_l", driver=["layout.end", "spine.end"], label=["World", "Chest"])
cmds.setRigConstraintSpace("rig:arm_ctrl_l_space", label="Chest")
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
| [docs/templates.md](docs/templates.md) | Template file format, every key, validation rules |
| [docs/commands.md](docs/commands.md) | Every Maya command and flag |
| [docs/modules.md](docs/modules.md) | Each module's joints, controls, child plugs and sockets |
| [tests/README.md](tests/README.md) | Running and writing tests |
| [design/decisions.md](design/decisions.md) | Why the system is built the way it is (decision log) |

[docs/README.md](docs/README.md) explains how the documentation is organized and kept current.

## Tests

The test suite runs in mayapy against the built plugin. Build Release, then run:

```powershell
cmd /c tests\RigTests.cmd
```

It runs in tiers (plugin registration, read-only checks, build smoke tests, modules, constraints, known bugs) and stops at the first tier that fails, so the first failure shown is the root cause. See [tests/README.md](tests/README.md).

## Repository layout

Source files sit at the repo root.

| Path | Contents |
|---|---|
| `PluginMain.cpp`, `ModuleRegistry.*`, `CommandRegistry.*` | Plugin entry and the registries every node and command is added to |
| `RigModule.*`, `Utilities.*` | Shared module layer and helpers |
| `LayoutModule.*`, `FkChain.*`, `AimModule.*` | Module nodes |
| `RigRoot.*` | The build: template read, validation, module creation, parenting, constraints |
| `RigTemplateValidation.*`, `RigJsonStructs.*`, `JointMarkup.*`, `JsonUtils.h`, `StringUtils.h`, `Constants.h` | Maya-free core |
| `Constraints.*`, `RigControlNode.*`, `JointMarkupScene.*`, `JointMarkupCmd.*` | Constraints, the control node, joint markup |
| `RigTemplates/` | Example template |
| `docs/` | User documentation |
| `design/` | Decision log and original planning document |
| `tests/` | mayapy test suite |
