# The library browser

**Status:** Proposed (D51), written 2026-11-14 and revised the same day with the author's notes from past libraries and answers to the first round of questions (section 12). Signed off by the author the same day. Phase 1 (the RigModules side) is built, with the differences in section 12c. Phase 2 (mayadev, headless) is built, with the differences in section 12d. Phase 3, the window, is built, with the differences in section 12e. Publish to Approved and thumbnails (phases 4 and 5) are not. It is the second panel of phase 3 in `mayadev-ui.md` (section 5), after the active rig panel (`active-rig-panel.md`, D50). The code is in mayadev, plus a set of RigModules changes (section 8). Changes to mayadev need the author's go-ahead and a commit in that repo.

## 1. The problem it solves

The pose and animation library exists as files and menu tools. Save Pose, Load Pose, Save Animation and Load Animation each ask for a name or open a file dialog (`assettools.*_interactive`). That is fine for the tenth file and bad for the three hundredth. The animator can't see what the library holds, can't find a pose by anything but its name, can't tell which poses the art director signed off, and can't apply one to only a hand.

The author's experience from earlier libraries (2026-11-14) sets the bar. Animators made their own "fast and simple" libraries of control values when the "robust but slow" way (baking controls to joints) didn't suit them, because that way was built for gameplay animation, not cinematic. This library is control data (D42), which is the fast and simple kind, and it has to be good enough that nobody builds a private one.

| Note from past libraries | Design response |
|---|---|
| Simple, fast, searchable, clear results | One window. A name and tag search over an index read once and cached (section 5). Apply is one click and one undo step |
| Accurate | Values stored as written, no baking, no joint round trip. Round trip tests for every kind of control (section 4) |
| Support all types of control | The format is by markup and channel, for every keyable writable channel (D34). A contract test saves and loads every module's controls (section 4) |
| Pretty, professional, thumbnails from playblasts | The shared theme and icons now. Thumbnails and a grid view are phase 5, and the model has a place for them (section 9). Not in the first build (author) |
| Saving a pose is a no-brainer | One button, one field, Enter. It goes to the user's own library with no permission and no dialog chain (section 7) |
| Approved, Shared, User, and per-shot | Library sources (section 3) |
| Search, different views | Section 5 |
| Apply to selection, a module, or a mask, without name filters | Scope: selection, whole rig or a mask, all by control markup (section 6) |
| Tags and metadata in search (template, skeleton, ...) | The files carry a `meta` block (section 4) and search reads it |

### What it does not do

- **No thumbnails in the first build** (author). A pose file has nowhere to store an image, and a capture needs a GUI session. Phase 5.
- **No joint data.** `exports/` and `sources/` hold FBX. Reopening an export or importing a source is the clip tools' job (`clips.md`), and the restore from joints has its own options. The browser lists control data only (`control-library.md`, section 2).
- **No version history UI.** `versions.version` keeps the old file when an entry is overwritten. Restoring one stays a file operation.
- **No remap table.** Markup is the only mapping (`control-library.md`, section 5).

## 2. Layers

```
bmaya.core.asset.library     sources, listing, the index, search, masks files       plain Python, no Maya
bmaya.maya.librarybrowser    LibrarySession: the view-model                         maya, rigmodules
bmaya.maya.posetools         plain functions with explicit arguments (exist)
bmaya.maya_ui.widgets        ListModel: a QAbstractListModel over a list of dataclasses
bmaya.maya_ui.librarybrowser LibraryBrowser: the Qt view, and show_window()         PySide6, librarybrowser
bmaya.maya_ui.host           show(..., dock=True): exists
```

The view-model imports no Qt and the view imports PySide6 and `bmaya.maya.librarybrowser` only (`test_core_layers.py` checks both). Everything that is only files and strings (sources, listing, search, the masks file) is in `core`, so it runs in plain Python and in the batch framework. `LibrarySession` is in `bmaya.maya` because it reads the scene and calls RigModules.

`ListModel` is built here, as `mayadev-ui.md` section 3.6 planned: a thin `QAbstractListModel` over a Python list, one callable per role, `set_items` resets. It holds no library logic.

## 3. Library sources: Approved, Shared, User

A source is a folder pair (`controlposes/` and `controlanims/`) with a label and a write rule. The list shows entries from every source with a source column, and a filter chip per source.

