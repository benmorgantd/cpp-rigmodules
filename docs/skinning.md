# Skin weights

Skin weights are saved to one file per mesh and loaded back onto the same mesh, even after joints are renamed or the rig is rebuilt. The commands are `setRigMeshId`, `saveRigSkinWeights` and `loadRigSkinWeights` ([commands.md](commands.md#setrigmeshid)).

```python
cmds.setRigMeshId("hero_body:body_geo")      # once per mesh
cmds.saveRigSkinWeights("hero_body:body_geo", file="C:/assets/hero/weights/body_geo.json")
# ... rebuild the rig, re-import the mesh ...
cmds.loadRigSkinWeights("hero_body:body_geo", file="C:/assets/hero/weights/body_geo.json")
```

## What a file matches on

A weight file loads only onto the mesh it was saved from:

- **Influences by joint markup.** Each influence is saved as its joint's `[side, usage]` markup ([rigJointMarkup](commands.md#rigjointmarkup)), never its name. On load, each one must match exactly one marked joint in the mesh's namespace. Other joints in the namespace are ignored.
- **The mesh by `meshId`.** `meshId` is a string attribute on the mesh transform holding a GUID, added by `setRigMeshId`. It survives import, referencing, renaming and reparenting. Duplicating a mesh copies it, so give the copy a new id with `setRigMeshId(copy, newId=True)`. Saving fails while two meshes in one namespace share an id.
- **The topology by hash.** The file stores the vertex count, the face count and a hash of each face's vertex count and vertex indices in order. Changing the vertex order or the faces fails the load. Moving points doesn't, so a sculpt or proportion change keeps its weights.

Every influence must be a joint with markup and `skin: true`, or saving fails.

## Weights are never changed

Saving and loading copy weights exactly: a weight that goes through a file comes back as the same number. Neither command rounds, prunes or normalizes:

- **Unnormalized weights are an error.** If any vertex's weights don't sum to 1 (within 0.00001), saving fails and names the vertices and their sums. Fix them in Maya (Normalize Weights, or Prune Small Weights, which also normalizes), then save again. A file with an unnormalized vertex fails to load the same way.
- **Pruning is a separate step.** Drop small weights with Maya's Prune Small Weights, and limit influences per vertex with [`pruneToMaxInfluences`](commands.md#prunetomaxinfluences), before saving. The save itself never does either.
- **Smoothing is a separate step too.** [`smoothSkinWeights`](commands.md#smoothskinweights) averages vertices with their neighbors. It respects locked influences and the skinCluster's maximum influences.
- **Painting is a context.** [`skinBrushCtx`](commands.md#skinbrushctx) smooths the weights under a brush as you drag over the selected meshes: the radius is a length over the surface, one stroke is one undo step, and `skinBrushStroke` repeats a stroke from a script (D65). Besides `smooth`, the brush has two rest-match operations, `restDelta` and `restEdge`, that paint the weights in a pose towards the rest shape, like a delta mush (D66, [details](commands.md#painting-towards-the-rest-shape)). How a stroke works, step by step, is in [skin-brush.md](skin-brush.md).

## Clean diffs

The file is laid out so that version control shows only what changed:

- One line per vertex, in vertex order, so changing a vertex's weights changes one line.
- Influences sorted by side, then usage, so the skinCluster's own influence order (which changes when influences are added or the mesh is rebound) never shows up. Adding or removing an influence renumbers the influences after it, so that change touches more lines.
- Each weight is written as the shortest number that reads back exactly, so a weight that didn't change is written the same way every time.

## Loading

- **An existing skinCluster is updated in place,** so it keeps its place in the deformer stack (after a blendShape, say). Influences the file doesn't list are removed, missing ones are added, and every vertex gets exactly the file's weights.
- **A mesh without one gets a new skinCluster** with the file's influences.
- **New influences bind at rest.** For a joint a rig module drives, that is the pose the rig was built in, whatever the rig's current pose, so loading onto a posed rig gives the same result as loading at rest. For a joint with no rig, the skeleton's current pose is its rest.
- The file's `skinMethod` is set on the skinCluster.
- **The skeleton is a status, not a gate** (D54, D70). A file whose `skeletonHash` is not the hash of the rig that drives the influences was made for another skeleton, and loads with a warning: what the load needs is the topology and every influence to resolve to a marked joint. Save the weights again to clear the warning. A file with no hash loads with a warning too, and joints no rig drives are not checked. This command is exact: an influence with no joint is an error. [`setRigLook`](looks.md#when-things-changed) loads a look's files with fallbacks (a joint that is gone folds into the next skin joint up) and reports what it did.
- The whole file and the match are checked first. Any error is reported and nothing changes. The load is one undo step.

## File format

```json
{
    "formatVersion": 3,
    "meshId": "C214154A-43E0-A9E2-3CAA-22B52F9603E1",
    "meshName": "box_geo",
    "vertexCount": 8,
    "faceCount": 6,
    "topologyHash": "97334e59ca3cfcab",
    "skeletonHash": "",
    "skinMethod": "Linear",
    "influences": [
        ["Center", "a"],
        ["Center", "b"]
    ],
    "skeleton": [
        [["Center", "a"], null],
        [["Center", "b"], ["Center", "a"]]
    ],
    "weights": [
        [0, 1],
        [0, 1],
        [0, 0.25, 1, 0.75],
        [0, 0.25, 1, 0.75],
        [0, 0.25, 1, 0.75],
        [0, 0.25, 1, 0.75],
        [0, 1],
        [0, 1]
    ]
}
```

This is a 2-unit-tall cube standing on the origin, skinned to joints marked `[Center, a]` at the bottom and `[Center, b]` 2 units up: its bottom four vertices follow `a`, and its top four are 0.25 `a` and 0.75 `b`.

| Key | Meaning |
|---|---|
| `formatVersion` | `3`. A version 1 file has no `skeletonHash` and loads as one with `""`. A file before version 3 has no `skeleton` and loads with `[]` |
| `meshId` | The mesh's `meshId` |
| `meshName` | The mesh's name without namespace, for people reading the file. Loading never matches on it. |
| `vertexCount`, `faceCount` | The mesh's counts |
| `topologyHash` | 16 lowercase hex digits: 64-bit FNV-1a over the vertex count, the face count, each face's vertex count and each face's vertex indices, each as 4 little-endian bytes |
| `skeletonHash` | The `skeletonHash` of the rig root that drives the influences, at save: the skeleton the weights were made on (D54). `""` when the influences belong to no rig, as in this example. A load compares it with the rig the influences belong to (see Loading) |
| `skinMethod` | `Linear`, `DualQuaternion` or `WeightBlended` |
| `influences` | `[side, usage]` per influence, sorted by side, then usage. A weight's influence index points into this list. |
| `skeleton` | The structure of the skeleton the weights were made on (D70). Saving fails when the namespace's joints have more than one root, because a skeleton has one root joint: every marked joint of the namespace as `[[side, usage], parent]`, with `null` for a root, sorted by side, then usage. No rests and no names. It is what lets a joint that is gone give its weight to its parent when `setRigLook` folds it. `[]` for a file saved before version 3 and for a rigid bind, which can't fold |
| `weights` | One list per vertex of influence index and weight pairs, in index order. Zero weights are left out. Each vertex must sum to 1. |
| `blendWeights` | `WeightBlended` only: one number per vertex, one per line |

Every key is required except `blendWeights`, and unknown keys are errors.
