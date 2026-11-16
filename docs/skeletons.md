# Skeleton files

A skeleton file describes a skeleton's marked joints and carries its **skeleton hash**. Two skeletons are the same when their hashes match. A rig, its looks (meshes and skin weights) and its animation are made against one skeleton, so the hash is how a tool tells whether they still belong together (D54, `design/rigs-and-looks.md`).

`saveRigSkeleton` writes the file. Nothing reads it back yet: stamping the hash on a built rig and checking files against it come later.

## What makes two skeletons the same

The hash covers every marked joint the save is given, marker joints included (joints marked `export: false`), and for each one:

- its markup: side, usage, `export` and `skin`
- its name, without a namespace
- its parent: the nearest ancestor among the given joints, by markup
- its rotate order
- its rest matrix relative to that parent

Rest matrix values are rounded to steps of 0.0001 before hashing. Float noise from a save, or a joint moved back to exactly where it was, gives back the same hash. Any change above that step makes another skeleton.

The hash is deterministic: the same joints always give the same hash, whatever order they are passed in. Undoing a change, or reverting the file, gives back the old hash. Display settings, colours, labels and anything else not in the list above leave it alone.

Rest matrices are local, so moving a joint changes its own entry and nothing below it. A group or other transform between two marked joints folds into the child's rest matrix.

## Saving

```python
joints = cmds.ls("skel:*", type="joint")
cmds.saveRigSkeleton(*joints, file="C:/assets/hero/skeletons/body/skeleton.json")
# Returns "159c28565ab750f5", the skeleton hash
```

The joints should be at rest: the file records them as they are. The full command reference is in [commands.md](commands.md#saverigskeleton).

## File format

The file for a three-joint chain: `a_jnt` at the origin, `b_jnt` 2 units up under it, and `c_jnt` 2 units further up under `b_jnt`.

```json
{
    "formatVersion": 1,
    "skeletonHash": "159c28565ab750f5",
    "joints": [
        {"markup": ["Center", "a"], "export": true, "skin": true, "name": "a_jnt", "parent": null, "rotateOrder": "xyz", "restLocal": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]},
        {"markup": ["Center", "b"], "export": true, "skin": true, "name": "b_jnt", "parent": ["Center", "a"], "rotateOrder": "xyz", "restLocal": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 2, 0, 1]},
        {"markup": ["Center", "c"], "export": true, "skin": true, "name": "c_jnt", "parent": ["Center", "b"], "rotateOrder": "xyz", "restLocal": [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 2, 0, 1]}
    ]
}
```

| Key | Meaning |
|---|---|
| `formatVersion` | 1 |
| `skeletonHash` | The hash: 16 lowercase hex digits |
| `joints` | One line per joint, sorted by side then usage, so an edit to one joint is a one-line diff |
| `markup` | `[side, usage]` |
| `export`, `skin` | The joint markup's flags |
| `name` | The joint's name without its namespace |
| `parent` | The parent's `[side, usage]`, or `null` for a joint with no marked ancestor among the saved joints |
| `rotateOrder` | `xyz`, `yzx`, `zxy`, `xzy`, `yxz` or `zyx` |
| `restLocal` | The rest matrix relative to the parent's (the world matrix when `parent` is `null`), 16 numbers row by row with translation in the last row, as Maya's `MMatrix`. Written exactly. Only the hash rounds |

The hash feeds, in this order: the joint count, then for each joint in file order its side, usage, `export`, `skin`, name, whether it has a parent, the parent's side and usage, the rotate order's index in the list above, and the 16 `restLocal` values each divided by 0.0001 and rounded half away from zero to a 64-bit integer. The algorithm is 64-bit FNV-1a, with each number as little-endian bytes and each string as its length then its bytes. Changing any of this would change every stamped hash, so it is fixed.
