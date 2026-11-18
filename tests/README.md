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
| `RigTests.cmd --durations [N]` | after the TOTAL line, print the time spent outside the tests (Maya startup) and the N slowest test files and tests (default 10) |
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

### Launching Maya (mayalaunch)

Every script here that starts Maya (the suite runner, the viewport gallery, `perf_scenes.py`, the shape generator) does it through `python/mayalaunch` (D48), a plain-Python package beside `rigmodules`. mayadev's batch framework calls it too. A new script that starts Maya adds `python/` to `sys.path` and calls `mayalaunch` instead of setting `MAYA_DISABLE_CER`, copying the plugin, calling `os._exit` or writing a `maya.exe -command` string itself.

| Module | Does |
|---|---|
| `install` | `find_maya_dir`, `mayapy_exe`, `maya_exe`: the argument, else `MAYA_LOCATION`, else the Maya 2026 folder. A missing install raises `FileNotFoundError` naming what was tried |
| `process` | `child_environment` (`MAYA_DISABLE_CER`, `MAYA_SKIP_USERSETUP_PY`, `PYTHONDONTWRITEBYTECODE`, and `TEMP` inside a run folder), `copy_plugin`, `run_watched` and `run_mayapy` (a child with a timeout: on timeout the process tree stops by pid, and after a crash or timeout the `senddmp.exe` and `cer_dialog.exe` it started are closed) |
| `standalone` | Inside a mayapy: `start_standalone`, `Redirect` (Maya's C++ output and Python's print into a log), `stop_standalone`, `finish_standalone` (uninitialize, flush, `os._exit`: Maya 2026 crashes at exit otherwise) |
| `gui` | `run_gui`: a script in a Maya window. The script path goes through `MAYALAUNCH_SCRIPT`, because `-command` takes MEL and a path in the string is mangled |

Its tests are `tests/maya/concepts/test_mayalaunch.py`, in the suite's plain Python group (see Tiers: about 15 s, because they start child processes and one starts a real mayapy, so they run beside the Maya tiers). Stopping Maya is always by the pid the launcher started, never by name.

## Tiers

Maya tests can't each run in isolation. If the plugin doesn't load, every test fails, and if building breaks, every behaviour test fails. To keep that readable, tests run in tiers, in dependency order:

