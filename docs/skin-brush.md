# Skin weight brushes

Technical reference for the skin weight brushes: how a drag over a mesh becomes a change in a skinCluster's weights, the three operations (`smooth`, `restDelta`, `restEdge`), the limits, and how to add another brush. For flags and examples, see [`skinBrushCtx`](commands.md#skinbrushctx) and [`skinBrushStroke`](commands.md#skinbrushstroke). Decisions and their reasons: D64 (smoothing), D65 (the brush), D66 (rest-match).

## What it is

One context, `skinBrushCtx`, paints on the selected skinned meshes. What a dab does to the weights is a plug-in **operation**, chosen by the `operation` setting:

| Operation | A dab moves each vertex's weights towards | Use it to |
|---|---|---|
| `smooth` | the average of itself and its neighbors | Even out noisy or uneven weights |
| `restDelta` | the blend of influences that puts the vertex where the rest shape's delta says it belongs, given where its neighbors are in the current pose | Restore local shape and volume in a pose, like a delta mush, painted |
| `restEdge` | the blend that returns each edge to its rest length | Fix stretched or squashed edges in a pose |

Picking, falloff, the visualizer, per-stroke undo and both commands are shared by every operation.

## Layers

```
contexts/SkinBrushContext   events, drawing, the repeat timer;   skinBrushCtx, skinBrushStroke commands
        |  hits, radius, strength, smoothness, falloff
shared/SkinBrushStroke      SkinBrushStroke: pick, reach, dab, journal, finish
                            SkinBrushOperation: what a dab does (smooth, restDelta, restEdge)
                            SkinBrushEdit: a finished stroke, for undo and redo
        |  vertices, per-vertex scales
shared/SkinWeightScene      SkinWeightPass -> SkinWeightSmooth, SkinWeightRestMatch; SkinWeightRows
        |  weight and neighbor callbacks
core/                       SurfaceBrush, SkinWeightSmooth, SkinWeightRestMatch   (no Maya)
```

