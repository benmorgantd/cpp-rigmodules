# Commands

Every command:

- Takes the nodes it acts on **by name**. None of them reads the selection, except `setRigMeshId`, `pruneToMaxInfluences` and `smoothSkinWeights` when nothing is named.
- Is undoable, apart from the queries `getRigControls`, `getRigControlInfo`, `getRigControlChannels`, `getRigControlSpace`, `getRigMirrorControl`, `getRigKeys` and `getRigInfo`, and `saveRigControlShapes`, `saveRigSkinWeights` and `saveRigSkeleton` (they write a file and change nothing in the scene).
- Reports errors through `displayError`. From Python, the error text arrives as a `RuntimeError`.

Examples use Python `maya.cmds` (`from maya import cmds`). Flags are keyword arguments named by their long flag name. A flag that can be given several times takes a list.

## rigJointMarkup

Writes, edits, queries or removes the `rigMarkup` attribute on joints. Markup says what a joint is, so templates can select it.

```python
# Create
cmds.rigJointMarkup("skel:upperArm_00_L_JNT", usage="upperArm", side="Left", export=True, skin=True)

# Edit one value on several joints
cmds.rigJointMarkup("skel:upperArm_00_L_JNT", "skel:lowerArm_00_L_JNT", edit=True, usage="shoulder")

# Query one value: a list with one entry per joint, "" for unmarked joints
cmds.rigJointMarkup("skel:upperArm_00_L_JNT", query=True, usage=True)   # ['upperArm']

# Query everything: the full markup JSON per joint
cmds.rigJointMarkup("skel:upperArm_00_L_JNT", query=True)

# Remove
cmds.rigJointMarkup("skel:upperArm_00_L_JNT", remove=True)
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

## renameRigNodes

Renames joints, rig controls, rig modules and rig constraints into the [naming convention](naming.md), `<camelCaseName>_##_<L|R|M>_<JNT|CTRL|MOD|CON>`. The nodes are the ones passed, else the selection. The index counts up in that order, so the first node gets `startIndex` and each one after it one more.

```python
# Three joints in selection order (or in the order passed): upperArm_00_L_JNT, upperArm_01_L_JNT, upperArm_02_L_JNT
cmds.renameRigNodes("a", "b", "c", base="upperArm", side="Left")

# The selection, numbered from 3
cmds.renameRigNodes(base="finger", startIndex=3)

# Renumber only: each node keeps its own base and side
cmds.renameRigNodes("skel:spine_03_M_JNT", "skel:spine_07_M_JNT")   # ['skel:spine_00_M_JNT', 'skel:spine_01_M_JNT']

# Query: what a name means. One JSON string per name, "" when the name is not in the convention
cmds.renameRigNodes("skel:upperArm_00_L_JNT", query=True)   # ['{"base":"upperArm","index":0,"kind":"Joint","side":"Left"}']
```

| Flag | Type | Meaning |
|---|---|---|
| `base` | string | The base name for every node: camelCase, a lowercase letter then letters and digits. Without it, each node keeps its own base, which must already be in the convention. |
| `side` | string | `Center`, `Left` or `Right`. Without it, each node keeps its own side, else `Center`. |
| `startIndex` | int | The index of the first node. Default 0. |
| `query` | bool | Return the parts of each object name instead of renaming. |

- **The kind comes from the node's type:** a joint is `JNT`, a rig control `CTRL`, a rig module `MOD` and a rig constraint `CON`. Any other node is an error.
- **Nothing changes unless every name is free.** A target that another node holds, or that two of the nodes would share, fails the command and lists each one. Nodes can swap names among themselves, so renumbering a selection never collides with itself.
- **Namespaces stay.** A node keeps its namespace.
- **Selection order needs Maya to track it** (`cmds.selectPref(trackSelectionOrder=True)`, then select again). The command fails on a selection of more than one node while it is off. Passing the nodes by name never depends on it.
- **One undo step.** The result is the new names, in order.
- Renaming joints of a skeleton that a rig was built on changes the [skeleton hash](skeletons.md), and a control keeps the name it was built with. Rename the skeleton before the build.

## createRigFromTemplate

Builds a rig from a template and the joints passed in.

```python
joints = ["skel:spine1_00_M_JNT", "skel:spine2_00_M_JNT", "skel:arm_00_L_JNT"]
cmds.createRigFromTemplate(*joints, template="C:/rigs/spine_arm.json", asset="shooter\\characters\\hero")
```