| Tier | Proves | Test files |
|---|---|---|
| 1 registration | the plugin loads, and every node type, command and long flag exists | `concepts/test_plugin` |
| 2 read only | commands and checks that never build a rig | joint markup command, control shapes and bounds (`concepts/test_control_shapes`), template loading, validation cases that expect errors |
| 3 build smoke | every fixture template builds | `concepts/test_smoke` (each golden file's template and skeleton, plus valid validation cases), `commands/test_create_rig`, `commands/test_selection_sets` (selection sets and `rigmodules.selection`) |
| 4 modules | each module poses correctly, and the rig around it is wired and named right | `modules/*` (`modules/test_ribbon`: the contract, 16 hand-worked pose cases (stretch, an interior control, sine, the ends' twist running the whole chain and an interior control's own twist adding to it, the secondary past 180 degrees, scale, volume stretched, squashed, halved and with the profile, a control's scale with volume, aim blend, a rotated parent), and what only the Ribbon does: its channels and their mirror modes, hidden keyable tweakers, locality, a pure evaluation, a parent that moves, a saved and reopened scene, and the plan step's checks, D56), (`modules/test_two_bone_ik`: the contract (with `joints_cannot_give`, the FK controls the blend hides at the posed blend 1), 31 hand-worked pose cases, and what only TwoBoneIk does: blend, visibility, sockets and child modules, the settings control, spaces, the pole on the line, channels and their mirror modes, the keep-list locks, the roll negated by a mirrored pose, D52), sockets, naming, undo/redo, control markup and the control commands (`commands/test_rig_control_cmds`), `commands/test_rig_look` (`setRigLook`, the `lookRoot`, swap and undo, and the skeleton hash check of `loadRigSkinWeights`, D54), `commands/test_attachment` (`parentRig` and `parent:` ids on a body and a face: a chain following a body socket and a constraint-driven control following another, hand-worked world positions, the driven control hidden and locked and a mixed-driver one left free, `getRigInfo` and the parent's `attachments`, the read-phase failures with every miss listed, the mirrored `parent:` id, undo and redo, D55) |
| 5 constraints | constraint commands and space switching | `commands/test_constraint_cmds` (including `getRigControlSpace` and `find_control_space`, D50, and `setRigControlSpace` on a constrained control: it matches as `setRigConstraintSpace` does, undo and redo, every error, D52), `commands/test_constraint_types` (Orient, Point and Swing, and the shoulder ribbon with a Swing start: the twist eases in from nothing at the shoulder to the whole twist at the elbow, a swing alone, a twist and a swing together, the forearm meeting it, and the same arm without the constraint turning rigidly), `concepts/test_docs_examples` (the README quick start ends with a space switch), `concepts/test_mirror` (mirrored templates include mirrored constraints), `commands/test_rig_pose_cmds` (the animation interface: channels, zero, mirror and flip pose, pose files and `rigmodules.pose` keying, D34), `commands/test_rig_inverse_cmds` (`poseRigFromJointPose` and `animateRigFromJointClip`: argument checks, every JointClip reader rule, the ControlAnim sidecar's refusals, a clip that matches no joint, D41), `commands/test_joint_clip_capture` (`rigmodules.jointclip.capture_joint_clip` on flat and parented rigs, hand-worked values, D41), `commands/test_joint_clip_fbx` (`write_fbx`, `read_fbx`, `write_skeleton_fbx` and `rigmodules.bonemap`: hand-written clips round trip through FBX with continuous rotation past 180 degrees and a scaled parent, markup with and without a bone map, a scene left as it was, every refusal, the skeleton export at rest, a rig animation through FBX and restore, D42), `commands/test_two_bone_ik_inverse` (the TwoBoneIk inverse, hand-worked from the design: the handle and its rotation, the start control, the pole's side, pin 0.5 and 1 giving the pole back exactly, roll from the sidecar and without it, soft with and without stretch, a stretched source, a limb near full reach and past it, the rig's current channel against the sidecar's, a pole in `Ik` space, FK at blend 0 matched by IK, FK scale, warnings at a blend of 0.5 and for a longer source arm, a missing limb joint, and a clip with keys, D52), `commands/test_two_bone_ik_cmds` (`setRigIkFkBlend`, the matched IK/FK switch: hand-worked control values for a rotated FK limb and a moved IK handle, and at rest; no pop in either direction at rest and posed, stretched, pinned, soft, rolled, with a space switch on the handle and the pole in `Ik` space, on a mirrored right limb; the matched controls holding the joints at blends 0, 0.5 and 1; the match equal to what `poseRigFromJointPose` writes; warnings for a pose IK can't make, a blend between 0 and 1 and a locked channel, and none for the pole's locked channels; the command's form, undo and redo, and every error; `rigmodules.query.find_ik_fk_limb` (every control of a limb finds it, the blend read, none for another module, an error for a non-control), and `setRigControlSpace` and `getRigControlSpace` on the pole's module-owned space: hand-worked pole locals and worlds for `Parent` to `Ik` and back with the handle moved, turned and in a constraint space, the space read from the control, the matched IK/FK switch leaving the pole in `Ik` space where it was, and every error, D52), `commands/test_inverse_solve` (the solve: a hand-worked clip, round trips through FkChain, Aim, Layout, space switches and an Orient constraint, missing joints, sidecar precedence, scopes, undo, and `animateRigFromJointClip` on clips: baked keys, ranges, euler continuity, the ControlAnim sidecar sampled on the clip's frames with its own sparse keys, D41, D42), `commands/test_rig_animation_cmds` (`saveRigAnimation` and `loadRigAnimation` and the tangents and flags of the keys payload: every tangent kind and infinity applied, fixed tangents with weights, every refusal, a save and load round trip, range clipping, offset, undo, mirror with negated channels and fixed angles and a Center flip, D42), `commands/test_rig_data_files` (the `meta` block in pose and ControlAnim files: the rig's fields and the caller's, every refusal, version 1 files, `getRigDataFileInfo`, `setRigDataFileMeta`, `rigmodules.library`, `loadRigPose(mirror=True)` against load then flip, and a save, zero and load of every channel of every control as a pose and as an animation on the Fk, Layout, space switch and Aim rigs, D51), `commands/test_rig_key_cmds` (`getRigKeys` and `setRigKeys`: keys by markup, units, stepped enums, replacing keys, scopes, one undo, every validation rule, D34) |
| 6 known bugs | expected failures. Never blocks anything. | `known/*` |
| plain Python | files that never touch the plugin or a scene, run in their own Python process beside the Maya tiers and gating nothing | `concepts/test_source_layout` (`core/` Maya-free, known folders, include paths, sorted include blocks, braces on their own lines: reads the source tree), `concepts/test_mayalaunch`, `concepts/test_python_template` |

- **Gating:** a tier runs only if every earlier tier passed. A blocked tier prints `NOT RUN (blocked by ...)` instead of a failure for every test in it, so the first failure shown is the root cause. Within a tier, every test runs, each from a new scene.
- **One Maya process, watched:** `run_tests.py` starts a single mayapy process that runs every tier, and watches it. That process writes each outcome as it happens and records the running test's id before the test starts.
  - **Crash:** the report names the test that caused it, keeps every result from before it, and marks the later tiers `NOT RUN`. With `--keep-going`, a new Maya process starts at the next tier.
  - **Startup failure:** if mayapy or the plugin fails to load, the report shows the error, with the end of the Maya log when there is no Python error to show.
  - **Hang:** a Maya that has not finished after `--timeout` seconds (default 900, the suite takes about 35 s) is stopped, and the report names the test it was running.
  - **No crash dialogs:** the Maya process runs with `MAYA_DISABLE_CER=1`, so a crash opens no "Error Report" window. `TEMP` points at the run folder, so crash dumps and recovery files stay out of your Temp folder. After a crash, the runner also closes any `senddmp.exe` or `cer_dialog.exe` that the crashed process started. It finds them by parent process id, so your own Maya sessions are never touched.
- **Plain Python group, run in parallel:** `PLAIN_PYTHON` in `run_tests.py` lists the files that import no `maya` module. `run_tests.py` starts them in a separate plain Python process before it starts Maya, and the report shows them on a `plain python` line after the tiers. They gate nothing: if the launcher is broken the Maya tiers fail on their own, and both results are reported. Their time overlaps Maya's startup and tests, which cut a full run from about 52 s to about 35 s. A file moves here only if it needs no plugin and no scene.
- **Where tiers are defined:** `TIERS` and `PLAIN_PYTHON` in `run_tests.py` are the one place that says which file runs where. A test file missing from it fails the run before anything starts.
- **Future tiers:** animation, then import/export, go after constraints, because they need working modules and constraints.

## Layout

```
tests/
  RigTests.cmd, run_tests.py     entry point and runner
  ViewportGallery.cmd            manual VP2 check of every control shape (not part of the suite)
  viewport/run_gallery.py        starts the gallery (through mayalaunch) and reports its log
  viewport/gallery.py            the script that runs in a GUI Maya
  maya/
    rigtest/                     shared helpers (no tests)
      case.py                    RigTestCase: new scene per test, build helpers, assertions
      cases.py                   loaders for the three data-driven case types
      module_contract.py         ModuleContract: tests every module must pass (the inverse round trip included, D41)
      jointclips.py              writing a rig's joints as a flat JointClip file, for tests of the solve
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
- **Node names** are written without a namespace and resolve in `rig:`. `RigTestCase.build` passes `asset=config.ASSET_ID` (`tests\rig`), whose last token gives that default namespace (D29).
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

Every new test file goes into `TIERS` in `run_tests.py` (or `PLAIN_PYTHON`, if it imports no `maya` module and needs no plugin), in the earliest tier whose prerequisites it needs and no earlier. A test that builds a rig cannot go in tier 2, and a test that uses constraints cannot go in tier 4. A file that mixes tiers is split.

- **New module:** create `modules/test_<module>.py` with a `ModuleContract` subclass (template, skeleton, node type, joint owners, and `MIRROR_TEMPLATE` and `MIRROR_SKELETON`: a template with the module marked `"mirror": true` and a skeleton with both sides, or `MIRROR_TEMPLATE = False` for a Center-only module; leaving it unset fails the mirror tests), `add_pose_cases(<Class>, "<module>")`, and module-specific tests. Add the fixtures it needs, a `fixtures/poses/<module>/` folder and a golden snapshot.
- **New command:** create `commands/test_<command>.py`. Cover flags, objects passed by name (the selection is never used, apart from `setRigMeshId` and `pruneToMaxInfluences` with no meshes named), every error path, and undo/redo. Add its long flags to `concepts/test_plugin.py`.
- **Python package:** a test of `python/rigmodules` goes in the tier its command needs (`commands/test_rig_info.py` builds a rig; `concepts/test_python_template.py` reads a file, so it is in `PLAIN_PYTHON`). The runner puts `python/` on `sys.path`, so tests import `rigmodules` directly.
- **New template check:** add a validation case. Write a code test in `concepts/test_template_validation.py` only when the check needs a scene change, such as a driven OPM.
- **Bug fix:** write the failing test first. If the bug can't be fixed yet, put the test in `known/` with `@unittest.expectedFailure` and name the open item in `design/decisions.md`.

### Temp probes become tests

While working out a change, a throwaway mayapy script is often the fastest way to look at what the rig does. That is fine, but a check that only lives in a scratch script protects nothing later. Before a change is finished, turn every check a probe made into a suite test, using the rules above. If you are not sure it belongs, list it as a proposed test instead (what it checks, which tier, data case or code).

## Viewport gallery

The suite runs in mayapy, which has no viewport, so it checks control bounds but never sees a control drawn. Batch renders (`mayabatch`, `ogsRender`) draw meshes but skip curves and draw override UI drawables, so they can't check it either. After changing `RigControlNode`'s draw code or the shape library, run:

```powershell
cmd /c tests\ViewportGallery.cmd
```

It starts Maya with a window (`maya.exe -command`, through `mayalaunch`, which stops it after 5 minutes if it hangs), builds every shape beside a -1 to 1 reference cube with a red line along +X, playblasts ten views, closes Maya and opens the image folder (`%TEMP%\RigViewportGallery\out`). It takes about a minute. Pass a plugin path to check another build. The docstring at the top of `viewport/gallery.py` says what each image should show.

- **Exit code:** 0 when every image was written. It does not judge the images. Look at them.
- **Unattended:** like the suite, it skips `userSetup.py` and turns off the crash reporter, so nothing waits for input.

## Performance scenes

`tools/perf/perf_scenes.py` builds scenes with 50 copies of a rig, animated, to measure playback speed at scale. It is a developer tool and a manual run: nothing here is in `RigTests.cmd` (D36). A full run is long (about 45 minutes with every mode), so run it when a module or constraint type changes, not on every build. `mayapy tools\perf\perf_scenes.py check` runs `check_perf_scene.py`, which takes seconds and confirms the generated rig is still valid, so a slow result is never a broken build. Run it after changing the generator or the plugin's modules.

```powershell
mayapy tools\perf\perf_scenes.py human                      # 50 instances of the Human asset, a pose cycle keyed every frame
mayapy tools\perf\perf_scenes.py complex                    # 50 instances of a generated 354 joint, 81 constraint rig with FkChain, Aim, TwoBoneIk (12 limbs) and Ribbon (7) modules, with a -90 to 90 range of motion and the limbs' blend, stretch, soft, pin and pole space keyed
mayapy tools\perf\perf_scenes.py complex --count 20 --attachments 4   # the same with 4 attachment rigs (D55) built onto each rig, saved as complex_x20_at4.mb: bench it beside complex_x20 for what attachments cost (the floor has none, so the 1.5 times check stays on 0)
mayapy tools\perf\perf_scenes.py floor                      # the same scene with native transforms and joints only: bench it beside complex for the 1.5 times check
mayapy tools\perf\perf_scenes.py bench tools\perf\out\complex_x50.mb   # evaluation only: no viewport, parallel (--all-modes adds serial and DG)
mayapy tools\perf\perf_scenes.py gui tools\perf\out\complex_x50.mb     # real playback in a Maya window: eval mode, viewport on and off
mayapy tools\perf\perf_scenes.py profile tools\perf\out\complex_x50.mb --frames 10   # where the time goes, from Maya's profiler, in a Maya window
```

- **Output:** scenes, the generated skeleton and the generated template go to `tools/perf/out` (ignored by git). `--count` and `--frames` change the size, and `--asset-dir` points `human` at the asset folder (`skeletons/default/skeleton.json` and `rigs/human/`, the layout of D54).
- **The goal** (D36, amended and signed off 2026-11-11) is 30 fps, 1:1 playback, for 20 of the complex rig (`complex --count 20`, then `bench tools\perf\out\complex_x20.mb`). The 50-rig scene (the default `--count`) is the stress benchmark: report its fps, and compare it with the native floor (no tool yet, so a manual comparison, within 1.5 times). Frame time is linear in rig count, so the floor on the author's machine is about 40 ms for 50 rigs (80 ms measured for 100), which is why 30 fps for that many rigs is out of reach. `floor` builds the native floor of the same scene (the same controls, the skeleton's joint hierarchy, the same keys, no rig layer), so the 1.5 times check is `bench` on `complex_xN.mb` and `floor_xN.mb` in one session. `bench` times parallel evaluation (the mode that counts, D36), prints PASS or FAIL against `--target-fps` (default 30) and exits 1 on FAIL. Timings drift by up to about 30% between sessions on the same scene, so compare numbers from one session, and the machine has 8 physical cores (16 logical). `--all-modes` adds serial and DG, which are much slower to run. When a module or constraint type is added, add many instances of it to the complex rig (`chain_specs`, `aim_specs` and `limb_specs` in `perf_scenes.py`) in the same change and run `bench`.
- **`bench`** steps the time with `MAnimControl`, times it with `perf_counter` over `--passes` passes after a warm-up, and pulls every chain tip's world matrix, in each evaluation manager mode (`off` is plain DG, which only evaluates what is pulled). That is evaluation only, with no viewport and no UI, so its numbers are an upper bound on playback speed. A Maya window measured several times slower on the complex scene.
- **`gui`** starts `maya.exe`, steps through the frame range as fast as it can (a redraw per frame), drops the first 1.5 seconds, and prints the frame rate per evaluation manager mode, with the viewport drawn and with it hidden. Close other heavy programs first, and don't run the suite at the same time.
- **`profile`** starts `maya.exe` through `profile_gui.py`, in parallel evaluation with cached playback off, and records Maya's profiler. It times each part without the profiler, then records it, and prints the plugin's own events (the `RigModules` category) in full plus the top events by total time. Node events are grouped by node type and attribute (`rigControlNode worldMatrix`), so 20,000 controls make one row. The parts are: a shape edit on one control (a check that the plugin's events record, so an empty table means no calls), selecting one rig's controls and then every control, and playback with the viewport drawn, suspended, with joints hidden and with controls hidden. `--frames` sets the playback length and the number of selection toggles (default 20). `--plugin` compares two builds. Build the other one in its own git worktree, never with a redirected `OutDir`. Durations from worker threads add up past the wall time, so compare rows with each other and take frame times from the unprofiled lines.

## Gotchas

- `cmds.namespace(add=..., setNamespace=...)` is undoable and clears the redo queue. Between an undo and a redo, use `om.MNamespace` instead.
- A Maya command error arrives as a `RuntimeError` whose text is everything the command passed to `displayError`. `assertMayaError` checks that text.
