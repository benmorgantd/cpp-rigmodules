# The active rig panel

**Status:** Decided (D50), written 2026-11-14, and the author answered the five open questions the same day (section 11). Not built. It is the first item of phase 3 in `mayadev-ui.md` (section 5). The code it changes is in mayadev, plus one small query gap in RigModules (section 6). Changes to mayadev need the author's go-ahead and a commit in that repo.

## 1. The problem it solves

The animator's tools today are menu items that act on "the selection, else the active rig" (`bmaya.maya.posetools.get_scope`). That works, but the animator cannot see which rig is active, cannot switch it without selecting one of its nodes, cannot select a limb without hunting for its controls in the viewport, and has no place for the one rig feature that is a state, not a command: the space of a control.

The panel makes the active rig visible and gives the animator the rig's tools in one place:

1. **Which rig is active**, and a way to change it when a scene has more than one (a shot with two characters).
2. **Select controls by module and side**, and select the mirror, without picking in the viewport.
3. **Pose tools**: zero, mirror, flip, key, on the selection or the whole rig.
4. **Space switching** for the selected control, which keeps the control where it is (`setRigConstraintSpace`).

It is a launcher and a state display over functions that already exist. It adds no pipeline behavior of its own (rule 6).

### What it does not do

- **No channel box.** Maya's channel box already shows and edits a control's keyable channels, and `getRigControlChannels` is the same list. A second one would be a worse copy (simplicity).
- **No selection mirroring.** The tree does not highlight what is selected in the viewport. Two-way sync needs a guard against loops, and it costs a scene read on every selection change. The tree is one-way: a click selects.
- **No library.** The pose and animation library browser is its own panel (section 8). It reads the active rig from the same view-model.
- **No build or asset UI.** That stays in the menu tools and is a separate design.

## 2. Layers

```
bmaya.maya.activerig      ActiveRigTracker: gains a `changed` Event                    maya, rigmodules
bmaya.maya.rigpanel       RigPanelSession: the view-model                              maya, rigmodules
bmaya.maya.posetools      plain functions with explicit arguments, beside the _interactive ones
bmaya.maya_ui.rigpanel    RigPanel: the Qt view, and show_window()                     PySide6, rigpanel
bmaya.maya_ui.host        show(..., dock=True): exists
```

The view-model is in `bmaya.maya`, not `core`, because it reads the scene (`mayadev-ui.md` section 3.3). It imports no Qt. The view imports PySide6 and `bmaya.maya.rigpanel` only, so it opens in mayapy offscreen.

## 3. The model side

### 3.1 `ActiveRigTracker` gets a `changed` Event

`ActiveRigTracker` already follows selection, new scene and open scene. Today it only logs on a change. It gains:

```python
class ActiveRigTracker:
    changed: Event     # emits the rig root (str) or None, when the active rig changes
    def set_rig_root(self, rig_root: str | None) -> None   # exists: emits changed
    def refresh(self) -> None                              # update_from_scene, public name
```

`Event` is class-level state on a singleton, so a view connects once and survives `stop()` and a new tracker: the Event belongs to the module, not the instance, as `LogOutput.writers` does. Open question 4 asks whether that is the right home.

The selection callback is the hot path (it runs on every selection change). It stays as it is: it checks the last 32 selected nodes with `find_rig_info`, and emits only when the rig changes. The panel adds nothing to it.

**A rig built after the panel opens.** The tracker does not see a build, because a build changes no selection. `build_rig` (and any workflow that creates or deletes a rig) calls `tracker.refresh()` when it finishes. The panel also has a Refresh button, for a rig deleted by hand.

### 3.2 `RigPanelSession`

Plain class, no Qt. State is what the window shows, and commands are what its buttons call.

```python
class RigPanelSession:
    changed: Event                  # the whole panel re-reads: rig or controls changed
    spaces_changed: Event           # the space row re-reads: selection or a space changed

    rigs: list[RigInfo]             # every rig in the scene, for the picker
    rig: RigInfo | None             # the active rig, None when the scene has none
    groups: list[ControlGroup]      # the active rig's controls, grouped (below)

    def refresh(self) -> None                        # reads rigs and controls once
    def set_active(self, rig_root: str) -> None      # the picker: tracker.set_rig_root, then refresh
    def select_group(self, group: ControlGroup, add: bool = False) -> None
    def select_mirrors(self) -> None                 # posetools.select_mirror_controls
    def zero(self) / mirror(self, left_to_right) / flip(self)         # posetools
    def key_rig(self) / key_modules(self)                             # posetools
    def get_space(self) -> SpaceState | None         # the selected control's space, if it has one
    def set_space(self, label: str, key: bool = False) -> None   # setRigConstraintSpace(label=...), keys when asked
    def get_status(self) -> RigStatus | None         # out of date? (section 4.2). None until that check exists
```