| Source | Folder | Who writes it | What it is for |
|---|---|---|---|
| **Approved** | The asset's own `controlposes/` and `controlanims/` (in source control, as today) | The art director, or whoever they allow, through source control permissions | Poses and animations signed off for this character: the standard looks. The art director owns the bar, the browser doesn't enforce it |
| **Shared** | The skeleton asset's folders, when `asset.skeleton != asset.id` (`asset.py`: assets with the same skeleton can share). Later also a folder per template, for "every rig of this type" | Whoever owns that asset | Poses that work on any rig of that skeleton or template |
| **User** | `<projects root>\user\<login>\animlibrary\<asset id>\controlposes` and `controlanims` (author, 2026-11-14, with the login added; the projects root is `RIGGING_PROJECT_DIR`, `projects_root()` in `project.py`) | The animator, no source control, no permission | Work in progress and personal poses. **Where Save goes by default** (section 7) |

Saving is always to User. An entry reaches Approved by a **Publish to Approved** action: it copies the file into the asset's folder through `sourcecontrol` (check out, add) and leaves the User copy. That is phase 4, and it is the one place the browser relies on source control permissions. Entries from Approved and Shared can be applied, searched and copied to User, but not renamed or deleted here.

**Per-shot and cinematic sets.** The author's idea, and a good one: a set of poses for one shot or sequence. The asset model has no shot concept yet (`asset-model.md`), so the source list is data (a label and a folder), and a shot source is one more entry once shots exist. Not built.

**Per face, per race, per rig type.** These are not folders. A rig type is the template and a face or race is a tag, and both are searchable (section 5). A folder per kind would make the same pose live in three places. The Shared source by template covers "all rigs from this template".

The default of the Approved folder being the existing asset folders means files saved by the current tools appear there. Today's `save_pose` writes to the asset's folder, so the existing behaviour becomes "saves to User" and the menu tool changes with it (phase 3).

## 4. The data: what a file must carry

The files are the ControlPose (`PoseFile`, D34) and the ControlAnim (D42). Two changes:

**A `meta` block, formatVersion 2 of both** (D33 update tables: a version 1 file loads with empty metadata, so nothing stored breaks).

```json
"meta": {
    "assetId": "Characters|Hero",
    "variant": "default",
    "template": "Human",
    "skeletonId": "Biped",
    "pluginVersion": "0.9.0",
    "created": "2026-11-14T10:22:00Z",
    "author": "bmorgan",
    "tags": ["hands", "fist", "angry"],
    "category": "Expressions",
    "rigRegions": ["hands"],
    "status": "scratch",
    "description": "Clenched right hand, thumb over fingers"
}
```

