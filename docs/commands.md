# Commands

Every command:

- Takes the nodes it acts on **by name**. None of them reads the selection, except `setRigMeshId` and `pruneToMaxInfluences` when no mesh is named.
- Is undoable, apart from the queries `getRigControls`, `getRigControlInfo`, `getRigControlChannels`, `getRigMirrorControl`, `getRigKeys` and `getRigInfo`, and `saveRigControlShapes` and `saveRigSkinWeights` (they write a file and change nothing in the scene).
- Reports errors through `displayError`. From Python, the error text arrives as a `RuntimeError`.

Examples use Python `maya.cmds` (`from maya import cmds`). Flags are keyword arguments named by their long flag name. A flag that can be given several times takes a list.

## rigJointMarkup

Writes, edits, queries or removes the `rigMarkup` attribute on joints. Markup says what a joint is, so templates can select it.

```python
# Create
cmds.rigJointMarkup("skel:upperArm_jnt", usage="upperArm", side="Left", export=True, skin=True)

# Edit one value on several joints
cmds.rigJointMarkup("skel:upperArm_jnt", "skel:upperArm_jnt1", edit=True, usage="shoulder")

# Query one value: a list with one entry per joint, "" for unmarked joints
cmds.rigJointMarkup("skel:upperArm_jnt", query=True, usage=True)   # ['upperArm']

# Query everything: the full markup JSON per joint
cmds.rigJointMarkup("skel:upperArm_jnt", query=True)

# Remove
cmds.rigJointMarkup("skel:upperArm_jnt", remove=True)
```

| Flag | Type | Meaning |
|---|---|---|
| `usage` | string | What the joint is, for example `upperArm`. Matched by template selectors. |
| `side` | string | `Center`, `Left` or `Right` |
| `export` | bool | Whether the joint is exported. Stored, not used by the build yet, apart from a warning when an `export` joint is left unclaimed. |
| `skin` | bool | Whether the joint is a skin joint. Stored, not used yet. |
| `edit` | bool | Change the given values on joints that already have markup |
| `query` | bool | Return markup. With one value flag, returns that value per joint (`""` for unmarked joints). With none, returns the full JSON per joint. |
| `remove` | bool | Delete the markup. Cannot be combined with `edit` or value flags. |

- **Create** needs all four value flags and fails on a joint that already has markup.
- **Edit** needs at least one value flag and fails on a joint with no markup.
- `(side, usage)` should be unique on a skeleton. The build fails if two joints passed to it share a pair.

The attribute holds JSON: `{"markupVersion": 1, "usage": "upperArm", "side": "Left", "export": true, "skin": true}`. The key names are public, so don't rename them.

## createRigFromTemplate

Builds a rig from a template and the joints passed in.

```python
joints = ["skel:spine1_jnt", "skel:spine2_jnt", "skel:arm_jnt"]
cmds.createRigFromTemplate(*joints, template="C:/rigs/spine_arm.json", asset="shooter|characters|hero")
```

