# mayadev tool registry: fixes and format version 2

**Status:** proposal (D47, Leaning). Nothing here is built. It is written from the author's review of 2026-11-14. The code it changes is in mayadev, and mayadev keeps its design history here (mayadev `docs/README.md`).

The registry stays three kinds of plain JSON file, edited by hand or in the two editors: one registry of tools and one layout per menu. What changes: every file is checked before it is used, with every problem reported at once. A tool names a function instead of carrying a Python string. Tools get readable ids. And the files carry a version with update steps.

## 1. What exists now

| Piece | File (mayadev) |
|---|---|
| `Tool`, `ToolRegistry` (load, save, by GUID) | `py/scripts/bmaya/core/tool_registry.py` |
| `ToolMenu` (reads a layout, builds a Maya menu, runs `script` with `exec`) and the Menu Editor | `py/scripts/bmaya/maya_ui/tool_views/tool_menu.py` |
| Registry Editor | `py/scripts/bmaya/maya_ui/tool_registry_editor.py` |
| Startup menus, and the RigModules menu rebuild | `py/scripts/userSetup.py`, `py/scripts/bmaya/maya/devreload.py` |
| Data | `toolsets/master_tool_registry.json` (48 tools), `bmaya_menu.json`, `rigmodules_menu.json` |

Of the 48 scripts, 41 are an import and one call. The other 7 are short inline snippets, and `Create a Cube` is in no menu.

## 2. Phase 1: fixes on the current format

These land first, on the current files, with tests. Phase 2 keeps every one of them.

| Problem | Fix |
|---|---|
| Opening a registry in an editor merges it into the one already loaded (`load_registry` never clears `tools`), and Save writes the union | Loading returns a new registry instead of filling one in place (`read_registry(path) -> Registry`). The editor replaces its registry on Open, and asks first when there are unsaved edits |
| The Menu Editor saves `"enabled": true` for every tool item, and ignores `enabled` when it loads | Tool rows in the editor are checkable, and the check box is the item's `enabled`. Load and save go through the layout dataclasses (section 3.3), so every key round trips |
| Unknown keys are dropped on load and lost on the next save | An unknown key is a check error (section 3.4), not something to drop or keep. New keys come with a format version (section 3.5). Keeping unknown keys was rejected: a misspelled `enabeld` would survive forever and do nothing |
| A missing registry gives an empty registry, so every item reads "Missing Tool", and nothing reports it | `read_registry` raises `FileNotFoundError` naming the path. `userSetup` is the top-level handler: it logs the error with `log.error` and finishes startup without menus, so the plugin and the active rig tracker still start. Reload Menu (dev) lets it raise |
| A layout item with no `guid` crashes the build on `guid[:8]`, and an item with no `type` is treated as a tool | Covered by the check pass: both are errors in the report, before any menu is touched |
| Scripts run in `tool_menu`'s module globals (the Registry Editor's Run uses its own module's globals, and the tests use `{}`) | Not fixed in phase 1 (author, 2026-11-14). Phase 2 removes `exec` (section 3.6), so a fix here would be thrown away. Until then, a script must import everything it uses, which is already the documented rule |
| Files are opened without an encoding | Read and write UTF-8, and write with `ensure_ascii=False`, so a label stays readable in a diff |
| Saving writes in place, so a crash mid-write truncates the source of truth | Write to `<file>.tmp` in the same folder, flush and `os.fsync`, then `os.replace` over the original. The check runs before the write, so an invalid registry is never written |
| Reload Menu (dev) rebuilds only the RigModules menu | `build_menus()` (section 3.7) |

## 3. Phase 2: format version 2

### 3.1 Files

Registry:

```json
{
    "version": 2,
    "tools": [
        {
            "id": "rigmodules.pose.mirror_left_to_right",
            "name": "Mirror Pose Left to Right",
            "short_name": "Pose L>R",
            "call": {
                "module": "bmaya.maya.posetools",
                "function": "mirror_pose_ui",
                "kwargs": { "left_to_right": true }
            },
            "icon": null,
            "enabled": true
        }
    ]
}
```

