# The Asset Editor: one dockable context for asset files, markup and shapes

**Status:** Decided (D61). Signed off by the author on 2026-11-19, with the answers in section 9. Phases 1 to 3 are built on mayadev branch `asset-editor` (four commits, not merged), with the differences in section 10. The New menu of phase 4 was built in phase 1. More file types (textures, shaders, tracked files) are not built. The code is in mayadev (D24); this page is its design history (D49). Changes to mayadev need a commit there.

## 1. The problem it solves

Author's notes from users (2026-11-19):

- Saving meshes and skinning, loading meshes, saving skeletons: each is a separate menu tool that asks the same questions.
- Marking up joints needs scripting.
- The asset id is typed at every tool. It should be a context the user works in, taken from the active rig or set once.
- Changing a control shape, or the shapes of many controls at once, needs scripting.

The Asset Editor is the context in which a modeler or a rigger sets the asset they work on, and creates, saves and loads that asset's files. Modelers use it for looks, models and textures. Riggers use it for skeletons, rigs, weights, controls and markup. Template authoring is a different tool (a rig template builder) and is out of scope.

## 2. Rules kept

The rules in `mayadev-ui.md` section 1 hold: no `.ui` files, model / view-model / view, no pipeline function tied to a button, `core` > `maya` > `maya_ui`, Qt dialogs only. The window follows the active rig panel (D50) and the library browser (D51).

## 3. The asset context

`AssetContext` (mayadev `bmaya.core.asset.context`, no Maya) holds the asset id and the chosen skeleton, rig and look, and has a `changed` event. `AssetContext.current()` is `None` unless the editor is open.

- It is set when the editor opens: the active rig's asset, else the last asset the editor used (an option value, for prefill only), else empty.
- It changes only by an explicit action: the picker, `Use active rig`, or switching to a newly created asset. File new, open and save never change it.
- When the active rig belongs to another asset, the header says so and offers `Switch`. It never follows.
- A rig-level save (rig, control shapes, weights) needs the chosen rig of the chosen asset in the scene. A rig of another asset does not count: the row is blocked and says why.
- Closing the editor closes the session: every callback the session registered is removed, it stops following the tracker and the context is cleared. The same happens on `destroyed` (a docked widget often gets no `closeEvent`) and on Maya closing.

Callbacks while open, all keyed by the session:

| Callback | Used for |
|---|---|
| Selection changed | The markup and shape models, only while their tab shows (as the rig panel does for spaces) |
| File new, open | Scene-derived state only: the rigs in the scene, scene meshes, file status |
| Maya closing | Closing the session |

## 4. Layers

```
core/asset/context.py      AssetContext
core/asset/catalog.py      the asset ids of every project under the projects root
core/shapemath.py          shape matrix <-> translate, rotate, scale (pure)
maya/asseteditor.py        AssetEditorSession: the context, the callbacks, the commands
maya/assetfiles.py         AssetFilesModel: one row per file type
maya/markupmodel.py        MarkupModel: markup of the selected joints and controls
maya/shapemodel.py         ShapeModel: shape fields of the selected controls
maya_ui/asseteditor.py     the dockable window, composed of the widgets below
maya_ui/widgets.py         AssetPicker, FileStatusTable, MixedFieldForm (new)
```

## 5. The Files tab

One row per file type of the chosen skeleton, rig and look: skeleton, model, skin weights, rig, control shapes, template, looks. Each row shows whether the file exists, when it changed, how many local versions exist, and has Save and Load. The rows come from the project's `fileTypes` and the handler registry: a new file type in `project.json` with a registered handler (textures, shaders and tracked high-res files, phase 4) gets a row with no window change. What Save needs from the scene (the selected joints or meshes, the rig) is a table in `maya/assetfiles.py`, data and not branches.

Every save goes through `assethandlers`, which keeps the local version copy (`versions.version`), and asks to replace first. Build Rig is on this tab.

## 6. The Markup tab

Shows the markup of the selected joints (`usage`, `side`, `export`, `skin`) or rig controls (`side`, `usage`). A field the selection disagrees on shows `Mixed`. Editing a field applies it to every selected joint through `rigJointMarkup(edit=True)` in one undo chunk. Unmarked joints are added, and `Remove` removes markup. Control markup is read only, because the build owns it and D25 makes it unique per rig. Mirror Joints and Add Twist Joints are buttons over the existing functions.

## 7. The Shapes tab

Shows `shapeType`, `filled`, `wireframeAlpha`, and translate, rotate and scale taken from `shapeTransform`, for the selected controls, with `Mixed`. Two modes: *Set* writes an absolute value to every selected control, *Adjust* multiplies (scale) or adds (translate, rotate, alpha) to each control's own value. Mirror Left to Right and Right to Left, Save Shapes and Load Shapes are buttons over the existing commands. The attributes are plain attributes on `rigControlNode`, so the edits are `setAttr` inside one undo chunk and RigModules changes nothing.

## 8. Maintenance surface, cost and what was rejected

Adds one catalog, one context class, three view-models, three widgets and a registry entry, all in mayadev. RigModules has no C++ or command change, no D36 impact.

Rejected: role presets for modelers and riggers (one more concept, tabs already separate the work), a lazy tree over the projects root (the registry scan already lists assets, a searchable combo is enough), editable control markup (the build owns it), template hints about missing usages (belongs with the template tool, deferred).

## 9. Answered (author, 2026-11-19)

1. Menu tools use the editor's asset when it is open, but confirm it first, so the asset a tool writes to is on screen.
2. The last asset is remembered for prefill only.
3. The asset list is local assets only.
4. Control markup is show only.
5. Template hints are deferred.
6. Shapes have relative edits as well as absolute ones.

## 10. As built

Phases 1 to 3 are built in mayadev on branch `asset-editor`: the context, picker and Files tab, the Markup tab, the Shapes tab, and a docs commit.

Differences from the text above:

- The picker is one searchable combo of every asset id, not project then asset. `core/asset/catalog.py` finds projects and the registry lists their assets.
- A rig-level save with the wrong asset's rig in the scene is blocked, not confirmed (section 3).
- The last asset is read after the active rig's, not before (section 3).
- The widgets are in `maya_ui/assetwidgets.py`, not `widgets.py`. `core/fieldset.py` (a field's value over a selection) and `core/shapemath.py` (the matrix as translate, rotate, scale) are new, and are shared or pure.
- The New menu (project, asset, rig, look) was built in phase 1 over the existing `*_interactive` tools, which now return what they made.
- Add Markup on several unmarked joints needs **Number them** (usages `usage_00`, `usage_01`, as Add Twist Joints names them), because two joints may not share a side and usage. Setting a field edits joints that have markup only.
- Adjust adds to each Euler angle instead of composing rotations.

Verified: the plain suites, the new mayapy session tests and the offscreen window tests pass. mayadev's older mayapy asset tests have the same 1 failure and 165 errors on a clean `main` as on the branch (they use pre-D58 joint and module names), so they were no check. Not verified: the window docked in a GUI Maya (the `addAction(text)` gotcha of D51 is avoided by building actions explicitly, but only a GUI run proves it), and drawing in a viewport. Proposed test: a `tests/gui/asseteditor` check like the rig panel's.

Not built: textures, shaders and tracked high-res files (a `FileAction` and a handler each), and template hints (deferred to the template tool).
