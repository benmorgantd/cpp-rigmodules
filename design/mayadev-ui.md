# mayadev user interfaces: thin views over headless workflows

**Status:** Leaning (D49). Written 2026-11-14 from a read of mayadev `main` (two commits ahead of `origin/main`) and the author's rules below. The author answered the open questions the same day (section 9). Nothing is built, and nothing in this document was run: every claim about the current code comes from reading it. The code it would change is in mayadev, and mayadev keeps its design history here (mayadev `docs/README.md`). Changes to mayadev need the author's go-ahead and a commit there.

The goal is a UI layer that is only a view. Every pipeline action, and every question a tool asks the user, runs the same way in a GUI Maya, a mayapy batch job and a plain-Python test. The UI binds widgets to state that lives somewhere else.

## 1. The rules

The author's rules (2026-11-14). The design must keep every one of them.

1. No Qt Designer `.ui` files.
2. Clean, data-backed model / view-model / view designs.
3. Scalable, reusable and fast.
4. Not tied to Maya where it can be avoided.
5. As little code as possible in the UI. As much as possible is callable from separate modules (the model or view-model layer). The UI is a thin layer over existing functionality.
6. **Hard rule:** no core pipeline function is tied to a button press. Everything (an exporter, say) is callable from pure mayapy.
7. All UI code goes in `bmaya.maya_ui`, all pure Python in `bmaya.core`, all Maya Python in `bmaya.maya`. Dependencies run `core` > `maya` > `maya_ui` only, never the other way.
8. Dialogs are Qt. No `maya.cmds` dialogs or UI commands, except where Maya is the host (section 3.5).

## 2. What exists now

| Piece | File under `py/scripts/bmaya` | State |
|---|---|---|
| Qt base classes | `maya_ui/qtbase.py` | `SimpleWindow`, `SimpleWidget`, `DockableWidget`, `Dialog`, `ConfirmDialog`, `ProgressWindow`, item-widget subclasses (`Table`, `List`, `Tree`, `TreeItem`), a syntax highlighter |
| Registry Editor | `maya_ui/tool_registry_editor.py` | One `QMainWindow` that holds the state and the logic |
| Menu builder and Menu Editor | `maya_ui/tool_views/tool_menu.py` | `ToolMenu`, `build_menus` (Maya menus by `cmds`), `ToolMenuEditorWindow` |
| Naming tool | `maya_ui/namingTool/` | Older naming convention, opened by nothing (`docs/status.md`: Reference) |
| Menu tools that ask questions | `maya/assettools.py` | `*_ui` entry points, with `_ask_*` helpers that open `cmds` dialogs |
| Progress | `core/progressmanager.py` | A singleton with one callback, watched by `ProgressWindow` |
| Tool model | `core/tool_registry.py` | Dataclasses, checks, `run_tool`. Maya-free, and already the shape this design wants |
| File model | `asset/` | Maya-free, and forbidden to import `bmaya.core` because `core/log.py` needs Maya |

### 2.1 Where the code breaks the rules

**Dependency direction (rule 7)**

- `maya/devreload.py` (`_delete_plugin_menus`, `_build_menus`) imports `bmaya.maya_ui.tool_views.tool_menu`. That's `maya` depending on `maya_ui`.
- `core/log.py` imports `maya.api.OpenMaya` for `MayaLogHandler`, and `bmaya.maya.constants`, which it doesn't use. So nothing in `core` that logs is plain Python. This blocks the view-model tests from running without Maya, and it's the only reason `bmaya.asset` can't be part of `core`. It's already recorded in mayadev `docs/status.md`.
- `core/progressmanager.py` keeps a strong reference to the window that registered (`_ui_window`), and holds one callback only. A core object knows about a view.

**UI code outside `maya_ui` (rules 5, 7 and 8)**

- `maya/assettools.py` has 17 `cmds` dialog calls (`promptDialog`, `layoutDialog`, `confirmDialog`, `fileDialog2`) in `_ask_text`, `_choose`, `_ask_clip_properties`, `_confirm_replace`, `_ask_file` and `_ask_folder`.
- The `*_ui` functions also hold workflow logic that only runs behind a dialog. For example, `save_pose_ui` decides "no asset folder: ask for a file", builds the pose path, and checks whether it exists before asking to replace. None of those branches has a test.

**The hard rule (rule 6)**