| Layer | File | Holds |
|---|---|---|
| core | `core/SurfaceBrush` | `SurfaceDistance` (shortest paths over the mesh edges), `volumeReach`, `brushFalloff`, `FalloffMode`, `StrokeJournal` |
| core | `core/SkinWeightSmooth` | `MeshAdjacency` and the neighbor-average pass. `SkinSmoothSettings::vertexScale` is the per-vertex factor a brush uses |
| core | `core/SkinWeightRestMatch` | `matchRestWeights`: the rest-match solve (D66) |
| shared | `maya/shared/SkinWeightScene` | `SkinWeightPass` (binds the skinCluster, locks, max influences, weight fetch, write, undo), `SkinWeightSmooth`, `SkinWeightRestMatch`, `SkinWeightRows` (whole rows for a stroke's undo, and `isAlive`) |
| shared | `maya/shared/SkinBrushStroke` | The engine below the mouse. It knows nothing about events, so a script can drive it |
| contexts | `maya/contexts/SkinBrushContext` | `SkinBrushContext` (`MPxContext`), `SkinBrushCtxCmd` (`skinBrushCtx`), `SkinBrushStrokeCmd` (`skinBrushStroke`, an `MPxToolCommand`) |

The context holds no weight logic. It turns events into hits and hands them to `SkinBrushStroke`. Maya creates no contexts in `mayapy`, so the tests drive the engine through `skinBrushStroke`.

## One stroke, step by step

1. **Tool on** (`toolOnSetup`, `doEnterRegion`): `gather()` reads the selection. Meshes with a skinCluster are kept; a selection of vertices or faces becomes a restriction. A key built from the operation, the mesh paths and a hash of the restricted vertices says whether anything changed. If not, `refresh()` only re-reads positions. If so, `SkinBrushStroke::begin` binds every mesh.
2. **`begin`** builds, once per selection: the `SkinBrushOperation` (which binds the skinCluster), the `MeshAdjacency` neighbor map, the deformed vertex positions, the ray accelerator, the `SurfaceDistance` field and an empty `StrokeJournal`. The default radius is 5% of the diagonal of the box around the meshes, until the user sets one.
3. **Press** (any button but the middle one; Ctrl switches to resizing): `refresh()` re-reads the positions, drops the accelerator and calls each operation's `refresh()` so the rest-match operations follow the pose. Then one dab, and the repeat timer starts.
4. **Pick**: `MFnMesh::closestIntersection` over every selected mesh, closest wins. With `throughMesh`, `allIntersections` returns every surface the ray crosses.
5. **Reach**: from the hit's face, for each mesh.
   - `surface`: Dijkstra over the mesh edges, seeded from the face's corners with their distance to the hit point, out to the radius. Two shells never share a dab, and nothing is reached across a gap.
   - `volume`: one pass over the vertex positions, a plain distance in space.
   - Each vertex's scale is `brushFalloff(distance / radius)`, a smoothstep: 1 at the center, 0 at the radius. Scales under 1e-3 are dropped, and so is anything outside a restriction.
6. **Dab**: for each mesh, `operation->prepare(vertices, scales, strength, smoothness, adjacency)` works the new rows out without writing them. If any vertex moved, `apply()` writes them with one `setWeights` call over only the influences that changed, and the stroke's journal records the first value of everything the dab changed. A mesh hit more than once in one dab (through a mesh) is dabbed once, and the nearest hit sets each vertex's scale.
7. **Drag**: a dab lands every quarter of the brush radius along the pointer's path, measured on screen (at least 2 px). One drag event handles at most 24 dabs and spends at most 40 ms on them, then skips to the pointer, so a fast drag leaves a gap instead of lagging behind. After a drag or repeat dab that landed, the viewport refreshes at once.
8. **Hold**: an `MTimerMessage` callback ticks 20 times a second on the main thread. A tick dabs again at the last pointer position unless a drag dab landed since the previous tick. Only a dab that landed counts as movement, so the jitter of a held mouse does not stop the repeat. The timer stops at release, tool change and context deletion.
9. **Release**: `finish()` reads the final rows of every touched vertex, pairs them with the journal's rows from before the stroke, and returns a `SkinBrushEdit`. The context hands it to a new `skinBrushStroke` tool command and calls `finalize()`, which puts the stroke on the undo queue as **one step**.

### When the meshes leave the scene

The stroke keeps bindings to the meshes between events, and reading a deleted mesh crashes Maya. A new scene, a delete or an undo can remove a mesh or its skinCluster while the tool is active, with the pointer still in the viewport. So every event (press, drag, release, pointer move, entering the viewport) and every repeat tick first calls `checkStroke()`, which asks `SkinBrushStroke::isAlive()`: each shape and skinCluster still valid (`MObjectHandle::isValid`, so a node waiting on the undo queue counts as gone) with the vertex count it was bound with. A dead stroke is dropped: the timer stops, a stroke in progress ends without an undo step, and the next press binds the selection again.

### Undo

A dab writes as it goes, so the user sees it. Undo does not replay the dabs: `SkinBrushEdit` keeps whole rows (every influence) for every touched vertex, before and after the stroke, so undo and redo each write one `setWeights` per mesh and restore the weights exactly, however much the dabs overlapped. `StrokeJournal` records first-write-wins, so an overlapped vertex undoes to its true starting weights. An influence no dab changed on a vertex has no journal entry and takes its value from the final rows.

`SkinBrushEdit::dabs` keeps each dab as a mesh, face and point, which is what `skinBrushStroke(hitPoint=...)` takes, so a stroke can be repeated from a script.

## The operations

All three read the weights of the working set (the target vertices and their neighbors), take part only through the influences that carry weight there and are not locked, and respect the cluster's `maintainMaxInfluences`. A vertex's change is `strength * falloff` of the move from its old weights to the new ones. A dab is **one pass**: neighbors are read as the last pass left them (Jacobi), so vertex order changes nothing, and repetition (a drag, a hold) supplies the convergence.

### smooth (D64)

Each vertex becomes the average of itself and its neighbors. The vertex counts in its own average, because a plain neighbor average on a quad mesh, which is bipartite, swaps a checkerboard every pass and never damps it. The command `smoothSkinWeights` runs the same pass with a `vertexScale` of none.

### restDelta and restEdge (D66)

With linear blend skinning, a vertex's posed position is `P = sum(w_k * c_k)`, where `c_k` is where the vertex would be if it were bound to influence `k` alone (its rest point through `bindPreMatrix` and the joint matrix). `P` is linear in the weights, so each vertex is a small least-squares problem: find the weights, at least 0 and with the row's unlocked total fixed (a simplex), whose `P` is nearest a **target position**. `core/SkinWeightRestMatch` solves it by projected gradient with a weak pull (1e-3 of the spread of the candidates) towards the starting weights. The pull only decides between answers when more than three influences fit equally.

The two operations differ in the target:

- **`restDelta`**: the average of the neighbors' posed positions, plus the rest delta (rest position minus the average of the neighbors' rest positions) turned by the polar rotation of the skin's blended matrix at the vertex. This is what a delta mush keeps. Edge angles are inside the delta, so there is no angle operation.
- **`restEdge`**: the average, over the vertex's edges, of the point at the rest edge length from the neighbor along the edge's current direction (a distance constraint, as in position-based dynamics).

The pose of the working set comes from the fetched weights and the cached joint matrices, never from re-reading the deformed mesh. A dab therefore costs what a smooth dab costs, and the neighbors are current within a stroke.

**Smoothness** (0 to 1, default 0.15, ignored by `smooth`): after every vertex of a dab is solved, each change (solved weights minus starting weights) is blended with the average change of its neighbors (a neighbor the dab did not reach counts as no change). The result is projected back onto the simplex and scaled by strength and falloff. `restEdge` can crunch because each vertex chases a target that depends on neighbors that are moving too. Blending the changes, not the weights, keeps the fixed point: a pose that already fits is left alone at any setting.

