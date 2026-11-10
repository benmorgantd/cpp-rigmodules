# RigModules tests

Python `unittest` tests that run in mayapy against the built plugin (D20).

## Running

Build the Release configuration, then run `tests\RigTests.cmd` (double-click or terminal).

| Command | Does |
|---|---|
| `RigTests.cmd` | every tier, stopping at the first tier that fails |
| `RigTests.cmd --keep-going` | run every tier even after one fails |
| `RigTests.cmd fkchain naming` | only tests whose id contains `fkchain` or `naming`. Earlier tiers are not run as prerequisites. |
| `RigTests.cmd -v` | print each test as it runs |
| `RigTests.cmd --update-golden golden` | rewrite the golden snapshots. Review the diff before committing. |
| `RigTests.cmd --plugin path\to\RigModules.mll` | test another build |

### From a mayapy that is already running

To check a change, a script often starts mayapy and loads the plugin anyway. End that script with the suite instead of starting Maya a second time:

```python
sys.path.insert(0, r"<repo>\tests")
import run_tests
ok = run_tests.run_in_current_maya()          # filters=["fkchain"], verbose=True, keep_going=True also work
```

- **Speed:** a full run takes about 4 s and prints the same tier report.
- **Call it last.** Every test starts with `file -new`, so the script's own scene is gone afterwards.
- **No crash protection.** If Maya crashes, the script dies with it, so rerun with `RigTests.cmd` to find the test.
- **Never in interactive Maya:** it refuses to run there, because `file -new` would throw away your scene. It also refuses if the plugin is not loaded.

**Setup:**
- The runner copies the `.mll` to a temp folder before loading it, so the build output stays unlocked.
- mayapy comes from `MAYA_LOCATION`, or the Maya 2026 default path if that is unset.

**Output:**
- Maya's own output goes to a log file whose path is printed at the end.
- The exit code is 0 only when nothing failed.
- While a tier runs it prints one mark per test (`.` pass, `F` fail, `E` error, `x` expected failure, `X` unexpected pass). Each failure's message follows the tier's line.
- The window waits for a key press when the script is started by double-click or from PowerShell.

## Tiers

Maya tests can't each run in isolation. If the plugin doesn't load, every test fails, and if building breaks, every behaviour test fails. To keep that readable, tests run in tiers, in dependency order:

