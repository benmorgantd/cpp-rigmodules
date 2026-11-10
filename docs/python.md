# Python package

`python/rigmodules` is a small Python package that wraps the plugin's commands. Pipeline tools, such as mayadev's asset tools, reach a built rig only through it, never by walking its nodes or parsing names.

It imports no Qt and no pipeline code, and reads no environment variables or asset paths, so it works in a bare mayapy with only the plugin loaded.

## Install

`RigModules.mod` at the repo root puts `python/` on `PYTHONPATH`. Maya reads `.mod` files from the folders in `MAYA_MODULE_PATH`, which includes `Documents\maya\modules`. Do one of these:

- Add the repo root to `MAYA_MODULE_PATH`. The repo's `RigModules.mod` uses a path relative to itself.
- Put a copy of `RigModules.mod` in `Documents\maya\modules` and replace its `.` with the repo's full path.

The `.mod` doesn't add the plugin to `MAYA_PLUG_IN_PATH`. Load the `.mll` by path, from a copy if you also build: a loaded plugin locks the file.

## rigmodules.query

```python
import rigmodules.query

info = rigmodules.query.get_rig_info("hero:arm_ctrl_l")      # a control, module, rig root or driven joint
info.asset_id      # "shooter|main|characters|hero"
info.variant       # "game"
info.rig_root      # "hero:rigRootModule1"

rigmodules.query.get_rigs("hero")    # every rig built into the hero namespace
```

| Function | Returns |
|---|---|
| `find_rig_info(node)` | A `RigInfo` for the rig a rig root, rig module, rig control or rig-driven joint belongs to, or `None` for any other node. Reports nothing for those, so it suits a selection callback. Raises `RuntimeError` for a missing node or a broken rig. Wraps [`getRigInfo`](commands.md#getriginfo) |
| `get_rig_info(node)` | Like `find_rig_info`, but raises `RuntimeError` when the node isn't part of a rig |
| `get_rigs(namespace=None)` | A `RigInfo` for every rig in the scene, or in one namespace, sorted by rig root name |

`RigInfo` is a frozen dataclass:

| Field | Value |
|---|---|
| `rig_root` | The rig root node |
| `asset_root` | The asset root node that owns the rig's namespace |
| `asset_id` | The asset id the rig was built for |
| `variant` | The rig root's `rigName` |
| `namespace` | The namespace the rig was built into, without a leading `:` |
| `rig_version` | The template's `rigVersion` |
| `plugin_version` | The RigModules version that built the rig |
| `publish_version` | The publish the rig came from, `""` until published |

## rigmodules.template

```python
import rigmodules.template

rigmodules.template.get_rig_name(r"C:\rigs\Human.json")    # "Human"
```

| Function | Returns |
|---|---|
| `get_rig_name(path)` | The template's `rigName`, which names the variant of every rig built from it. Raises `ValueError` when the file isn't JSON or has no `rigName` |

It reads the file without checking the rest of it. Template validation runs when the rig is built ([templates.md](templates.md)).

## rigmodules.skinweights

Helpers for tools that save or load the weights of many meshes at once. The commands do the work ([skinning.md](skinning.md)). This module knows the `meshId` attribute and where a weight file keeps its `meshId`, so tools don't hard-code either. Folders and file names are the tool's choice.

```python
import rigmodules.skinweights

for mesh in rigmodules.skinweights.get_skinned_meshes("hero"):
    cmds.saveRigSkinWeights(mesh, file=f"C:/weights/{mesh.split(':')[-1]}.json")
meshes = rigmodules.skinweights.get_meshes("hero")          # {meshId: mesh}
mesh = meshes[rigmodules.skinweights.read_file_mesh_id("C:/weights/body_geo.json")]
```

| Function | Returns |
|---|---|
| `get_mesh_id(mesh)` | The mesh transform's `meshId`, or `None` |
| `get_meshes(namespace)` | `{meshId: mesh transform}` for the meshes directly in the namespace (`""` for the root) that have a `meshId`. Raises `RuntimeError` when two share one |
| `get_skinned_meshes(namespace)` | The mesh transforms in the namespace with a skinCluster in their history, sorted |
| `read_file_mesh_id(path)` | The `meshId` a weight file is for, read from the top of the file. Raises `ValueError` when it has none |

`MESH_ID_ATTR` is the attribute name, `"meshId"`.