Most tools pass, because each `*_ui` ends in a plain function (`save_pose`, `save_joint_clip`, `build_rig`, `save_skeleton_export`). One path doesn't. When an asset has no folder, `_capture_joint_clip` calls `AnimClipHandler().save(..., own_folder=False)` directly, so "capture the export joints to this FBX path" has no plain function and is only reachable from the dialog.

**`qtbase.py` (rules 1, 2 and 4)**

- `loadUiFile` and the `designerFile=` argument of `SimpleWindow`, `SimpleWidget`, `DockableWidget` and `ProgressWindow` exist only to load `.ui` files.
- `SimpleWindow` and `SimpleWidget` call `cmds.deleteUI(self.title)` to keep one instance. Nothing sets the Qt `objectName`, so the call probably never finds the old window. Not verified in a GUI Maya.
- `Table`, `List` and `Tree` subclass item widgets (`QTableWidget` and the like), so the data lives in the widget. `List` and `Tree` hard-code a 200 px height, which the Menu Editor then overrides. `Table.translateToType` and `cellObjects` have no callers.
- `ProgressWindow` polls the manager every 100 ms and also calls `QApplication.processEvents()` inside each refresh, so a refresh can re-enter itself.

**The editors (rules 2 and 5)**

- The Registry Editor window holds the session state (`registry`, `active_tool_id`, `dirty`, `current_filepath`) and the edit logic: `_sync_form_to_memory` writes widget values into the `Tool`, and `check_everything` combines the checks.
- `list_modules`, `list_functions` and `_unwrap_optional` are pure Python in a Qt file.
- `menu_layout_paths` is path logic over `bmaya.maya.constants`, but lives in `maya_ui`, which is why the editor imports `tool_menu`.

### 2.2 What to keep

- `core/tool_registry.py` is already a Maya-free model with checks, so the editors need a view-model, not a new model.
- `ArgumentForm` and `ArgumentRow` build a form from a function's signature and type hints. This is the most reusable UI in the repo, and section 3.6 generalizes it.
- `bmaya.asset.handlers.register` and `bmaya.maya.assethandlers.register()` already work like a registration pass: the Maya-free layer owns a registry of handlers, and the Maya layer fills it once per session. Section 3.1 makes that the general pattern.

## 3. Design

```
bmaya.core       models, view-models, slots with headless defaults, Event, Prompter, asset   plain Python
bmaya.maya       Maya models, view-models and workflows; startup.register()                  maya, rigmodules
bmaya.maya_ui    Qt views, the Qt Prompter, host and menus; startup.register()               PySide6 (+ maya in host and menus only)
```

| Layer | Holds | May import |
|---|---|---|
| Model | Dataclasses and operations with explicit arguments: `tool_registry`, `asset`, `assethandlers`, the plain functions in `assettools` and `posetools` | Its own layer and below |
| View-model | Editing state and the commands on it, plus `Event`s for change. No Qt, no widgets. In `core`, or `maya` when it needs the scene | Its own layer and below |
| Workflow | A menu tool's sequence: read the scene, ask through the `Prompter`, call the plain function. In `maya` | `maya`, `core` |
| View | Widgets bound to a view-model, and the Qt `Prompter` | PySide6, the view-model's layer |
| Maya host | Main-window parent, docking, the Maya menus, deferred calls | `maya`, PySide6 |

### 3.1 Registration: a lower layer owns the slot, a higher layer fills it at startup

The author's suggestion (2026-11-14). When `core` needs something only a higher layer can provide, such as where log text goes, how to ask the user, or what to do around a plugin reload, `core` doesn't import it. `core` declares a **slot**: the interface, plus a default that works headless. At startup each higher layer runs one registration pass that fills the slots it can. The dependency arrow stays `maya_ui` → `maya` → `core`, because only the higher layer names the lower one.

The logger is the first case:

```python
# bmaya/core/log.py: plain Python
class LogOutput(logging.Handler):
    """Writes each record through the function registered for its level. Empty: stdout."""
    writers: typing.ClassVar[dict[int, Callable[[str], None]]] = {}

    def emit(self, record):
        writer = LogOutput.writers.get(record.levelno, _print)
        writer(self.format(record))

def register_writers(writers: Mapping[int, Callable[[str], None]]) -> None:
    LogOutput.writers = dict(writers)
```