Layout:

```json
{
    "version": 2,
    "label": "RigModules",
    "items": [
        {
            "type": "submenu",
            "label": "pose",
            "items": [
                { "type": "tool", "tool": "rigmodules.pose.mirror_left_to_right", "enabled": true },
                { "type": "separator" }
            ]
        }
    ]
}
```

| Registry key | Meaning |
|---|---|
| `id` | Identity, and how layouts (and the planned shelf) refer to a tool. Public, like a RigModules control name: renaming one breaks every layout that uses it |
| `name` | The menu label |
| `short_name` | The shelf label. Kept for the planned shelf view (author, 2026-11-14). Required in the file. The editor fills it from `name` for a new tool |
| `call.module`, `call.function` | The function the tool runs, by import path |
| `call.kwargs` | Keyword arguments, JSON values only (bool, number, string, null, lists of those). Omitted arguments take the function's default. No positional arguments: names survive a reordered signature, and `inspect.Signature.bind` checks them |
| `icon` | Optional image for the menu item |
| `enabled` | `false` greys the tool out wherever it appears |

Every key is written on every save, so a file always shows the whole entry. `script` is gone. A menu item holds one call and no code (author, 2026-11-14), and that call is the same function a script or batch job uses.

### 3.2 Ids and uniqueness

- **Format:** dot-separated lowercase segments, `^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)*$`.
- **Convention:** `<area>.<topic>.<action>`: `rigmodules.pose.flip`, `rigmodules.clip.capture_joint_clip`, `bmaya.joints.orient_to_world`, `bmaya.tools.registry_editor`. Ids are named by topic, not by menu position, because one tool can sit in several layouts and on a shelf.
- **Unique (each a check error):**
  - Within the registry: `id` and `name`, with `name` compared after `casefold()`. `short_name` is not checked until the shelf view is built (author, 2026-11-14). The shelf adds that rule.
  - Within a layout: a tool appears at most once, and sibling submenu labels differ.
  - Every layout `tool` reference names an id in the registry.
- **Creating and renaming:** the Registry Editor asks for the id when a tool is created and checks it as it is typed. After that it is read-only. A rename is a hand edit of the registry and every layout. The check pass catches anything missed. No rename tool, because renames should be rare.

### 3.3 Dataclasses

Plain `dataclasses` in `bmaya/core/tool_registry.py`, which stays Maya-free. They are not frozen, because the editors edit them.

```python
@dataclasses.dataclass
class ToolCall:
    module: str
    function: str
    kwargs: dict[str, JsonValue] = dataclasses.field(default_factory=dict)


@dataclasses.dataclass
class Tool:
    id: str
    name: str
    short_name: str
    call: ToolCall
    icon: str | None = None
    enabled: bool = True


@dataclasses.dataclass
class Registry:
    tools: dict[str, Tool]      # by id, in file order


@dataclasses.dataclass
class ToolItem:
    tool: str
    enabled: bool = True


@dataclasses.dataclass
class Separator:
    pass


@dataclasses.dataclass
class Submenu:
    label: str
    items: list["LayoutItem"]


LayoutItem = ToolItem | Separator | Submenu


@dataclasses.dataclass
class Layout:
    label: str
    items: list[LayoutItem]

    def tool_items(self) -> Iterator[ToolItem]:
        """Every tool item, depth first."""
```

