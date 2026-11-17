# Looks

A look is the model a rig wears: its meshes and their skin weights. A rig can wear one look at a time, and swapping looks never touches the controls or the animation, because a look is meshes bound to the rig's joints and nothing else ([rigs-and-looks.md](../design/rigs-and-looks.md) has the design).

A look reaches the scene through one command, [`setRigLook`](commands.md#setriglook), and is recorded on a `lookRoot` node.

## Applying a look

RigModules reads no asset paths, so the caller imports the look's meshes into the rig's namespace first (mayadev does this) and then passes the meshes and their weight files:

```python
cmds.setRigLook("hero_body:rigRootModule1",
                mesh=["hero_body:body_geo", "hero_body:hat_geo"],
                file=["C:/assets/hero/looks/red/weights/body_geo.json", "C:/assets/hero/looks/red/weights/hat_geo.json"],
                asset="shooter\\characters\\hero", look="red")
# Returns 'hero_body:lookRoot'
```

The command, as one undo step:

1. Matches each mesh to the weight file with its `meshId`. Order and names are never used.
2. Loads each file's weights onto the rig's joints ([skinning.md](skinning.md#loading)). A file whose `skeletonHash` is not the rig's fails the whole command with nothing changed.
3. Deletes the previous look's meshes and their history.
4. Names each new mesh after its file's `meshName`, so a look's meshes keep their names whatever Maya called them on import.
5. Creates or updates the `lookRoot`.

## What a look can delete

Only the `lookRoot`'s members: the meshes the last `setRigLook` was given. A mesh the animator added to the rig's namespace is never deleted. Materials a look brings are not members and stay in the scene.

## The `lookRoot` node

One per rig that has had a look applied, in the rig's namespace, connected by `rigRoot.look` to `lookRoot.rigRoot`. A rig with none wears no recorded look.

| Attribute | Meaning |
|---|---|
| `assetId` | The look's own asset, which can differ from the rig's |
| `lookName` | The look's name |
| `publishVersion` | The publish the look came from, `""` for an unpublished one |
| `rigRoot` | Message, from the rig root's `look` |
| `lookMembers[]` | Messages from the look's meshes |

[`getRigInfo`](commands.md#getriginfo) returns the same three values as `look`, and `rigmodules.query.RigInfo.look` is a `LookInfo`, or `None` before a look is applied ([python.md](python.md)). The node has no inputs, outputs or `compute`, so it costs nothing to evaluate.

## When the skeleton is different

Every weight file records the skeleton it was made on, and the rig records its own ([skeletons.md](skeletons.md)). `setRigLook` and `loadRigSkinWeights` compare them:

- **Different:** an error, with nothing changed. `ignoreSkeletonHash=True` makes it a warning.
- **A file with no hash** (made before D54, or on joints no rig drove): loads with a warning, because it can't be checked.
- **With `skeleton` and `rigSkeleton`** (the look's and the rig's `skeleton.json`), `setRigLook` compares them first and names every joint that differs (`[Center, b]: a different rest transform`).

## Not here yet

The face's blend shape wiring, attachments, and listing looks that aren't synced are later phases of D54.