RigModules fills the fields it knows from the rig root and its stamps (`getRigInfo`: asset id, variant, plugin version) and the template name. The caller passes `author`, `tags`, `description`, `category` (free text), `rigRegions` (a list from the project's rig regions: lower body, upper body, head, hands) and `status` (a label such as scratch, review or final). RigModules owns the format and the update steps, and it treats `tags` and `description` as plain data: it validates their shape (strings, a list of strings) and reads and writes them, never interprets them. Putting the metadata in the file, not a sidecar, means a pose that is moved, emailed or copied to a shared drive keeps its tags and its origin. Search reads it (question 5).

**Every control type.** The format already stores every keyable, writable channel by control markup (`side`, `usage`) and channel name (D34, D25), so IK/FK blend, `space`, stretch, Layout controls and a custom channel are all in it. The proof is tests, not argument: a contract-style test per module type saves a pose and an animation from a posed rig, zeroes the rig, loads, and compares every channel to the value that was saved, including enum channels and a mirror of each. A module added later is covered by `ModuleContract` the way its channels already are.

**File names.** A ControlAnim is `<name>.controlanim.json` (author). A ControlPose should match: `<name>.controlpose.json`, so a file says what it is outside its folder and a search of a drive finds the right ones (question 2). Existing `<name>.json` files are still read and are listed as legacy until renamed. The `<name>.anim.json` copy in an export folder (`control-library.md`, section 6) becomes `<name>.controlanim.json` too, so there is one name for the format. That changes `docs/library.md`, `control-library.md`, `docs/mayadev.md` and `animateRigFromJointClip`'s documented `sidecar` example, and mayadev's `ControlDataHandler.file_path` and its export folder code.

## 5. Search and views

**The index** is a list of entries: name, kind, source, path, modified time, and the `meta` fields (template, skeleton, asset, author, tags, description, created) plus, for an animation, the frame range and frame rate. Building it needs each file's contents, so it comes from one batch command (`getRigDataFileInfo`, section 8) over the files that changed since the last read. It is cached in memory by path and modified time. The first listing of a few hundred files is one command call, and later refreshes read only new or changed files. A Refresh button and the window's `showEvent` refresh it, and nothing watches the disk.

**Search** is one box. Free text matches name, tags and description (case-insensitive, every word must match). Prefixed terms match a field: `tag:fist`, `template:Human`, `skeleton:Biped`, `by:bmorgan`, `source:approved`, `kind:animation`, `region:hands` (matches `rigRegions`), `status:final`, `category:Expressions`. Facet chips under the box (source, kind, region, status, category, tags in use, template) set the same terms, so the animator never needs the syntax. The parser and the filter are in `core.asset.library`, plain Python with tests, and `LibrarySession.entries` is their result.

**Views** are a presentation of the same list: **List** (name, a kind icon, source) and **Details** (a table: name, tags, source, template, author, date, frames), both in the first build, and **Grid** (large thumbnails) with thumbnails in phase 5. The session doesn't know about views. A view is a `QListView` mode and a `QTableView` over the same `ListModel`.

**Fit.** The selected entry's details show how many of the file's controls this rig has, and a warning when some it lacks (`unmatchedControls`). It doesn't block the click, since an unmatched control is skipped with a warning already (D34).

## 6. Scope: apply to the selection, a module, or a mask

The animators' favorite, and what past libraries did badly with name filters. Everything here is by control markup, so it works on any rig of the template and never matches on text.

A **scope** for Apply is one of:

- **Selection**: the selected controls (the default when anything is selected).
- **Whole rig**: the active rig's controls (the default with nothing selected).
- **A mask**: a named filter that picks controls by markup.

A **mask** is a list of items `{module, side, usage}`, each field optional, the same filter `getRigControls` takes (`RigControlFilter`: `moduleType`, `side`, `usage` lists). The browser builds masks from the active rig, with no authoring needed: one per module type present ("Hands", an FkChain's module name; `Aim`), one per side (Left, Right, Center), and the modules themselves from `getRigControls`. A user can also save a named mask (a hand pair, "upper body", "face") as JSON beside the library (question 4). It resolves to a control list by `rigmodules.query.get_rig_controls(rig_root, module_type=, side=, usage=)` and, when a selection exists, the intersection with the selection: "mask Hands, selection narrows". The resolved list is passed to the commands as controls, which they already take as a scope (D34). No RigModules change.

Masks are the same idea as the selection sets of D39 (a top set, a set per side, a set per module, with `setMarkup`), as items by markup. The browser reads the rig's own sets when they exist and doesn't create sets for itself.

Apply works the same way for poses and animations, so a hand pose is a pose saved over a hand mask and applied over the same (the file holds only the controls it was saved from). Saving a pose from a mask or selection stores only those controls.

## 7. The view-model and the window

```python
class LibrarySession:
    changed: Event
    # state
    asset_id: str | None            # from RigInfo.asset_id of the active rig
    kind: str                       # "pose" or "animation"
    query: str                      # the search box
    sources: list[LibrarySource]    # with their on/off filter state
    entries: list[LibraryEntry]     # filtered and sorted
    selected: LibraryEntry | None
    masks: list[Mask]
    scope: str                      # "selection", "rig" or a mask name
    # commands
    refresh() -> None
    set_query(text) -> None
    select(entry) -> None
    apply(mirror=False, start_frame=None) -> list[str]
    save(name, tags=(), description="") -> LibraryEntry
    set_meta(entry, tags=None, description=None) -> None
    rename(entry, name) -> LibraryEntry
    delete(entry) -> None
    publish(entry) -> LibraryEntry      # phase 4
```

It follows the active rig through `ActiveRigTracker.changed` (D50). With no rig, or a rig whose asset is not on disk, the library is the User folder of that rig's asset id when the id is valid and empty with a reason otherwise. Errors never raise into Qt: the session raises, the view catches, logs through `bmaya.core.log` and shows a one-line status.

**Apply.**

- **Pose:** `loadRigPose(controls, file=, mirror=)`. **Animation:** `loadRigAnimation(controls, file=, offset=0, mirror=)`: the keys land on the frames they were saved on, **the animation's own start** (author, not the current frame). A field "Start at" overrides it, with a button that fills the current frame. A frame-rate mismatch is refused by the command, and the panel says so in the details before the click.
- **Mirror is data first** (author). The file's content is mirrored before it is written: the entries move to their mirror controls and the negated channels flip sign. `loadRigAnimation(mirror=True)` already does it that way (`mirrorRigKeys` takes keys in and gives keys out, then `setRigKeys` writes them). `loadRigPose` gets the same, a `mirror` flag over a data transform of the `PoseMap` (section 8). That replaces my earlier plan to load and then flip the rig, which writes the scene twice and flips what the animator had.
- One undo chunk per apply. Nothing is applied by hover, single click or arrow keys, only by double click, Enter or the Apply button.

**Pre-computing mirrors.** A mirror depends on the rig (a Center control's flip axis comes from its rest orientation, a channel's mirror mode from the rig's channel definitions), so it is the pair (file, rig) and can't be made once for the file. Computing it ahead is possible, but not on a background thread: `maya.cmds` and the API are not thread safe, so it would run on the main thread in idle time (`maya.utils.executeDeferred`), and it's cheap. The transform is an in-memory map or key list, tens of controls and a few thousand keys at most, so the cost is a command call and a mirror, in the milliseconds. So the first build doesn't precompute. The session keeps a small cache of the last mirrored result for the selected entry, keyed by (path, modified time, rig root), so a repeat Apply with Mirror on the same entry doesn't redo it. If a real library shows a lag, a deferred precompute of the selected entry is the next step and needs no format change.

**Save is a no-brainer.** The Save button, with an entry focused in the name field, which is already filled with a free name (`pose_001`, then `pose_002`, by what the User folder holds). Enter saves, and nothing else is asked: the scope is the current one (selection or mask, else the rig), the kind is the open tab, an animation takes the playback range, and the meta fills itself. A "Tags" field beside it (comma-separated, optional, remembering the tags in use as a completer) sets `tags`. An existing name asks to replace through the `Prompter`. Save goes through `assettools`' save functions with the User folder as the target, so versioning, source control and the clip name rule are the existing ones. Tags and the description can be edited later on a selected entry (`set_meta`, question 6).

**The window.** One docked widget (`host.show(..., dock=True)`), a second tool beside Active Rig Panel under the RigModules menu's `panels` submenu. On the shared theme (`theme.py`):

```
Human                                                [Refresh]
[ Poses | Animations ]   [ Search name, tag, template ........ ]
 Source: [Approved] [Shared] [User]    View: [List|Details]
+---------------------------------------------------------------+
| fist_angry        hands, fist     User       2026-11-14        |
| idle_a            idle            Approved   2026-11-10        |
| t_pose            reference       Shared     2026-11-03        |
+---------------------------------------------------------------+
 42 controls, 318 channels, 0 unmatched. Template Human. By bmorgan.
 Scope: [ Selection v ]  [x] Mirror   Start at [ own start ]
 [ Apply ]                [ Name ________ Tags ______ ] [ Save ]
```

Rename, Delete, Publish and Edit Tags are in the entry's context menu, disabled for entries the user can't change.

## 8. RigModules changes

All read or write the data files and the format, which belong on this side of the boundary (D24, the placement rule).

| Change | Why |
|---|---|
| **`meta` in both formats (version 2)**, update steps for version 1 (D33), `saveRigPose` and `saveRigAnimation` flags `tags` (a list), `description`, `category`, `rigRegions` (a list), `status` and `author` (strings the caller passes), and RigModules writes `assetId`, `variant`, `template`, `skeletonId`, `pluginVersion` and `created` | Metadata search, section 5. The format stays in one place |
| **`getRigDataFileInfo(rigRoot, *files)`** (batch) | The index. Per file: `kind`, `formatVersion`, `meta`, `frameRate`, `startFrame`, `endFrame` (animation), `controlCount`, `channelCount`, `unmatchedControls` for the rig. It writes nothing and reports a bad file as an error entry, not a failure of the batch. Wrapped as `rigmodules.query.get_data_file_info` and `DataFileInfo` |
| **`setRigDataFileMeta(file, tags=, description=, category=, rigRegions=, status=)`** | Edit tags later without loading the file onto a rig. Rewrites the file's `meta` and nothing else. The caller versions and checks out first (as every save does) |
| **`loadRigPose(..., mirror=True)`** | Data-first mirror for poses, as `loadRigAnimation` has. A data transform in `RigPoseScene` that shares its channel rules with `flipRigPose` and `mirrorRigKeys` (`isNegatedByFlip`, `getFlipAxis`), so the three can't drift |
| **File names** | `.controlanim.json` and `.controlpose.json` (section 4), the docs, and the examples |

New surface: two commands, one flag, and `meta` keys. No node, attribute or name changes, so no pose or animation breaks, and nothing touches `compute()` (D36 doesn't apply). Tests: commands tier for each command and flag (a version 1 file loads, a version 2 round trip keeps the meta, a bad tag type is refused, a mirror on load equals a flip of the same pose, a file with controls the rig lacks, a broken file, a missing file), the contract test for every control type (section 4), a worked example in `tests/maya/concepts/test_docs_examples.py`, and docs in `commands.md`, `python.md` and `library.md`.

## 9. Thumbnails, later

Phase 5, designed now only so the first build doesn't block it. A thumbnail is `<name>.png` beside the file, a still from a playblast (a pose: the current frame, an animation: the middle frame, later a short strip), 256 pixels on its long side. It is made in a GUI Maya from the Save button's context, offscreen with the active camera framed on the rig (a gallery-style `playblast`, `ViewportGallery.cmd` shows it runs), so it can't be made headless, and an entry without a PNG shows the kind icon. `LibraryEntry.thumbnail` is `None` until then, and the Grid view appears with the first image. The pairing by name means a rename moves two files: the library's rename handles both.

## 10. Tests in mayadev

`docs/tools.md`, `architecture.md`, `framework.md` (`ListModel`), `assets.md` (sources, the file names, masks) and `rigmodules.md` (the new calls) change with it.

| What | Where | Checks |
|---|---|---|
| `core.asset.library`: sources, listing, query parsing, filtering, masks files | plain Python, a temp project | each source's folder, the shared source from `asset.skeleton`, `tag:`, `template:`, free words, facets, a legacy `.json` is listed, rename and delete move the thumbnail too, an existing name is refused |
| The index cache | plain Python with a fake reader | only changed files are read again |
| `LibrarySession` on a built rig, mayapy, no Qt | tests/rigging | the listing follows the tracker, apply gives the saved values and one undo restores them, an animation lands on its own start, mirror gives the flipped result, a mask narrows the scope, save then list round trips with tags, a frame-rate mismatch is refused, no rig gives an empty state |
| `ListModel`, `LibraryBrowser` offscreen | mayapy, `QT_QPA_PLATFORM=offscreen` | rows, roles, a reset keeps the selection by path, a fake session records the calls, the Start at field hides for poses, an Approved entry disables Rename and Delete |
| Layer test | plain Python | the import rules in section 2 |

A click-through in a GUI Maya through `mayalaunch.run_gui` is a manual check, and each build says it was not done unless it was.

## 11. Maintenance surface, cost and what was rejected

- **mayadev:** `core.asset.library` (sources, listing, query, masks), `LibrarySession`, `ListModel`, `LibraryBrowser`, one tool id, and the User folder convention.
- **RigModules:** two commands, one flag, a `meta` block in two formats and its update steps, and the file name change.
- **Cost:** none on playback, selection or evaluation. A first listing is one batch command, later ones read changed files only. An apply is one command call.
- **Rejected:** a folder per face, race or rig type (one pose in many places; tags and the template are searchable), name filters for masks (the animators' pain point), a sidecar file for metadata (a pose moved or emailed loses it), a pose preview on hover or selection (a scene write per keypress), the browser inside the rig panel (D50), a library service that watches the disk (state that can go stale), applying a mirror by loading and then flipping the rig (two writes, and it flips what was already there), a background thread for mirrors (the Maya API is not thread safe), enforcing approval in the browser (source control permissions are the real gate).

## 12. The author's answers, and what is still open

**Answered (2026-11-14):** the skeleton asset's folders are a Shared source. No thumbnails in the first build. Mirror the data first, not the rig (section 7). An animation applies at its own start time, not the current frame. The ControlAnim file name is `<name>.controlanim.json`, and poses match with `<name>.controlpose.json`. The User library is under the projects root at `user\animlibrary\<asset id>\controlposes` and `controlanims`. Per-shot sources wait for a shot concept. Saved masks live both with the team's library and the user's own. Metadata is data in the file. Tags are editable after save. The metadata also gets `status`, `category` and `rigRegions` (section 4).

**Settled (author, 2026-11-14):**

1. **The root is `projects_root()`** (`RIGGING_PROJECT_DIR`), not a new variable.
2. **The User path has the login:** `<projects root>\user\<login>\animlibrary\<asset id>\...`, so each animator has their own. The login is the OS user, with no setting.
3. **The registry skips a top-level `user` folder**, and an asset id may not start with `user`: a small check in `core.asset`, with a test.
4. **Vocabularies.** All three are project data, so a studio changes them without code. `getRigDataFileInfo` checks the shape (strings), not the vocabulary: a value outside the list is the browser's warning, not the format's error.
   - **Rig regions** are declared in `project.json` under a `rig` parent, `"rig": {"regions": ["lowerBody", "upperBody", "head", "hands"]}`, because a bare `regions` is too generic a name for that file. The file's key is `rigRegions` (a list, since a pose can span two), and the search term is `region:`.
   - **`status`** is `scratch`, `review` or `final`, single-valued, and separate from the source: Approved is where a file lives, `status` is a label on the work. The list is `"library": {"statuses": [...]}` in `project.json`.
   - **`category`** is free text with a completer from the categories in use.

**Still open:** nothing. The design is ready for sign-off to start phase 1.

## 12b. Later: a 3D preview

The author's idea (2026-11-14): a live 3D view of the selected pose or animation on a skeleton with a simple proxy mesh is a more appealing way to go through a library than thumbnails, and more useful. It is in no phase below, but the design leaves room.

What it needs: the joints for the pose. A ControlPose is control values, and turning them into joints is the rig's forward evaluation, so there are two ways:

- **A joint preview saved beside the control data** (recommended). At save time the rig is there, so the save also captures the joints: one JointPose for a pose, a JointClip for an animation, through the existing `capture_joint_clip` (D41), written as `<name>.preview.json` beside the file. It is a sidecar, like the thumbnail, so the control file's format doesn't change, it is optional, and a batch tool on a machine with Maya can regenerate it. Browsing then needs no rig at all, which matters for a library opened without the rig loaded.
- **Evaluate on a rig at browse time.** Apply to a hidden rig in a scratch scene. Slow, needs the rig, and risks the user's scene. Rejected for browsing.

The pairing key is already there: `meta.skeletonId` (section 4) names the skeleton, and the skeleton export (`<asset>_skeleton.fbx` with `<asset>_skeleton.bonemap.json`, D42) holds its rest joints and hierarchy. A viewer takes the rest from there, the joint transforms from the preview, and a proxy mesh per bone (capsules between joints generated from the hierarchy, or a low-poly mesh stored with the skeleton asset). The viewer is a Qt 3D or OpenGL widget in `maya_ui` with a play slider for animations. It renders from plain data, so it needs no Maya and never touches the scene. It would be a view mode next to List, Details and Grid. It costs nothing now beyond `skeletonId` in `meta` (phase 1) and reserving the `.preview.json` name. Whether the skeleton export carries enough to draw (it holds joints, not a proxy) is untested.

## 12c. Phase 1 as built (2026-11-14)

Phase 1 (the RigModules side) is built and the suite passes (TOTAL in the commit message). What differs from section 8, and why:

| Section | Designed | Built | Why |
|---|---|---|---|
| 8 | `tags`, `description`, `category`, `rigRegions`, `status` and `author` as flags of the two save commands | One flag, `meta`, a JSON object, on `saveRigPose` and `saveRigAnimation` | Six flags on each of two commands, and the next field would be a seventh. The project's rule is that structured data in a string is JSON. The same object goes to `setRigDataFileMeta` |
| 4, 8 | RigModules fills `template` and `skeletonId` from the rig | The caller passes both. The rig fills `assetId`, `variant` (the rig root's `rigName`), `pluginVersion` and `created` | The rig stores no template name and no skeleton id (`rigName` is the variant, and defaults to the template name only when none is given). Storing them would be a node change that old rigs lack. mayadev knows both |
| 8 | `setRigDataFileMeta(file, tags=, description=, ...)` | `setRigDataFileMeta(file=, meta=)`, the same JSON object, each key replacing its field and an empty value clearing it | Follows the save flag |
| 8 | `getRigDataFileInfo(rigRoot, *files)` | `getRigDataFileInfo(rigRoot, file=[...])`, a multi-use flag | A file path is not a Maya object, and the other commands that take lists use a flag |
| 8 | `rigmodules.query.get_data_file_info` | `rigmodules.library.get_data_file_info` and `set_data_file_meta`, in a new module | The module holds a setter too, and `query` only reads the scene |
| 8 | `loadRigPose(mirror=True)` a data transform | As designed: `RigPoseScene::mirrorPoseData`, with the rules of `flipRigPose` and `mirrorRigKeys`. A test compares it with load then flip on every channel of every control of two rigs | |
| 4 | Files named `.controlpose.json` and `.controlanim.json` | The docs and examples use them. The commands take any path, so nothing in the plugin changed. mayadev's handlers still write `.json` (`docs/mayadev.md`) | The name is a mayadev matter |

Also: both formats are version 2 with a required `meta` object, and a version 1 file loads with an empty one. `core/` gained `LibraryMeta` (the field table is the one list of fields) and `LibraryFile` (a file's summary, the info JSON and a meta-only edit), and `maya/shared` gained `RigDataFileScene`. The `status`, `rigRegions` and `category` vocabularies are not checked by RigModules (section 12). Nothing in the vocabulary or the project.json parts is built: they belong to phase 2, in mayadev.

## 12d. Phase 2 as built (2026-11-14)

Phase 2 is built in mayadev on branch `ui-phase1` (uncommitted when this was written, with the suites passing). No window. What differs from sections 3 to 7, and why:

| Section | Designed | Built | Why |
|---|---|---|---|
| 2 | `ListModel` is a `QAbstractListModel` | A `QAbstractTableModel` with optional `columns` | The Details view is a `QTableView` over the same model as the List view. A list model has one column. A `QListView` over a table model shows column 0 |
| 5 | The index is keyed by path and modified time | Path, modified time and a **context** (the rig root) | `getRigDataFileInfo` reports `unmatchedControls` for one rig, so a cached answer is wrong for another. Switching rigs rereads the files once |
| 5 | Search terms `tag:`, `template:`, ... | `tag`, `template`, `skeleton`, `by`, `category`, `asset` match a part of the value, ignoring case. `source`, `kind`, `status`, `region` match the whole value. Every term must match | Typing `tag:fis` finds `fist` while the animator types. A source or a status is a fixed word, so a part of it would be a surprise |
| 6 | A mask item is `{module, side, usage}` | `MaskItem(module_type, side, usage)`, saved as `moduleType`, `side`, `usage` | It is the `moduleType` filter of `getRigControls`, and `module` could be read as a module's name. The rig's per-module mask is the list of its controls' `(side, usage)`, which is unique in a rig (D25) |
| 6 | Saved masks as JSON beside the library | `masks.json` beside the `controlposes` and `controlanims` folders of the Approved source (the team's) and of the User source (the user's). A user mask replaces a team mask of the same name | "Beside the library" has two libraries. A mask is per asset because module types differ between rigs |
| 7 | `scope: str` | `None` (selection when something is selected, else the rig), `"selection"`, `"rig"` or a mask's name | The default follows the selection without the view setting it. A mask with a selection keeps the selected controls of the mask |
| 7 | A cache of the last mirrored result | Not built | The command mirrors inside `loadRigPose` and `loadRigAnimation` and returns no data, so there is nothing to cache. The design already said the first build doesn't precompute |
| 7 | `save(name, tags, description)` | Adds `replace`, `start_frame` and `end_frame`. An existing name raises `LibraryError` unless `replace`. The menu tools ask through the prompter, the session raises | The view asks the prompter, and the session stays free of dialogs |
| 3 | Approved, Shared and User, in `core.asset.library` | As designed. `sources(asset)` leaves Approved and Shared out while the asset isn't on disk, so a rig whose asset was never created still has a User library | Section 7: "the User folder of that rig's asset id when the id is valid" |
| 3 | The registry skips a top-level `user` folder and an id can't start with `user` | `assetid.check` refuses a first token of `user`, ignoring case, and the registry skips `<projects root>\user`. A project whose folder is below the projects root never walks into it | RigModules' `core/AssetId` has no such rule: flagged in `docs/mayadev.md` |
| 4 | The vocabularies in `project.json` | `Project.rig_regions` (`rig.regions`) and `Project.library_statuses` (`library.statuses`), both in `DEFAULT_PROJECT`, an absent key an empty list, the `formatVersion` unchanged | Additive keys. An existing `project.json` keeps working and is not checked against them |
| 4 | File names, `.anim.json` becomes `.controlanim.json` | The two handlers write `.controlpose.json` and `.controlanim.json`, list a legacy `<name>.json`, and a clip's sidecar `.anim.json` is still read. A rename gives a legacy file the new suffix | One name for the format, and nothing stored stops loading |
| 7 | Save goes through `assettools` with the User folder as the target | `assettools.save_pose` and `save_animation` write to the User library with `meta` (author, template, skeletonId, tags, description). `save_control_file(folder=)` versions the old file and never calls source control for it. Load goes through `posetools.load_pose` and `load_animation` (one undo step), which the session and the menu tools share | A file in the User folder isn't in source control, so a checkout or add would be wrong under Perforce |
| 4 | `template` in `meta` | The rig's variant (`rigName`) | The rig stores no template name (section 12c), and a variant is the template's `rigName` |

Delete keeps a local version copy (`versions.version`) before it removes the file. `copy_to_user` was added to the session and the library, since the Approved and Shared entries can be copied to User (section 3).

Not done, by design: the window, the tool registry entry and the `panels` item (phase 3), Publish to Approved (phase 4), thumbnails (phase 5). The menu tools are still file-dialog tools and write to the User library now.

## 12e. Phase 3 as built (2026-11-14)

Phase 3 is built in mayadev on branch `ui-phase1` (uncommitted when this was written). The window is `bmaya/maya_ui/librarybrowser.py`, the tool is `rigmodules.panels.library_browser` under `panels`, and the user-facing description is `docs/tools.md` there ("The library browser"). What differs from section 7, and why:

| Section | Designed | Built | Why |
|---|---|---|---|
| 7 | A `QListView` for List and a `QTableView` for Details over one model | One `QTableView`: List shows Name and Source, Details all seven columns | One selection model, no syncing between two views. The Grid view of phase 5 adds a `QListView` in icon mode then. The author agreed |
| 5, 7 | Facet chips for source, kind, region, status, category, tags and template | Chips for the sources only. A **Filter** menu lists the tags, templates, regions, statuses and categories in use, and a pick writes its term in the search box (`toggle_term`, in `core.asset.library`). Kind is the tab | A chip per tag does not fit 385 px and grows with the library. The box still shows exactly what filters the list |
| 7 | The session offers what the window needs | Added: `facets()`, `name_taken(name)`, `current_frame()`, `playback_range()`, `mask_from_selection(name)` | The window has no Maya, so a frame, a free name check and the selected controls have to come from the session |
| 12 | The browser warns on a status or region outside the project's lists | `details()` adds a line for each. A project that lists none is not checked, and an asset whose project can't be read is not checked | The author asked for it. The format still checks only the shape |
| 6 | A user can save a named mask | **Masks** button: Save Selection as Mask (one item per `(side, usage)`, so it picks the same controls on any rig of the template) and Delete Mask (only the user's own). The saved mask becomes the scope | The author asked for it in phase 3 |
| 7 | Apply on a click, Enter or the button | Only a double click, Return or the button, through an `EntryTable` signal, not `activated` (which fires from a single click on some platforms) | Nothing is applied by hover, a click or arrow keys |
| 7 | Save: name, Tags, Save | The same. The tags field clears after a save (a stale tag on the next file would be a surprise), and the name returns to a free suggestion. A taken name asks through the `Prompter` | |
| 7 | Rename, Delete, Edit Tags, Publish in the context menu | Apply, Copy to My Library, Rename, Edit Tags and Description, Delete. The ones a source can't have are disabled. No Publish | Publish is phase 4 |

Also built: `widgets.TagsEdit` (completes the tag under the cursor from the tags in use), a `pose` icon and a `warning` label property in `theme.py`, and a layout that fits the 385 px dock (the scope on one row, then Mirror, Start at, Now and Apply on another, because one row made the window about 560 px wide in Animations).

**Found in a GUI Maya, not in mayapy.** `menu.addAction(text)`, and `menu.addMenu(text)`, returned objects whose C++ side was gone by the next reload, and `widget.style()` returned a deleted style. The window now makes each `QAction` and submenu explicitly with the menu as its parent, and shows errors and warnings in labels that are made for them and hidden while empty, rather than changing a label's style. `tests/gui/librarybrowser/run.bat` checks the actions are alive, so a regression there is caught, though not by the offscreen suite.

**Verified:** `tests/core` 112 plain and 135 in mayapy (42 of them the window and `TagsEdit`, offscreen, over a fake session), `tests/asset` 97 and 155, the menu tier (43, 40 tools), and `tests/gui/librarybrowser/run.bat` in a GUI Maya: the dock, a save by Enter, apply and its one undo, search, the Animations tab, reopening three times, and a closed window's session no longer listening. **Not verified:** the tag completer's pop-up and the right-click menu in a GUI Maya (the offscreen tests call the completion and menu code, not the pop-ups), and a library of hundreds of files.

## 13. Phases

Each phase leaves every menu tool working and is reviewable alone.

1. **RigModules** (section 8): `meta` and its update steps, the save flags, `getRigDataFileInfo`, `setRigDataFileMeta`, `loadRigPose(mirror=)`, the file names, the contract and round trip tests, docs.
2. **mayadev headless (built, section 12d):** `core.asset.library` (sources, index, search, masks), `LibrarySession`, `ListModel`, the menu tools' save and load moved onto the library functions (Save writes to User), tests. No window.
3. **The view (built, section 12e):** `LibraryBrowser` with List and Details, search and facets, scope, apply, save, edit tags, the tool registry entry and the `panels` item, offscreen tests, mayadev docs.
4. **Approved:** Publish to Approved, and the Approved and Shared write rules.
5. **Thumbnails and the Grid view** (section 9).