`Layout.tool_items()` replaces the three tree walks there are today (menu build, editor load, the menu test's `_walk`). `ToolMenu` takes a `Layout`, not a path. The editors convert their tree to and from a `Layout`, and the dataclasses handle the JSON.

Rejected: pydantic, which is a new dependency in Maya's Python for a schema this small; and JSON Schema files, which would be a second definition of the same keys.

### 3.4 The check pass

A read-only pass over the raw JSON, run before anything is built from it. It returns every problem, not the first one.

```python
@dataclasses.dataclass
class Issue:
    where: str      # "tools[12] (rigmodules.pose.flip).call.kwargs"
    message: str


def check_registry(data: dict) -> list[Issue]: ...
def check_layout(data: dict, tool_ids: Collection[str]) -> list[Issue]: ...
def check_calls(registry: Registry) -> list[Issue]: ...


class ToolDataError(Exception):
    """Raised with every Issue of one load or save. Its message lists them, one per line, under the file path."""
```

| Level | Checks | Needs | Runs |
|---|---|---|---|
| Structure (`check_registry`, `check_layout`) | Required keys, types, unknown keys, item `type` values, the id format, every uniqueness rule in 3.2, layout references against the registry | Plain Python | On every read and before every write. Startup and Reload Menu stop here |
| Calls (`check_calls`) | The module imports, `function` is a callable in it, `kwargs` bind to its signature, required parameters are given, and each value matches a simple annotation (`bool`, `int`, `float`, `str`, `None`, unions and `Literal` of those; others are not checked) | Imports the tool modules, so Maya for most | The Registry Editor's Save, and the test suite. Not at startup, which would import every tool module before anyone uses them |

The pass stops early only when the file isn't JSON or its version can't be read, because nothing after that point means anything. `read_registry` and `read_layout` raise `ToolDataError` when the structure check finds anything. `build_menus` collects the issues of the registry and every layout before it raises, so one Reload Menu shows every broken file.

Example message:

```
toolsets/master_tool_registry.json: 3 problems
  tools[5] (bmaya.joints.orient_to_world): unknown key 'enabeld'
  tools[9]: id 'rigmodules.pose.flip' is already used by tools[4]
toolsets/rigmodules_menu.json: 1 problem
  items[6].items[2]: tool 'rigmodules.pose.flipp' is not in the registry
```

### 3.5 Versions and update steps

Each format has its own version: the registry, and the layout. Each has an update table, the Python form of RigModules' `FormatUpdate` (D33):

```python
REGISTRY_VERSION = 2
REGISTRY_UPDATES: dict[int, Callable[[dict], None]] = {}   # n -> step from n to n+1, in place on raw JSON


def upgrade(data: dict, current: int, updates: dict[int, Callable[[dict], None]]) -> None:
    """Runs the steps from data["version"] to current in order. Raises ToolDataError for a version newer than
    current, or for a gap in the table."""
```

- Reading runs the updates, then the structure check, in that order. The check sees the current format only.
- An update step works on raw JSON alone. It reads no other file and imports no tool module, so it can't break when a module is renamed. It uses `.get`, because it runs before the check. Each step has a test with a file of the old version.
- Saving always writes the current version. Updating files on disk is just open and save.
- A version newer than the code raises "written by a newer mayadev".
- No version per tool (author, 2026-11-14). A tool's contents are content, like a control's shape. Only the shape of the file is versioned.

**Version 1 to 2 is not an update step.** It has to read two files at once (a layout's GUIDs become ids only through the registry), and the 7 inline scripts need functions written by hand. So it is a one-time script in mayadev that rewrites the three repo files together, and it is deleted once it has run. These are the only version 1 files, and no other layouts exist (personal project). A file without `version` then fails with "version 1 is no longer read". `REGISTRY_UPDATES` and `LAYOUT_UPDATES` start empty at version 2, and the next format change adds the first step.

Where the 7 inline scripts go (agreed, author, 2026-11-14). Each becomes a function that a script can call too:

| Tool | Proposed function |
|---|---|
| Print Joint Markup, Remove Joint Markup | `bmaya.maya.assettools.print_joint_markup_ui`, `remove_joint_markup_ui` |
| Select All Controls, Select Mirror Controls | `bmaya.maya.posetools.select_all_controls_ui`, `select_mirror_controls_ui` |
| Mirror Shapes Left to Right / Right to Left | `bmaya.maya.assettools.mirror_control_shapes_ui(left_to_right: bool)` |
| Zero Transforms | `bmaya.maya.rigging.joint.zero_transforms_ui` |
| Orient to World, Orient to Mesh | Already functions: `bmaya.maya.rigging.joint.orient_to_world`, `orient_to_mesh` wrapped by `_ui` functions that read the selection. This also fixes the `joints` to `joint` typo |
| Create a Cube | Deleted: it is in no menu |

### 3.6 Running a tool

```python
def resolve_call(call: ToolCall) -> Callable[..., object]: ...
def run_tool(tool: Tool) -> object:
    """resolve_call(tool.call)(**tool.call.kwargs)"""
```

- The import happens on each click, not at menu build. So Reload RigModules (dev), which drops `rigmodules` from `sys.modules`, gets the new code, and a module that fails to import breaks only its own items.
- A module or function that can't be found raises `ToolError("Tool rigmodules.pose.flip: no function 'flip_pose_ui' in bmaya.maya.posetools")`.
- An exception from the function itself is raised unchanged. Its traceback now shows the function's own file and line, not `<string>`.
- The menu item's command is `lambda *_, tool=tool: run_tool(tool)`. The Registry Editor's Run button calls `run_tool` on the tool as it is being edited. The menu tests call `run_tool(registry.tools["rigmodules.pose.flip"])` by id. Nothing calls `exec`.

### 3.7 One `build_menus`

```python
# bmaya.maya.constants
MENUS = (
    MenuSpec(menu_id="bmaya_menu", layout="bmaya_menu.json", plugin=None),
    MenuSpec(menu_id="rigmodules_menu", layout="rigmodules_menu.json", plugin="RigModules"),
)

# bmaya.maya_ui.tool_views.tool_menu
def build_menus() -> None:
    """Reads the registry and every layout, checks them all, then deletes and rebuilds every menu.
    A menu whose plugin isn't loaded is deleted and not rebuilt.
    Raises ToolDataError before touching a menu, so a bad edit leaves the current menus in place."""
```

`userSetup` and `devreload` both call it. Reload Menu (dev) then rebuilds both menus. The bmaya menu's Maya id changes from `test` to `bmaya_menu`.

**A menu with a `plugin` is built only while that plugin is loaded** (author, 2026-11-14). The check still covers its layout either way, so a broken RigModules layout is reported even without the plugin.

The plugin flag has one consequence. Today Unload RigModules (dev) rebuilds the RigModules menu without the plugin, only so that Reload stays reachable. With the flag, the menu disappears on unload. So the three dev tools (Reload RigModules, Unload RigModules, Reload Menu) move from the RigModules menu's `dev` submenu to a `dev` submenu in the bmaya menu, which has no plugin and is always there. `devreload` then calls `build_menus()` with no exception of its own. The RigModules menu loses its `dev` submenu, and its layout test changes from nine submenus and 41 tools to eight and 38.

### 3.8 Editors

**Registry Editor.** The call picker is a hybrid of typing and choosing:

| Field | Behaviour |
|---|---|
| Module | A text field with a completer over every module under `bmaya` and `rigmodules`, listed with `pkgutil` without importing leaf modules. Any import path can be typed |
| Function | A drop-down, filled when the module imports: the public callables defined in that module (`__module__` matches, no leading `_`), with the first docstring line shown under it |
| Arguments | A form built from `inspect.signature`, one row per parameter: name, a widget chosen by annotation, and a **Set** check box. An unset row shows the default greyed and isn't saved, so `kwargs` holds only what was chosen. A parameter with no default is always set |
| Status | "OK", or the first `check_calls` issue for this tool. Run is enabled only on OK |

| Annotation | Widget |
|---|---|
| `bool` | Check box |
| `int`, `float` | Spin box |
| `str` | Text field |
| `Literal[...]`, an `Enum` | Drop-down |
| `X \| None` | The widget for `X`. Unset means `None` when that is the default |
| Missing or anything else | A text field holding JSON, checked with `json.loads` |

Example: Mirror Pose, Mirror Pose Left to Right and Mirror Pose Right to Left are all `mirror_pose_ui`. The `left_to_right` row is unset, set and ticked, or set and unticked.

A saved kwarg that the function no longer has shows as a red row with a Remove button. That is the same issue `check_calls` reports.

Rejected: free text only, which brings back typos; and cascading drop-downs (package, module, function), which take three clicks through deep trees and still need typing to search.

Other Registry Editor changes:
- **New Tool** asks for the id (checked as typed) and the name. `short_name` starts as the name.
- The id is read-only after that.
- **Save** runs the structure check and `check_calls` on the whole registry, and checks every layout in `MENUS` against it. Deleting a tool a layout still uses is an error that names the layout. Save refuses with the full report, and otherwise writes atomically.

**Menu Editor:**
- Tool rows are checkable (`enabled`), and show the id as a tooltip.
- Open uses `read_layout`, so a bad file is refused with its report, not loaded partly.
- Save uses `write_layout`, which checks against the loaded registry.
- The three copies of the insert-a-row code become one helper, because they are being edited anyway.

## 4. Tests (mayadev `tests/`)

| Test | Tier |
|---|---|
| Registry and layout read and write: the repo files round trip byte for byte, including `enabled: false` on a layout item and a non-ASCII label | Plain Python |
| One bad file reports every issue: a fixture with an unknown key, a duplicate id, a duplicate name, a bad id format and a missing key gives five issues | Plain Python |
| Layout checks: a dangling reference, the same tool twice, an item without `type`, an unknown item type, duplicate sibling submenus | Plain Python |
| Missing file raises. A version newer than current raises. A file without `version` gives the version 1 message | Plain Python |
| Atomic write: a serialization that fails partway leaves the original file unchanged and no `.tmp` behind | Plain Python |
| `upgrade` runs steps in order and rejects a gap. Each future step gets a fixture of its old version | Plain Python |
| `check_calls` on fixtures: missing module, missing function, an unknown kwarg, a missing required argument, a wrong value type | Plain Python, with fixture modules |
| `check_calls` on the repo registry, and every layout in `MENUS` against it | mayapy (the tool modules import `maya.cmds`) |
| The existing menu script tests, run through `run_tool` by id | mayapy with a plugin build, as now |
| `build_menus` raises before deleting a menu when a layout is broken | Not covered: Maya standalone has no `MayaWindow`. The check half runs in plain Python |
| Editor tree to `Layout` and back | mayapy with a `QApplication`, or not covered (decide at implementation) |

This closes two gaps listed in mayadev `docs/status.md`: the registry has no test, and nothing checks that registry imports resolve.

## 5. Docs and the RigModules side

- **mayadev:**
  - `docs/tools.md`: entry and layout tables, ids, the editors, Reload Menu.
  - `docs/setup.md`: startup menus, and the RigModules menu always built.
  - `docs/architecture.md`: the recipe for adding a tool.
  - `docs/status.md`: the fixed problems and the closed test gaps.
  - `tests/README.md`.
- **RigModules:**
  - `CLAUDE.md` and `docs/mayadev.md` say the registry scripts are "Python strings in JSON, so a name search is the only check". After phase 2, a RigModules command is still called inside a mayadev function, so the name search stays. A renamed `rigmodules` function used directly in a `call` is now caught by `check_calls`.
  - D47 in `decisions.md`.

## 6. Answered questions (author, 2026-11-14)

1. **The 7 inline scripts' functions** go where the table in 3.5 puts them.
2. **The RigModules menu keeps a plugin flag** in `MenuSpec`, and is built only while the plugin is loaded. The dev tools move to the bmaya menu so they stay reachable after an unload (3.7), approved by the author.
3. **The `__main__` globals fix is skipped.** Phase 2 removes `exec`.
4. **`short_name` uniqueness waits for the shelf view.**
