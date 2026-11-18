---
paths:
  - "python/**"
  - "cpp/maya/commands/**"
  - "docs/mayadev.md"
  - "docs/python.md"
  - "docs/commands.md"
  - "design/mayadev*.md"
  - "design/attachments-mayadev*.md"
  - "design/tool-registry.md"
---

# The mayadev boundary

Loaded when working on commands, the Python package or the mayadev pages. Moved out of CLAUDE.md so it is read only for that work.

The author's Python pipeline lives in a separate repo: https://github.com/benmorgantd/mayadev, local checkout `C:\Users\bmorgan\Documents\dev\maya\mayadev`, package `bmaya` under `py/scripts/bmaya`. The author uses it for the asset file model, the menus and tools, the active rig, skinning, animation and GUI tools. Read `design/mayadev-integration.md` before touching either side of the boundary. The boundary is decided (D24).

**mayadev code is visible from here.** Its documentation is in its own `docs/` folder (`docs/README.md` there maps the pages; `docs/rigmodules.md` is the list of every command and `rigmodules` function it calls, and `docs/status.md` its known problems). `docs/mayadev.md` in this repo is the reverse view for someone changing RigModules. In a cloud session `benmorgantd/mayadev` is in the repository scope: attach it with `add_repo` when it isn't cloned, and read it there. **Before renaming, removing or reshaping a command, flag, return value or `rigmodules` function, search `py/scripts`, `toolsets` and `tests` in mayadev for it**, and list the call sites it breaks in the change summary. The menu tools in `toolsets/master_tool_registry.json` are a module, a function and keyword arguments (D47), so a `rigmodules` function named directly there is checked by mayadev's `check_calls`. A command called inside a mayadev function is not, so a name search is still the only check for it. Propose mayadev edits to the author instead of making them.

- **Dependency direction:** mayadev depends on RigModules, never the reverse. mayadev tools reach rig internals only through the RigModules Python query API, never by walking node connections or parsing names.
- **Legacy rig builder:** mayadev has its own builder (`maya/rigging/base/` with `Rig`, `Module`, `Layout`, plus `maya/rigging/modules/`). It uses cmds constraints, DAG offset groups, name-based joints and `L/R/M` sides. It is deleted on mayadev main (branch `asset-tools`, merged), along with `rigManager` and the legacy `bmaya.maya.asset`. The tag `legacy-rig-builder` keeps it, and an untracked copy sits in mayadev's ignored `legacy_reference/` folder for reading. mayadev stays the pipeline repo, and its asset tools (`bmaya.maya.assettools`) use `rigmodules.query` (D30). No rigs built with it need a migration path. Do not extend it, port its code, or copy its conventions into RigModules. Read its modules only as a list of animator-facing requirements when a new module goes through the gate.
- **What mayadev holds now:** `bmaya.core.asset` (Maya-free: asset ids of any depth with the same rules as `core/AssetId` here, `project.json`, `asset.json`, skeletons, rigs and looks (D54), the registry, publishes, local versions, file handlers: D29), `bmaya.maya.assethandlers` and `assettools` (the only code that calls RigModules, with `activerig` and `posetools`; menu entry points are `*_interactive` and ask through a `Prompter`, D49), the rest of `core/` (plain Python: log, progress manager, `Event`, prompts, tool registry, batch framework), `maya/startup.py` and `maya_ui/startup.py` (the registration passes, D49), `maya/eventsregistry.py`, `maya_ui/` (Qt base classes, the Qt prompter and forms, the editors), the tool menus (`toolsets/`), and older skinning, joint and utility code in `maya/rigging/` and `maya/util/` that is not yet on the query API. The D49 `ui-phase1` branch is merged into mayadev `main`. D54 phase 4 (skeletons, rigs and looks: `Asset.skeletons`, `rigs`, `looks`, `rig.json` and `look.json`) is on mayadev branch `rigs-and-looks-phase4`. The attachment side of D55 (`rig.json` `parent`, `build_rig` with a parent, `apply_look` for a look that spans several skeletons) is on mayadev branch `attachments-phase8`, uncommitted.
- **Same words, different meanings:** both repos have `Layout`, `FkChain` and `LayoutRig.json`. In this repo they always mean the RigModules ones. Call mayadev's versions "legacy".
- mayadev is a different repo with its own conventions. This file's rules (Allman, docs pass, suite) apply only to this repo. Changes to mayadev need the author's go-ahead and a commit in that repo.

Rule: if logic can live in the core, it goes in `core/`. The plugin layer should be thin adapters. A new file goes in `core/` when it needs no Maya header, including through what it includes.

