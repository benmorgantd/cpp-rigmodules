# Skin weights

Skin weights are saved to one file per mesh and loaded back onto the same mesh, even after joints are renamed or the rig is rebuilt. The commands are `setRigMeshId`, `saveRigSkinWeights` and `loadRigSkinWeights` ([commands.md](commands.md#setrigmeshid)).

```python
cmds.setRigMeshId("hero:body_geo")      # once per mesh
cmds.saveRigSkinWeights("hero:body_geo", file="C:/assets/hero/weights/body_geo.json")
# ... rebuild the rig, re-import the mesh ...
cmds.loadRigSkinWeights("hero:body_geo", file="C:/assets/hero/weights/body_geo.json")
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
- The whole file and the match are checked first. Any error is reported and nothing changes. The load is one undo step.

## File format

```json
{
    "formatVersion": 1,
    "meshId": "C214154A-43E0-A9E2-3CAA-22B52F9603E1",
    "meshName": "box_geo",
    "vertexCount": 8,
    "faceCount": 6,
    "topologyHash": "97334e59ca3cfcab",
    "skinMethod": "Linear",
    "influences": [
        ["Center", "a"],
        ["Center", "b"]
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
| `formatVersion` | `1` |
| `meshId` | The mesh's `meshId` |
| `meshName` | The mesh's name without namespace, for people reading the file. Loading never matches on it. |
| `vertexCount`, `faceCount` | The mesh's counts |
| `topologyHash` | 16 lowercase hex digits: 64-bit FNV-1a over the vertex count, the face count, each face's vertex count and each face's vertex indices, each as 4 little-endian bytes |
| `skinMethod` | `Linear`, `DualQuaternion` or `WeightBlended` |
| `influences` | `[side, usage]` per influence, sorted by side, then usage. A weight's influence index points into this list. |
| `weights` | One list per vertex of influence index and weight pairs, in index order. Zero weights are left out. Each vertex must sum to 1. |
| `blendWeights` | `WeightBlended` only: one number per vertex, one per line |

Every key is required except `blendWeights`, and unknown keys are errors.