**What it assumes**:

- The **rest shape** is the skinCluster's `input[0].inputGeometry`, taken to world space with `geomMatrix`.
- The skin is **classic linear** (`skinningMethod` 0). `SkinWeightRestMatch::bind` fails for anything else, and it compares the linear blend model with the deformed mesh on up to 2000 vertices (tolerance 1e-3 of the mesh's largest extent), failing if they differ. A deformer after the skinCluster or a bind that does not match the rest pose is an error, not a wrong pose.
- It **blends the influences the neighborhood already has**. It cannot fix a candy wrapper that needs a new influence.
- Weights fitted to one pose can hurt another. Paint in several poses.
- The pose is read when a stroke starts and when the pointer re-enters the viewport, so change the pose between strokes.

## Locked influences and limits

`SkinWeightPass::bindCluster` reads each joint's `lockInfluenceWeights` and the cluster's `maintainMaxInfluences` once, when the mesh is bound. A locked influence keeps its weight on every vertex, and the unlocked influences keep the total they had, so a row still adds up. A changed vertex keeps its largest `maxInfluences` weights (`keepLargestWeights`): locked ones count towards the limit, and unlocked ones keep at least one. Because they are read at bind, a lock changed while the tool is active is not seen until the selection changes and the meshes are bound again.

## Cost

- A dab costs the vertices in its reach, not the mesh. Nothing about topology is kept between strokes beyond the neighbor map built at `begin`.
- Only vertices that moved by more than 1e-9 are written.
- No threading. Maya's `getWeights` and `setWeights` are not thread-safe and are most of the cost. On a 50k-vertex mesh, D64 measured about 19 ms per one-vertex or dab call, almost all of it Maya re-evaluating the skinned mesh after the weight edit, so the brush rate follows Maya's skin evaluation (a ceiling near 40 fps against a 15 fps target). Not measured in a real brush session.
- The deformed positions are read at press and when the pointer re-enters the viewport. A mesh that deforms during a stroke (because the weights change the skin) is not re-read within the stroke.

## Drawing

The ring and the reach are drawn in `drawFeedback` only, as Maya's own brush contexts do: event handlers change state and ask for a redraw, so nothing drawn is left behind. The ring is at the hit; one point per reached vertex is coloured by falloff (blue at the edge, yellow at the center) and drawn in one `MUIDrawManager::mesh` call with a depth priority that keeps it on top. The reach shown is the reach the next dab uses. The ring and reach keep the last surface they were on when the pointer leaves the mesh mid-stroke.

## Scripting and tests

`skinBrushStroke` replays a stroke from `hitPoint` and `viewRay` flags and is undoable, which is how the engine is tested. Flag names are not `hit` or `ray`, because Maya drops a three-letter long flag name without an error. `debug=True` on the context prints a line for the press, every repeat tick and the release.

| Test | Covers |
|---|---|
| `tests/maya/commands/test_skin_brush.py` | The engine on a 5 x 5 plane: falloff over the surface worked by hand, a shell close in space but not connected, closest-surface and through-mesh picking, multi-mesh strokes, one undo step. The `skinBrushCtx` settings class is skipped in batch and runs in a GUI Maya. Then `restDelta` and `restEdge` (`RestMatchCases`): a stretched vertex's weights worked by hand, nothing moves at rest or under a rigid move, strength, edge error falling, locks, max influences, undo, the dual quaternion refusal |
| `tests/maya/commands/test_smooth_skin_weights.py` | The smoothing pass the brush reuses |

Not tested automatically: mouse events, drawing, Ctrl + drag, the repeat timer, a stroke dropped when its meshes leave the scene, the selection restriction, and the feel of the brushes in a GUI Maya.

## Adding a brush

1. Write a `SkinBrushOperation` subclass in `maya/shared/SkinBrushStroke.cpp`: `bind` (find the skinCluster, read what you keep), `prepare` (work out the rows for the sorted, unique vertices and their scales, and keep them as a `SmoothedWeights`), `apply` (write them) and `edit` (return them). Override `refresh` if you cache anything about the pose.
2. Put the math in `core/` with weight and neighbor callbacks, so it has no Maya, as `SkinWeightSmooth` and `SkinWeightRestMatch` do. Derive the Maya side from `SkinWeightPass` to get binding, locks, max influences, write and undo.
3. Add the name to `createBrushOperation` and `brushOperationNames`. The picking, falloff, visualizer, journal, undo, `skinBrushCtx` and `skinBrushStroke` come with it, and no flag changes.
4. Add a test next to `RestMatchCases` in `test_skin_brush.py` with expectations worked by hand, and document the operation in [`commands.md`](commands.md#skinbrushctx) and here.
5. Decide whether an operation's setting is new. A setting only one operation uses (like `smoothness`) is ignored by the others, and is added to `prepare`, `SkinBrushStroke::dab`, both commands and the context.