```python
@dataclasses.dataclass(frozen=True)
class ControlGroup:
    module: str                 # the module node
    module_type: str            # "FkChain", ...
    side: str                   # "Left", "Right", "Center"
    controls: tuple[str, ...]   # in the module's control order
```

`refresh()` calls `rigmodules.query.get_rig_controls(rig_root)` once and groups the result by module and side in Python. One command call per rig change, none per selection change. The selection sets of D39 are not used: the controls are already in hand, and a set would add a second source for the same list.

`ControlGroup` carries node names as the commands return them, so a referenced rig (D32) with namespaced names works with no special case.

**A broken rig.** `find_rig_info` raises `RuntimeError` for a broken rig. `refresh()` catches it, logs it through `bmaya.core.log`, and shows the panel's empty state. It never raises into a Qt slot.

### 3.3 `posetools`: plain functions

The `*_interactive` functions take their scope from the selection. The panel's scope is the same (selection, else the rig), so it could call them directly. It still needs a plain version for each, because the session must be callable from a test with explicit controls, and rule 6 says the work is not tied to a button:

```python
def zero_pose(scope: list[str]) -> int
def mirror_pose(scope: list[str], left_to_right: bool | None = None) -> int
def flip_pose(scope: list[str]) -> int
```

`zero_pose_interactive` becomes `zero_pose(get_scope())` plus the log line. The session calls the plain function with `get_scope()`. This is a small refactor of three functions, in the same change, and no tool id changes.

## 4. The view

One `QWidget`, docked beside the Attribute Editor (`host.show(..., dock=True)`, object name `bmaya_active_rig_panel`). About 385 px wide, the host's default.

```
+----------------------------------+
| hero  (main)            [Refresh]|   rig picker: a label for one rig, a combo for several
| newer publish exists (section 4.2)|   only when the rig is out of date
+----------------------------------+
| [filter............]            |
| v arm_l  (FkChain)               |   module
|     Left                         |   side, one row when a module has one side
|       upperArm  lowerArm  hand   |   controls: a click selects, ctrl adds
| v spine  (FkChain)               |
|     Center ...                   |
|  [All]   [Mirror]                |   select every control, select the other side
+----------------------------------+
| Pose   [Zero] [Mirror L>R] [R>L] |
|        [Flip] [Key] [Key Module] |
+----------------------------------+
| Space  [ World | Chest | Hip ]   |   only when the selected control has one
|        [ ] Key                   |   off by default (section 5)
+----------------------------------+
```

- **The tree** is a `QTreeView` over a `QStandardItemModel` filled from `session.groups`, rebuilt when `changed` fires, with a `QSortFilterProxyModel` for the filter box. The model is small (a rig has tens of controls) and is replaced whole, so a custom `QAbstractItemModel` would cost more code than it saves. This is the call `mayadev-ui.md` section 3.6 made for the Menu Editor's tree.
- **The buttons** call one `session` method each. A button's slot is one line.
- **The space row** is a `QButtonGroup` of the labels, built from `SpaceState.labels`, with the current one checked. It is hidden when `get_space()` is `None`.
- **Empty state:** one label, "There is no RigModules rig in the scene.", and every other widget disabled.
- **Errors** from a command (`RuntimeError`) are caught in one wrapper in the view and sent to `bmaya.core.log.error`, which is where every other tool's errors go (`MGlobal.displayError` in a GUI session, once `maya.startup.register()` has run). The view does not show a dialog for an error.

### 4.1 When the panel updates

| Event | Source | Panel does |
|---|---|---|
| Active rig changed | `ActiveRigTracker.changed` | `session.refresh()`, rebuild the tree |
| New or opened scene | tracker (it already listens) | the same, through `changed` |
| A rig built or deleted | the build workflow calls `tracker.refresh()`, or the Refresh button | the same |
| Selection changed | the tracker's selection callback, only when it changes the rig | nothing, except the space row |
| Space row | the selection callback | `spaces_changed`, see below |

The space row has to follow the selection, which means one more thing on the selection hot path. To keep it cheap, `get_space()` runs only when the panel is visible, and only for the one control that is selected: `cmds.ls(selection=True, type="rigControlNode")` with exactly one result, then one query. With zero or several controls selected it returns `None` without a query. The panel connects its own selection callback through the `EventsRegistry` while it is shown and removes it on close (`closeEvent`), so a closed panel costs nothing.