| Flag | Type | Meaning |
|---|---|---|
| `template` | string | Path to the template. Required. |
| `asset` | string | The asset the rig belongs to: its folder path below the projects root, `\|` between folders (`shooter\|characters\|hero`), any depth. Each folder name is letters, digits and `_`. Compared without case. Required. Templates name no asset, so one template builds many assets. |
| `namespace` | string | The rig's namespace. Defaults to the asset's last folder name (`hero`). `":"` is the root, nested paths such as `char:rig` work, and an existing namespace is reused. |
| `checkMirrorPositions` | bool | Default `False`. With `True`, a mirrored module ([templates.md](templates.md#mirroring)) fails the build when one of its joints is more than 0.001 from its source joint reflected across X = 0. |

Objects: one or more joint names, with their namespace (`skel:arm_jnt`).

1. **Read and check.** Loads the template, reads markup from the given joints, and validates everything (see [templates.md](templates.md#validation)). Every joint passed in must have markup. On any error nothing is built, and every error is reported at once.
2. **Build.** Creates the rig root, the `layout` module and every template module, connects child plugs to sockets, makes the display layers and selection sets (below) and builds the template's constraints.

Every node created goes in the rig namespace. The input joints are moved there too, with their names unchanged. A joint already in that namespace stays where it is. Joint and module names must be free in the rig namespace, so `skel:a` and `other:a` cannot both be passed.

The asset owns the rig namespace. The namespace holds one `assetRootModule`, which stores `asset` in its `assetId` attribute. Every rig built into the namespace connects to it, one per rig variant (`rigName`). The build reuses the namespace's asset root if there is one, and creates it otherwise. The build fails, building nothing, when the namespace holds more than one asset root, when the asset root's `assetId` differs from `asset` (ignoring case), or when a rig with the template's `rigName` is already connected to it.

The build makes three display layers in the rig namespace: `jointsLayer` (the joints passed in), `meshesLayer` (empty) and `controlsLayer` (every control). A layer of the same name already in the namespace is deleted first, and a node that was in another layer moves into the rig's layer. Each layer connects to its own message attribute on the rig root, so tools find them without reading names:

| Attribute | Layer |
|---|---|
| `jointDisplayLayer` | `jointsLayer` |
| `meshDisplayLayer` | `meshesLayer` |
| `controlDisplayLayer` | `controlsLayer` |

Meshes are not inputs of the build: the model is imported after it, so `meshesLayer` starts empty and the tool that imports the model fills it with the imported meshes ([`rigmodules.displaylayers.add_meshes`](python.md#rigmodulesdisplaylayers)). A second rig variant built into the same namespace replaces the first variant's layers.

The build also makes selection sets in the rig namespace, for selecting and for skinning. There are two hierarchies, one for the controls and one for the skin joints (the joints marked `skin`):

```
controls_set                    joints_set
  controls_left_set               joints_left_set
  controls_right_set              joints_right_set
  controls_center_set             joints_center_set
  controls_modules_set            joints_modules_set
    <module>_controls_set           <module>_joints_set
```

Only the side sets and the module sets hold items. A control or joint is in the set for its side and the set for its module, and selecting `controls_set` or `joints_set` selects it once. `<module>` is the module node's name without the namespace. A module with no skin joints has no joints set, and a skin joint no module drives is in its side set only. The side sets exist even when empty. Every set connects to the rig root's `selectionSets` message array and carries a JSON `setMarkup` attribute (`{"markupVersion": 1, "kind": "joints", "role": "module", "side": null, "module": "arm_l"}`): `kind` is `controls` or `joints`, `role` is `all`, `side`, `modules` or `module`, and `side` and `module` are null unless the role names one. Tools find a set through it, not through names: [`rigmodules.selection`](python.md#rigmodulesselection). A set of the same name already in the namespace is replaced.

The rig root records what built it, so tools can find out-of-date rigs in a scene:

| Attribute | Meaning |
|---|---|
| `rigName` | The rig variant, from the template |
| `rigVersion` | The template's `rigVersion` |
| `pluginVersion` | The RigModules plugin version that ran the build |
| `publishVersion` | The id of the asset publish the rig came from, for example `20261108T142012Z_bmorgan` (UTC time and user). A build leaves it empty, and the publish step sets it |

Undo removes the rig and moves the joints back, but leaves the (empty) rig namespace and an asset root that existed before the build.

## createRigConstraint

Adds a space switch to a rig control after the build. Templates can declare the same thing in `constraints` ([templates.md](templates.md#constraints)).

```python
cmds.createRigConstraint("hero:arm_ctrl_l", driver=["layout.end", "spine.end"], label=["World", "Chest"], initial=1)
# Returns the constraint node: 'hero:arm_ctrl_l_space'
```

| Flag | Type | Meaning |
|---|---|---|
| `driver` | string, multi-use | A socket id, `module.socket`. At least one. |
| `label` | string, multi-use | The driver's enum label. Give none, or one per `driver` in the same order. Defaults to the socket id. |
| `initial` | int | Index of the driver that starts active. Defaults to 0. |
| `name` | string | The constraint node's name. Defaults to `<control>_space`. |
| `type` | string | `Parent` (the default), `Orient` or `Point`. Orient takes only the driver's rotation and Point only its translation. The rest comes from the child plug's parent ([templates.md](templates.md#constraint-types)). |

Object: the control, which must be the control of a module's child plug.

```python
# The arm keeps the world's rotation, and its shoulder pivot follows the chest.
cmds.createRigConstraint("hero:arm_ctrl_l", driver=["layout.end", "spine.end"], label=["World", "Chest"], type="Orient")
```

- **Driver modules** without a namespace are looked up in the control's namespace, whatever the current namespace is. A driver that names a namespace (`other:layout.end`) is used as given.
- **The constraint node** goes in the control's namespace unless `name` includes a namespace.
- A module cannot drive its own child plug, and a control can have only one space constraint.
- An Orient or Point constraint fails on a child plug with no parent connected. A built rig always has one, `layout.end` by default.
- The node is a `rigConstraint`, and its `constraintType` attribute holds the type.
- With more than one driver, the control gets a `space` enum attribute listing the labels.
- The control does not move. On a posed rig its translate, rotate and scale are changed to keep it in place, the same match `setRigConstraintSpace` does. At rest nothing changes.

## setRigConstraintSpace

Switches a constraint to another driver without moving the control at that frame.

```python
cmds.setRigConstraintSpace("hero:arm_ctrl_l_space", label="World")
cmds.setRigConstraintSpace("hero:arm_ctrl_l_space", space=0)
```

| Flag | Type | Meaning |
|---|---|---|
| `space` | int | Driver index to switch to |
| `label` | string | Driver label to switch to |

Give exactly one of `space` or `label`. Object: the constraint node.

The command changes the control's translate, rotate and scale so it keeps its world position and orientation in the new space, then sets the control's `space` enum. Nothing is stored on the constraint, so every space gives the rest pose when the controls are zeroed, and a pose is just the control values plus the enum.

- **Rotation** keeps the control's rotate order and picks the euler values closest to the current ones, so animation curves don't flip.
- **Locked channels**, and channels driven by anything other than keys, keep their values. The command warns and names them, and the control moves by what they couldn't absorb.
- **No keys.** On an animated control, key the changed channels and the enum yourself, or the values revert when the frame changes.
- **Setting the enum directly** (or through keys) does no matching, so the control moves unless the rig is at rest.

Undo restores both the enum and the control's values.

## getRigControls

Returns a rig's controls, found through the rig's connections. Filter by module type and control markup (see [controls.md](controls.md#control-markup)).

```python
cmds.getRigControls("hero:rigRootModule1")                                # every control
cmds.getRigControls("hero:rigRootModule1", moduleType="FkChain", side="Left")
cmds.getRigControls("hero:rigRootModule1", usage=["spine1", "spine2"])
```

| Flag | Type | Meaning |
|---|---|---|
| `moduleType` | string, multi-use | A template `moduleType`: `Layout`, `FkChain` or `Aim` |
| `side` | string, multi-use | `Center`, `Left` or `Right` |
| `usage` | string, multi-use | A control markup usage |

Object: the rig root node (type `rigRootModule`).

- Values of one flag are alternatives. Every flag given must match.
- Returns control names, grouped by module in module-name order, then in each module's control order. No match returns an empty list.
- An unknown `side` or `moduleType` is an error, not an empty result. So is a control without valid markup.
- Not undoable: it changes nothing.

## saveRigControlShapes

Writes every control's shape to a shape file (format in [controls.md](controls.md#shape-file)).

```python
cmds.saveRigControlShapes("hero:rigRootModule1", file="C:/assets/hero/shapes.json")
```

| Flag | Type | Meaning |
|---|---|---|
| `file` | string | The file to write. Required. Overwritten if it exists. |

Object: the rig root node. Returns the names of the controls saved. Not undoable: it writes a file and changes nothing in the scene.

## loadRigControlShapes

Sets control shapes from a shape file, matching controls by markup.

```python
cmds.loadRigControlShapes("hero:rigRootModule1", file="C:/assets/hero/shapes.json")
```

| Flag | Type | Meaning |
|---|---|---|
| `file` | string | The file to read. Required. |

Object: the rig root node. Returns the names of the controls changed.

- The whole file is validated first. Any error is reported and nothing changes.
- A control with no entry in the file keeps its shape, and an entry with no matching control is skipped. Both print a warning.
- Sets `shapeType`, `shapeTransform` and `filled`. Color is left alone.

## getRigMirrorControl

Returns the control on the other side of the same rig: the control with the same `usage` and the swapped `side` in its markup ([controls.md](controls.md#control-markup)).

```python
cmds.getRigMirrorControl("hero:upperArm_l_ctrl_l")
# Returns 'hero:upperArm_r_ctrl_r'
```

Object: one control.

- A `Center` control returns itself.
- A control with no mirror is an error that names the side and usage it looked for.
- Not undoable: it changes nothing.

## getRigInfo

Returns what a rig node belongs to, as JSON text: its rig root and asset root, the asset id, the variant, the namespace and the rig root's version stamps. Python tools use it through `rigmodules.query` ([python.md](python.md)).

```python
cmds.getRigInfo("hero:arm_ctrl_l")
# Returns '{"assetId":"shooter|main|characters|hero","assetRoot":"hero:assetRootModule1","namespace":"hero",
#           "pluginVersion":"0.1","publishVersion":"","rigName":"game","rigRoot":"hero:rigRootModule1","rigVersion":1}'
```

Object: one rig root, rig module, rig control, or joint driven by a rig. A joint belongs to the rig whose module drives its `offsetParentMatrix`.

| Key | Value |
|---|---|
| `rigRoot` | The rig root node |
| `assetRoot` | The asset root node that owns the rig's namespace |
| `assetId` | The asset root's `assetId` |
| `rigName` | The rig root's `rigName`: the variant |
| `namespace` | The namespace the rig root is in, without a leading `:` |
| `rigVersion` | The template's `rigVersion` |
| `pluginVersion` | The RigModules version that built the rig |
| `publishVersion` | The publish the rig came from, `""` until published |

- Any other node (an unrigged joint, an asset root, a mesh) returns `""`, with no error, so a selection callback can ask about anything.
- A missing node is an error. So is a broken rig, such as a rig root with no asset root.
- Not undoable: it changes nothing.

## mirrorRigControlShapes

Mirrors control shapes from one side of a rig to the other.

```python
cmds.mirrorRigControlShapes("hero:rigRootModule1")                    # Left onto Right
cmds.mirrorRigControlShapes("hero:rigRootModule1", leftToRight=False) # Right onto Left
```

| Flag | Type | Meaning |
|---|---|---|
| `leftToRight` | bool | Default `True`: Left controls are the source. `False` mirrors Right onto Left. |

Object: the rig root node. Returns the names of the controls changed.

- **World space.** Each mirror control's shape becomes its source control's shape reflected across world X = 0. The reflection uses both controls' rest matrices, so posing the rig first doesn't change the result.
- **Copies** `shapeType`, `shapeTransform` (as the reflection) and `filled`. Color is left alone, because it follows the module's side.
- **Skips** `Center` controls, and warns about a source control with no mirror.
- Undoable.

## getRigControlInfo

Reports what each control is: its module, its markup and its mirror.

```python
import json
data = json.loads(cmds.getRigControlInfo("hero:upperArm_l_ctrl_l", "hero:chest_ctrl_c"))
data["hero:upperArm_l_ctrl_l"]
# {"moduleType": "FkChain", "module": "hero:arm_l", "side": "Left", "usage": "upperArm", "mirror": "hero:upperArm_r_ctrl_r"}
```

Objects: one or more rig controls. Returns a JSON object keyed by control name. From Python, [`rigmodules.query.get_control`](python.md#rigmodulesquery) and `get_rig_controls` wrap it.

- **`mirror`** is the control with the same usage on the other side ([`getRigMirrorControl`](#getrigmirrorcontrol)). A `Center` control is its own mirror, and a side control with no mirror gives `null` where `getRigMirrorControl` raises an error.
- Not undoable: it changes nothing.

## getRigControlChannels

Reports a control's animation interface: the channels an animator keys and a pose tool writes.

```python
import json
data = json.loads(cmds.getRigControlChannels("hero:upperArm_l_ctrl_l", "hero:chest_ctrl_c"))
data["hero:chest_ctrl_c"][0]
# {"name": "translateX", "type": "doubleLinear", "rest": 0.0, "min": None, "max": None, "mirror": "copy"}
```

Objects: one or more rig controls. Returns a JSON object keyed by control name, each with its channels in channel box order. From Python, [`rigmodules.query.get_control_channels`](python.md#rigmodulesquery) wraps it.

- **A channel** is a keyable attribute that isn't locked and isn't driven by anything but an animation curve.
- **`rest`** is the attribute's default, which the module set when it built the control. `min` and `max` are the attribute's range, or `null`. Angles and distances are in the scene's UI units, like `cmds.getAttr`.
- **`mirror`** is `copy` or `negate`: how the value goes to the other side in [`mirrorRigPose`](#mirrorrigpose). Transforms copy. A custom channel declares its mode as an attribute category, `mirrorCopy` or `mirrorNegate`. One with neither (or an enum or bool that negates) is an error, and so it is for every pose command.
- Channel names are public API: saved poses and animation use them.
- Not undoable: it changes nothing.

## zeroRigControls

Puts a rig's controls back to their build pose.

```python
cmds.zeroRigControls("hero:rigRootModule1")                           # every control
cmds.zeroRigControls("hero:upperArm_l_ctrl_l", "hero:chest_ctrl_c")   # these two
# Returns the controls that moved
```

Objects: the rig root node (every control in the rig), or one or more controls of one rig (those only). A rig root can't be combined with controls, and controls of two rigs are an error. Every channel of each control (see [`getRigControlChannels`](#getrigcontrolchannels)) goes to its `rest`. Channels that are locked or driven keep their values. Sets values and keys nothing. Undoable.

## mirrorRigPose

Copies the pose from one side of a rig to the other.

```python
cmds.mirrorRigPose("hero:rigRootModule1")                    # Left onto Right
cmds.mirrorRigPose("hero:rigRootModule1", leftToRight=False) # Right onto Left
cmds.mirrorRigPose("hero:upperArm_l_ctrl_l", "hero:lowerArm_l_ctrl_l")   # these controls onto their mirrors
```

| Flag | Type | Meaning |
|---|---|---|
| `leftToRight` | bool | `True`: Left controls are the source. `False`: Right controls are. Without it, see below. |

Objects: the rig root node, or one or more controls of one rig. Returns the controls changed.

- **The source side.** With the rig root and no flag, Left. With controls and no flag, the side of the controls when they all share one, and Left otherwise. Center controls don't count. An explicit `leftToRight` always wins. Controls on the other side are not sources: one named only as a mirror is overwritten, and one with no source named keeps its values.

- **Each source channel's value** goes onto the same channel of the control with the same usage on the other side, negated where the channel's `mirror` is `negate`. Transform channels copy, so a rig whose joints are mirrored by behavior gets a behavior mirror.
- **Center controls and the source side** keep their values. A channel the mirror control doesn't have, or can't write, is skipped with a warning.
- Sets values and keys nothing. Undoable.

## flipRigPose

Swaps the pose between the two sides, and reflects the Center controls.

```python
cmds.flipRigPose("hero:rigRootModule1")
cmds.flipRigPose("hero:upperArm_l_ctrl_l", "hero:chest_ctrl_c")   # this pair, and this Center control
```

Objects: the rig root node, or one or more controls of one rig. Returns the controls changed.

- **Left and Right** controls exchange values, by the same rules as `mirrorRigPose`. With controls, each one named swaps with its mirror, and a pair named from both ends swaps once.
- **Center controls** are reflected across the world YZ plane through their own channels. This needs one of the control's rest axes to lie along world X: that axis' translate is negated, and so are the rotates of the other two axes. A control with no such axis keeps its values, with a warning. A custom channel negates only if it says `negate`.
- Flipping twice restores the pose. Sets values and keys nothing. Undoable.

## saveRigPose

Writes the channel values of controls to a pose file.

```python
cmds.saveRigPose("hero:rigRootModule1", file="C:/poses/idle.json")
cmds.saveRigPose("hero:upperArm_l_ctrl_l", "hero:lowerArm_l_ctrl_l", file="C:/poses/arm.json")
# Returns the controls saved
```

Objects: the rig root node (every control), or one or more controls of one rig. Flag: `file` (required, the path).

- The file is keyed by control markup (`side`, `usage`) and channel name, never by node name, so a pose survives renames and rebuilds. Values are in the scene's UI units. An enum stores its index and a bool stores 0 or 1.
- The file carries a `formatVersion` (1). Its shape: `{"formatVersion": 1, "controls": {"Left": {"upperArm": {"translateX": 0.0, "rotateY": 12.5}}}}`.
- Changes no scene data and isn't undoable.

## loadRigPose

Applies a pose file to controls.

```python
cmds.loadRigPose("hero:rigRootModule1", file="C:/poses/idle.json")
cmds.loadRigPose("hero:upperArm_l_ctrl_l", file="C:/poses/idle.json")   # only this control
# Returns the controls that changed
```

Objects: the rig root node, or one or more controls of one rig. Flag: `file` (required).

- Controls match the file by markup. A control the file lacks keeps its values, and so does a channel it lacks. With the rig root, a control or channel the file has and the rig lacks is skipped with a warning. With controls, the rest of the file is ignored without one.
- The whole file is checked before anything is applied (bad JSON, an unknown side, a value that isn't a number, a `formatVersion` this build doesn't read), so a bad file changes nothing. An older `formatVersion` is updated on load.
- Sets values and keys nothing: use [`rigmodules.pose`](python.md#rigmodulespose) to key. Undoable.

## getRigKeys

Reads the keys of a rig's controls.

```python
import json

keys = json.loads(cmds.getRigKeys("hero:rigRootModule1"))
# {"Left": {"upperArm": {"rotateZ": [[1.0, 0.0], [10.0, 90.0]]}}}
cmds.getRigKeys("hero:upperArm_l_ctrl_l")   # only this control
```

Objects: the rig root node (every control), or one or more controls of one rig. Returns one JSON string.

- The string is `{side: {usage: {channel: keys}}}`, keyed by control markup like a pose, so it survives renames and rebuilds. Frames are in the scene's UI time unit and values in its UI units, as `cmds.keyframe` reports them. The [keys format](#keys-format) is below.
- Only channels with an animation curve are listed. A control with none is left out.
- Changes no scene data and isn't undoable.

## setRigKeys

Adds keys to a rig's controls, every key of the call at once.

```python
import json

keys = {"Left": {"upperArm": {"rotateZ": [[1, 0], [10, 90], [20, 45]]}}}
cmds.setRigKeys("hero:rigRootModule1", keys=json.dumps(keys))
# Returns the controls keyed
```

The string is the one `getRigKeys` returns ([format](#keys-format)):

```json
{ "Left": { "upperArm": { "rotateZ": [[1, 0], [10, 90], [20, 45]], "translateX": [[1, 0], [20, 2.5]] } } }
```

Objects: the rig root node, or one or more controls of one rig. Flag: `keys` (required, the JSON string).

- Controls and channels match by markup and channel name. A control or channel the keys have and the rig lacks is skipped with a warning. With controls, the keys for the rest of the rig are ignored without one.
- Each channel gets all its keys in one `MFnAnimCurve::addKeys` call, and a channel with no curve gets one. A channel's keys replace the curve's keys from its first frame to its last, and keys outside that range stay. Keys may come in any order, but a frame can't repeat within a channel.
- A key with two numbers has two `auto` tangents. A key that names its tangents gets them, and a curve object sets the curve's `weighted` flag and infinities. A curve the data gives no flags goes back to unweighted and constant, so `animateRigFromJointClip` and the plain two-number form leave the curve with default flags.
- Enum, bool and integer channels (`space`) key stepped whatever the tangents say.
- The whole string is checked before anything is written (bad JSON, a side outside `Center`, `Left` and `Right`, an empty usage or channel name, a channel that is neither a key array nor a curve object, a key that isn't `[frame, value]` or `[frame, value, in, out]`, a tangent or infinity that doesn't exist, a repeated frame), so a bad string changes nothing.
- Undoable as one step, however many channels and keys it wrote.

### Keys format

```json
{
    "Left": { "upperArm": {
        "rotateX": [[1, 0.0], [10, 12.5]],
        "rotateZ": {
            "weighted": false, "preInfinity": "constant", "postInfinity": "constant",
            "keys": [[1, 0.0, "auto", "auto"], [10, 90.0, [-12.5, 1.0], "flat"], [20, 45.0, "auto", "step"]]
        }
    } }
}
```

- A channel is an array of keys, or a curve object with `keys` and optionally `weighted` (default `false`), `preInfinity` and `postInfinity` (`constant`, `linear`, `cycle`, `cycleRelative` or `oscillate`, default `constant`). `getRigKeys` returns the array form for a curve with default flags and the object form otherwise.
- A key is `[frame, value]` or `[frame, value, in, out]`. A tangent is a type name (`auto`, `clamped`, `linear`, `flat`, `step` or `spline`) or `[angleDegrees, weight]` for a fixed tangent. `getRigKeys` writes two numbers for a key whose tangents are both `auto`.
- Maya has no step in-tangent: a key written with `"step"` as its in tangent reads back `auto`. A tangent type with no name here (plateau, step next, the auto variants) is read as the fixed tangent it currently is.
- A key's tangents are not locked or unlocked by the data: a key with two different tangents is unlocked.

## saveRigAnimation

Writes the keys of a rig's controls as a ControlAnim file, the animator's own library format ([library.md](library.md)).

```python
cmds.saveRigAnimation("hero:rigRootModule1", file="C:/anims/walk.anim.json", startFrame=1, endFrame=48)
cmds.saveRigAnimation("hero:upperArm_l_ctrl_l", file="C:/anims/wave.anim.json")   # only this control, over the playback range
# Returns the controls it read
```

Objects: the rig root node, or one or more controls of one rig. Flags:

| Flag | Type | Meaning |
|---|---|---|
| `file` | string | Where to write the file. Required. An existing file is replaced. |
| `startFrame` | float | First frame to save. Default: the playback range's start. |
| `endFrame` | float | Last frame to save. Default: the playback range's end. |

- Only keys from `startFrame` to `endFrame` go in. A channel with no key in the range is left out, and so is a control with none.
- The file holds the keys with their tangents and each curve's flags ([keys format](#keys-format)), the range, and the scene's frame rate. Its format is in [library.md](library.md#controlanim-files).
- A pose that has to be an animation (an engine wants two frames) is the rig keyed on two frames with the same values: `rigmodules.pose.key_rig(root, time=1)` and again at `time=2`, then save frames 1 to 2.
- Changes no scene data and isn't undoable. It fails when `endFrame` is before `startFrame`.

## loadRigAnimation

Keys a rig's controls from a ControlAnim file.

```python
cmds.loadRigAnimation("hero:rigRootModule1", file="C:/anims/walk.anim.json")
cmds.loadRigAnimation("hero:rigRootModule1", file="C:/anims/walk.anim.json", offset=100, mirror=True)
# Returns the controls keyed
```

Objects: the rig root node, or one or more controls of one rig. Flags:

| Flag | Type | Meaning |
|---|---|---|
| `file` | string | The ControlAnim file. Required. |
| `offset` | float | Moves every key by this many frames. Default 0. |
| `mirror` | bool | `True` puts each side's keys on the other side (a flip, below). Default `False`. |

- Matching, replacing and undo are those of [`setRigKeys`](#setrigkeys): a control the file lacks keeps its keys, a control or channel only one side has is skipped with a warning, and the whole load is one undo step.
- The file is checked whole before anything is written. A file that fails validation, one with a newer `formatVersion` than this build reads, and one whose frame rate isn't the scene's change nothing. A resample is not built.
- **Mirror** is a flip. Left keys go onto the Right control of the same usage and the Right keys onto Left. A channel the mirror negates (a custom channel declared `mirrorNegate`, [D34](../design/decisions.md)) has its values negated and its fixed tangent angles with it, and the other tangent types recompute from the negated keys and come out mirrored. A Center control reflects through its flip axis, as [`flipRigPose`](#fliprigpose) does. Frames, weights, curve flags and tangent types don't change. With controls as the objects, only those controls are keyed: the keys that mirror onto them.

## poseRigFromJointPose

Sets a rig's control values from a joint pose, by solving each module's controls from the joints ([how it solves](#how-the-joints-become-control-values)). The design is in [`design/inverse-solve.md`](../design/inverse-solve.md).

```python
cmds.poseRigFromJointPose("hero:rigRootModule1", jointPose="C:/tmp/idle.jointclip.json", sidecar="C:/poses/idle.rigdata.json", mode="match")
cmds.poseRigFromJointPose("hero:upperArm_l_ctrl_l", jointPose="C:/tmp/idle.jointclip.json")   # only this control
# Returns the controls that changed
```

Objects: the rig root node, or one or more controls of one rig. Flags:

| Flag | Type | Meaning |
|---|---|---|
| `jointPose` | string | A JointClip file with exactly one JointPose, as written by [`rigmodules.jointclip.capture_joint_clip`](python.md#rigmodulesjointclip). Required. |
| `sidecar` | string | A pose file with the control values the joints can't give. Optional. |
| `mode` | string | `match`, the only mode. Optional. |

- Sets values and keys nothing, like `loadRigPose`. Undoable.
- Everything is checked before anything is written, so a bad file changes nothing.
- The whole rig is solved, whatever the objects: with controls, only those controls are set.

### How the joints become control values

- A source joint matches a rig joint by markup (`side`, `usage`). A source joint the rig lacks is ignored. A rig joint the source lacks is a warning: its control comes from the sidecar, and stays at rest without one.
- The rig's modules are solved parents first, so every module's child plugs see where its parents' sockets are. The [Layout](modules.md) has no joints, so its controls come only from the sidecar (at rest without one) and everything below it hangs from them. A rig whose layout was moved but whose sidecar doesn't say so still matches the joints: the controls below the layout take up the difference.
- An [FkChain](modules.md) gets every control's translate, rotate and scale from its joints. An Aim gets its target and up controls from their joints, and its aim joint follows from them. A source aim joint that doesn't sit where its target and up aim it is a warning.
- A control with a [space switch](commands.md#createrigconstraint) is solved in the space the sidecar's `space` channel names. Without one, it is solved in the constraint's current space. Its values are relative to that space, so they differ from space to space while the joints match.
- Per channel, the value comes from the joints, else the sidecar, else rest. A control's transform channels always get a value. A custom channel, `space` included, is set only when the sidecar has it: nothing derives it, so a missing one keeps its value.
- A rotation is the euler nearest the control's current value (nearest the previous frame for a clip), in the control's own rotate order. Shear the channels can't hold, and locked or driven channels, are warnings.
- The source's rest is compared with the rig's. If a joint is more than 0.01 cm out, the source has other proportions: the command warns that the result won't reproduce the source's pose, and a retarget is needed.

## animateRigFromJointClip

Keys a rig's controls from an animated joint clip, by the solve of [`poseRigFromJointPose`](#how-the-joints-become-control-values) on every frame.

```python
cmds.animateRigFromJointClip("hero:rigRootModule1", jointClip="C:/tmp/walk.jointclip.json", sidecar="C:/anims/walk.rigdata.json", startFrame=1, endFrame=60)
# Returns the controls keyed
```

Objects: the rig root node, or one or more controls of one rig. Flags:

| Flag | Type | Meaning |
|---|---|---|
| `jointClip` | string | A JointClip file. Required. |
| `sidecar` | string | An animation sidecar file ([format](#animation-sidecar-format)). Optional. |
| `mode` | string | `match`, the only mode. Optional. |
| `startFrame`, `endFrame` | int | The part of the clip to write, the whole clip by default. Both must be inside the clip's frames, and `startFrame` can't be after `endFrame`. |

- Every channel it writes gets a key on every frame of the range, with the global default tangents (`space` and other enums step). Nothing is simplified: that is a separate tool on the animation. The whole call is written through [`setRigKeys`](#setrigkeys)'s path, so it is one undo.
- The keys land on the clip's own frame numbers (`startFrame` in the file). Keys the animator set outside the range stay, and the keys inside it are replaced.
- The clip must run at the scene's frame rate, and the sidecar at the clip's, or the command is an error. A resample is not built.
- Controls are solved as for `poseRigFromJointPose`, so a Layout control is keyed at rest on every frame unless the sidecar says otherwise.
- A frame the sidecar has no entry for is a warning, and that frame has no values for the channels only the sidecar gives.
- Everything is checked before anything is written, so a bad file changes nothing. Undoable.

### Animation sidecar format

The control values a clip's joints can't give, one entry for every frame: the [pose file](#saverigpose)'s `controls` map, per frame.

```json
{
    "formatVersion": 1,
    "frameRate": 24.0,
    "frames": {
        "1": { "Center": { "layout_0": { "translateX": 0.0 } }, "Left": { "upperArm": { "space": 0 } } },
        "2": { "Center": { "layout_0": { "translateX": 0.5 } }, "Left": { "upperArm": { "space": 0 } } },
        "3": { "Center": { "layout_0": { "translateX": 1.0 } }, "Left": { "upperArm": { "space": 0 } } }
    }
}
```

- `frames` maps a frame number to what the pose file's `controls` holds: markup, then channel values in UI units, an enum by index. Every channel is baked on every frame, so the file has no sparse keys and no tangent data.
- The reader checks the whole file (bad JSON, a missing or unknown key, a `formatVersion` this build doesn't read, a `frameRate` that isn't above 0, a frame that isn't a whole number, anything a pose file's `controls` rejects).

## setRigMeshId

Gives meshes a `meshId`, the GUID a skin weight file is matched to ([skinning.md](skinning.md)).

```python
cmds.setRigMeshId("hero:body_geo")
# Returns ['C214154A-43E0-A9E2-3CAA-22B52F9603E1']
cmds.setRigMeshId()                                   # every selected mesh
cmds.setRigMeshId("hero:body_geo_copy", newId=True)   # replace a duplicate's copied id
```

| Flag | Type | Meaning |
|---|---|---|
| `newId` | bool | Default `False`: a mesh that already has a `meshId` keeps it. `True` gives every mesh a new one. |

Objects: meshes, by transform or shape name. With none named, every selected mesh, in selection order. Selected nodes that aren't meshes are skipped.

- Returns each mesh's `meshId`, in mesh order.
- A named node that isn't a mesh is an error. So is an empty selection, or one with no meshes.
- Undoable. Undo removes the attribute from a mesh that didn't have one.

## saveRigSkinWeights

Writes a mesh's skin weights to a weight file (format in [skinning.md](skinning.md#file-format)).

```python
cmds.saveRigSkinWeights("hero:body_geo", file="C:/assets/hero/weights/body_geo.json")
# Returns 42, the number of influences saved
```

| Flag | Type | Meaning |
|---|---|---|
| `file` | string | The file to write. Required. Overwritten if it exists. |

Object: one mesh, by transform or shape name.

- Errors, writing nothing: the mesh has no `meshId`, or shares it with another mesh in its namespace; it has no skinCluster; an influence has no joint markup, is marked `skin: false`, or shares its markup with another influence; a vertex's weights don't sum to 1 (the error lists the vertices).
- Saves the weights exactly, with no rounding, pruning or normalizing ([skinning.md](skinning.md#weights-are-never-changed)).
- Not undoable: it writes a file and changes nothing in the scene.

## loadRigSkinWeights

Sets a mesh's skin weights from a weight file, matching influences by joint markup ([skinning.md](skinning.md#loading)).

```python
cmds.loadRigSkinWeights("hero:body_geo", file="C:/assets/hero/weights/body_geo.json")
# Returns 'hero:skinCluster1'
```

| Flag | Type | Meaning |
|---|---|---|
| `file` | string | The file to read. Required. |

Object: one mesh, by transform or shape name. Returns the skinCluster's name.

- The whole file is validated first, including that every vertex's weights sum to 1, then checked against the mesh: its `meshId`, vertex and face counts and topology hash, and one marked joint in the mesh's namespace per influence. Any error is reported and nothing changes.
- Updates the mesh's skinCluster in place, or creates one. Sets exactly the file's weights. New influences bind at their joint's rest.
- Undoable, as one step.

## pruneToMaxInfluences

Limits every vertex to at most `maxInfluences` influences.

```python
cmds.pruneToMaxInfluences("hero:body_geo", maxInfluences=4)
# Returns 1204, the number of vertices changed
cmds.pruneToMaxInfluences(maxInfluences=4)   # every selected mesh
```

| Flag | Type | Meaning |
|---|---|---|
| `maxInfluences` | int | Required, at least 1. The most non-zero weights a vertex keeps. |

Objects: meshes with a skinCluster, by transform or shape name. With none named, every selected mesh. Selected nodes that aren't meshes are skipped.

- On each vertex with more than `maxInfluences` non-zero weights, keeps the largest, sets the rest to 0 and divides the kept weights by their sum. Equal weights keep the influence with the lower index in the skinCluster, so the result is the same every time.
- Vertices already within the limit aren't written, so they keep their exact weights.
- Locked influences are pruned like any other. The skinCluster's `maxInfluences` and `maintainMaxInfluences` settings aren't changed.
- A named node that isn't a mesh, a mesh without a skinCluster, or a missing `maxInfluences` is an error, and nothing changes.
- Undoable, as one step. Undo restores every weight exactly.