```python
# bmaya/maya/startup.py
def register() -> None:
    """Fills core's slots with Maya's. Idempotent. Called by userSetup.py and by Maya batch jobs."""
    bmaya.core.log.register_writers({
        logging.ERROR: om.MGlobal.displayError,
        logging.WARNING: om.MGlobal.displayWarning,
        logging.INFO: om.MGlobal.displayInfo,
    })
    bmaya.maya.assethandlers.register()
```

```python
# bmaya/maya_ui/startup.py
def register() -> None:
    """Fills the slots a GUI session adds. Idempotent. Called by userSetup.py when Maya isn't in batch."""
    bmaya.core.prompts.register_prompter(bmaya.maya_ui.prompter.QtPrompter())
    bmaya.maya.devreload.register_hooks(
        before_unload=bmaya.maya_ui.menus.delete_plugin_menus,
        after_load=bmaya.maya_ui.menus.build_menus,
    )
    bmaya.maya_ui.progress.watch(bmaya.core.progressmanager.ProgressManager())
```

`userSetup.py` calls `bmaya.maya.startup.register()`, then `bmaya.maya_ui.startup.register()` unless Maya is in batch. A mayapy batch job calls the first one only. A plain-Python test calls neither and gets the defaults.

The slots this design needs:

| Slot | Owner | Default (headless) | Filled by |
|---|---|---|---|
| Log writers by level | `core.log.LogOutput.writers` | stdout | `maya.startup`: `MGlobal.display*` |
| File handlers by file type | `core.asset.handlers` (exists as `asset.handlers`) | none: `NotImplementedError` | `maya.startup`: `assethandlers.register()` |
| The prompter | `core.prompts` | `NoPrompter`: raises naming the question | `maya_ui.startup`: `QtPrompter` |
| Reload hooks | `maya.devreload` | no-ops | `maya_ui.startup`: delete and rebuild the menus |

**Rules for a slot**

1. The lower layer defines the slot's type (a `Callable` or a `Protocol`) and a default that works with nothing registered. A headless run never needs a registration to get a correct result. A missing GUI service fails loudly (`NoPrompter`) rather than doing nothing.
2. A slot is read when it's used, never copied at import. So a module imported before registration, or reloaded, still sees the current value.
3. A slot is filled only through its `register_*` function, and `register_*` is called only from a layer's `startup.register()`. A test pins this (section 7), so every cross-layer wiring is in two files.
4. `register()` is idempotent and replaces values without appending them. Calling it again after a reload is safe.
5. Each slot module has `reset()` for tests, which puts the default back.

**Slot or `Event`**

These are two forms of the same idea, and each has its job:

- A **slot** holds one implementation per session: log writers, the prompter, file handlers, reload hooks.
- An **`Event`** (section 3.2) has many listeners that come and go: views watching a view-model, progress windows.

`devreload` uses a slot, not the two `Event`s this document proposed first. The menus are the only listener, and a slot makes the order explicit.

**Rejected:** passing every service as an argument down every call. A registry `call` can't carry an object, so each menu tool would need a wrapper in `maya_ui` only to pass a prompter. Explicit arguments stay where they're cheap: workflows keep a `prompter=` keyword for tests (section 3.4).

**Risks:** a slot is global state, which hides a dependency from the call site. Rules 3 and 5 contain that: the wiring is in two `startup.py` files, and tests reset what they fill. Startup order matters only for output: anything logged before `maya.startup.register()` goes to stdout, which is correct.

### 3.2 `core/observable.py`: change notification without Qt

A view-model can't use Qt signals without making `core` depend on Qt. One small class replaces them:

```python
class Event:
    """A list of listeners. Bound methods are held weakly, so a closed view is dropped without a disconnect."""
    def connect(self, listener: Callable[..., None]) -> None: ...
    def disconnect(self, listener: Callable[..., None]) -> None: ...
    def emit(self, *args) -> None: ...
```

A view connects its refresh method in its constructor. A test connects a list's `append`.

### 3.3 View-models

A view-model is a plain class with the state a window shows, the commands its buttons call, and `Event`s for what changed. View-models live in `core`, or in `maya` when they read the scene (author, 2026-11-14). For example, the Registry Editor's would be `bmaya.core.tool_registry_edit.RegistryEditSession`:

```python
session = RegistryEditSession.open(path)        # read_registry, dirty False
session.select("rigmodules.pose.flip")
session.set_field("short_name", "Flip")         # dirty True, emits changed
session.set_call("bmaya.maya.posetools", "flip_pose_interactive", {})
session.status()                                # "OK" or the first problem of check_call
problems = session.check()                      # check_registry + check_calls + layouts
session.save()                                  # refuses with the report, else write_registry
```

The window becomes widgets, connections and `session.*` calls. `list_modules`, `list_functions` and `menu_layout_paths` move to the model side (`core.tool_registry` or `maya.constants`). mayapy, or a test with no Qt, can drive every editing rule: dirty tracking, refusing a save, the stale-kwargs report, the check of layouts against the master registry.

`ProgressManager` becomes a view-model in the same way. It gets a `changed` `Event` with any number of listeners, and drops `_ui_window` and `register_callback`. Progress goes to the log in mayapy, and to a window in a GUI session.

### 3.4 `core/prompts.py`: questions as a protocol

A workflow asks questions, so asking can't be UI-only. It's an interface in `core`, implemented in `maya_ui`, and installed through the slot in section 3.1:

```python
class Prompter(typing.Protocol):
    def ask_text(self, title: str, message: str, default: str = "") -> str | None: ...
    def choose(self, title: str, message: str, options: Sequence[str]) -> str | None: ...
    def confirm(self, title: str, message: str) -> bool: ...
    def ask_file(self, title: str, file_filter: str, save: bool, folder: str = "") -> str | None: ...
    def ask_folder(self, title: str, folder: str = "") -> str | None: ...
    def ask_form(self, title: str, value: T) -> T | None: ...   # value is a dataclass instance
```

`None` (or `False`) means cancelled, as the `_ask_*` helpers return today.

| Implementation | Where | Use |
|---|---|---|
| `QtPrompter` | `maya_ui/prompter.py` | GUI sessions. Qt dialogs only (`QInputDialog`, `QMessageBox`, `QFileDialog`, a `QDialog` around the form in section 3.6), parented by the host (section 3.5) |
| `ScriptedPrompter(answers)` | `core/prompts.py` | Tests and batch jobs. Answers questions in order, records each one, and raises naming any question it has no answer for |
| `NoPrompter` | `core/prompts.py` | The slot's default. Raises `PromptError("<title>: <message> needs an answer. Call <plain function> with it.")`, so a headless run fails loudly and never hangs on a dialog |

A workflow takes `prompter=None` and falls back to the registered one, `prompts.current()`. The menu's `call` still names the `bmaya.maya` function directly, with no wrapper per tool in `maya_ui`.

Today and with this design, side by side:

```python
# now: bmaya/maya/assettools.py
def save_pose_ui() -> str | None:
    scope = bmaya.maya.posetools.get_scope()
    ...
    name = _ask_text("Save Pose", "...", "")                     # cmds.promptDialog
    if os.path.exists(path) and not _confirm_replace(...):     # cmds.confirmDialog

# proposed: same module, no UI import
def save_pose_interactive(prompter: bmaya.core.prompts.Prompter | None = None) -> str | None:
    prompter = prompter or bmaya.core.prompts.current()
    scope = bmaya.maya.posetools.get_scope()
    ...
    name = prompter.ask_text("Save Pose", "...")
    if os.path.exists(path) and not prompter.confirm("Save Pose", REPLACE_MESSAGE.format(what=...)):
```

And the workflow, tested in mayapy with no window:

```python
answers = bmaya.core.prompts.ScriptedPrompter(["idle", True])
path = bmaya.maya.assettools.save_pose_interactive(prompter=answers)
assert answers.asked[1].kind == "confirm"   # the pose existed, so it asked to replace
```

The six `_ask_*` helpers and their 17 `cmds` dialog calls leave `assettools`. `_choose_variant` stays, because it's workflow logic (one variant means no question), and calls `prompter.choose`.

### 3.5 The Maya host: `maya_ui/host.py` and `maya_ui/menus.py`

Rule 8 allows Maya UI calls only where Maya is the host, and those calls are kept in two files:

| File | Holds | Why it needs Maya |
|---|---|---|
| `host.py` | `parent_window()` (Maya's main window through `MQtUtil` and `shiboken6`, `None` outside Maya), `show(widget, *, object_name, dock=False)`, `defer(fn)` | Docking is a `workspaceControl`: `cmds` creates it, and the widget is put in it through `MQtUtil.findControl`. Outside Maya, `show` opens a plain window and `defer` is `QTimer.singleShot(0, fn)` |
| `menus.py` (from `tool_views/tool_menu.py`) | `ToolMenu`, `build_menus`, `delete_plugin_menus` | Menus on Maya's main menu bar are built with `cmds.menu` and `cmds.menuItem`, so they survive Maya's own menu rebuilds and support tear-offs |

`show` finds the open instance by `objectName` among `QApplication.topLevelWidgets()`, not by `cmds.deleteUI`. Building menus as `QMenu`s directly on Maya's `QMenuBar` was considered and rejected: Maya rebuilds its menu bar (menu sets, tear-offs), and a menu it doesn't know about isn't part of that. Views, widgets and the Qt prompter import PySide6 and their view-model only, so they open in plain Python with pip PySide6 as well as in mayapy offscreen. A test enforces this (section 7).

### 3.6 Reusable widgets: `maya_ui/widgets/`

| Widget | Replaces | Built from |
|---|---|---|
| `field_widget(hint)` and `read_value` / `show_value` | `ArgumentRow._make_widget` and its value code | A type hint: `bool`, `int`, `float`, `str`, `Literal`, `Enum`, else JSON text |
| `DataclassForm(value)` | `_ask_clip_properties`, the tool fields of the Registry Editor | A dataclass's fields and hints. `value()` returns a new instance by `dataclasses.replace` |
| `SignatureForm(function, kwargs)` | `ArgumentForm` | `inspect.signature`, with the Set boxes and the stale kwargs it has now |
| `ListModel(items, roles)` | `QListWidget` filled by hand, `setHidden` filtering | A `QAbstractListModel` over a view-model's list, with `QSortFilterProxyModel` for the search box |

The clip properties dialog becomes `prompter.ask_form("Clip Properties", choice)`, where `choice` is a small dataclass of `loop`, `root_motion` and `additive`, with no per-dialog widget code. A `Literal` hint on `additive` (or `ADDITIVE_TYPES` as an `Enum`) gives it its drop-down.

Item models are for lists that grow: controls, poses, clips, assets. The Menu Editor's tree stays a `QTreeWidget`, synced through `get_layout` and `set_layout`. A menu has about 50 rows, and a custom drag-and-drop tree model would cost far more code than it saves (simplicity over flexibility).

### 3.7 `bmaya.asset` moves to `bmaya.core.asset`

The author's call (2026-11-14). `bmaya.asset` was kept out of `core` only because `core/log.py` imported Maya, and section 3.1 removes that. After the move, `asset` may import the rest of `core` (the logger, `Event`). Its own rule stays: no `maya`, no `rigmodules`. The package moves as is, with no compatibility module at `bmaya.asset`, because no saved data names the module path (the tool registry calls no `bmaya.asset` function) and no older rigs need to survive.

Call sites found 2026-11-14 by a name search: `maya/assethandlers.py` (5), `maya/assettools.py` (41), five test files under `tests/asset` and `tests/rigging/rigmodules_menu` (32), `tests/asset/run.bat`, `tests/README.md`, mayadev `README.md` and every page in mayadev `docs/` except `tools.md` and `framework.md` (31), and the deprecated `Asset Layout.md` guide, which is left as it is. In this repo, the current-state mentions in `CLAUDE.md`, `docs/mayadev.md` and `design/mayadev-integration.md` change in the matching change. Dated decision entries keep the name they had.

### 3.8 The `*_ui` entry points are renamed `*_interactive`

The author's call (2026-11-14). After phase 1 these functions contain no UI code. Each is a menu entry point that takes its scope from the session (the selection, the active rig, the playback range) and asks for the rest through the prompter. `_interactive` says that: it needs a session or a prompter, and batch code calls the plain function beside it. Proposed rule: every menu tool's function in `bmaya.maya` that reads its scope from the session ends in `_interactive`, and a plain function with explicit arguments does the work.

| Module | Functions | Count |
|---|---|---|
| `maya/assettools.py` | every `*_ui` (`save_pose_ui` → `save_pose_interactive`, ...) | 25 |
| `maya/posetools.py` | `zero_pose_ui`, `mirror_pose_ui`, `flip_pose_ui`, `select_all_controls_ui`, `select_mirror_controls_ui`, `key_all_controls_ui`, `key_selected_modules_ui` | 7 |
| `maya/rigging/joint.py` | `zero_transforms_ui`, `orient_to_world_ui`, `orient_to_mesh_ui` | 3 |

The functions in `maya_ui` that open a window (`tool_registry_editor.show_ui`, `tool_menu.show_menu_editor_ui`) are UI, and take the name `show_window` in their own modules. Tool ids don't change, so menu layouts and tests that run tools by id keep working. Each renamed function's `call` in `toolsets/master_tool_registry.json` changes in the same change, and `check_calls` fails on any that are missed. RigModules is not affected: no command or `rigmodules` function is renamed.

## 4. What this removes and adds

| Removed | Added |
|---|---|
| `loadUiFile`, every `designerFile=` argument | `core/observable.py` (`Event`) |
| `Table`, `List`, `Tree`, `TreeItem` item-widget subclasses (`Tree` stays until the Menu Editor no longer needs it) | `core/prompts.py` (`Prompter`, `ScriptedPrompter`, `NoPrompter`, `current`, `register_prompter`, `reset`) |
| The six `_ask_*` helpers and 17 `cmds` dialog calls in `assettools` | `core.log.LogOutput` and `register_writers`, in place of `MayaLogHandler` |
| `ProgressManager._ui_window`, `register_callback`, `ProgressWindow`'s poll timer | `maya/startup.py` and `maya_ui/startup.py`, each with `register()` |
| `devreload`'s import of `maya_ui` | `devreload.register_hooks` |
| `core/log.py`'s Maya imports | `core/tool_registry_edit.py` (`RegistryEditSession`) |
| The top-level `bmaya.asset` package (moved into `core`) | `maya_ui/host.py`, `maya_ui/menus.py`, `maya_ui/prompter.py`, `maya_ui/widgets/` |
| | A plain function to capture a clip to a path |

Maintenance surface: the `Prompter` protocol becomes the interface every interactive tool is written against, so its six methods are public within mayadev, and adding a seventh is a decision. Each slot is an interface between layers, and the slot table in section 3.1 is its list. Tool ids don't change.

## 5. Phases

Each phase is reviewable on its own and leaves every menu tool working.

**Phase 1: fix the rule breaks, with the registration pass**

1. The registration pass: `LogOutput` and `register_writers` in `core/log.py`, `maya/startup.py` and `maya_ui/startup.py`, called from `userSetup.py` and the batch framework (section 3.1). `core` is plain Python after this step.
2. Move `bmaya.asset` to `bmaya.core.asset` (section 3.7).
3. `core/observable.py`. `ProgressManager` gets a `changed` `Event`, and `ProgressWindow` listens instead of polling. `devreload` gets its reload hooks slot.
4. `core/prompts.py` and `maya_ui/prompter.py`. Move the `assettools` dialogs behind the `Prompter`, and rename the `*_ui` entry points to `*_interactive` with their registry `call`s (section 3.8), in one change because it touches the same functions.
5. Add the plain function for capturing a clip to a path, and call it from `_capture_joint_clip_interactive`.
6. Delete `loadUiFile`, the `designerFile` arguments and the unused `Table` members.
7. Add the tests in section 7.

**Phase 2: view-models and host**

1. `RegistryEditSession`. Move `list_modules`, `list_functions` and `_unwrap_optional` to `core`, and `menu_layout_paths` to `bmaya.maya.constants`. Rebuild the Registry Editor on the session.
2. `maya_ui/host.py` and `maya_ui/menus.py`, replacing `get_maya_main_window`, the `deleteUI` single-instance code, `DockableWidget.dock` and `tool_views/`.
3. `field_widget`, `SignatureForm` and `DataclassForm`. `QtPrompter.ask_form` uses `DataclassForm`, and the clip properties workflow uses `ask_form`.

**Phase 3: the panels the pipeline needs, on this base**

The active rig panel, and the pose and animation library browser (`control-library.md`) with `ListModel` over a library view-model in `bmaya.maya`. Each is a separate design when its time comes. This document only fixes the pattern they follow.

## 6. Documentation impact (mayadev)

| Page | Change |
|---|---|
| `docs/architecture.md` | The layer table (three layers, `core.asset`), the registration pass and its slots, a recipe for a new slot, the `*_interactive` convention in "Add a menu tool" |
| `docs/framework.md` | Logger (`LogOutput`, writers), progress manager (`changed`), the Qt layer (host, menus, prompter, widgets) in place of `qtbase` |
| `docs/tools.md` | Every renamed function in the tool tables |
| `docs/setup.md` | The startup sequence: the two `register()` calls |
| `docs/assets.md`, `clips.md`, `rigmodules.md`, `status.md`, `README.md`, `tests/README.md` | `bmaya.asset` → `bmaya.core.asset`; `status.md` drops the `core/log.py` problem and the qtbase notes that phase 1 fixes |

## 7. Tests

| Test | Tier | Checks |
|---|---|---|
| Layer imports (AST over `py/scripts/bmaya`) | Plain Python | `core` imports nothing from `maya`, `rigmodules`, `bmaya.maya`, `bmaya.maya_ui`, PySide6. `maya` imports nothing from `bmaya.maya_ui` or PySide6. Already proposed in mayadev `docs/status.md` |
| UI stays in `maya_ui` | Plain Python | No PySide6 or `shiboken6` import, and no `cmds` UI or dialog call (`promptDialog`, `confirmDialog`, `layoutDialog`, `fileDialog2`, `window`, `button`, ...), outside `maya_ui` |
| Maya stays in the host | Plain Python | In `maya_ui`, only `host.py` and `menus.py` import `maya`, and no file calls a `cmds` dialog |
| Slots are filled only at startup | Plain Python | Every call of a `register_*` function is in `maya/startup.py` or `maya_ui/startup.py` |
| Slot defaults | Plain Python | With nothing registered: logging goes to stdout, `prompts.current()` raises on any question, the reload hooks do nothing. `reset()` restores each default |
| `maya.startup.register()` | mayapy with the plugin | Fills the log writers and file handlers. Calling it twice changes nothing |
| `Event` | Plain Python | Connect, emit, disconnect. A dead bound method is dropped |
| `ScriptedPrompter`, `NoPrompter` | Plain Python | Answers in order, records questions. An unexpected question raises and names it |
| `RegistryEditSession` | Plain Python | Dirty tracking, status, save refused with the full report, stale kwargs kept until removed |
| Workflows with a `ScriptedPrompter` | mayapy with the plugin | Each `assettools` `*_interactive` with its no-asset and replace branches. These replace today's untested dialog paths |
| Views bind | mayapy, offscreen Qt (as `test_maya_tool_editors.py` runs now) | A view shows its session's state, and a widget edit reaches the session |
| The asset tests in plain Python | Plain Python | `tests/asset/test_asset_model.py` and `test_asset_animproperties.py` still run with nothing installed after the move |

## 8. Rejected

- **Qt signals in view-models** (a `QObject` view-model): makes `core` depend on PySide6, and the view-models couldn't run in plain Python.
- **A third-party MVVM or binding library:** a dependency for a few hundred lines of binding. `Event` plus explicit connections in the view is enough at this size.
- **Generating whole windows from data** (a JSON layout of widgets): that's a `.ui` file by another name. Forms are generated from dataclasses, and window layout stays in plain Python code.
- **`cmds` dialogs for the prompter** (author, 2026-11-14): rule 8.
- **Passing services as arguments everywhere, or a `maya_ui` wrapper per interactive tool:** section 3.1.
- **View-models in `maya_ui`** (author, 2026-11-14): they'd sit in the layer that rule 6 can't depend on.
- **Two `Event`s for the plugin reload:** a slot, because the menus are the one listener (section 3.1).
- **A compatibility module at `bmaya.asset`:** nothing saved names the module, and nothing old needs to keep working.

## 9. Answered (author, 2026-11-14)

1. **Keeping layers apart:** a registration pass at startup that fills slots in lower layers (section 3.1). Phases as proposed.
2. **`bmaya.asset`** moves into `core` (section 3.7).
3. **View-models** live in `core` and `maya`.
4. **The `*_ui` names** are renamed (section 3.8). The suffix `_interactive` is this document's proposal.
5. **The prompter** uses Qt dialogs. `cmds` UI only where Maya is the host, which is the docking and the main menus (section 3.5).

Still open: the `_interactive` suffix itself, and whether `register()` should also start the active rig tracker, which `userSetup.py` starts now.
