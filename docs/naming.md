# Naming convention

Every joint, control, module and constraint in a rig has a name in one form:

```
<camelCaseName>_<##>_<L|R|M>_<JNT|CTRL|MOD|CON>
```

For example `upperArm_00_L_JNT`, `upperArm_00_L_CTRL`, `fkChain_00_L_MOD` and `armSpace_00_L_CON`. The name is public: saved poses, animation and exports, template socket ids and tools depend on it, so rename with care.

| Part | Rule |
|---|---|
| `camelCaseName` | A lowercase letter, then letters and digits. No underscore, so a name splits cleanly on its underscores. `spine1` is a valid base. |
| `##` | The index, at least two digits (`00`, `01`, ... `99`, `100`). It numbers things that share a base, such as the joints of a chain. |
| `L`, `R`, `M` | The side: `Left`, `Right` or `Center` (middle). |
| `JNT`, `CTRL`, `MOD`, `CON` | The kind: a joint, a rig control, a rig module, a rig constraint. |

Only the canonical text is a name: `arm_0_L_JNT` (one digit) and `arm_000_L_JNT` (a leading zero past two digits) are not.

## Where each kind comes from

- **Joints** are named by you, and the build requires it. `createRigFromTemplate` fails, before it changes the scene, on any joint passed in whose name is not `<camelCaseName>_##_<L|R|M>_JNT`, and the message says which. Use [`renameRigNodes`](commands.md#renamerignodes) to rename a skeleton.
- **Controls** are named by the build: the joint's base and index, the module's side and `CTRL`. `skel:upperArm_00_M_JNT` in a `Left` module gives `upperArm_00_L_CTRL`. A control with no joint takes its [markup](controls.md) usage: a trailing `_NN` becomes the index, so the Layout's `layout_0` gives `layout_00_M_CTRL`, and a TwoBoneIk's `armIK` gives `armIK_00_L_CTRL`. The usage's base must be camelCase, which a module reports when its `controlName` is not.
- **Modules** are named in the template, `"name": "arm_00_L_MOD"`. The side letter must match the module's `side`. The Layout is always `layout_00_M_MOD`, so a template refers to its sockets as `layout_00_M_MOD.end`.
- **Constraints** are named in the template (`armSpace_00_L_CON`). [`createRigConstraint`](commands.md#createrigconstraint) without `name` takes the control's name with `CON`, so `arm_00_L_CTRL` gives `arm_00_L_CON`. A `name` it is given must be in the convention too.

## Mirroring

A mirrored module or constraint swaps `L` and `R` in its name: `arm_00_L_MOD` becomes `arm_00_R_MOD`, and ids that point at a `Left` or `Right` module swap the same way. A mirrored entry's name must have a side of `L` or `R` (see [templates.md](templates.md#mirroring)).

## Renaming

[`renameRigNodes`](commands.md#renamerignodes) renames the selection, or the nodes it is given, in order: `cmds.renameRigNodes(base="finger", side="Left")` on three selected joints gives `finger_00_L_JNT`, `finger_01_L_JNT` and `finger_02_L_JNT`. From Python, see [`rigmodules.naming`](python.md#rigmodulesnaming).

The rules are in one class, `NamingConvention` in `cpp/core/`, which the build, template validation, mirroring and the command all use.
