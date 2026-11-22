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
# Returns the report, JSON text: {"lookRoot": "hero_body:lookRoot", "skeletonStale": false, "meshes": [...]}
```

The command, as one undo step:

1. Matches each mesh to the weight file with its `meshId`. Order and names are never used.
2. Loads each file's weights onto the rig's joints ([skinning.md](skinning.md#loading)). A file that no longer fits is not an error: see [When things changed](#when-things-changed).
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

## When things changed

While an asset is in progress its skeleton, meshes and weights change independently, so a build never fails over them (D70): the build is how you repair the data, and a failed build leaves nothing in the scene to repair. Each mesh with a file takes the first rung of this ladder that fits, and the report says which:

| Rung | When | Result |
|---|---|---|
| `loaded` | The topology matches and every influence is a joint of the rig | The file's weights, as saved. A `skeletonHash` that is not the rig's does not matter: the file is only marked `staleSkeleton`. Adding a joint or moving a rest does not touch the weights |
| `folded` | The topology matches, and some influences are joints the rig no longer has (removed, renamed, unmarked) | Each missing influence's weight goes to its parent in the skeleton the file stored, and if the parent is gone or is not a `skin` joint, to the parent's parent, up to the first `skin` joint the rig has. Weights that land on one joint are added |
| `rigid` | The topology changed, an influence has no `skin` joint above it, or the file has no stored skeleton (saved before weight file version 3) | The mesh stays in the look, bound whole to the topmost skin joint like a `rigidMesh`, with a warning, to be skinned again |

A `rigidMesh` is reported as `rigid` with the reason "the mesh has no weight file yet".

The rig's joints must be one skeleton: one root joint, which need not be a skin joint. Two roots is an error (the fold and the weight file's `skeleton` need a hierarchy). When the root is not a skin joint, the rigid fallback binds to the first skin joint below it and warns; with no skin joint at all it fails.

`rigid` is a quiet failure: a mesh bound to the root exports skinned to the root. The report is how it shows, so a tool that calls `setRigLook` should show it, and saving the weights again replaces a stale or degraded file with one that fits the scene. The hash also does not gate the weight load commands: `loadRigSkinWeights` loads a stale file with a warning, and is exact about everything else.

With `skeleton` and `rigSkeleton` (the look's and the rig's `skeleton.json`), `setRigLook` compares them first, warns with every joint that differs (`[Center, b]: a different rest transform`) and sets `skeletonStale`.

## Not here yet

The face's blend shape wiring is a later phase of D54. A look can span a rig and its attachment ([attachments.md](attachments.md)): `setRigLook` still works on one rig root, and mayadev's `apply_look` gives each rig the files saved from its own skeleton. Listing looks that aren't synced is built on the mayadev side (phase 6): `Asset.all_looks()` asks source control for the look folders and `apply_look` syncs one before it calls `setRigLook`, so `setRigLook` always gets files that are on disk. It needs nothing from RigModules.