| Flag | Type | Meaning |
|---|---|---|
| `template` | string | Path to the template. Required. |
| `asset` | string | The asset the rig belongs to: its folder path below the projects root, `\` between folders (`shooter\characters\hero`), any depth. Each folder name is lowercase letters, digits and `_`: uppercase is an error, not converted (D54). Required. Templates name no asset, so one template builds many assets. |
| `namespace` | string | The rig's namespace. Defaults to the asset's last folder name and the template's `rigName`, `{asset name}_{rigName}` (`hero_body`), with no special case for an asset with one rig (D54). `":"` is the root, nested paths such as `char:rig` work, and an existing namespace with no rig in it is reused. |
| `parentRig` | string | The rig root this rig is attached to ([attachments.md](attachments.md)). A template with `parent:module.socket` ids needs it, and a template without them refuses it. It must be a rig root in another namespace, and every socket the template takes from it must exist, or the read phase fails and lists them all. Optional. |
| `checkMirrorPositions` | bool | Default `False`. With `True`, a mirrored module ([templates.md](templates.md#mirroring)) fails the build when one of its joints is more than 0.001 from its source joint reflected across X = 0. |

Objects: one or more joint names, with their namespace (`skel:arm_00_L_JNT`).

After a successful build the selection is cleared.

1. **Read and check.** Loads the template, reads markup from the given joints, and validates everything (see [templates.md](templates.md#validation)). Every joint passed in must have markup. On any error nothing is built, and every error is reported at once.
2. **Build.** Creates the rig root, the layout module and every template module, connects child plugs to sockets, makes the display layers, groups and selection sets (below) and builds the template's constraints.

Every node created goes in the rig namespace. The input joints are moved there too, with their names unchanged. A joint already in that namespace stays where it is. Joint and module names must be free in the rig namespace, so `skel:a` and `other:a` cannot both be passed.

A namespace holds one rig (D54), so the display layers, groups and selection sets below, named per namespace, are that rig's own. The build fails, building nothing, when the namespace already holds a rig root, whatever its asset or `rigName`: pass `namespace` to build a second instance. The rig root stores `asset` in its `assetId` attribute.

The build makes three display layers in the rig namespace: `jointsLayer` (the joints passed in), `meshesLayer` (empty, with its display type set to Reference so the model can't be selected by mistake) and `controlsLayer` (every control, with `hideOnPlayback` on so controls aren't drawn while the timeline plays). A layer of the same name already in the namespace is deleted first, and a node that was in another layer moves into the rig's layer. Each layer connects to its own message attribute on the rig root, so tools find them without reading names:

| Attribute | Layer |
|---|---|
| `jointDisplayLayer` | `jointsLayer` |
| `meshDisplayLayer` | `meshesLayer` |
| `controlDisplayLayer` | `controlsLayer` |

Meshes are not inputs of the build: the model is imported after it, so `meshesLayer` starts empty and the tool that imports the model fills it with the imported meshes ([`rigmodules.displaylayers.add_meshes`](python.md#rigmodulesdisplaylayers)). A layer left in the namespace by a deleted rig is replaced.

The build also makes two groups in the rig namespace. `rig_all` is the parent of the rig's topmost joints and topmost controls, and `mesh_grp` is an empty group under it. The tool that imports the model parents the imported transforms to `mesh_grp` ([`rigmodules.groups.parent_meshes`](python.md#rigmodulesgroups)). Both groups have translate, rotate and scale locked and hidden, and only `visibility` is keyable. Each connects to a message attribute on the rig root:

| Attribute | Group |
|---|---|
| `allGroup` | `rig_all` |
| `meshGroup` | `mesh_grp` |

The names `rig_all` and `mesh_grp` must be free in the rig namespace, like joint and module names.

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
| `assetId` | The `asset` flag |
| `rigName` | The rig, from the template: its folder name in the asset |
| `skeletonHash` | The hash of the input joints as they were handed in, the same one [`saveRigSkeleton`](#saverigskeleton) returns for them (D54). Looks, weight files and library files that carry it can be matched to the skeleton |
| `rigVersion` | The template's `rigVersion` |
| `pluginVersion` | The RigModules plugin version that ran the build |
| `publishVersion` | The id of the asset publish the rig came from, for example `20261108T142012Z_bmorgan` (UTC time and user). A build leaves it empty, and the publish step sets it |

Undo removes the rig and moves the joints back, but leaves the (empty) rig namespace.

## createRigConstraint

Adds a space switch to a rig control after the build. Templates can declare the same thing in `constraints` ([templates.md](templates.md#constraints)).

```python
cmds.createRigConstraint("hero_body:arm_00_L_CTRL", driver=["layout_00_M_MOD.end", "spine_00_M_MOD.end"], label=["World", "Chest"], initial=1)
# Returns the constraint node: 'hero_body:arm_00_L_CON'
```

| Flag | Type | Meaning |
|---|---|---|
| `driver` | string, multi-use | A socket id, `module.socket`. At least one. |
| `label` | string, multi-use | The driver's enum label. Give none, or one per `driver` in the same order. Defaults to the socket id. |
| `initial` | int | Index of the driver that starts active. Defaults to 0. |
| `name` | string | The constraint node's name. Defaults to the control's name with `CON`, `arm_00_L_CTRL` giving `arm_00_L_CON`. A name must be in the [naming convention](naming.md), ending in `CON`. |
| `type` | string | `Parent` (the default), `Orient`, `Point`, `Swing` or `Twist`. Orient takes only the driver's rotation, Point only its translation, Swing its translation and its rotation without the twist about X, and Twist its translation and its twist about X on the parent's swing. The rest comes from the child plug's parent ([templates.md](templates.md#constraint-types)). |
| `constraintWeight` | float | 0 to 1, default 1. How much of the constraint's effect the control gets: 0 makes the control just follow its parent, whatever the type, and 1 is the full constraint. In between, positions blend and rotations slerp. It is set at build time and is not an animation channel ([templates.md](templates.md#constraint-weight)). |

Object: the control, which must be the control of a module's child plug.

```python
# The arm keeps the world's rotation, and its shoulder pivot follows the chest.
cmds.createRigConstraint("hero_body:arm_00_L_CTRL", driver=["layout_00_M_MOD.end", "spine_00_M_MOD.end"], label=["World", "Chest"], type="Orient")

# The same, at half strength: the arm turns halfway with the chest.
cmds.createRigConstraint("hero_body:arm_00_L_CTRL", driver=["layout_00_M_MOD.end", "spine_00_M_MOD.end"], label=["World", "Chest"], type="Orient", constraintWeight=0.5)
```

- **Driver modules** without a namespace are looked up in the control's namespace, whatever the current namespace is. A driver that names a namespace (`other:layout_00_M_MOD.end`) is used as given.
- **The constraint node** goes in the control's namespace unless `name` includes a namespace.
- A module cannot drive its own child plug, and a control can have only one space constraint.
- An Orient, Point, Swing or Twist constraint fails on a child plug with no parent connected. A built rig always has one, `layout_00_M_MOD.end` by default.
- A Parent constraint below `constraintWeight` 1 also reads the child plug's parent, so it fails on a plug with none.
- The node is a `rigConstraint`, and its `constraintType` attribute holds the type, and `constraintWeight` its weight.
- With more than one driver, the control gets a `space` enum attribute listing the labels.
- The control does not move. On a posed rig its translate, rotate and scale are changed to keep it in place, the same match `setRigConstraintSpace` does. At rest nothing changes.

## setRigConstraintSpace

Switches a constraint to another driver without moving the control at that frame.

```python
cmds.setRigConstraintSpace("hero_body:arm_00_L_CON", label="World")
cmds.setRigConstraintSpace("hero_body:arm_00_L_CON", space=0)
```

| Flag | Type | Meaning |
|---|---|---|
| `space` | int | Driver index to switch to |
| `label` | string | Driver label to switch to |

Give exactly one of `space` or `label`. Object: the constraint node. To switch from the control instead, for either kind of space, use [`setRigControlSpace`](#setrigcontrolspace).

The command changes the control's translate, rotate and scale so it keeps its world position and orientation in the new space, then sets the control's `space` enum. Nothing is stored on the constraint, so every space gives the rest pose when the controls are zeroed, and a pose is just the control values plus the enum.

- **Rotation** keeps the control's rotate order and picks the euler values closest to the current ones, so animation curves don't flip.
- **Locked channels**, and channels driven by anything other than keys, keep their values. The command warns and names them, and the control moves by what they couldn't absorb.
- **No keys.** On an animated control, key the changed channels and the enum yourself, or the values revert when the frame changes.
- **Setting the enum directly** (or through keys) does no matching, so the control moves unless the rig is at rest.

Undo restores both the enum and the control's values.

## setRigIkFkBlend

Switches a [TwoBoneIk](modules.md#twoboneik) limb between IK and FK without a pop. It first writes the control values that reproduce the limb's current pose on both sides, then sets `ikFkBlend`.

```python
cmds.setRigIkFkBlend("hero_body:armSettings_00_L_CTRL", blend=0)    # to FK
cmds.setRigIkFkBlend("hero_body:arm_00_L_MOD", blend=1)                   # to IK (the default)
# Returns the controls that changed
```

| Flag | Type | Meaning |
|---|---|---|
| `blend` | float | The `ikFkBlend` to set, from 0 (FK) to 1 (IK). Default 1. |

Object: one of the limb's six controls, or its module node. A joint is not accepted.

- **What it writes.** The FK start, lower and end controls from the limb's three joints, and the IK handle and pole from the same joints, each in the control's own rotate order and as the euler nearest its current rotation. Both sides are written every time, so the side you switch away from is also current. The values are the ones [`poseRigFromJointPose`](#poserigfromjointpose) writes for the same pose (the module's [inverse](modules.md#restoring-a-limb-from-joints)), and the limb's `stretch`, `soft`, `pin`, `roll` and pole `space` are the scene's current ones.
- **No pop at blend 0 and 1.** The joints are where they were after the switch, in either direction, with a space switch on the IK handle, the pole in `Ik` space, and stretch, soft, pin and roll set.
- **Warnings.** A pose IK can't make (bone lengths other than the rig's with `stretch` and `pin` off, or a limb past its reach) warns: IK is as near as the limb gets, and FK matches exactly. A current `ikFkBlend` between 0 and 1 warns that the IK side is approximate. A locked or driven channel keeps its value and warns when that value isn't the match's. A locked or driven `ikFkBlend` is left alone and warned about.
- Sets values and keys nothing: a keyed switch is a tool built on this command. Undoable as one step. Everything is checked before anything is written.
- Setting `ikFkBlend` directly, by `setAttr`, a loaded pose or a key, does no match, so it pops.

## getRigControls

Returns a rig's controls, found through the rig's connections. Filter by module type and control markup (see [controls.md](controls.md#control-markup)).

```python
cmds.getRigControls("hero_body:rigRootModule1")                                # every control
cmds.getRigControls("hero_body:rigRootModule1", moduleType="FkChain", side="Left")
cmds.getRigControls("hero_body:rigRootModule1", usage=["spine1", "spine2"])
```

| Flag | Type | Meaning |
|---|---|---|
| `moduleType` | string, multi-use | A template `moduleType`: `Layout`, `FkChain`, `Aim`, `TwoBoneIk` or `Ribbon` |
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
cmds.saveRigControlShapes("hero_body:rigRootModule1", file="C:/assets/hero/shapes.json")
```

