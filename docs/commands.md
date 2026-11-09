# Commands

Every command:

- Takes the nodes it acts on **by name**. None of them reads the selection.
- Is undoable, apart from the queries `getRigControls`, `getRigMirrorControl` and `getRigInfo`, and `saveRigControlShapes` (writes a file, changes nothing in the scene).
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
2. **Build.** Creates the rig root, the `layout` module and every template module, connects child plugs to sockets, and builds the template's constraints.

Every node created goes in the rig namespace. The input joints are moved there too, with their names unchanged. A joint already in that namespace stays where it is. Joint and module names must be free in the rig namespace, so `skel:a` and `other:a` cannot both be passed.

The asset owns the rig namespace. The namespace holds one `assetRootModule`, which stores `asset` in its `assetId` attribute. Every rig built into the namespace connects to it, one per rig variant (`rigName`). The build reuses the namespace's asset root if there is one, and creates it otherwise. The build fails, building nothing, when the namespace holds more than one asset root, when the asset root's `assetId` differs from `asset` (ignoring case), or when a rig with the template's `rigName` is already connected to it.

The rig root records what built it, so tools can find out-of-date rigs in a scene:

| Attribute | Meaning |
|---|---|
| `rigName` | The rig variant, from the template |
| `rigVersion` | The template's `rigVersion` |
| `pluginVersion` | The RigModules plugin version that ran the build |
| `publishVersion` | The id of the asset publish the rig came from, for example `20261108T142012Z_bmorgan` (UTC time and user). A build leaves it empty, and the publish step sets it |

Undo removes the rig and moves the joints back, but leaves the (empty) rig namespace and an asset root that existed before the build.

## createRigParentConstraint

Adds a space switch to a rig control after the build. Templates can declare the same thing in `constraints` ([templates.md](templates.md#constraints)).

```python
cmds.createRigParentConstraint("hero:arm_ctrl_l", driver=["layout.end", "spine.end"], label=["World", "Chest"], initial=1)
# Returns the constraint node: 'hero:arm_ctrl_l_space'
```

| Flag | Type | Meaning |
|---|---|---|
| `driver` | string, multi-use | A socket id, `module.socket`. At least one. |
| `label` | string, multi-use | The driver's enum label. Give none, or one per `driver` in the same order. Defaults to the socket id. |
| `initial` | int | Index of the driver that starts active. Defaults to 0. |
| `name` | string | The constraint node's name. Defaults to `<control>_space`. |

Object: the control, which must be the control of a module's child plug.

- **Driver modules** without a namespace are looked up in the control's namespace, whatever the current namespace is. A driver that names a namespace (`other:layout.end`) is used as given.
- **The constraint node** goes in the control's namespace unless `name` includes a namespace.
- A module cannot drive its own child plug, and a control can have only one space constraint.
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

Returns what a rig node belongs to, as JSON text: its rig root and asset root, the asset id, the variant, the namespace and the rig root's version stamps. Python tools use it through `rigmodules.query.get_rig_info` ([python.md](python.md)).

```python
cmds.getRigInfo("hero:arm_ctrl_l")
# Returns '{"assetId":"shooter|main|characters|hero","assetRoot":"hero:assetRootModule1","namespace":"hero",
#           "pluginVersion":"0.1","publishVersion":"","rigName":"game","rigRoot":"hero:rigRootModule1","rigVersion":1}'
```

Object: one rig root, rig module or rig control.

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

- Any other node (a joint, an asset root) is an error. So is a rig root with no asset root.
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