### 4.2 Out of date: the panel warns and never updates

The header shows one line under the rig name when the active rig is out of date: "A newer publish exists (v12, this is v10)" or "Built with an older RigModules (0.4, loaded 0.5)". It is a warning only. The panel never updates a rig, for three reasons: a referenced published rig is read only (D32); the update path is an explicit step, a new publish or a rebuild, not something a view runs; and an update that changes a rig under an animator mid-shot is the surprise the priority list rules out.

The check itself is not built. It is the out-of-date check in `asset-model.md`, which runs in mayadev: it compares the rig root's `publishVersion` with the latest publish of that asset and variant, and `pluginVersion` with the loaded plugin. `RigInfo` already carries both stamps, so the panel needs no new RigModules query. The panel's part is small: `RigPanelSession.get_status()` calls the mayadev function and returns `None` until it exists, and the header label is hidden when it is `None`. It runs on `refresh()` only (a rig change, a new scene), never on selection. The check of the whole scene's rigs for a "list the stale rigs" tool is a separate tool and not part of the panel.

## 5. Space switching

The animator-facing rule is `setRigConstraintSpace`: the control keeps its world position and orientation in the new space, then the `space` enum changes (`commands.md`). The panel is a button group over that command.

**Keying (author, 2026-11-14).** A switch never keys by default: it runs `setRigConstraintSpace` and nothing else. The row has a **Key** checkbox, off by default and remembered between sessions with the panel's other view settings. With it on, a switch keys the control's channels on the previous frame, switches, then keys the current frame, so the animation holds the old space up to the cut. `set_space(label, key=False)` carries the same behavior to mayapy, so the keying is a model function and not part of the checkbox (rule 6). It keys only the switched control, and uses `rigmodules.pose.key_controls`.

**The query gap.** `setRigConstraintSpace` takes the constraint node, and the panel starts from a control. There is no Python query that finds a control's constraint, its labels or its current space (`rigmodules.query` has none, and a tool may not walk connections or parse names, D24). Section 6 proposes the query.

## 6. What RigModules needs

One addition, which the panel's third phase (section 9) depends on:

```python
# rigmodules.query
@dataclasses.dataclass(frozen=True)
class ControlSpace:
    constraint: str            # the rigConstraint node
    labels: tuple[str, ...]    # the driver labels, in driver order
    current: int               # the index the control is in now

def find_control_space(control: str) -> ControlSpace | None   # None for a control with no space switch
```

The options:

| Option | Cost |
|---|---|
| A. Add `spaces` to `getRigControlInfo`'s JSON for each control that has a space constraint | One command, no new name. The JSON shape gains a key when present, so existing callers keep working. Every `getRigControlInfo` call (and `get_rig_controls`) now does a constraint lookup per control, which the panel's tree load pays for even though it needs only one control |
| B. A new command `getRigControlSpace(control)` | Another command to maintain and document, but it is called for one control and costs nothing else |
| C. Python only: read the control's `space` enum and find `<control>_space` | No. The name is a default, not a rule, and the constraint can be named by `name` |

**Recommendation: B.** The panel asks for one control's space on a selection change, and the tree load should not pay for it. A is smaller but puts a lookup into the call every tool makes. Either way it is a read-only query that follows the shape in D14 (`JointMarkupCmd` is the reference) and needs the usual docs (`commands.md`, `python.md`), a test in the commands tier, and a `ModuleContract`-style check that every control with a `space` attribute has a constraint the query finds.

Nothing else changes in RigModules: no node, no attribute, no name. D36 is not affected, and no `bench` run is needed.

## 7. Tests

| Test | Tier | Checks |
|---|---|---|
| `RigPanelSession` on a built rig (Human) | mayapy, no Qt | `refresh` groups every control once and in module order, `select_group` selects exactly the group, `set_active` changes the tracker and emits `changed`, a deleted rig gives the empty state, a broken rig logs and does not raise |
| `ActiveRigTracker.changed` | mayapy | emits on a selection that changes the rig, does not emit on one that does not, survives `stop()` and a new tracker |
| `posetools` plain functions | mayapy | explicit controls give the same result as the `_interactive` ones, which are now one line each |
| `RigPanel` offscreen | mayapy with `QT_QPA_PLATFORM=offscreen` | the tree matches `session.groups`, the filter hides rows, each button calls its session method (a fake session records the calls), the space row is hidden for a control with no space |
| Layer test | plain Python | `maya_ui/rigpanel.py` imports `PySide6` and `bmaya.maya.rigpanel` only, and `maya/rigpanel.py` imports no Qt. `maya_ui` imports Maya only in `host.py` and `menus.py` (the rule from phase 2 holds) |
| `find_control_space` or `getRigControlSpace` | RigModules, commands tier | a control with two drivers returns its labels and current, a control with none returns `None`, an invalid node raises |