| Flag | Type | Meaning |
|---|---|---|
| `file` | string | The file to write. Required. Overwritten if it exists. |

Object: the rig root node. Returns the names of the controls saved. Not undoable: it writes a file and changes nothing in the scene.

## loadRigControlShapes

Sets control shapes from a shape file, matching controls by markup.

```python
cmds.loadRigControlShapes("hero_body:rigRootModule1", file="C:/assets/hero/shapes.json")
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
cmds.getRigMirrorControl("hero_body:upperArm_00_L_CTRL")
# Returns 'hero_body:upperArm_00_R_CTRL'
```

Object: one control.

- A `Center` control returns itself.
- A control with no mirror is an error that names the side and usage it looked for.
- Not undoable: it changes nothing.

## getRigInfo

Returns what a rig node belongs to, as JSON text: its rig root, the asset id, the rig name, the skeleton hash, the namespace, the rig root's version stamps and the look it wears. Python tools use it through `rigmodules.query` ([python.md](python.md)).

```python
cmds.getRigInfo("hero_body:arm_00_L_CTRL")
# Returns '{"assetId":"shooter\\main\\characters\\hero","namespace":"hero_body","pluginVersion":"0.1",
#           "publishVersion":"","rigName":"body","rigRoot":"hero_body:rigRootModule1","rigVersion":1,
#           "skeletonHash":"3f2a9c0b7d4e1f68"}'
```

Object: one rig root, rig module, rig control, or joint driven by a rig. A joint belongs to the rig whose module drives its `offsetParentMatrix`.

| Key | Value |
|---|---|
| `rigRoot` | The rig root node |
| `assetId` | The rig root's `assetId` |
| `rigName` | The rig root's `rigName` |
| `skeletonHash` | The rig root's `skeletonHash` |
| `namespace` | The namespace the rig root is in, without a leading `:` |
| `rigVersion` | The template's `rigVersion` |
| `pluginVersion` | The RigModules version that built the rig |
| `publishVersion` | The publish the rig came from, `""` until published |
| `look` | `null` until `setRigLook` has applied one, then `{"assetId", "name", "publishVersion"}` (D54) |
| `parentRigRoot` | `null`, or the rig root an attachment follows, as the node name ([attachments.md](attachments.md), D55) |

- Any other node (an unrigged joint, a mesh) returns `""`, with no error, so a selection callback can ask about anything.
- A missing node is an error. So is a broken rig, such as a control whose module has no rig root.
- Not undoable: it changes nothing.

## mirrorRigControlShapes

Mirrors control shapes from one side of a rig to the other.

```python
cmds.mirrorRigControlShapes("hero_body:rigRootModule1")                    # Left onto Right
cmds.mirrorRigControlShapes("hero_body:rigRootModule1", leftToRight=False) # Right onto Left
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
data = json.loads(cmds.getRigControlInfo("hero_body:upperArm_00_L_CTRL", "hero_body:chest_00_M_CTRL"))
data["hero_body:upperArm_00_L_CTRL"]
# {"moduleType": "FkChain", "module": "hero_body:arm_00_L_MOD", "side": "Left", "usage": "upperArm", "mirror": "hero_body:upperArm_00_R_CTRL"}
```

