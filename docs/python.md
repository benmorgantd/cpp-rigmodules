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
info.asset_id      # "shooter\\main\\characters\\hero"
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

## rigmodules.groups

A rig's groups ([`createRigFromTemplate`](commands.md#createrigfromtemplate)). The build makes `rig_all` and `mesh_grp`, and the model is imported after it, so a tool that imports the model parents the imported transforms to `mesh_grp`.

```python
import rigmodules.groups

nodes = cmds.file(path, i=True, namespace="hero", returnNewNodes=True)
rigmodules.groups.parent_meshes("hero:rigRootModule1", nodes)
group = rigmodules.groups.get_group("hero:rigRootModule1", "all")   # "|hero:rig_all"
```

| Function | Meaning |
|---|---|
| `get_group(rig_root, kind)` | The rig's `"all"` (`rig_all`) or `"mesh"` (`mesh_grp`) group, as a full path, because a model's own group can share the short name `mesh_grp`. Raises `RuntimeError` when the rig has none |
| `parent_meshes(rig_root, nodes)` | Parents the topmost transforms among `nodes` to the mesh group, keeping their world transforms, and returns them (long names). A mesh shape counts as its transform, joints are skipped, and a transform already under the mesh group stays. Undoable |

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
| `find_set(rig_root, kind, side=None, module=None)` | The set of `kind` (`"controls"` or `"joints"`, the skin joints) for a side (`"Left"`, `"Right"` or `"Center"`) or a module (its node name without the namespace). With neither, the top set (`controls_set` or `joints_set`). The set that holds the module sets is not returned. `None` when there is none. Raises `RuntimeError` for a node that is not a rig root, or a rig without sets |
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

## rigmodules.jointclip

Joint animation as a file the plugin reads (D41). A JointClip file is JSON: a source skeleton's joints with their markup and rest, and one JointPose per frame. [`poseRigFromJointPose`](commands.md#poserigfromjointpose) and [`animateRigFromJointClip`](commands.md#animaterigfromjointclip) read it.

```python
import rigmodules.jointclip

# Samples the active rig's export joints over frames 1 to 48 and writes the file. The same frame twice is a JointPose.
joints = rigmodules.jointclip.capture_joint_clip("hero:rigRootModule1", 1, 48, "C:/anims/walk.json")
```

