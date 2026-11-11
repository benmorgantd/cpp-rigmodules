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
| `get_rig_controls(rig_root, module_type=None, side=None, usage=None)` | A list of `RigControl` in module order, then each module's control order. Each filter is optional, takes one value or a list, and every one given must match. Wraps [`getRigControls`](commands.md#getrigcontrols) and [`getRigControlInfo`](commands.md#getrigcontrolinfo) |
| `get_control(node)` | The `RigControl` for a rig control. Raises `RuntimeError` for any other node |
| `find_control(node)` | Like `get_control`, but returns `None` for a node that isn't a rig control |
| `get_channels(controls)` | A dict from each control's name to its list of `ControlChannel`, in one command call. Use it instead of `get_control_channels` in a loop |
| `get_control_channels(control)` | A list of `ControlChannel`, the control's animation interface in channel box order. Raises `RuntimeError` for a node that isn't a rig control, or a custom channel with no mirror mode. Wraps [`getRigControlChannels`](commands.md#getrigcontrolchannels) |

`RigControl` is a frozen dataclass: `node`, `module` (the module node that owns it), `module_type` (`"Layout"`, `"FkChain"` or `"Aim"`), `side`, `usage` and `mirror` (the control on the other side, itself for a Center control, `None` when a side control has none).

`ControlChannel` is a frozen dataclass: `name`, `type` (Maya's attribute type, such as `doubleAngle` or `enum`), `rest` (the attribute default), `min` and `max` (`None` for no limit) and `mirror` (`"copy"` or `"negate"`). Values are in the scene's UI units.

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

## rigmodules.pose

Keying a rig's controls. The pose commands set values and key nothing, so keying is a separate step. Each function keys every channel of its controls (the keyable, writable attributes [`getRigControlChannels`](commands.md#getrigcontrolchannels) lists) with one `setKeyframe` call, at the current time or at `time`, and returns the number of channels keyed.

```python
import rigmodules.pose

rigmodules.pose.key_rig("hero:rigRootModule1")                                  # every control
rigmodules.pose.key_modules("hero:rigRootModule1", ["hero:arm_l", "hero:arm_r"])   # these modules' controls
rigmodules.pose.key_controls(["hero:upperArm_l_ctrl_l"], time=12)
```

- **`key_controls(controls, time=None)`**: the named controls.
- **`key_rig(rig_root, time=None)`**: every control in the rig.
- **`key_modules(rig_root, modules, time=None)`**: every control of the named module nodes. A node that isn't one of the rig's modules raises `RuntimeError`.

## rigmodules.template

```python
import rigmodules.template

rigmodules.template.get_rig_name(r"C:\rigs\Human.json")    # "Human"
```

| Function | Returns |
|---|---|
| `get_rig_name(path)` | The template's `rigName`, which names the variant of every rig built from it. Raises `ValueError` when the file isn't JSON or has no `rigName` |

It reads the file without checking the rest of it. Template validation runs when the rig is built ([templates.md](templates.md)).

## rigmodules.displaylayers

A rig's display layers ([`createRigFromTemplate`](commands.md#createrigfromtemplate)). The build makes the joint, mesh and control layers, and the model is imported after it, so a tool that imports the model puts the meshes in the mesh layer.

```python
import rigmodules.displaylayers

nodes = cmds.file(path, i=True, namespace="hero", returnNewNodes=True)
rigmodules.displaylayers.add_meshes("hero:rigRootModule1", nodes)
layer = rigmodules.displaylayers.get_layer("hero:rigRootModule1", "control")   # "hero:controlsLayer"
```

| Function | Meaning |
|---|---|
| `get_layer(rig_root, kind)` | The rig's `"joint"`, `"mesh"` or `"control"` display layer. Raises `RuntimeError` when the rig has none |
| `add_meshes(rig_root, nodes)` | Puts the mesh objects among `nodes` and under them in the mesh layer, moving them out of any other layer, and returns the mesh transforms added. Skips intermediate shapes. Undoable |

## rigmodules.selection

A rig's selection sets ([`createRigFromTemplate`](commands.md#createrigfromtemplate)) and the skinning selections built on them. Sets are found through their `setMarkup`, and joints through their markup, never through names.

```python
import rigmodules.selection

rigmodules.selection.select_skin_joints("hero:rigRootModule1", module="arm_l")          # the left arm's skin joints
rigmodules.selection.get_skin_joints("hero:rigRootModule1", side="Right", usage="hand")
joints = rigmodules.selection.get_missing_influences("hero:rigRootModule1", "hero:body_geo")   # for a first bind
```

| Function | Returns |
|---|---|
| `find_set(rig_root, kind, side=None, module=None)` | The set of `kind` (`"controls"`, `"joints"` or `"skinJoints"`) for a side (`"Left"`, `"Right"` or `"Center"`) or a module (its node name without the namespace). With neither, the "all" set. `None` when there is none. Raises `RuntimeError` for a node that is not a rig root, or a rig without sets |
| `get_set(...)` | `find_set`, raising `RuntimeError` instead of returning `None` |
| `get_members(set_node)` | Every node in a set, nested sets included, as unique long names |
| `get_skin_joints(rig_root, side=None, module=None, usage=None)` | The skin joints, narrowed by every argument given. A module with no skin joints gives `[]` |
| `select_skin_joints(rig_root, side=None, module=None, usage=None, add=False)` | Selects `get_skin_joints(...)`, replacing the selection or, with `add`, extending it, and returns them |
| `get_missing_influences(rig_root, mesh)` | The rig's skin joints that are not influences of the mesh's skinCluster. All of them when the mesh has none |
| `get_extra_influences(rig_root, mesh)` | The influences of the mesh's skinCluster that are not skin joints of the rig |

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