The view test uses a fake session with the same methods and `Event`s, so it checks the wiring without a rig. A click-through in a GUI Maya through `mayalaunch.run_gui` is a manual check, and the first build will say it was not done unless it was.

## 8. The library browser

The library browser (`control-library.md`, step 7) is the second panel of phase 3. It gets the active rig from `ActiveRigTracker.changed` and the asset from `RigInfo.asset_id`, and it is the first user of a `ListModel`: a library's poses and animations are lists that grow. The active rig panel does not need `ListModel`, so the panel does not build it. This corrects a line in the earlier recommendation that said the panel was the first one that needs it. Open question 3 asks whether the two panels share a window.

## 9. Phases

Each phase leaves every menu tool working and is reviewable alone.

1. **Headless base.** `ActiveRigTracker.changed` and `refresh`, `posetools` plain functions, `RigPanelSession` without spaces, their tests. The build workflow calls `tracker.refresh()`. No window yet.
2. **The view.** `maya_ui/rigpanel.py`, the tool registry entry and the RigModules menu item (`Active Rig Panel`), the view tests, and docs in mayadev (`docs/tools.md`, `architecture.md`).
3. **Spaces.** The RigModules query (section 6), then `get_space` and `set_space` on the session and the space row in the view, with the Key checkbox.
4. **Out of date** (section 4.2), when mayadev's out-of-date check exists: `get_status` and the header line. Until then `get_status` returns `None`, so phases 1 to 3 don't wait on it.

## 10. Maintenance surface and cost

- **New public surface in mayadev:** `ActiveRigTracker.changed` and `refresh`, `RigPanelSession` and `ControlGroup`, `SpaceState`, three plain `posetools` functions, one tool id.
- **New public surface in RigModules:** one query (section 6), with its docs and tests.
- **Cost on the selection hot path:** none while the panel is closed. While it is open, one `cmds.ls` with a type filter per selection change, and one query only when exactly one control is selected.
- **Rejected:** a channel box (a copy of Maya's), two-way selection sync (loop guard and a per-selection scene read), selection sets as the control source (a second source for a list already in hand), a custom `QAbstractItemModel` for the tree (more code than the small model it would serve), keying inside `set_space` without a decision (section 5), `getRigControlInfo` with spaces for every control (option A).

## 11. Answers (author, 2026-11-14)

All five questions are answered, so the design has no open question. The rest of this file is written to the answers.

1. **Space switch keying:** never by default, with an optional Key checkbox (section 5).
2. **Docking:** beside the Attribute Editor or the Outliner. The host's default is a tab beside the Attribute Editor, and the user can move it. Nothing to build.
3. **Two panels, two separate tools** (section 8).
4. **`changed` on the tracker class** (section 3.1).
5. **No "key selected controls" button.** Maya's own keying does that, so the panel keeps Key and Key Module only.

The questions as first asked, kept for the history:

1. **Does switching space key?** Options: (a) it never keys, and the animator keys before and after, as with Maya's own constraint switches; (b) the row has a "Key" checkbox that keys the control's channels on the previous frame, switches, then keys the current frame; (c) it always does (b). Recommendation: (a) first, since it is predictable and needs no policy, then (b) if animators ask. This needs a view on how your animators work.
2. **Where does the panel dock?** The host's default is a tab beside the Attribute Editor. A panel that sits next to the channel box works with it, but a user with the Attribute Editor closed gets a floating window. Recommendation: keep the host default.
3. **One window or two for the panel and the library?** Recommendation: two panels. They have different lifetimes (the library is opened to load something and closed), and a tab can be added later with no change to either.
4. **Where does `changed` live?** On the tracker class (survives `stop()`, as proposed) or on the instance (a view reconnects after a dev reload, and a view that does not leaks nothing because bound methods are weak). Recommendation: the class, because the dev reload tool restarts the tracker and the panel should not have to know.
5. **Which control scope for Key?** `key_all_controls` keys the whole rig and `key_selected_modules` keys the modules of the selection. The panel shows both. Is a third ("key selected controls") wanted? It is a one-line function over `rigmodules.pose.key_controls`, but each button is one more thing to learn.