Objects: one or more rig controls. Returns a JSON object keyed by control name. From Python, [`rigmodules.query.get_control`](python.md#rigmodulesquery) and `get_rig_controls` wrap it.

- **`mirror`** is the control with the same usage on the other side ([`getRigMirrorControl`](#getrigmirrorcontrol)). A `Center` control is its own mirror, and a side control with no mirror gives `null` where `getRigMirrorControl` raises an error.
- Not undoable: it changes nothing.

## getRigControlSpace

Reports a control's space switch: who owns it, the space labels and the space the control is in now. It reports both kinds of space: a [constraint](#createrigconstraint) space (a `rigConstraint` holds the drivers) and a [module-owned](controls.md#channels) space (the [TwoBoneIk](modules.md#twoboneik) pole's `Parent` and `Ik`). Switch either kind with [`setRigControlSpace`](#setrigcontrolspace).

```python
import json
text = cmds.getRigControlSpace("hero_body:arm_00_L_CTRL")
json.loads(text)
# {"constraint": "hero_body:arm_00_L_CON", "module": "", "labels": ["World", "Chest"], "current": 1}
text = cmds.getRigControlSpace("hero_body:armPole_00_L_CTRL")
json.loads(text)
# {"constraint": "", "module": "hero_body:arm_00_L_MOD", "labels": ["Parent", "Ik"], "current": 0}
```

Object: exactly one rig control. Returns a JSON string, or an empty string for a control that has no space switch. A control built with a single driver has none: the `space` enum exists only for two or more drivers. From Python, [`rigmodules.query.find_control_space`](python.md#rigmodulesquery) wraps it.

| Key | Meaning |
|---|---|
| `constraint` | The `rigConstraint` node of a constraint space, which `setRigConstraintSpace` takes. Empty for a module-owned space. |
| `module` | The module node of a module-owned space. Empty for a constraint space. |
| `labels` | The space labels in order, which is the order of the `space` enum. For a constraint, the drivers' labels in driver order. |
| `current` | The index into `labels` that the control is in now. |

- Not undoable: it changes nothing.

## setRigControlSpace

Switches a control to another space and keeps the control where it is in the world. One command for both kinds of space: a [constraint](#createrigconstraint) space and a module-owned space (the [TwoBoneIk](modules.md#twoboneik) pole's `Parent` and `Ik`). It names the control, so a tool needs no constraint or module name: [`getRigControlSpace`](#getrigcontrolspace) lists the labels.

```python
cmds.setRigControlSpace("hero_body:arm_00_L_CTRL", label="Chest")
cmds.setRigControlSpace("hero_body:armPole_00_L_CTRL", space=1)    # the pole follows the IK handle
```

| Flag | Type | Meaning |
|---|---|---|
| `space` | int | The index into `getRigControlSpace`'s `labels`. |
| `label` | string | The label to switch to. |

Object: exactly one rig control, by name. Give exactly one of `space` or `label`.

- **Keeps the control's world matrix.** The control's translate, rotate and scale are rewritten by the same formula as [`setRigConstraintSpace`](#setrigconstraintspace) (D27). For a constraint space the command does exactly what `setRigConstraintSpace` does. For a module-owned space it reads the delta of the space the control is in now and of the new one from the module.
- **The current space is the control's `space` enum**, so a space set by hand, or keyed, switches from where it is.
- A locked or driven channel keeps its value and warns, and the control moves. Sets no keys: key the changed channels and the enum yourself on an animated control.
- A control with no space switch, a bad index, an unknown label, both or neither of `space` and `label` are errors, and nothing changes.
- Setting the `space` enum directly (`setAttr`, a key, a loaded pose) does no match, so the control moves.
- Undoable as one step. [`setRigConstraintSpace`](#setrigconstraintspace) stays for scripts that already hold the constraint.

## getRigControlChannels

Reports a control's animation interface: the channels an animator keys and a pose tool writes.

```python
import json
data = json.loads(cmds.getRigControlChannels("hero_body:upperArm_00_L_CTRL", "hero_body:chest_00_M_CTRL"))
data["hero_body:chest_00_M_CTRL"][0]
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
cmds.zeroRigControls("hero_body:rigRootModule1")                           # every control
cmds.zeroRigControls("hero_body:upperArm_00_L_CTRL", "hero_body:chest_00_M_CTRL")   # these two
# Returns the controls that moved
```

Objects: the rig root node (every control in the rig), or one or more controls of one rig (those only). A rig root can't be combined with controls, and controls of two rigs are an error. Every channel of each control (see [`getRigControlChannels`](#getrigcontrolchannels)) goes to its `rest`. Channels that are locked or driven keep their values. Sets values and keys nothing. Undoable.

## mirrorRigPose

Copies the pose from one side of a rig to the other.

```python
cmds.mirrorRigPose("hero_body:rigRootModule1")                    # Left onto Right
cmds.mirrorRigPose("hero_body:rigRootModule1", leftToRight=False) # Right onto Left
cmds.mirrorRigPose("hero_body:upperArm_00_L_CTRL", "hero_body:lowerArm_00_L_CTRL")   # these controls onto their mirrors
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
cmds.flipRigPose("hero_body:rigRootModule1")
cmds.flipRigPose("hero_body:upperArm_00_L_CTRL", "hero_body:chest_00_M_CTRL")   # this pair, and this Center control
```

Objects: the rig root node, or one or more controls of one rig. Returns the controls changed.

- **Left and Right** controls exchange values, by the same rules as `mirrorRigPose`. With controls, each one named swaps with its mirror, and a pair named from both ends swaps once.
- **Center controls** are reflected across the world YZ plane through their own channels. This needs one of the control's rest axes to lie along world X: that axis' translate is negated, and so are the rotates of the other two axes. A control with no such axis keeps its values, with a warning. A custom channel negates only if it says `negate`.
- Flipping twice restores the pose. Sets values and keys nothing. Undoable.

## saveRigPose

Writes the channel values of controls to a pose file.

```python
cmds.saveRigPose("hero_body:rigRootModule1", file="C:/poses/idle.controlpose.json")
cmds.saveRigPose("hero_body:upperArm_00_L_CTRL", "hero_body:lowerArm_00_L_CTRL", file="C:/poses/arm.controlpose.json")
cmds.saveRigPose("hero_body:rigRootModule1", file="C:/poses/fist.controlpose.json", meta=json.dumps({"tags": ["hands", "fist"], "status": "scratch"}))
# Returns the controls saved
```

Objects: the rig root node (every control), or one or more controls of one rig. Flags:

| Flag | Type | Meaning |
|---|---|---|
| `file` | string | Where to write the file. Required. An existing file is replaced. |
| `meta` | string | A JSON object with the caller's part of the file's [meta](library.md#file-meta): `tags`, `description`, `category`, `rigRegions`, `status`, `author`, `template` and `skeleton`. Optional. The fields the rig fills (`assetId`, `rigName`, `skeletonHash`, `pluginVersion`, `created`) can't be set. A bad value or an unknown key is an error and nothing is written. |

- The file is keyed by control markup (`side`, `usage`) and channel name, never by node name, so a pose survives renames and rebuilds. Values are in the scene's UI units. An enum stores its index and a bool stores 0 or 1.
- The file carries a `formatVersion` (3). Its shape: `{"formatVersion": 3, "meta": {}, "controls": {"Left": {"upperArm": {"translateX": 0.0, "rotateY": 12.5}}}}`. The saved `meta` always holds the five fields the rig fills. A version 1 file, which has no meta, still loads, and a version 2 file's `variant` and `skeletonId` are read as `rigName` and `skeleton`.
- Changes no scene data and isn't undoable.

## loadRigPose

Applies a pose file to controls.

```python
cmds.loadRigPose("hero_body:rigRootModule1", file="C:/poses/idle.controlpose.json")
cmds.loadRigPose("hero_body:upperArm_00_L_CTRL", file="C:/poses/idle.controlpose.json")   # only this control
cmds.loadRigPose("hero_body:rigRootModule1", file="C:/poses/idle.controlpose.json", mirror=True)   # the pose on the other side
# Returns the controls that changed
```

Objects: the rig root node, or one or more controls of one rig. Flags:

| Flag | Type | Meaning |
|---|---|---|
| `file` | string | The pose file. Required. |
| `mirror` | bool | Default `False`. `True` mirrors the file's data before it is written, so the rig is written once: each entry goes to its mirror control, with a channel negated where the mirror says (the rules of [`flipRigPose`](#fliprigpose)). It is a flip: a pose saved from one side lands on the other. An entry whose control has no mirror is skipped with a warning. With controls, only those controls are written. |

- Controls match the file by markup. A control the file lacks keeps its values, and so does a channel it lacks. With the rig root, a control or channel the file has and the rig lacks is skipped with a warning. With controls, the rest of the file is ignored without one.
- The whole file is checked before anything is applied (bad JSON, an unknown side, a value that isn't a number, a `formatVersion` this build doesn't read), so a bad file changes nothing. An older `formatVersion` is updated on load.
- Sets values and keys nothing: use [`rigmodules.pose`](python.md#rigmodulespose) to key. Undoable.

## getRigKeys

Reads the keys of a rig's controls.

```python
import json

keys = json.loads(cmds.getRigKeys("hero_body:rigRootModule1"))
# {"Left": {"upperArm": {"rotateZ": [[1.0, 0.0], [10.0, 90.0]]}}}
cmds.getRigKeys("hero_body:upperArm_00_L_CTRL")   # only this control
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
cmds.setRigKeys("hero_body:rigRootModule1", keys=json.dumps(keys))
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
cmds.saveRigAnimation("hero_body:rigRootModule1", file="C:/anims/walk.controlanim.json", startFrame=1, endFrame=48)
cmds.saveRigAnimation("hero_body:upperArm_00_L_CTRL", file="C:/anims/wave.controlanim.json")   # only this control, over the playback range
# Returns the controls it read
```

Objects: the rig root node, or one or more controls of one rig. Flags:

| Flag | Type | Meaning |
|---|---|---|
| `file` | string | Where to write the file. Required. An existing file is replaced. |
| `startFrame` | float | First frame to save. Default: the playback range's start. |
| `endFrame` | float | Last frame to save. Default: the playback range's end. |
| `meta` | string | A JSON object with the caller's part of the file's [meta](library.md#file-meta), as for [`saveRigPose`](#saverigpose). Optional. |

- Only keys from `startFrame` to `endFrame` go in. A channel with no key in the range is left out, and so is a control with none.
- The file holds the keys with their tangents and each curve's flags ([keys format](#keys-format)), the range, and the scene's frame rate. Its format is in [library.md](library.md#controlanim-files).
- A pose that has to be an animation is the rig keyed on one frame: `rigmodules.pose.key_rig(root, time=1)`, then save frames 1 to 1.
- Changes no scene data and isn't undoable. It fails when `endFrame` is before `startFrame`.

## loadRigAnimation

Keys a rig's controls from a ControlAnim file.

```python
cmds.loadRigAnimation("hero_body:rigRootModule1", file="C:/anims/walk.controlanim.json")
cmds.loadRigAnimation("hero_body:rigRootModule1", file="C:/anims/walk.controlanim.json", offset=100, mirror=True)
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

## getRigDataFileInfo

Reads what pose files and ControlAnim files hold, without loading them onto the rig: their kind, [meta](library.md#file-meta), size and how well they fit the rig. It is what a library browser lists and searches.

```python
import json
found = json.loads(cmds.getRigDataFileInfo("hero_body:rigRootModule1", file=["C:/lib/fist.controlpose.json", "C:/lib/walk.controlanim.json"]))
found[1]["kind"], found[1]["startFrame"], found[1]["endFrame"]   # ('animation', 1, 48)
# Returns JSON text: a list with one object per file, in the order given
```

Object: the rig root node. Flag: `file`, one use per file (a multi-use flag).

Each entry has:

| Key | Meaning |
|---|---|
| `file` | The path as given |
| `kind` | `pose` or `animation` |
| `formatVersion` | The version stored in the file, before any update |
| `meta` | The file's meta object (`{}` when there is none) |
| `frameRate`, `startFrame`, `endFrame` | Animations only |
| `controlCount`, `channelCount` | The controls and the channels the file holds |
| `unmatchedControls` | The `[side, usage]` of every file control the rig has no control for. A file that fits the rig has `[]` |

- A file that can't be read, or fails validation (an older version is updated first), gives `{"file": ..., "error": ...}` and the other files still answer. A listing survives a broken file.
- A file with a `frameRate` key is an animation, and any other is a pose.
- Changes nothing and isn't undoable. It fails when the object isn't a rig root.

`rigmodules.library.get_data_file_info(rig_root, files)` wraps it ([python.md](python.md#rigmoduleslibrary)).

## setRigDataFileMeta

Edits the [meta](library.md#file-meta) of a pose file or a ControlAnim file and nothing else.

```python
cmds.setRigDataFileMeta(file="C:/lib/fist.controlpose.json", meta=json.dumps({"tags": ["hands"], "status": "review"}))
cmds.setRigDataFileMeta(file="C:/lib/fist.controlpose.json", meta=json.dumps({"description": ""}))   # clears the description
```

| Flag | Type | Meaning |
|---|---|---|
| `file` | string | The file to edit. Required. |
| `meta` | string | A JSON object. Each key it has replaces that field, and an empty string or empty list clears it. The others stay. Required. |

- The fields the rig fills (`assetId`, `rigName`, `skeletonHash`, `pluginVersion`, `created`) can't be set. A value of the wrong shape, an unknown key, and a file that fails validation are errors, and the file is left as it was.
- The file is read in full and written again at the current format version, so an older file is brought forward. Controls, keys and everything else in it are unchanged.
- Edits a file, so it isn't undoable and doesn't version or check the file out: the caller does that first, as for a save.

`rigmodules.library.set_data_file_meta(file, meta)` wraps it ([python.md](python.md#rigmoduleslibrary)).

## poseRigFromJointPose

Sets a rig's control values from a joint pose, by solving each module's controls from the joints ([how it solves](#how-the-joints-become-control-values)). The design is in [`design/inverse-solve.md`](../design/inverse-solve.md).

```python
cmds.poseRigFromJointPose("hero_body:rigRootModule1", jointPose="C:/tmp/idle.jointclip.json", sidecar="C:/poses/idle.controlpose.json", mode="match")
cmds.poseRigFromJointPose("hero_body:upperArm_00_L_CTRL", jointPose="C:/tmp/idle.jointclip.json")   # only this control
# Returns the controls that changed
```

Objects: the rig root node, or one or more controls of one rig. Flags:

| Flag | Type | Meaning |
|---|---|---|
| `jointPose` | string | A JointClip file with exactly one JointPose, as written by [`rigmodules.jointclip.capture_joint_clip`](python.md#rigmodulesjointclip). Required. |
| `sidecar` | string | A pose file (a ControlPose, [`saveRigPose`](#saverigpose)) with the control values the joints can't give. Optional. |
| `mode` | string | `match`, the only mode. Optional. |

- Sets values and keys nothing, like `loadRigPose`. Undoable.
- Everything is checked before anything is written, so a bad file changes nothing.
- The whole rig is solved, whatever the objects: with controls, only those controls are set.

### How the joints become control values

- A source joint matches a rig joint by markup (`side`, `usage`). A source joint the rig lacks is ignored. A rig joint the source lacks is a warning: its control comes from the sidecar, and stays at rest without one.
- The rig's modules are solved parents first, so every module's child plugs see where its parents' sockets are. The [Layout](modules.md) has no joints, so its controls come only from the sidecar (at rest without one) and everything below it hangs from them. A rig whose layout was moved but whose sidecar doesn't say so still matches the joints: the controls below the layout take up the difference.
- An [FkChain](modules.md) gets every control's translate, rotate and scale from its joints. An Aim gets its target and up controls from their joints, and its aim joint follows from them. A source aim joint that doesn't sit where its target and up aim it is a warning. A [TwoBoneIk](modules.md#twoboneik) gets its FK controls from its three joints, and its IK handle and pole from the same joints, so the IK side matches the FK side and the blend can be switched without a pop (see [Restoring a limb](modules.md#restoring-a-limb-from-joints)). Its `ikFkBlend`, `stretch`, `soft`, `pin`, `roll` and the pole's `space` are never read from joints: they come from the sidecar, else the rig's current value, because the inverse has to use the value the rig will evaluate with.
- A control with a [space switch](commands.md#createrigconstraint) is solved in the space the sidecar's `space` channel names. Without one, it is solved in the constraint's current space. Its values are relative to that space, so they differ from space to space while the joints match.
- Per channel, the value comes from the joints, else the sidecar, else rest. A control's transform channels always get a value. A custom channel, `space` included, is set only when the sidecar has it: nothing derives it, so a missing one keeps its value.
- A rotation is the euler nearest the control's current value (nearest the previous frame for a clip), in the control's own rotate order. Shear the channels can't hold, and locked or driven channels, are warnings.
- The source's rest is compared with the rig's. If a joint is more than 0.01 cm out, the source has other proportions: the command warns that the result won't reproduce the source's pose, and a retarget is needed.

## animateRigFromJointClip

Keys a rig's controls from an animated joint clip, by the solve of [`poseRigFromJointPose`](#how-the-joints-become-control-values) on every frame.

```python
cmds.animateRigFromJointClip("hero_body:rigRootModule1", jointClip="C:/tmp/walk.jointclip.json", sidecar="C:/export/walk/walk.controlanim.json", startFrame=1, endFrame=60)
# Returns the controls keyed
```

Objects: the rig root node, or one or more controls of one rig. Flags:

| Flag | Type | Meaning |
|---|---|---|
| `jointClip` | string | A JointClip file. Required. |
| `sidecar` | string | A [ControlAnim file](library.md#controlanim-files) with the control data the joints can't give: `space`, IK/FK blend, custom channels, Layout controls. An export folder's `<name>.anim.json` is one. Optional. |
| `mode` | string | `match`, the only mode. Optional. |
| `startFrame`, `endFrame` | int | The part of the clip to write, the whole clip by default. Both must be inside the clip's frames, and `startFrame` can't be after `endFrame`. |

- **What is keyed.** A channel the joints gave (FkChain and Aim transforms) gets a key on every frame of the range, with auto tangents (`space` and other enums step). Nothing is simplified: that is a separate tool on the animation. A channel only the sidecar gave gets the sidecar's own keys, tangents and curve flags, the ones inside the range, not a baked key on every frame. A channel nothing gave is not keyed, so a control with no joints and no sidecar entry keeps its keys. The whole call is written through [`setRigKeys`](#setrigkeys)'s path, so it is one undo.
- **The joints win.** A channel the joints give and the sidecar also has comes from the joints.
- **How the sidecar feeds the solve.** Its curves are evaluated on every frame of the range (on scratch animation curves that are deleted afterwards, so the scene and the undo queue are untouched) and the solve reads the values, `space` first, because it decides which driver feeds each constrained control. Outside the sidecar's own range its curves hold their first and last values, with a warning.
- The keys land on the clip's own frame numbers (`startFrame` in the file), which the sidecar shares. Keys the animator set outside the range stay, and the keys inside it are replaced.
- The clip must run at the scene's frame rate, and the sidecar at the clip's, or the command is an error. A resample is not built.
- Everything is checked before anything is written, so a bad file, including a sidecar that isn't a valid ControlAnim file, changes nothing. Undoable.

### The sidecar

The ControlAnim file for `sidecar` is the one `saveRigAnimation` writes ([format](library.md#controlanim-files)). It needs only the channels the joints can't give:

```json
{
    "formatVersion": 3,
    "meta": {},
    "frameRate": 24.0,
    "startFrame": 1,
    "endFrame": 3,
    "controls": {
        "Center": { "layout_0": { "translateX": { "keys": [[1, 0.0, "linear", "linear"], [3, 1.0, "linear", "linear"]] } } },
        "Left": { "upperArm": { "space": { "keys": [[1, 0, "auto", "step"]] } } }
    }
}
```

## setRigMeshId

Gives meshes a `meshId`, the GUID a skin weight file is matched to ([skinning.md](skinning.md)).

```python
cmds.setRigMeshId("hero_body:body_geo")
# Returns ['C214154A-43E0-A9E2-3CAA-22B52F9603E1']
cmds.setRigMeshId()                                   # every selected mesh
cmds.setRigMeshId("hero_body:body_geo_copy", newId=True)   # replace a duplicate's copied id
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
cmds.saveRigSkinWeights("hero_body:body_geo", file="C:/assets/hero/weights/body_geo.json")
# Returns 42, the number of influences saved
```

| Flag | Type | Meaning |
|---|---|---|
| `file` | string | The file to write. Required. Overwritten if it exists. |

Object: one mesh, by transform or shape name.

- Errors, writing nothing: the mesh has no `meshId`, or shares it with another mesh in its namespace; it has no skinCluster; an influence has no joint markup, is marked `skin: false`, or shares its markup with another influence; a vertex's weights don't sum to 1 (the error lists the vertices).
- Saves the weights exactly, with no rounding, pruning or normalizing ([skinning.md](skinning.md#weights-are-never-changed)).
- Records the skeleton: the `skeletonHash` of the rig root that drives the influences (D54). Influences of two rigs are an error. Influences that belong to no rig save an empty hash, with a warning.
- Not undoable: it writes a file and changes nothing in the scene.

## loadRigSkinWeights

Sets a mesh's skin weights from a weight file, matching influences by joint markup ([skinning.md](skinning.md#loading)).

```python
cmds.loadRigSkinWeights("hero_body:body_geo", file="C:/assets/hero/weights/body_geo.json")
# Returns 'hero_body:skinCluster1'
```

| Flag | Type | Meaning |
|---|---|---|
| `file` | string | The file to read. Required. |
| `ignoreSkeletonHash` | bool | Load a file made for another skeleton, with a warning, instead of failing. Default false |

Object: one mesh, by transform or shape name. Returns the skinCluster's name.

- The whole file is validated first, including that every vertex's weights sum to 1, then checked against the mesh: its `meshId`, vertex and face counts and topology hash, and one marked joint in the mesh's namespace per influence. Any error is reported and nothing changes. The file's `skeletonHash` is compared with the rig the influences belong to (D54): a different one fails the load, a file with none loads with a warning, and joints in no rig are not checked.
- Updates the mesh's skinCluster in place, or creates one. Sets exactly the file's weights. New influences bind at their joint's rest.
- Undoable, as one step.

## setRigLook

Puts a rig in a look: loads the look's weights onto its meshes, deletes the previous look's meshes, and records the look on the rig's `lookRoot` ([looks.md](looks.md)).

```python
cmds.setRigLook("hero_body:rigRootModule1",
                mesh=["hero_body:body_geo"],
                file=["C:/assets/hero/looks/red/weights/body_geo.json"],
                asset="shooter\\characters\\hero", look="red")
# Returns 'hero_body:lookRoot'
```

| Flag | Type | Meaning |
|---|---|---|
| `mesh` | string, multi-use | A mesh of the look, already imported into the rig's namespace. Required, one use per mesh |
| `file` | string, multi-use | A weight file, one for each mesh, in any order: each goes to the mesh with its `meshId`. Required |
| `asset` | string | The look's asset id. Required |
| `look` | string | The look's name, a lowercase name ([templates.md](templates.md)). Required |
| `publishVersion` | string | The publish the look came from. Default `""` |
| `skeleton` | string | The look's `skeleton.json`. When its hash is not the rig's the command fails before reading any weights |
| `rigSkeleton` | string | The rig's `skeleton.json`. With `skeleton`, the error names every joint that differs |
| `ignoreSkeletonHash` | bool | A weight file with another skeleton's hash loads with a warning. Default false |

Object: one rig root. Returns the `lookRoot`'s name.

- Every file is read and checked against its mesh and the rig before anything changes, so any error leaves the scene as it was. That includes a mesh outside the rig's namespace, a mesh whose `meshId` no file has, a file whose `skeletonHash` is not the rig's, and a `look` or `asset` that isn't a valid name.
- Deletes only the previous look's meshes (the `lookRoot`'s members) and their history, never any other mesh. Names each new mesh after its file's `meshName`.
- The controls and the animation are not touched.
- Undoable, as one step: undo brings the previous look and its weights back and removes the `lookRoot` if this was the first look.

## pruneToMaxInfluences

Limits every vertex to at most `maxInfluences` influences.

```python
cmds.pruneToMaxInfluences("hero_body:body_geo", maxInfluences=4)
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

## smoothSkinWeights

Smooths skin weights: each vertex becomes the average of itself and its neighbors, one pass after another (D64).

```python
cmds.smoothSkinWeights("hero_body:body_geo.vtx[120:180]", iterations=3, strength=1.0)
# Returns 54, the number of vertices whose weights changed
cmds.smoothSkinWeights("hero_body:body_geo")   # every vertex of the mesh
cmds.smoothSkinWeights()                       # the selected vertices, or every vertex of a selected mesh
```

| Flag | Type | Meaning |
|---|---|---|
| `iterations` | int | Passes, at least 1. Default 3. |
| `strength` | double | How far each pass goes towards the average, above 0 and at most 1. Default 1. |

Objects: meshes with a skinCluster (every vertex), or vertex, edge or face components of them (edges and faces count as the vertices they touch). With none named, the selection. Selected nodes that aren't meshes are skipped.

- Every pass reads the weights the last pass left, so all vertices update at the same time and their order changes nothing. Vertices outside the named ones are read as neighbors and never written.
- **Locked influences** (a joint's `lockInfluenceWeights`) keep their weight on every vertex. The other weights of a vertex keep the total they had, so it still adds up.
- When the skinCluster has `maintainMaxInfluences` on, a smoothed vertex keeps only its largest `maxInfluences` weights (locked ones count towards the limit, and the unlocked ones keep at least one) and the rest is scaled back to the total.
- Only the vertices whose weights moved are written, and only over the influences that matter around them. A mesh whose weights are already smooth changes nothing and returns 0.
- A named node that isn't a mesh or mesh components, a mesh without a skinCluster, an `iterations` below 1 or a `strength` outside 0 to 1 is an error, and nothing changes.
- Undoable, as one step across all the meshes. Undo restores every weight exactly.

## skinBrushCtx

The skin weight brush (D65): a context you paint with, on the selected skinned meshes. Drag over a mesh and the weights under the brush are smoothed, as strongly as the falloff there allows. The settings are edited and queried on the context.

```python
ctx = cmds.skinBrushCtx("skinBrushCtx1")    # create it: a context needs a name
cmds.skinBrushCtx(ctx, edit=True, radius=8.0, strength=0.4)
cmds.setToolTo(ctx)
cmds.skinBrushCtx(ctx, query=True, radius=True)    # 8.0
```

| Flag | Type | Meaning |
|---|---|---|
| `operation` | string | What a dab does. `smooth`: average the weights with the neighbors'. `restDelta`: move the weights so each vertex's offset from its neighbors' average matches the rest shape's, turned by the skin (what a delta mush keeps). `restEdge`: move them so each edge goes back towards its rest length. See [Painting towards the rest shape](#painting-towards-the-rest-shape). Default `smooth`. |
| `radius` | double | The brush radius, at least 0.001, in scene units. With the `surface` falloff it is a **length over the mesh**. Until you set one (a flag or a Ctrl + drag), it is 5% of the diagonal of the box around the selected meshes, worked out when the tool starts, so the brush fits a mesh of any size. Queried before the tool has started it is 5. |
| `falloff` | string | How the radius is measured. `surface` (default): along the mesh edges from where the brush landed, so it never reaches across a gap and two shells never share a dab. `volume`: in a straight line in space, a ball, which also reaches a part of the mesh that is close but not connected (the other side of a thin wall, a lip against a lip). |
| `strength` | double | How strongly each dab acts at the center of the brush, above 0 and at most 1. Default 0.5. |
| `throughMesh` | bool | Off: a dab lands on the closest surface under the pointer. On: it lands on every surface the ray crosses, so painting a body also paints its back. Default off. |
| `debug` | bool | Prints a line to the Script Editor for the press, every repeat timer tick (and why it did or did not repeat) and the release, to see what a stroke is doing. Default off. |

- **Meshes:** the selected meshes with a skinCluster. The brush picks the closest surface across all of them, and a stroke may move from one mesh to another. A selection of vertices or faces limits the brush to them (their neighbors are still read). Select again, or move the pointer out of the viewport and back, to pick up a new selection.
- **Falloff** is smooth, 1 at the center and 0 at the radius, measured as `falloff` says. With `surface`, two surfaces that are close in space but not connected (a lip against a lip, two shells, the far side of a thin wall) never share a dab. Switch to `volume` when you want them to.
- **Resize** with Ctrl + left drag sideways. The ring shows the radius; the colored points are the vertices the next dab reaches (blue at the edge, yellow at the center).
- A drag lands a dab every quarter of the brush radius along the pointer's path, so a fast drag leaves no gaps. **Holding the button still repeats the dab** 20 times a second, so the weights keep moving towards the operation's result (smoother, or closer to the rest shape) while you hold. Maya skinning the mesh after each dab sets the real rate on a dense mesh.
- **Strength** is how far one dab goes at the center of the brush. At the default 0.5 a single dab is gentle, which is what holding the button is for. Raise it with `cmds.skinBrushCtx(ctx, edit=True, strength=0.8)`, or from the Skin Brush menu tool's `strength` argument.
- **Undo:** one stroke, from press to release, is one undo step. Undo restores the weights from before the stroke exactly, however much the dabs overlapped.
- Like Maya's other tools, the context is created in a GUI session: Maya creates no contexts in `mayapy`. The tests drive the same engine through `skinBrushStroke`.

### Painting towards the rest shape

`restDelta` and `restEdge` (D66) are for painting weights in a pose so the mesh in that pose looks more like its rest shape: a delta mush, painted. Pose the rig where the skin is wrong, select the mesh, and paint.

- **What a dab does.** For each vertex it reaches, the brush works out where the rest shape wants the vertex given where its neighbors are now, then moves the weights to the blend of the influences that gets closest. `restDelta` keeps each vertex's rest offset from its neighbors' average (turned by the skin's rotation there), so it restores local shape, volume and angles. `restEdge` keeps edge lengths. Strength and falloff mean what they mean for `smooth`: how far towards the answer one dab goes.
- **It only blends influences the neighborhood already has**, never a joint that carries no weight there, and never below 0 or above 1. Locked influences and the cluster's maximum influences are respected. It cannot fix what no blend of the existing influences can reach (a candy wrapper needs another influence).
- **One pass per dab.** The neighbors move as you paint, so hold the brush or stroke again to keep converging. At rest, or under a move that keeps the shape (every joint turned together), there is nothing to fix and a dab changes nothing.
- **Weights are fitted to the pose you are in.** Paint in a few poses, not one.
- **The rest shape is the geometry the skinCluster is fed** (its input), not a file. The skin must be classic linear skinning (`skinningMethod` 0), and the mesh must be what that skinCluster gives from its input: the bind checks a sample of vertices and fails with an error for dual quaternion skinning, or a deformer after the skinCluster, since the solve would paint towards the wrong pose.
- The pose is read when a stroke starts (and when the pointer re-enters the viewport), so change the pose between strokes.

## skinBrushStroke

One brush stroke as an undoable command (D65). The context runs it for you at the end of each stroke. Run it yourself to repeat a stroke or to paint without the mouse.

```python
# Two dabs, each at a face and the point on it
cmds.skinBrushStroke(operation="smooth", radius=0.3, strength=1.0,
                     hitPoint=[("hero_body:body_geo", 5, 0.0, 0.0, 0.0), ("hero_body:body_geo", 6, 0.1, 0.0, 0.0)])
# A ray from above, onto either of two meshes (origin x y z, then direction x y z)
cmds.skinBrushStroke(radius=0.3, mesh=["hero_body:body_geo", "hero_body:shirt_geo"], viewRay=[(0, 5, 0, 0, -1, 0)], throughMesh=True)
# Each returns the number of vertices whose weights changed
```

| Flag | Type | Meaning |
|---|---|---|
| `operation` | string | As on the context. Default `smooth`. |
| `radius` | double | At least 0.001. With `falloff="surface"`, a length over the surface. Default 5. |
| `falloff` | string | `surface` (default) or `volume`, as on the context. |
| `strength` | double | Above 0 and at most 1. Default 0.5. |
| `hitPoint` | string, int, 3 doubles | Multi-use. A mesh, a face of it and the point on it. Each is one dab. |
| `mesh` | string | Multi-use. Meshes a `viewRay` may hit. A mesh named in a `hitPoint` is included too. |
| `viewRay` | 6 doubles | Multi-use. A ray, origin then direction. Each is one dab at what it picks, after the `hitPoint`s. A ray that hits nothing is skipped. |
| `throughMesh` | bool | As on the context, for the rays. |

- Needs at least one `hitPoint` or `viewRay`. A named mesh without a skinCluster, a `radius` or `strength` out of range, or an unknown `operation` is an error, and nothing changes.
- Locked influences and the skinCluster's maximum influences are respected, as in [`smoothSkinWeights`](#smoothskinweights).
- Undoable, as one step across every mesh the stroke touched. Undo restores every weight exactly.

## saveRigSkeleton

Writes a skeleton file with the joints' markup, names, parents, rotate orders and rest matrices, and returns the skeleton hash (format and hash rules in [skeletons.md](skeletons.md)).

```python
cmds.saveRigSkeleton(*cmds.ls("skel:*", type="joint"), file="C:/assets/hero/skeletons/body/skeleton.json")
# Returns "727fc558f44954b9", the skeleton hash
```

| Flag | Type | Meaning |
|---|---|---|
| `file` | string | The file to write. Required. Overwritten if it exists. |

Objects: one or more joints, by name. The selection is not read.

- Records the joints as they are now, so they should be at rest.
- Every joint must carry markup, marker joints (`export: false`) included, and no two may share a `[side, usage]`. Otherwise the errors are listed and nothing is written.
- Errors, writing nothing: no joints named, a name that isn't a joint, no `file`.
- Not undoable: it writes a file and changes nothing in the scene.

## loadRigSkeleton

Creates the joints of a skeleton file, at rest, parented and marked, and returns them in the file's order (what a load gives back is in [skeletons.md](skeletons.md#loading)).

```python
cmds.loadRigSkeleton(file="C:/assets/hero/skeletons/body/skeleton.json", namespace="hero_body")
# Returns ["hero_body:a_00_M_JNT", "hero_body:b_00_M_JNT", "hero_body:c_00_M_JNT"]
```

| Flag | Type | Meaning |
|---|---|---|
| `file` | string | The skeleton file to read. Required. |
| `namespace` | string | The namespace the joints go into, made if it doesn't exist. Colons at its ends are dropped. The current namespace when left out. |

Objects: none.

- The whole file is checked before anything is created. Every problem is listed, and nothing is created: bad JSON, a missing or unknown key, a `formatVersion` this build doesn't read, a joint with invalid markup, name, `rotateOrder` or `restLocal`, two joints with one `[side, usage]`, a `parent` that names no joint in the file, parents that form a loop.
- A node in the namespace that already has a joint's name is an error, and nothing is created: Maya would rename the joint, and the name is part of the skeleton.
- A stored `skeletonHash` that doesn't match the joints is a warning, not an error.
- Undoable. Undo removes the joints. A namespace the command made stays, empty.