| Function | Does |
|---|---|
| `capture_joint_clip(rig_root, start_frame, end_frame, clip_path)` | Writes a JointClip file from the rig's export joints (the joints in its namespace whose markup has `export` set), one JointPose per frame, and returns the joints captured, parents first. It reads each frame without moving the time slider. Raises `ValueError` when `start_frame` is after `end_frame`, and `RuntimeError` when the rig has no export joints or two would share a path |
| `write_fbx(clip_path, fbx_path)` | Writes a JointClip file as an FBX file ([below](#fbx-files)). A clip with one pose is a one-frame animation, so a pose reaches the engine as an animation. An existing file is replaced, and a failed write leaves it as it was. Raises `ValueError` for a file that isn't a JointClip, a clip with no poses, a joint name Maya can't use, two joints with one name, or a frame rate that differs from the scene's, and `RuntimeError` when the scene already has a node named like a joint |
| `read_fbx(fbx_path, clip_path, bone_map=None)` | Reads an FBX file into a JointClip file, with the markup of a [bone map](#bone-maps) when given. Raises `FileNotFoundError`, and `ValueError` for a bad bone map or a file with no joints |
| `write_skeleton_fbx(rig_root, skeleton_id, fbx_path, bone_map_path)` | The skeleton export: the rig's export joints at rest as a static FBX, and their bone map with the rig's asset id and `skeleton_id` (the id of the shared skeleton asset, which the caller knows). Raises what `write_fbx` raises, and `RuntimeError` when the rig has no export joints |
| `check_skeleton_match(animation_fbx, skeleton_fbx)` | Checks that an animation FBX is valid for a skeleton export: the same joint names, each under the same parent. Rests and bone lengths aren't compared. Reads both files with `read_fbx`. Raises `FileNotFoundError`, and `ValueError` naming every joint that differs ("the animation lacks head", "the skeleton lacks neck", "head is under hips but the skeleton has it under spine") |

A joint's transform is its world matrix relative to its nearest captured ancestor, so it doesn't depend on the rig's joints inheriting transforms. A joint's rest is its build pose. Shear is dropped, and a quaternion keeps the sign nearest the previous frame, so a long turn doesn't flip.

### File format

```json
{
    "formatVersion": 1,
    "frameRate": 30.0,
    "startFrame": 1,
    "joints": [
        { "path": "hips", "markup": "{\"markupVersion\":1,\"usage\":\"hips\",\"side\":\"Center\",\"export\":true,\"skin\":true}", "rest": { "translate": [0, 100, 0], "rotate": [0, 0, 0, 1], "scale": [1, 1, 1] } },
        { "path": "hips/spine", "markup": "", "rest": { "translate": [0, 10, 0], "rotate": [0, 0, 0, 1], "scale": [1, 1, 1] } }
    ],
    "poses": [
        [
            { "translate": [0, 100, 0], "rotate": [0, 0, 0, 1], "scale": [1, 1, 1] },
            { "translate": [0, 10, 0], "rotate": [0, 0, 0.7071068, 0.7071068], "scale": [1, 1, 1] }
        ]
    ]
}
```

- `startFrame` is the frame number of the first pose, and the poses are consecutive frames. `frameRate` is in frames per second.
- A joint's `path` is its hierarchy: the path above it is its parent, which comes earlier in the list. `markup` is the joint's `rigMarkup` JSON as it is, or `""` for a joint that has none. Unmarked joints stay in the file and are never matched to a rig. Two joints can't have the same side and usage.
- Every transform is local to the joint's parent: `translate` in Maya's internal units (centimeters), `rotate` a unit quaternion (x, y, z, w) and `scale`. A pose has one transform per joint, in joint order. `rest` is the source's rest, kept for retargeting.
- The reader checks the whole file before anything uses it (bad JSON, a missing or unknown key, a `formatVersion` this build doesn't read, a bad path or markup, a pose of the wrong length, a rotation that isn't a unit quaternion), so a bad file changes nothing.

### FBX files

FBX is the joint file for mocap and the engine (D42). `write_fbx` and `read_fbx` are adapters over the JointClip file and run through a Maya session with the FBX plugin, which they load. Both build or import a scratch skeleton and delete it, restore the selection, time, playback range and time unit, and leave nothing on the undo queue.

- **Writing** builds plain joints named like the clip's, parented by path, with the clip's local transforms keyed on every frame (euler rotation, the solution nearest the frame before, so a long turn doesn't flip), and exports them baked in the scene's up axis. A clip with one pose gives a one-frame animation. `write_skeleton_fbx` is the only writer with no keys. A joint's markup is written as its `rigMarkup` attribute. The rig's own joints aren't exported, because the exporter ignores `offsetParentMatrix`.
- **Reading** imports the file into a scratch namespace, applies the bone map, and samples every joint on the file's whole frames. The clip's frame rate is the file's (the scene keeps its own unit), its rest is the file's first frame, and a file with no keys gives one pose.
- **Names.** FBX bones bind by name, so joint names are unique in a clip, valid Maya node names, and the scene mustn't already hold a node of the same name when writing. A namespace is dropped in the file.
- **Frame rate.** Writing refuses a clip whose frame rate isn't the scene's. A restore does too (`poseRigFromJointPose` and `animateRigFromJointClip`).
- **Precision.** A round trip matches to about 1e-7.
- **Importer messages.** Maya's importer prints `setAttr: No object matches name: ...rigMarkup` for each marked joint when the file is imported into a namespace, and sets the attribute anyway. `read_fbx` mutes script errors for the import (and restores the setting), because importing without a namespace would rename joints that clash with the scene's.
- **Unreal.** The skeleton and animation pairing and the import and export paths are in `design/control-library.md`, section 6b. The import into Unreal itself has not been tested.

```python
import rigmodules.jointclip

rigmodules.jointclip.capture_joint_clip("hero:rigRootModule1", 1, 48, "C:/tmp/walk.json")
rigmodules.jointclip.write_fbx("C:/tmp/walk.json", "C:/export/walk/walk.fbx")
rigmodules.jointclip.read_fbx("C:/export/walk/walk.fbx", "C:/tmp/walk.json")
cmds.animateRigFromJointClip("hero:rigRootModule1", jointClip="C:/tmp/walk.json")
```

### Bone maps

A foreign FBX (a mocap take, a vendor skeleton) carries no markup, so a bone map file gives it some: a joint's leaf name (without namespace) to its `rigMarkup` object. `write_skeleton_fbx` writes one beside the skeleton FBX, `read_fbx` applies one, and a vendor skeleton's is written once and reused for every take.

```json
{
    "formatVersion": 1,
    "assetId": "shooter\\characters\\hero",
    "skeletonId": "shooter\\skeletons\\human",
    "joints": {
        "LeftArm": { "markupVersion": 1, "side": "Left", "usage": "upperArm", "export": true, "skin": true },
        "Hips": { "markupVersion": 1, "side": "Center", "usage": "pelvis", "export": true, "skin": true }
    }
}
```

| Function (`rigmodules.bonemap`) | Does |
|---|---|
| `write_bone_map(path, asset_id, skeleton_id, markups)` | Writes a bone map. `markups` is `{leaf name: rigMarkup JSON}`, and a joint with `""` is left out. A failed write keeps the old file. Raises `ValueError` for a bad id or markup |
| `read_bone_map(path)` | Reads and checks a bone map. Raises `FileNotFoundError`, and `ValueError` listing every problem: a `formatVersion` this build doesn't read, a bad id, a missing or mistyped markup key, an empty usage, two joints with the same side and usage |
| `apply_bone_map(bone_map, joints)` | Writes the map's markup on the joints it names, replacing markup they have, and returns the names no joint has |

A joint the map leaves out keeps the markup the file carries, or none: it stays in the clip for the world matrices of the joints below it and is never matched to a rig.

