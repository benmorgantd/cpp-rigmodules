# Commands

Every command:

- Takes the nodes it acts on **by name**. None of them reads the selection.
- Is undoable.
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
cmds.createRigFromTemplate(*joints, template="C:/rigs/spine_arm.json", namespace="char:rig")
```

| Flag | Type | Meaning |
|---|---|---|
| `template` | string | Path to the template. Required. |
| `namespace` | string | The rig's namespace. Defaults to `rig`. `":"` is the root, nested paths such as `char:rig` work, and an existing namespace is reused. |

Objects: one or more joint names, with their namespace (`skel:arm_jnt`).

1. **Read and check.** Loads the template, reads markup from the given joints, and validates everything (see [templates.md](templates.md#validation)). Every joint passed in must have markup. On any error nothing is built, and every error is reported at once.
2. **Build.** Creates the rig root, the `layout` module and every template module, connects child plugs to sockets, and builds the template's constraints.

Every node created goes in the rig namespace. The input joints are moved there too, with their names unchanged. A joint already in that namespace stays where it is. Joint and module names must be free in the rig namespace, so `skel:a` and `other:a` cannot both be passed.

Undo removes the rig and moves the joints back, but leaves the (empty) rig namespace.

## createRigParentConstraint

Adds a space switch to a rig control after the build. Templates can declare the same thing in `constraints` ([templates.md](templates.md#constraints)).

```python
cmds.createRigParentConstraint("rig:arm_ctrl_l", driver=["layout.end", "spine.end"], label=["World", "Chest"], initial=1)
# Returns the constraint node: 'rig:arm_ctrl_l_space'
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

## setRigConstraintSpace

Switches a constraint to another driver without moving the control at that frame.

```python
cmds.setRigConstraintSpace("rig:arm_ctrl_l_space", label="World")
cmds.setRigConstraintSpace("rig:arm_ctrl_l_space", space=0)
```

| Flag | Type | Meaning |
|---|---|---|
| `space` | int | Driver index to switch to |
| `label` | string | Driver label to switch to |

Give exactly one of `space` or `label`. Object: the constraint node.

The command stores an offset for the new driver so the child keeps its current world position, then sets the control's enum (or the constraint's active driver when there is no enum). It does not set keys.