| Tier | Proves | Test files |
|---|---|---|
| 1 registration | the plugin loads, every node type, command and long flag exists, and the source layout holds (`core/` Maya-free, known folders, include paths, sorted include blocks, braces on their own lines) | `concepts/test_plugin`, `concepts/test_source_layout` (reads the source tree, not the scene) |
| 2 read only | commands and checks that never build a rig | joint markup command, control shapes and bounds (`concepts/test_control_shapes`), template loading, validation cases that expect errors |
| 3 build smoke | every fixture template builds | `concepts/test_smoke` (each golden file's template and skeleton, plus valid validation cases), `commands/test_create_rig` |
| 4 modules | each module poses correctly, and the rig around it is wired and named right | `modules/*`, sockets, naming, undo/redo, control markup and the control commands (`commands/test_rig_control_cmds`) |
| 5 constraints | constraint commands and space switching | `commands/test_constraint_cmds`, `commands/test_constraint_types` (Orient and Point), `concepts/test_docs_examples` (the README quick start ends with a space switch), `concepts/test_mirror` (mirrored templates include mirrored constraints), `commands/test_rig_pose_cmds` (the animation interface: channels, zero, mirror and flip pose, D34) |
| 6 known bugs | expected failures. Never blocks anything. | `known/*` |

- **Gating:** a tier runs only if every earlier tier passed. A blocked tier prints `NOT RUN (blocked by ...)` instead of a failure for every test in it, so the first failure shown is the root cause. Within a tier, every test runs, each from a new scene.
- **One Maya process, watched:** `run_tests.py` starts a single mayapy process that runs every tier, and watches it. That process writes each outcome as it happens and records the running test's id before the test starts.
  - **Crash:** the report names the test that caused it, keeps every result from before it, and marks the later tiers `NOT RUN`. With `--keep-going`, a new Maya process starts at the next tier.
  - **Startup failure:** if mayapy or the plugin fails to load, the report shows the error, with the end of the Maya log when there is no Python error to show.
  - **No crash dialogs:** the Maya process runs with `MAYA_DISABLE_CER=1`, so a crash opens no "Error Report" window. `TEMP` points at the run folder, so crash dumps and recovery files stay out of your Temp folder. After a crash, the runner also closes any `senddmp.exe` or `cer_dialog.exe` that the crashed process started. It finds them by parent process id, so your own Maya sessions are never touched.
- **Where tiers are defined:** `TIERS` in `run_tests.py` is the one place that says which file runs in which tier. A test file missing from it fails the run before anything starts.
- **Future tiers:** animation, then import/export, go after constraints, because they need working modules and constraints.

## Layout

```
tests/
  RigTests.cmd, run_tests.py     entry point and runner
  ViewportGallery.cmd            manual VP2 check of every control shape (not part of the suite)
  viewport/gallery.py            the script ViewportGallery.cmd runs in a GUI Maya
  maya/
    rigtest/                     shared helpers (no tests)
      case.py                    RigTestCase: new scene per test, build helpers, assertions
      cases.py                   loaders for the three data-driven case types
      module_contract.py         ModuleContract: tests every module must pass
      fixtures.py                reading skeletons, templates, case files; JSON pointer edits
    fixtures/
      skeletons/                 marked skeletons, built in the 'skel' namespace
      templates/                 test templates (independent of RigTemplates/)
      poses/<module>/            pose cases
      validation/                validation cases
      weights/                   hand-written skin weight files, loaded by commands/test_skin_weight_cmds.py
    golden/                      golden interface snapshots
    modules/                     one file per rig module
    commands/                    one file per Maya command
    concepts/                    naming, sockets, undo/redo, validation, loading, plugin, doc examples
    known/                       known bugs as expected failures
```

## What goes where

Expected results that are data (a pose and the transforms it gives, a template edit and the errors it gives) live in JSON. Step-by-step behaviour (undo sequences, namespaces, command errors) lives in code.

### Skeleton: `fixtures/skeletons/<name>.json`

```json
{
  "joints": [
    { "name": "a_jnt", "side": "Center", "usage": "a", "translate": [0, 0, 0], "rotate": [0, 0, 30] },
    { "name": "b_jnt", "side": "Center", "usage": "b", "translate": [0, 2, 0], "parent": "a_jnt", "export": false }
  ]
}
```

- **Required keys:** `name`, `side`, `usage` and `translate`.
- **Optional keys:** `rotate`, `parent`, `export` and `skin`. They default to `[0, 0, 0]`, no parent, `true` and `true`.
- **Values** are world space, rotation in degrees (XYZ order), whether or not the joint has a parent. Joints are created in file order, so a parent comes before its children.
- **Rotated sockets:** a skeleton without `rotate` gives every socket a translation-only rest. Use a rotated skeleton (`fk_two_chains_rotated`) when the math under test composes rotations.

### Pose case: `fixtures/poses/<module>/<name>.json`

```json
{
  "description": "Rotating the first FK control 90 degrees about Z swings the whole chain around the origin",
  "skeleton": "fk_three",
  "template": "fkchain_three",
  "set": { "a_ctrl_c.rotateZ": 90 },
  "expect": {
    "b_jnt": { "translate": [-2, 0, 0], "rotate": [0, 0, 90] }
  },
  "tolerance": 0.0001
}
```

- **Required keys:** `description`, `skeleton`, `template` and `expect`.
- **Optional keys:** `set`, which defaults to no changes, so a case with no `set` is a rest-pose test, and `tolerance`, which defaults to 1e-4.
- **Node names** are written without a namespace and resolve in `rig:`. `RigTestCase.build` passes `asset=config.ASSET_ID` (`tests|rig`), whose last token gives that default namespace (D29).
- **Values** are world space, rotation in degrees. Rotations are compared as matrices, so equivalent euler triples pass.
- **Partial checks:** only the nodes and channels listed are checked.
- **Hand-written only:** work expected values out by hand. Never copy them from what the rig currently does, because a recorded value turns a bug into the expected result.

### Validation case: `fixtures/validation/<name>.json`

```json
{
  "description": "A parentStructure value must name an existing socket",
  "base": "layout_rig",
  "skeleton": "layout_rig",
  "edits": [ { "set": "/rigModules/1/parentStructure/start", "value": "fkchain_1.ned" } ],
  "expectErrors": [ { "path": "rigModules[1] 'fkchain_2'.parentStructure.start", "contains": "ned" } ]
}
```

- **Edits:** `edits` applies `{"set": pointer, "value": v}` or `{"remove": pointer}` to the base template in order. Pointers are JSON pointers.
- **Matching errors:** each `expectErrors` entry must match one error line. The line must start with `path` (optional) and contain `contains` after it. Match on a name or key, not on wording, so error messages can be reworded.
- **Failed builds:** when errors are expected, the test also checks that nothing was built and that the `rig` namespace was not created.
- **Valid templates:** `"expectErrors": []` means the template must build.

### Golden snapshot: `golden/<name>.json`

Golden snapshots record the rig's public names: modules with their sockets and child plugs, controls with their keyable attributes, markup and space labels, joints, and constraint labels.

- **Adding one:** write `description`, `template` and `skeleton`, then run `RigTests.cmd --update-golden golden`.
- **When one fails:** a public name changed, which breaks saved poses, animation and exports. If the change is intended, update the golden file and record it in `design/decisions.md` in the same change.

### Unknown keys

Every fixture rejects unknown keys, the same rule templates follow, so a typo fails instead of silently testing nothing.

## Adding tests

Every new test file goes into `TIERS` in `run_tests.py`, in the earliest tier whose prerequisites it needs and no earlier. A test that builds a rig cannot go in tier 2, and a test that uses constraints cannot go in tier 4. A file that mixes tiers is split.

- **New module:** create `modules/test_<module>.py` with a `ModuleContract` subclass (template, skeleton, node type and joint owners), `add_pose_cases(<Class>, "<module>")`, and module-specific tests. Add the fixtures it needs, a `fixtures/poses/<module>/` folder and a golden snapshot.
- **New command:** create `commands/test_<command>.py`. Cover flags, objects passed by name (the selection is never used, apart from `setRigMeshId` and `pruneToMaxInfluences` with no meshes named), every error path, and undo/redo. Add its long flags to `concepts/test_plugin.py`.
- **Python package:** a test of `python/rigmodules` goes in the tier its command needs (`commands/test_rig_info.py` builds a rig, `concepts/test_python_template.py` reads a file). The runner puts `python/` on `sys.path`, so tests import `rigmodules` directly.
- **New template check:** add a validation case. Write a code test in `concepts/test_template_validation.py` only when the check needs a scene change, such as a driven OPM.
- **Bug fix:** write the failing test first. If the bug can't be fixed yet, put the test in `known/` with `@unittest.expectedFailure` and name the open item in `design/decisions.md`.

### Temp probes become tests

While working out a change, a throwaway mayapy script is often the fastest way to look at what the rig does. That is fine, but a check that only lives in a scratch script protects nothing later. Before a change is finished, turn every check a probe made into a suite test, using the rules above. If you are not sure it belongs, list it as a proposed test instead (what it checks, which tier, data case or code).

## Viewport gallery

The suite runs in mayapy, which has no viewport, so it checks control bounds but never sees a control drawn. Batch renders (`mayabatch`, `ogsRender`) draw meshes but skip curves and draw override UI drawables, so they can't check it either. After changing `RigControlNode`'s draw code or the shape library, run:

```powershell
cmd /c tests\ViewportGallery.cmd
```

It starts Maya with a window (`maya.exe -command`), builds every shape beside a -1 to 1 reference cube with a red line along +X, playblasts ten views, closes Maya and opens the image folder (`%TEMP%\RigViewportGallery\out`). It takes about a minute. Pass a plugin path to check another build. The docstring at the top of `viewport/gallery.py` says what each image should show.

- **Exit code:** 0 when every image was written. It does not judge the images. Look at them.
- **Unattended:** like the suite, it skips `userSetup.py` and turns off the crash reporter, so nothing waits for input.

## Gotchas

- `cmds.namespace(add=..., setNamespace=...)` is undoable and clears the redo queue. Between an undo and a redo, use `om.MNamespace` instead.
- A Maya command error arrives as a `RuntimeError` whose text is everything the command passed to `displayError`. `assertMayaError` checks that text.
