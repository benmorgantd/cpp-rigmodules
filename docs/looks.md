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
3. Binds each `rigidMesh` whole to the rig's topmost skin joint (below).
4. Deletes the previous look's meshes and their history.
5. Names each new mesh after its file's `meshName`, so a look's meshes keep their names whatever Maya called them on import. A rigid mesh keeps its own name.
6. Creates or updates the `lookRoot`.

### Meshes not skinned yet

A mesh of the look that has a `meshId` but no weight file (a prop you haven't skinned) goes in `rigidMesh`:

```python
cmds.setRigLook("hero_body:rigRootModule1",
                mesh=["hero_body:body_geo"],
                file=["C:/assets/hero/looks/red/weights/body_geo.json"],
                rigidMesh=["hero_body:cigarette_geo"],
                asset="shooter\\characters\\hero", look="red")
```

It is bound with weight 1 to the topmost skin joint (the first `skin` joint with no `skin` joint above it, usually the root) and is a `lookRoot` member like the others. So it follows the rig, the next look swap deletes it, and you can paint it in the built rig. The next weight save writes its first file, and after that it is an ordinary look mesh. mayadev's `apply_look` passes every marked mesh with no file this way.

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
- **A file that can't load on the changed skeleton** (it names a joint that is no longer marked, or the topology changed): with `ignoreSkeletonHash=True` the mesh is not dropped. It is bound whole to the topmost skin joint like a `rigidMesh`, with a warning, so it stays in the look to be skinned again. Save the weights afterwards to replace the old file. Without the flag it is an error.
- **A file with no hash** (made before D54, or on joints no rig drove): loads with a warning, because it can't be checked.
- **With `skeleton` and `rigSkeleton`** (the look's and the rig's `skeleton.json`), `setRigLook` compares them first and names every joint that differs (`[Center, b]: a different rest transform`).

## Not here yet

The face's blend shape wiring is a later phase of D54. A look can span a rig and its attachment ([attachments.md](attachments.md)): `setRigLook` still works on one rig root, and mayadev's `apply_look` gives each rig the files saved from its own skeleton. Listing looks that aren't synced is built on the mayadev side (phase 6): `Asset.all_looks()` asks source control for the look folders and `apply_look` syncs one before it calls `setRigLook`, so `setRigLook` always gets files that are on disk. It needs nothing from RigModules.
