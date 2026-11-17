# Attachments in mayadev (D55 follow-up, phase 7b)

Status: approved by the author 2026-11-17, including the trimmed model import per rig for spanning looks (section 5). Not built. The handoff for the build is `attachments-mayadev-handoff.md`. RigModules already builds an attachment (`attachments.md`, `docs/attachments.md`). This page is the mayadev side: the file model, the build tool, and looks that span a rig and its attachment. mayadev code is read from `C:\Users\bmorgan\Documents\dev\maya\mayadev`. Edits there need the author's go-ahead, and go on a mayadev branch stacked on `rigs-and-looks-phase5`.

## 1. What mayadev has to do

1. Remember what a rig attaches to, so a face rig on disk says "I follow the body rig".
2. Build the parent and the attachment together, and pass `parentRig`.
3. Put looks on both rigs, when one look covers both skeletons.

Nothing else changes: poses, animation, shapes, skeleton files and the library are per rig already (an attachment is a rig).

## 2. `rig.json`: an optional `parent`

```
rigs/face/rig.json   {"formatVersion": 1, "skeleton": "face", "parent": "body"}
rigs/prop/rig.json   {"formatVersion": 1, "skeleton": "prop", "parent": {"asset": "shooter\\characters\\hero", "rig": "body"}}
```

- `RigRecord.parent: RigRef | None`, the same `RigRef` that `source` uses (a plain string in the same asset, two fields in another). Read and written by `records.read_rig` and `write_rig`, and `parent` joins the optional keys.
- **No format version bump.** The key is optional and additive, so every existing file reads as before. A D33 step would add nothing. (The proposal in `attachments.md` section 3.6 said a step. This is why it isn't needed.)
- `Asset.add_rig(name, skeleton, source=None, parent=None)`. `Asset.parent_of(rig)` returns the `(Asset, rig name)` pair, as `skeleton_of` does.
- **Checks when a rig is added:** the parent can't be the rig itself, and following parents must end (a rig can't be its own ancestor, across assets too). The parent needs to exist when the rig is built, not when it is added, so a face can be added before the body is saved, as a skeleton can.
- A rig can be both a copy (`source`) and an attachment (`parent`): they are separate questions.

## 3. `new_rig`

`new_rig(asset_id, template_path, rig, skeleton, skeleton_asset, source, parent)` takes a `parent` (`RigRef`). `New Rig...` asks "Attaches to a rig?" with the asset's rigs (and "another asset..."), default none. It does not read the template: the RigModules read phase checks that the template's `parent:` ids and the parent agree, and lists every problem at build time.

## 4. `build_rig`

`build_rig(asset_id, rig, look=None, namespace=None, parent_rig_root=None)`:

1. Read the rig's record. With no `parent`, nothing changes from today.
2. With a `parent`, find the parent's rig root:
   - `parent_rig_root` given: it must be a rig of that asset and rig name (`RigInfo.asset_id`, `rig_name`), else an error.
   - Not given: look in the scene for rigs with that asset id and rig name. One is used. **None: the parent is built first**, with its default look, by a recursive `build_rig` (so a hat on a face on a body builds all three). Several: an error that names them and asks for `parent_rig_root`.
3. Build the attachment as today, adding `parentRig=<root>` to `createRigFromTemplate`. The default namespace is `{asset name}_{rig}` as ever.
4. A failed attachment build leaves the parent built, since it is a valid rig on its own, and the error says so. The look, shapes and active-rig steps run per rig.

`build_rig_interactive` asks nothing new unless the parent is ambiguous. It selects the attachment's root.

## 5. Looks across a rig and its attachment

`look.json` already lists several skeletons, so one look (`default`) can cover the body and the face: one model file and one weights folder for the whole character.

The gap is `apply_look`. It imports the whole model into a rig's namespace and passes every weight file to `setRigLook`, and `setRigLook` refuses a file whose `skeletonHash` is another skeleton's. **Proposed fix, with no change to the folder layout:** `apply_look` keeps only what belongs to the rig.

1. List the look's weight files and read each one's `skeletonHash` (plain JSON, in `RigSkinWeightsHandler`).
2. A rig takes the files whose hash is its own `RigInfo.skeleton_hash`. A file with no hash belongs to the rig only when the look names one skeleton (an old file, as `loadRigSkinWeights` already treats it), and is an error for a look that spans several.
3. Import the model, keep the meshes whose `meshId` is in the rig's files, delete the rest, then call `setRigLook` with the kept meshes and files. A look with no file for the rig is an error that names the rig's skeleton.

`build_rig` applies the look to the parent and to the attachment, each through `apply_look`, so a character with a face is one `build_rig` call. The cost is one model import per rig that is then trimmed. The alternative, a model and a weights folder per skeleton (`looks/{look}/model/{skeleton}/`), makes the files match the rigs exactly but changes the layout and every look already saved.

## 6. What does not change

- `save_rig`, the library, poses and animation: per rig.
- The active rig panel: selecting a face control makes the face the active rig. The panel does not show the parent. That is a panel decision for later.
- Skeleton files, export and the registry: a rig's `parent` is not a publish dependency yet. A published face that needs a published body is the asset browser's problem (phase 8).

## 7. Tests (mayadev)

- `tests/asset`: `RigRecord` with a `parent` (string and cross-asset), round trip, a file with no `parent` reads as before, unknown keys still refused, the self-parent and cycle checks, `parent_of`, and `add_rig` writing the key.
- `tests/asset` (mayapy, needs the plugin): build a body and a face (the RigModules fixtures `attach_face` and `fkchain_three`, copied in), the face following the body, `parentRig` recorded in `RigInfo.parent_rig_root`, the parent built automatically, an existing parent reused, two candidates refused, a wrong `parent_rig_root` refused, and a failed attachment leaving the parent. A look that spans both skeletons: each rig gets its own meshes and weights, a file with no hash on a two-skeleton look refused, a look with no file for the attachment refused.
- The registered tool calls (`check_calls`) for the new `New Rig` form.

## 8. Docs (mayadev and here)

mayadev: `docs/assets.md` (the `parent` key, the build and the look rules), `docs/rigmodules.md` (`parentRig`, `RigInfo.parent_rig_root`), `docs/tools.md` (New Rig, Build Rig), `docs/status.md`. This repo: `docs/mayadev.md`, `design/attachments.md` (status), `design/rigs-and-looks.md` section 7 and phase 7, and `CLAUDE.md`'s sibling-repo paragraph.

## 9. Not covered

- A prop's space switch between two rigs that are not parent and attachment (`rigs-and-looks.md` 7.2): RigModules has no such driver yet.
- A panel for attachments (show the parent, build or detach from the panel).
- Publishing an attachment with a pinned parent publish.
