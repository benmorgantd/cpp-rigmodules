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

info = rigmodules.query.get_rig_info("hero:arm_ctrl_l")
info.asset_id      # "shooter|main|characters|hero"
info.variant       # "game"
info.rig_root      # "hero:rigRootModule1"

rigmodules.query.get_rigs("hero")    # every rig built into the hero namespace
```

| Function | Returns |
|---|---|
| `get_rig_info(node)` | A `RigInfo` for the rig a rig root, rig module or rig control belongs to. Raises `RuntimeError` for any other node, or a rig root with no asset root. Wraps [`getRigInfo`](commands.md#getriginfo) |
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
